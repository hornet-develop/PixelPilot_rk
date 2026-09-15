#ifndef DVR_H
#define DVR_H

#include <atomic>
#include <string>

#include "dvr_common.h"
#include "../encoder/stream_consumer.h"
#include "../encoder/video_encoder.h"
#include "ts_writer.h"
#include "storage_guard.h"

// Records the encoded stream to segmented MPEG-TS files. A consumer of VideoEncoder, not its owner:
// it never sees a captured frame, only finished access units, and it runs entirely on the encoder
// thread (control operations are posted there, so nothing here needs its own lock).
class Dvr : public StreamConsumer {
public:
    Dvr(VideoEncoder *encoder, char *filename_template, int segment_minutes,
        uint64_t min_free_bytes, bool require_mount, int nominal_fps);
    ~Dvr() override;

    // Control. Safe from any thread; each posts onto the encoder thread.
    void start_recording();
    void stop_recording();
    void toggle_recording();
    // Latch the DVR off for the rest of the process (the display thread uses this when writeback
    // commits keep failing). Returns immediately; the file is finalized on the encoder thread.
    void disable(const std::string &reason);
    // Finalize any open recording. Call before stopping the encoder thread.
    void shutdown();

    // --- StreamConsumer ---
    bool active() const override;
    void on_access_unit(const AccessUnit &au) override;
    void on_encoder_reset(int width, int height) override;
    void on_encoder_failed(const std::string &reason) override;
    void on_tick(bool idle) override;

private:
    int  start();
    void stop();
    void fail(const std::string &reason, bool fatal);
    bool open_next_file();          // finalize whatever is open, then open the next one
    bool open_output_file();
    void finalize_current_file();
    std::string generate_filename();
    int  next_frame_duration(int64_t pts_ms);
    void request_rotate();          // roll to a new file at the next keyframe
    void update_storage_status(bool force_update);
    bool file_active() const;

    VideoEncoder *encoder;

    char *filename_template;
    int64_t segment_limit_ms = 0;
    int64_t segment_video_ticks = 0;
    // Nominal source rate, used only for the fallback frame duration when a real pts delta is
    // unusable. The encoder owns the actual rate.
    int nominal_fps = 0;

    std::string rec_dir;
    StorageGuard storage;
    uint64_t max_file_bytes = 0;
    uint64_t session_free_at_start = 0;  // free bytes at recording start; mid-recording est baseline
    bool     session_free_known = false; // false if statvfs failed: skip the free-space estimates
    dev_t    session_dev = 0;
    bool     session_dev_known = false;
    int64_t  last_storage_check_ms = 0;

    dev_t storage_status_dev = 0;
    bool storage_status_dev_known = false;
    uint64_t storage_total_bytes = 0;
    bool storage_total_known = false;

    // Read by other threads through active(), written on the encoder thread.
    std::atomic<bool> recording_armed{false};
    // Waiting for a keyframe before opening the next file, so every recording starts decodable.
    // Until it clears we keep writing to the file already open, so a rotation loses no frames.
    bool pending_open = false;
    int  open_attempts = 0;

    TsWriter writer;
    std::string current_filename;

    uint32_t frames_written = 0;     // frames handed to the writer (enqueued), not yet on disk
    int64_t  rec_start_pts = -1;     // feed-pts (ms) of the first frame of the current segment
    int      last_good_duration = 0; // last computed duration (90k ticks); fallback
};

#endif
