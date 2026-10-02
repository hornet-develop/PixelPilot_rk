#ifndef DVR_H
#define DVR_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "dvr_common.h"
#include "../encoder/stream_consumer.h"
#include "../encoder/video_encoder.h"
#include "ts_writer.h"
#include "storage_guard.h"

// One unit of work for the DVR thread. RPC_AU is the bulk traffic and carries its own bytes,
// because the encoder's buffer is only valid for the duration of the on_access_unit() call; the
// rest are control commands, each handled by its own case in Dvr::loop().
struct dvr_rpc {
    enum {
        RPC_AU,
        RPC_START,
        RPC_STOP,
        RPC_TOGGLE,
        RPC_DISABLE,
        RPC_ROTATE,
        RPC_FAIL,
        RPC_SHUTDOWN,
    } command = RPC_AU;
    std::vector<uint8_t> data;   // RPC_AU only
    int64_t pts_ms = 0;          // RPC_AU only
    bool    keyframe = false;    // RPC_AU only
    std::string text;            // RPC_FAIL only
};

// Records the encoded stream to segmented MPEG-TS files. A consumer of VideoEncoder, not its owner:
// it never sees a captured frame, only finished access units.
//
// Everything that can touch storage - muxing, writing, opening and closing files, scanning the
// recording directory, statvfs - runs on this class's own worker thread. on_access_unit() only
// copies the bytes into a queue and returns, so a stalled or pulled SD card can never block the
// encoder thread and therefore can never interrupt the live stream. That isolation is the whole
// reason the worker exists.
class Dvr : public StreamConsumer {
public:
    Dvr(VideoEncoder *encoder, const std::string &filename_template, int segment_minutes,
        uint64_t min_free_bytes, bool require_mount, int nominal_fps);
    ~Dvr() override;

    // Control. Safe from any thread; each just queues a command for the DVR thread.
    void start_recording();
    void stop_recording();
    void toggle_recording();
    // Latch the DVR off for the rest of the process (the display thread uses this when writeback
    // commits keep failing). Returns immediately; the file is finalized on the DVR thread.
    void disable(const std::string &reason);
    // Finalize any open recording and stop the DVR thread. Call this while the encoder is still
    // running (it needs drain_pending() to run there to collect the tail), then join tid_dvr after
    // the encoder thread has been joined.
    void shutdown();

    static void *__THREAD__(void *context);

    bool active() const override;
    void on_access_unit(const AccessUnit &au) override;
    void on_encoder_reset(int width, int height) override;
    void on_encoder_failed(const std::string &reason) override;

private:
    void enqueue(dvr_rpc rpc);
    void enqueue_dvr_command(dvr_rpc rpc);
    void handle_access_unit(const dvr_rpc &rpc);

    void loop();
    int  start();
    void stop();
    void fail(const std::string &reason, bool fatal);
    bool open_next_file(); 
    bool open_output_file();
    void finalize_current_file();
    std::string generate_filename();
    int  next_frame_duration(int64_t pts_ms);
    void request_rotate();
    void update_storage_status(bool force_update);
    bool file_active() const;

    VideoEncoder *encoder;

    std::queue<dvr_rpc> queue_;
    size_t queue_bytes_ = 0;      // queued AU bytes, capped so a dead card cannot exhaust memory
    bool   queue_overflow_ = false;
    std::mutex mtx;
    std::condition_variable cv;

    std::string filename_template;
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

    uint32_t frames_written = 0;     // frames written to the current segment
    int64_t  rec_start_pts = -1;     // feed-pts (ms) of the first frame of the current segment
    int      last_good_duration = 0; // last computed duration (90k ticks); fallback
};

#endif
