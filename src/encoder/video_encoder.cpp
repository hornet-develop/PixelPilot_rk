#include <pthread.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstring>

#include <rockchip/rk_mpi.h>

#include "spdlog/spdlog.h"

#include "video_encoder.h"

// Max frames allowed to sit in the queue before new ones are dropped. Keeps the encoder current: in
// VideoOnly we carry only the decoder's prime_fd, which the decoder recycles (its MAX_FRAMES pool)
// soon after we enqueue, so a deep backlog would encode a recycled (wrong) buffer. Keeping this
// small bounds lateness to a few frames, well inside the recycle window.
static const size_t MAX_PENDING_FRAMES = 3;

// Max wait for the DRM writeback capture fence to signal (the VOP finished writing the composited
// frame into the WB buffer). A timeout means something is wrong; we drop that frame rather than
// encode a half-written buffer.
static const int WB_FENCE_TIMEOUT_MS = 200;

// Failed encoder setups before we give up. Without a cap a permanently broken encoder is re-created
// on every frame, and cleanup()'s mpi->reset() alone can block 8s - at 60fps that is a retry storm,
// not a recovery.
static const int MAX_INIT_ATTEMPTS = 3;

// Consecutive frames lost to per-frame errors (buffer import, capture fence, encoder submit) before
// we treat the pipeline as broken. These are individually transient, so only a sustained run counts.
static const int MAX_FRAME_ERROR_STREAK = 60;

// Keyframe cadence, in source-clock ms. Driven from the submit side so it belongs to the stream
// rather than to any one consumer's timeline. Sets seek granularity for the DVR (MPEG-TS has no
// index, so a player starts at a keyframe) and rejoin latency for a network consumer.
static const int64_t KEYFRAME_INTERVAL_MS = 500;

// Release the writeback resources a frame carries (capture fence + pool slot). No-op for decode-tap
// frames, whose fence_fd/wb_index are -1. Must be called for every writeback frame that does NOT
// reach encode_wb(), or the display thread's pool starves.
static inline void release_wb_frame(enc_frame_info &fi) {
    if (fi.fence_fd >= 0) {
        close(fi.fence_fd);
        fi.fence_fd = -1;
    }
    if (fi.wb_index >= 0) {
        pp_wb_release(fi.wb_index);
        fi.wb_index = -1;
    }
}

// True if the access unit contains an HEVC IRAP NAL (types 16..21), i.e. it is a random access
// point. Scans Annex-B start codes; a 4-byte start code contains the 3-byte one at offset 1, so
// matching on 00 00 01 covers both.
static bool au_is_keyframe(const uint8_t *d, int len) {
    if (len < 4) {
        return false;
    }
    for (int i = 0; i + 3 < len; i++) {
        if (d[i] == 0x00 && d[i + 1] == 0x00 && d[i + 2] == 0x01) {
            int nal_type = (d[i + 3] >> 1) & 0x3f;
            if (nal_type >= 16 && nal_type <= 21) {
                return true;
            }
            i += 2;
        }
    }
    return false;
}

VideoEncoder::VideoEncoder(int fps, int bitrate_bps, uint32_t video_width, uint32_t video_height)
    : mode(RecordingMode::VideoOnly),
      bitrate(bitrate_bps),
      enc_fps(fps > 0 ? fps : 60),
      video_frm_width(video_width),
      video_frm_height(video_height) {}

VideoEncoder::VideoEncoder(int fps, int bitrate_bps,
                           uint32_t wb_width, uint32_t wb_height,
                           uint32_t wb_hor_stride, uint32_t wb_ver_stride)
    : mode(RecordingMode::VideoWithOsdWriteback),
      bitrate(bitrate_bps),
      enc_fps(fps > 0 ? fps : 60),
      wb_enc_width(wb_width),
      wb_enc_height(wb_height),
      wb_enc_hor_stride(wb_hor_stride),
      wb_enc_ver_stride(wb_ver_stride) {}

VideoEncoder::~VideoEncoder() {}

void VideoEncoder::add_consumer(StreamConsumer *consumer) {
    if (consumer) {
        consumers_.push_back(consumer);
        recompute_wants_frames();
    }
}

void VideoEncoder::recompute_wants_frames() {
    bool any = false;
    for (StreamConsumer *s : consumers_) {
        if (s->active()) {
            any = true;
            break;
        }
    }
    wants_frames_.store(any, std::memory_order_release);
}

bool VideoEncoder::wants_frames() const {
    return wants_frames_.load(std::memory_order_acquire);
}

void VideoEncoder::consumers_changed() {
    recompute_wants_frames();
}

void VideoEncoder::frame(enc_frame_info info) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue_.size() >= MAX_PENDING_FRAMES) {
            return; // backlog full - drop this frame to stay current
        }
        queue_.push(Task{Task::FRAME, info, nullptr});
    }
    cv.notify_one();
}

void VideoEncoder::writeback_frame(enc_frame_info info) {
    bool dropped = false;
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue_.size() >= MAX_PENDING_FRAMES) {
            dropped = true;
        } else {
            queue_.push(Task{Task::FRAME, info, nullptr});
        }
    }
    if (dropped) {
        release_wb_frame(info);
        return;
    }
    cv.notify_one();
}

void VideoEncoder::enqueue(Task t, bool drop_frames) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (drop_frames) {
            drop_pending_frames();
        }
        queue_.push(std::move(t));
    }
    cv.notify_one();
}

void VideoEncoder::drop_pending_frames() {
    std::queue<Task> kept;
    while (!queue_.empty()) {
        if (queue_.front().kind != Task::FRAME) {
            kept.push(std::move(queue_.front()));
        } else {
            release_wb_frame(queue_.front().frame_info); // free WB slot/fence of discarded frames
        }
        queue_.pop();
    }
    queue_.swap(kept);
}

void VideoEncoder::post(std::function<void()> fn, bool drop_frames) {
    enqueue(Task{Task::RUN, enc_frame_info{}, std::move(fn)}, drop_frames);
}

void VideoEncoder::set_video_params(uint32_t w, uint32_t h) {
    post([this, w, h] {
        if (w == video_frm_width && h == video_frm_height) {
            return;
        }
        video_frm_width = w;
        video_frm_height = h;
        geometry_dirty = true;
    }, true);
}

void VideoEncoder::restart() {
    post([this] { geometry_dirty = true; }, true);
}

void VideoEncoder::request_keyframe() {
    idr_requested.store(true, std::memory_order_release);
}

void VideoEncoder::shutdown() {
    enqueue(Task{Task::SHUTDOWN, enc_frame_info{}, nullptr}, true);
}

bool VideoEncoder::ready() const {
    return encoder.ready();
}

int VideoEncoder::width() const {
    return cur_width;
}

int VideoEncoder::height() const {
    return cur_height;
}

void *VideoEncoder::__THREAD__(void *context) {
    auto *self = (VideoEncoder *)context;
    self->loop();
    return nullptr;
}

bool VideoEncoder::init() {
    int enc_w, enc_h, enc_hor, enc_ver;
    if (mode == RecordingMode::VideoWithOsdWriteback) {
        // Geometry is fixed to the writeback buffers the display thread fills; the decoded video
        // size is irrelevant here.
        enc_w   = (int)wb_enc_width;
        enc_h   = (int)wb_enc_height;
        enc_hor = (int)wb_enc_hor_stride;
        enc_ver = (int)wb_enc_ver_stride;
    } else {
        if (video_frm_width == 0 || video_frm_height == 0) {
            spdlog::warn("[ Encoder ] awaiting video params");
            return false;
        }
        enc_w   = (int)video_frm_width;
        enc_h   = (int)video_frm_height;
        enc_hor = (int)((video_frm_width  + 15) & ~15u);
        enc_ver = (int)((video_frm_height + 15) & ~15u);
    }
    if (enc_w <= 0 || enc_h <= 0 || enc_fps <= 0) {
        spdlog::warn("[ Encoder ] invalid encoder params {}x{} @{}", enc_w, enc_h, enc_fps);
        return false;
    }

    spdlog::info("[ Encoder ] setting up encoder {}x{} @{}fps bitrate={} H265 [{}]",
                 enc_w, enc_h, enc_fps, bitrate,
                 mode == RecordingMode::VideoWithOsdWriteback ? "writeback WYSIWYG" : "zero-copy");

    if (!encoder.init(enc_w, enc_h, enc_hor, enc_ver, enc_fps, bitrate)) {
        return false;
    }
    cur_width  = enc_w;
    cur_height = enc_h;
    idr_requested.store(true, std::memory_order_release);
    return true;
}

void VideoEncoder::teardown() {
    if (encoder.ready()) {
        // Emit what the encoder still holds so consumers get the tail of the stream before it ends.
        drain_to_consumers();
        encoder.flush([this](const uint8_t *data, int len) -> bool {
            if (frames_emitted >= frames_submitted) {
                return false; // all submitted frames accounted for - stop
            }
            emit(data, len);
            return true;
        });
    }
    // The last submitted buffer has now been drained/flushed, so release its slot.
    if (wb_pending_index >= 0) {
        pp_wb_release(wb_pending_index);
        wb_pending_index = -1;
    }
    encoder.cleanup();
    while (!submitted_pts.empty()) {
        submitted_pts.pop();
    }
    frames_submitted = 0;
    frames_emitted   = 0;
    idr_requested.store(true, std::memory_order_release);
}

bool VideoEncoder::ensure_encoder() {
    if (encoder.ready() && !geometry_dirty) {
        return true;
    }
    if (init_attempts >= MAX_INIT_ATTEMPTS) {
        return false;
    }
    init_attempts++;
    teardown();
    geometry_dirty = false;
    if (!init()) {
        if (init_attempts >= MAX_INIT_ATTEMPTS) {
            const std::string reason = "encoder setup failed " + std::to_string(init_attempts) + " times";
            spdlog::error("[ Encoder ] {}", reason);
            for (StreamConsumer *s : consumers_) {
                s->on_encoder_failed(reason);
            }
            recompute_wants_frames();
        }
        return false;
    }
    init_attempts = 0;
    for (StreamConsumer *s : consumers_) {
        s->on_encoder_reset(cur_width, cur_height);
    }
    return true;
}

void VideoEncoder::emit(const uint8_t *data, int len) {
    int64_t pts_ms;
    if (!submitted_pts.empty()) {
        pts_ms = submitted_pts.front();
        submitted_pts.pop();
    } else {
        pts_ms = last_emitted_pts_ms;   // should not happen: one packet per submitted frame
    }
    last_emitted_pts_ms = pts_ms;
    frames_emitted++;

    AccessUnit au{data, len, pts_ms, au_is_keyframe(data, len)};
    for (StreamConsumer *s : consumers_) {
        if (s->active()) {
            s->on_access_unit(au);
        }
    }
}

void VideoEncoder::drain_to_consumers() {
    // emit() calls into consumers, and a consumer may react by stopping itself, which drains again. Bail on
    // the nested call rather than re-entering MPP.
    if (draining_) {
        return;
    }
    draining_ = true;
    encoder.drain([this](const uint8_t *data, int len) { emit(data, len); });
    draining_ = false;
}

void VideoEncoder::drain_pending() {
    if (encoder.ready()) {
        drain_to_consumers();
    }
}

void VideoEncoder::maybe_request_idr(int64_t pts_ms) {
    // A backwards pts means the source restarted; treat it as due rather than waiting out the
    // interval against a stale anchor.
    if (idr_requested.load(std::memory_order_acquire) ||
        pts_ms < last_idr_pts_ms ||
        pts_ms - last_idr_pts_ms >= KEYFRAME_INTERVAL_MS) {
        encoder.request_idr();
        last_idr_pts_ms = pts_ms;
        idr_requested.store(false, std::memory_order_release);
    }
}

MppBuffer VideoEncoder::import_frame(const enc_frame_info &info) {
    // Import the captured frame's DRM buffer directly as encoder input.
    MppBufferInfo buf_info;
    memset(&buf_info, 0, sizeof(buf_info));
    buf_info.type = MPP_BUFFER_TYPE_DRM;
    buf_info.fd   = info.prime_fd;
    buf_info.size = info.buf_size;

    MppBuffer buf = nullptr;
    if (mpp_buffer_import(&buf, &buf_info) || !buf) {
        spdlog::warn("[ Encoder ] mpp_buffer_import failed");
        return nullptr;
    }
    return buf;
}

void VideoEncoder::encode_wb(enc_frame_info info) {
    // 1) Wait for the VOP to finish writing the composited frame into this WB buffer.
    if (info.fence_fd >= 0) {
        struct pollfd pfd = { info.fence_fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, WB_FENCE_TIMEOUT_MS);
        close(info.fence_fd);
        info.fence_fd = -1;
        if (pr <= 0) {
            spdlog::warn("[ Encoder ] writeback fence wait failed/timed out ({}), dropping frame", pr);
            pp_wb_release(info.wb_index);
            frame_error_streak++;
            return;
        }
    }

    // 2) Drain the previous submission (the encoder reads a frame for one more cycle - IPPP
    //    latency, same as the VideoOnly path). After the drain MPP is done with the previous WB
    //    buffer, so release that pool slot back to the display thread.
    drain_to_consumers();
    if (wb_pending_index >= 0) {
        pp_wb_release(wb_pending_index);
        wb_pending_index = -1;
    }

    // 3) Import & submit the freshly-composited buffer; hold its slot until the next drain.
    MppBuffer buf = import_frame(info);
    if (!buf) {
        pp_wb_release(info.wb_index);
        frame_error_streak++;
        return;
    }
    maybe_request_idr((int64_t)info.pts);
    int ret = encoder.submit(buf, (int64_t)info.pts,
                             (int)wb_enc_width, (int)wb_enc_height,
                             (int)wb_enc_hor_stride, (int)wb_enc_ver_stride);
    mpp_buffer_put(buf); // encoder holds its own ref; release our import ref
    if (ret == 0) {
        submitted_pts.push((int64_t)info.pts);
        frames_submitted++;
        wb_pending_index = info.wb_index;
        frame_error_streak = 0;
    } else {
        pp_wb_release(info.wb_index);
        frame_error_streak++;
    }
}

void VideoEncoder::encode(enc_frame_info info) {
    if (mode == RecordingMode::VideoWithOsdWriteback) {
        encode_wb(info);
        return;
    }

    // VideoOnly: zero-copy (drain previous output, import, submit).
    drain_to_consumers();

    // Sync encoder config strides to actual decoded frame strides
    if ((int)info.hor_stride != encoder.get_hor_stride() ||
        (int)info.ver_stride != encoder.get_ver_stride()) {
        encoder.sync_strides((int)info.hor_stride, (int)info.ver_stride);
    }

    MppBuffer buf = import_frame(info);
    if (!buf) {
        frame_error_streak++;
        return;
    }
    maybe_request_idr((int64_t)info.pts);
    int ret = encoder.submit(buf, (int64_t)info.pts,
                             (int)info.width, (int)info.height,
                             (int)info.hor_stride, (int)info.ver_stride);
    mpp_buffer_put(buf); // encoder holds its own ref; release ours
    if (ret == 0) {
        submitted_pts.push((int64_t)info.pts);
        frames_submitted++;
        frame_error_streak = 0;
    } else {
        frame_error_streak++;
    }
}

void VideoEncoder::loop() {
    pthread_setname_np(pthread_self(), "__ENCODER");
    while (true) {
        Task task;
        bool has_task;
        {
            std::unique_lock<std::mutex> lock(mtx);
            has_task = cv.wait_for(lock, std::chrono::seconds(1),
                                   [this] { return !this->queue_.empty(); });
            if (has_task) {
                task = std::move(queue_.front());
                queue_.pop();
            }
        }

        // Consumers get a heartbeat whether or not anything is encoding, so housekeeping that must run
        // while idle (storage monitoring) keeps running with no video.
        for (StreamConsumer *s : consumers_) {
            s->on_tick(!has_task);
        }
        if (!has_task) {
            continue;
        }

        switch (task.kind) {
        case Task::SHUTDOWN:
            goto end;

        case Task::RUN:
            if (task.fn) {
                task.fn();
            }
            recompute_wants_frames();
            break;

        case Task::FRAME:
            if (!wants_frames()) {
                release_wb_frame(task.frame_info);
                break;
            }
            if (!ensure_encoder()) {
                release_wb_frame(task.frame_info);
                break;
            }
            encode(task.frame_info);
            if (frame_error_streak >= MAX_FRAME_ERROR_STREAK) {
                const std::string reason = std::to_string(frame_error_streak) +
                                           " consecutive frames failed to encode";
                spdlog::error("[ Encoder ] {}", reason);
                frame_error_streak = 0;
                for (StreamConsumer *s : consumers_) {
                    s->on_encoder_failed(reason);
                }
                recompute_wants_frames();
            }
            break;
        }

        // Nobody wants frames any more - give the hardware encoder back.
        if (!wants_frames() && encoder.ready()) {
            teardown();
        }
    }
end:
    teardown();
    spdlog::info("Encoder thread done.");
}
