#ifndef VIDEO_ENCODER_H
#define VIDEO_ENCODER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "encoder_common.h"
#include "stream_consumer.h"
#include "mpp_encoder.h"

// Owns the one H.265 hardware encoder and the thread that drives it, and fans the encoded access
// units out to any number of consumers (DVR file, RTP output, ...). Frames are captured only while at
// least one consumer is active, so with no consumer the encoder stays torn down and the capture paths
// in main.cpp skip their work entirely.
//
// Only this class ever touches a captured frame - consumers receive plain byte buffers. That is what
// keeps writeback pool slots single-owner: pp_wb_release() is called from here and nowhere else.
class VideoEncoder {
public:
    // Decode-tap capture: the decoded frame is submitted at its native size, zero-copy.
    VideoEncoder(int fps, int bitrate_bps, uint32_t video_width, uint32_t video_height);
    // Writeback capture: the composited display output (video + OSD) the VOP wrote into the
    // writeback buffers. Geometry is fixed to those buffers and never follows the video size.
    VideoEncoder(int fps, int bitrate_bps,
                 uint32_t wb_width, uint32_t wb_height,
                 uint32_t wb_hor_stride, uint32_t wb_ver_stride);
    ~VideoEncoder();

    // Register a consumer. Call before starting the thread; the consumer must outlive the encoder.
    void add_consumer(StreamConsumer *consumer);

    // Ingress. frame() is the decode tap (VideoOnly); writeback_frame() is the composited display
    // output (VideoWithOsdWriteback). Both drop rather than block when the queue is full, and both
    // release any writeback resources the dropped frame carried.
    void frame(enc_frame_info info);
    void writeback_frame(enc_frame_info info);

    // True when at least one consumer is active - the gate the capture paths in main.cpp test before
    // doing any per-frame work. Cheap: a plain atomic load.
    bool wants_frames() const;
    // Consumers call this after changing their active() state so the gate is recomputed.
    void consumers_changed();

    // Run fn on the encoder thread. Lets a consumer serialise its own control operations (start/stop a
    // recording) against encoding without needing a thread or a lock of its own. drop_frames
    // discards queued frames first, matching the old RPC behaviour.
    void post(std::function<void()> fn, bool drop_frames);

    void set_video_params(uint32_t video_frm_width, uint32_t video_frm_height);
    void restart();                 // codec change: rebuild the encoder at the current geometry
    void request_keyframe();        // ask for an IDR on the next submitted frame
    // Deliver whatever MPP has already produced. A consumer about to go inactive calls this so the tail
    // of its stream is not left sitting inside the encoder. Unlike teardown()'s flush it does not
    // end the session, so other consumers are unaffected. Encoder thread only.
    void drain_pending();
    void shutdown();

    bool ready() const;
    // Geometry the encoder is currently configured at. Valid once ready().
    int width() const;
    int height() const;

    static void *__THREAD__(void *context);

private:
    struct Task {
        enum Kind { FRAME, RUN, SHUTDOWN } kind;
        enc_frame_info frame_info;
        std::function<void()> fn;
    };

    void loop();
    void enqueue(Task t, bool drop_frames);
    void drop_pending_frames();     // caller holds mtx
    void recompute_wants_frames();

    bool init();                    // (re)create the MPP context at the current geometry
    bool ensure_encoder();          // init if needed; notifies consumers on reset / on giving up
    void teardown();                // flush what is in flight, then destroy the context
    void encode(enc_frame_info info);
    void encode_wb(enc_frame_info info);
    MppBuffer import_frame(const enc_frame_info &info);
    void maybe_request_idr(int64_t pts_ms);
    void drain_to_consumers();
    void emit(const uint8_t *data, int len);

    std::queue<Task> queue_;
    std::mutex mtx;
    std::condition_variable cv;

    std::vector<StreamConsumer *> consumers_;
    std::atomic<bool> wants_frames_{false};

    enum class RecordingMode {
        VideoOnly,             // zero-copy - decoded frame submitted straight to the encoder
        VideoWithOsdWriteback  // DRM writeback - encode the composited display output (video+OSD)
    };
    RecordingMode mode = RecordingMode::VideoOnly;
    int bitrate = 8000000;
    int enc_fps = 0;                // nominal rate, seeded from the display refresh

    uint32_t video_frm_width = 0;
    uint32_t video_frm_height = 0;

    // Writeback geometry: the composited buffer the display thread hands us, always NV12.
    uint32_t wb_enc_width = 0;
    uint32_t wb_enc_height = 0;
    uint32_t wb_enc_hor_stride = 0;   // bytes (Y stride for NV12)
    uint32_t wb_enc_ver_stride = 0;   // aligned rows (matches the WB buffer's CbCr plane offset)
    int      wb_pending_index = -1;   // the one writeback slot MPP still holds

    int cur_width = 0;                // geometry the MPP context is actually configured at
    int cur_height = 0;

    bool draining_ = false;           // re-entrancy guard: a consumer may call drain_pending() from
                                      // inside on_access_unit() (e.g. a write failure stopping it)
    bool geometry_dirty = true;       // geometry changed; rebuild before the next submit
    int  init_attempts = 0;
    int  frame_error_streak = 0;

    // Keyframe placement, in source-clock ms. Driven from the submit side so it does not depend on
    // any consumer's timeline; see the note on KEYFRAME_INTERVAL_MS in video_encoder.cpp.
    int64_t last_idr_pts_ms = 0;
    // Set by request_keyframe() from any thread (a consumer opening a new file), and on every fresh
    // encoder session so the stream always opens on an IDR.
    std::atomic<bool> idr_requested{true};

    MppEncoder encoder;
    // FIFO of submitted frame pts (ms) in encode order. MPP emits one packet per submitted frame
    // (IPPP, no B-frames), so popping in order re-attaches the right timestamp to each output.
    std::queue<int64_t> submitted_pts;
    uint32_t frames_submitted = 0;
    uint32_t frames_emitted   = 0;
    int64_t  last_emitted_pts_ms = 0;
};

#endif
