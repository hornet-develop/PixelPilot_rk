#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <sys/sysmacros.h>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <system_error>
#include <algorithm>
#include <regex>

#include "spdlog/spdlog.h"

#include "dvr.h"

extern "C" {
#include "../osd.h"
}

namespace fs = std::filesystem;

std::atomic<DvrState> dvr_state{DvrState::Idle};

static const int SEQUENCE_PADDING = 4;   // zero-padding width for sequence-numbered filenames

static const int TS_TIMEBASE_90K = 90000;  // MPEG-TS clock (ticks per second)
static const int MS_TO_90K       = 90;     // 1 ms = 90 ticks at 90kHz

// Cap on a single frame's duration (90k ticks). A drop burst (e.g. frames lost while the encoder
// rebuilds) leaves a large pts gap; without this cap the resulting frame would be held for
// seconds - a freeze. 0.25s is far above any real inter-frame gap, so normal frames are unaffected;
// it only bounds the pathological case.
static const int MAX_FRAME_DURATION_90K = TS_TIMEBASE_90K / 4;

//Periodic storage guard check runs during recording (free-space / mount check).
static const int64_t  STORAGE_CHECK_INTERVAL_MS = 3000;

// Consecutive failed frame writes (disk full / I/O error) before we fail-stop the recording.
static const uint32_t MAX_CONSECUTIVE_WRITE_FAILURES = 5;

// Failed file opens before we give up on the recording.
static const int MAX_OPEN_ATTEMPTS = 3;

static int64_t monotonic_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

Dvr::Dvr(VideoEncoder *enc, char *template_path, int segment_minutes,
         uint64_t min_free_bytes, bool require_mount, int fps)
    : encoder(enc),
      filename_template(template_path),
      segment_limit_ms((int64_t)segment_minutes * 60 * 1000),
      nominal_fps(fps > 0 ? fps : 60),
      rec_dir(fs::path(template_path).parent_path().string()),
      storage(rec_dir, min_free_bytes, require_mount) {}

Dvr::~Dvr() {}

void Dvr::start_recording() {
    encoder->post([this] {
        if (dvr_is_disabled() || recording_armed.load(std::memory_order_relaxed)) {
            return;
        }
        start();
    }, false);
}

void Dvr::stop_recording() {
    DvrState expected = DvrState::Recording;
    dvr_state.compare_exchange_strong(expected, DvrState::Idle, std::memory_order_acq_rel);
    encoder->post([this] {
        if (recording_armed.load(std::memory_order_relaxed)) {
            stop();
        }
    }, true);
}

void Dvr::toggle_recording() {
    encoder->post([this] {
        if (dvr_is_disabled()) {
            return;
        }
        if (recording_armed.load(std::memory_order_relaxed)) {
            stop();
        } else {
            start();
        }
    }, false);
}

void Dvr::disable(const std::string &reason) {
    DvrState prev = dvr_state.exchange(DvrState::Disabled, std::memory_order_acq_rel);
    if (prev == DvrState::Disabled) {
        return;
    }
    spdlog::error("[ DVR ] disabling DVR for this session: {}", reason);
    encoder->post([this] {
        if (recording_armed.load(std::memory_order_relaxed)) {
            stop();
        }
        osd_publish_bool_fact("dvr.recording", NULL, 0, false);
    }, true);
}

void Dvr::shutdown() {
    DvrState expected = DvrState::Recording;
    dvr_state.compare_exchange_strong(expected, DvrState::Idle, std::memory_order_acq_rel);
    encoder->post([this] {
        if (recording_armed.load(std::memory_order_relaxed)) {
            stop();
        }
    }, true);
}

bool Dvr::active() const {
    return recording_armed.load(std::memory_order_acquire);
}

void Dvr::on_access_unit(const AccessUnit &au) {
    if (!recording_armed.load(std::memory_order_relaxed)) {
        return;
    }

    // Roll to the next file only on a keyframe, so every recording - and every rotated segment -
    // opens on an IDR carrying its own VPS/SPS/PPS. Until one arrives we keep writing to the file
    // already open, so the rotation seam loses nothing.
    if (pending_open) {
        if (!au.keyframe) {
            return;
        }
        if (!open_next_file()) {
            return;
        }
        pending_open = false;
    }
    if (!writer.is_open()) {
        return;
    }

    if (writer.write_nal(au.data, au.len, next_frame_duration(au.pts_ms))) {
        frames_written++;
    }

    // FAT32 per-file size cap: roll before reaching the 4GB.
    if (max_file_bytes > 0 && writer.size() >= max_file_bytes) {
        spdlog::info("[ DVR ] file size cap reached, starting new file");
        request_rotate();
    } else if (segment_limit_ms > 0 && segment_video_ticks >= segment_limit_ms * MS_TO_90K) {
        spdlog::info("[ DVR ] segment time limit reached, starting new file");
        request_rotate();
    }

    const uint32_t fails = writer.consecutive_write_failures();
    if (fails >= MAX_CONSECUTIVE_WRITE_FAILURES) {
        fail(std::to_string(fails) + " consecutive write failures (disk full or I/O error)", false);
    }
}

void Dvr::on_encoder_reset(int, int) {
    if (recording_armed.load(std::memory_order_relaxed)) {
        request_rotate();
    }
}

void Dvr::on_encoder_failed(const std::string &reason) {
    if (recording_armed.load(std::memory_order_relaxed)) {
        fail(reason, true);
    }
}

void Dvr::on_tick(bool idle) {
    update_storage_status(idle);
}

static std::string build_sequence_pattern(const std::string &filename_pattern) {
    std::string pattern = "^";
    bool sequence_capture_added = false;

    for (size_t i = 0; i < filename_pattern.size(); ++i) {
        if (filename_pattern[i] == '%' && i + 1 < filename_pattern.size()) {
            const char spec = filename_pattern[i + 1];
            if (spec == 'N') {
                if (!sequence_capture_added) {
                    pattern += R"((\d+))";
                    sequence_capture_added = true;
                } else {
                    pattern += R"(\d+)";
                }
            } else if (spec == 'Y') {
                pattern += R"(\d{4})";
            } else {
                pattern += R"(\d{2})";
            }
            ++i;
            continue;
        }
        const char c = filename_pattern[i];
        if (std::strchr(".^$|()[]{}*+?\\", c)) {
            pattern += '\\';
        }
        pattern += c;
    }
    pattern += "$";
    return pattern;
}

std::string Dvr::generate_filename() {
    fs::path pathObj(filename_template);
    std::string filename_pattern = pathObj.filename().string();

    std::error_code dir_ec;
    if (!fs::exists(rec_dir, dir_ec)) {
        spdlog::error("[ DVR ] Directory does not exist: {}", rec_dir);
        return "";
    }

    const size_t sequence_pos = filename_pattern.find("%N");
    const bool with_sequence = sequence_pos != std::string::npos;
    if (with_sequence) {
        const bool multiple_sequence_placeholders = filename_pattern.find("%N", sequence_pos + 2) != std::string::npos;
        if (multiple_sequence_placeholders) {
            spdlog::warn("[ DVR ] Filename template contains more than one %N placeholder");
        }
        // Next sequence number = max existing sequence matching the filename template + 1.
        // This runs on every segment rotation, so it must never throw: the card can disappear
        // mid-scan (filesystem_error) and a long sequence value would overflow a plain stoi - either
        // would terminate the process.
        int maxNumber = -1;
        try {
            const std::string regex_pattern = build_sequence_pattern(filename_pattern);
            std::regex pattern(regex_pattern);
            std::error_code ec;
            for (const auto &entry : fs::directory_iterator(rec_dir, ec)) {
                std::error_code entry_ec;
                if (!entry.is_regular_file(entry_ec))
                    continue;
                std::string filename = entry.path().filename().string();
                std::smatch match;
                if (!std::regex_match(filename, match, pattern))
                    continue;
                errno = 0;
                long number = std::strtol(match[1].str().c_str(), nullptr, 10);
                if (errno == 0 && number >= 0 && number < INT_MAX) {
                    maxNumber = std::max(maxNumber, (int)number);
                }
            }
            if (ec) {
                spdlog::warn("[ DVR ] could not scan {} for sequence numbers: {}", rec_dir, ec.message());
            }
        } catch (const std::exception &e) {
            spdlog::warn("[ DVR ] sequence scan of {} failed ({}), falling back to sequence 0", rec_dir, e.what());
            maxNumber = -1;
        }
        int nextFileNumber = (maxNumber == -1) ? 0 : maxNumber + 1;

        std::ostringstream stream;
        stream << std::setw(SEQUENCE_PADDING) << std::setfill('0') << nextFileNumber;
        const std::string sequence = stream.str();

        size_t pos = 0;
        while ((pos = filename_pattern.find("%N", pos)) != std::string::npos) {
            filename_pattern.replace(pos, 2, sequence);
            pos += sequence.size();
        }
    }

    std::time_t now = std::time(nullptr);
    char formattedFilename[256];
    std::strftime(formattedFilename, sizeof(formattedFilename),
                  filename_pattern.c_str(), std::localtime(&now));

    return rec_dir + "/" + formattedFilename;
}

int Dvr::start() {
    std::string reason;
    if (!storage.is_ready(reason)) {
        spdlog::error("[ DVR ] not starting recording: {}", reason);
        osd_publish_bool_fact("dvr.recording", NULL, 0, false);
        return -1;
    }

    frames_written = 0;
    rec_start_pts  = -1;
    segment_video_ticks = 0;
    open_attempts  = 0;

    pending_open = true;
    recording_armed.store(true, std::memory_order_release);
    osd_publish_bool_fact("dvr.recording", NULL, 0, true);
    dvr_state.store(DvrState::Recording, std::memory_order_release);

    encoder->consumers_changed();
    encoder->request_keyframe();
    return 0;
}

bool Dvr::open_output_file() {
    std::string ts_filename = generate_filename();
    if (ts_filename.empty()) {
        return false;
    }
    if (!writer.open(ts_filename)) {
        return false;
    }
    current_filename = ts_filename;
    // Start the muxer for this file. Without it the writer thread discards every access unit and
    // the file stays empty, while write_nal() still reports success because it only enqueues.
    if (!writer.begin_video(encoder->width(), encoder->height())) {
        spdlog::error("[ DVR ] could not start the muxer for {}", current_filename);
        writer.close();
        std::error_code ec;
        fs::remove(current_filename, ec);
        current_filename.clear();
        return false;
    }

    max_file_bytes = storage.file_size_cap();
    session_dev_known  = storage.device_id(session_dev);
    session_free_known = storage.free_bytes(session_free_at_start);
    std::string dev_desc = session_dev_known
        ? std::to_string((unsigned)major(session_dev)) + ":" + std::to_string((unsigned)minor(session_dev))
        : std::string("unknown");
    if (session_free_known) {
        spdlog::info("[ DVR ] storage: {} (dev {}), {}MB free, per-file cap {}MB",
                     rec_dir, dev_desc,
                     session_free_at_start / (1024 * 1024),
                     max_file_bytes / (1024 * 1024));
    } else {
        session_free_at_start = 0;
        spdlog::warn("[ DVR ] storage: {} (dev {}) - could not read free space, free-space "
                     "monitoring disabled for this file (per-file cap {}MB)",
                     rec_dir, dev_desc, max_file_bytes / (1024 * 1024));
    }
    spdlog::info("[ DVR ] recording to {}", current_filename);
    last_storage_check_ms = monotonic_ms();
    return true;
}

int Dvr::next_frame_duration(int64_t pts) {
    const int default_duration = TS_TIMEBASE_90K / nominal_fps;

    if (rec_start_pts < 0) {
        rec_start_pts = pts;   // anchor the segment on its first frame
    }
    int64_t target   = (pts - rec_start_pts) * MS_TO_90K;   // real elapsed since segment start (ticks)
    int64_t duration = target - segment_video_ticks;         // close the drift to real time
    if (duration < 1) {
        duration = 1;   // MP4 sample durations must be positive
    }
    if (duration > MAX_FRAME_DURATION_90K) {
        spdlog::warn("[ DVR ] large PTS gap: last timeline={} target={}, raw duration={}, skipping gap",
            segment_video_ticks, target, duration);

        duration = last_good_duration > 0 ? last_good_duration : default_duration;
        segment_video_ticks = target;
    }
    last_good_duration = (int)duration;
    segment_video_ticks += duration;
    return (int)duration;
}

void Dvr::finalize_current_file() {
    if (!writer.is_open() && current_filename.empty()) {
        return;
    }

    if (!current_filename.empty()) {
        spdlog::info("[ DVR ] recording finalized: {} frames, {:.1f}s",
                     frames_written, segment_video_ticks / 90000.0);
    }

    bool empty = (frames_written == 0);
    bool finalized_ok = writer.close();

    if (!empty && !finalized_ok && !current_filename.empty()) {
        spdlog::error("[ DVR ] recording truncated (storage stopped responding): {} — "
                      "playable up to the last flushed frame",
                      current_filename);
    }

    if (empty && !current_filename.empty()) {
        std::error_code ec;
        fs::remove(current_filename, ec);
        if (ec) {
            spdlog::warn("[ DVR ] failed to remove empty recording {}: {}", current_filename, ec.message());
        } else {
            spdlog::info("[ DVR ] removed empty recording {}", current_filename);
        }
    }
    current_filename.clear();
    segment_video_ticks = 0;
}

void Dvr::request_rotate() {
    if (pending_open) {
        return;
    }
    pending_open = true;
    encoder->request_keyframe();
}

bool Dvr::open_next_file() {
    finalize_current_file();
    frames_written = 0;
    rec_start_pts  = -1;
    segment_video_ticks = 0;

    if (!open_output_file()) {
        if (++open_attempts >= MAX_OPEN_ATTEMPTS) {
            fail("could not open a recording file after " + std::to_string(open_attempts) +
                     " attempts",
                 false);
        }
        return false;
    }
    open_attempts = 0;
    return true;
}

bool Dvr::file_active() const {
    return recording_armed.load(std::memory_order_relaxed) && writer.is_open();
}

void Dvr::stop() {
    encoder->drain_pending();
    finalize_current_file();
    pending_open = false;
    recording_armed.store(false, std::memory_order_release);
    osd_publish_bool_fact("dvr.recording", NULL, 0, false);
    DvrState expected = DvrState::Recording;
    dvr_state.compare_exchange_strong(expected, DvrState::Idle, std::memory_order_acq_rel);
    encoder->consumers_changed();
}

void Dvr::fail(const std::string &reason, bool fatal) {
    if (fatal && dvr_is_disabled()) {
        return;
    }
    if (fatal) {
        spdlog::error("[ DVR ] disabling DVR for this session: {}", reason);
    } else {
        spdlog::warn("[ DVR ] stopping recording: {}", reason);
    }
    stop();
    if (fatal) {
        dvr_state.store(DvrState::Disabled, std::memory_order_release);
    }
}

void Dvr::update_storage_status(bool force_update) {
    const int64_t now_ms = monotonic_ms();
    if (!force_update && now_ms - last_storage_check_ms < STORAGE_CHECK_INTERVAL_MS) {
        return;
    }
    last_storage_check_ms = now_ms;

    if (!storage.mount_ok()) {
        if (file_active()) {
            fail("storage no longer mounted", false);
        }
        if (storage_status_dev_known) {
            spdlog::info("[ DVR ] recording storage is no longer mounted");
        }
        storage_status_dev_known = false;
        storage_total_known = false;
        osd_publish_uint_fact("dvr.storage_status", NULL, 0, 0);
        return;
    }

    dev_t now_dev = 0;
    if (!storage.device_id(now_dev)) {
        if (file_active() && session_dev_known) {
            fail("recording storage was removed", false);
        }
        spdlog::warn("[ DVR ] failed to get recording storage device id");
        storage_status_dev_known = false;
        storage_total_known = false;
        osd_publish_uint_fact("dvr.storage_status", NULL, 0, 0);
        return;
    }
    const bool same_recording_storage = session_dev_known && now_dev == session_dev;
    if (file_active() && session_dev_known && !same_recording_storage) {
        fail("recording storage was removed", false);
    }

    const bool new_storage = !storage_status_dev_known || now_dev != storage_status_dev;
    uint64_t available_bytes = 0;

    if (new_storage || !storage_total_known) {
        if (!storage.space_bytes(available_bytes, storage_total_bytes)) {
            spdlog::warn("[ DVR ] failed to get recording storage space");
            osd_publish_uint_fact("dvr.storage_status", NULL, 0, 0);
            return;
        }
        storage_status_dev = now_dev;
        storage_status_dev_known = true;
        storage_total_known = true;
    }
    else if (file_active() && session_free_known && same_recording_storage) {
        const uint64_t written = writer.size();
        available_bytes = session_free_at_start > written ? session_free_at_start - written : 0;
    }
    else if (!storage.free_bytes(available_bytes)) {
        spdlog::warn("[ DVR ] failed to get recording storage free space");

        osd_publish_uint_fact("dvr.storage_status", NULL, 0, 0);
        return;
    }

    if (file_active() && session_free_known && !storage.has_enough_free(available_bytes)) {
        fail("low free space (~" + std::to_string(available_bytes / (1024 * 1024)) +
                                 "MB left, need " + std::to_string(storage.min_free() / (1024 * 1024)) + "MB)",
                             false);
    }

    const bool low_space = available_bytes <= storage.min_free() ||
                           (storage_total_bytes > 0 && available_bytes < storage_total_bytes / 10);
    const uint64_t dvr_available_bytes = available_bytes > storage.min_free() ? available_bytes - storage.min_free() : 0;

    osd_publish_uint_fact("dvr.storage_available_bytes", NULL, 0, dvr_available_bytes);
    osd_publish_uint_fact("dvr.storage_status", NULL, 0, low_space ? 2 : 1);
}

// C-compatible interface
extern "C" {
    void dvr_start_recording(Dvr *dvr) {
        if (dvr) dvr->start_recording();
    }

    void dvr_stop_recording(Dvr *dvr) {
        if (dvr) dvr->stop_recording();
    }
}
