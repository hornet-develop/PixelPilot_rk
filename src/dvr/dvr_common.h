#ifndef DVR_COMMON_H
#define DVR_COMMON_H

#include <cstdint>
#include <atomic>

// The single cross-thread recording state. Written on the encoder thread (start/stop/fail), by the
// mavlink thread (stop_recording) and by main (shutdown); read by the OSD and mavlink to show
// whether a recording is in progress. Note this says nothing about whether the ENCODER is running -
// that is VideoEncoder::wants_frames(), since an RTP consumer can need frames with no recording.
// Disabled is a latch: an unrecoverable failure sets it and nothing clears it for the rest of the
// process, so a broken DVR cannot retry in a loop.
enum class DvrState {
    Idle,       // not recording, but a start request would be honoured
    Recording,  // a recording is in progress
    Disabled    // unrecoverable failure - ignore every start/toggle until restart
};

extern std::atomic<DvrState> dvr_state;

inline bool dvr_is_recording() {
    return dvr_state.load(std::memory_order_acquire) == DvrState::Recording;
}

inline bool dvr_is_disabled() {
    return dvr_state.load(std::memory_order_acquire) == DvrState::Disabled;
}

#endif
