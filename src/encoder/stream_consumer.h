#ifndef STREAM_CONSUMER_H
#define STREAM_CONSUMER_H

#include <cstdint>
#include <string>

// One complete H.265 access unit as produced by the MPP encoder: Annex-B, with VPS/SPS/PPS
// prepended on every IDR (MPP_ENC_HEADER_MODE_EACH_IDR), so any keyframe is a valid entry point.
struct AccessUnit {
    const uint8_t *data;
    int            len;
    // Source feed timestamp in ms - the same clock the decoder stamped onto the frame. Consumers
    // derive whatever they need from it: the DVR turns it into drift-closed container durations,
    // an RTP consumer scales it to 90kHz. Deliberately NOT a duration, so each consumer keeps its own
    // timeline and none of them can perturb another's.
    int64_t        pts_ms;
    bool           keyframe;
};

// A consumer of the encoded stream. Consumers are registered with VideoEncoder before its thread
// starts and live for the process, going active/inactive as their own state changes (e.g. the DVR
// is only active while recording).
class StreamConsumer {
public:
    virtual ~StreamConsumer() = default;

    // Does this consumer currently want frames? The encoder polls this to decide whether to run at
    // all - if no consumer is active, no frames are captured and the encoder stays torn down.
    virtual bool active() const = 0;

    // Called on the encoder thread, once per access unit. Must not block: a slow consumer stalls
    // encoding and therefore every other consumer too.
    virtual void on_access_unit(const AccessUnit &au) = 0;

    // The encoder was (re)configured at this geometry - a resolution or codec change. Any consumer
    // holding stream-scoped state should reset it. Anything still in the old stream has already
    // been delivered via on_access_unit() before this is called.
    virtual void on_encoder_reset(int width, int height) { (void)width; (void)height; }

    // The encoder is unrecoverably broken; every consumer loses the stream.
    virtual void on_encoder_failed(const std::string &reason) { (void)reason; }

    // Called on the encoder thread once per loop iteration - so once per encoded frame while the
    // stream is running (tens of times a second), plus once per second when idle, and on every
    // consumer whether or not it is active. It is only a wake-up, carrying no state: a consumer
    // that wants a periodic job must throttle on its own clock. Same rule as on_access_unit:
    // must not block.
    virtual void on_tick() { }
};

#endif
