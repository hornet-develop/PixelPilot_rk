#ifndef ENCODER_COMMON_H
#define ENCODER_COMMON_H

#include <cstdint>
#include <cstddef>

// One captured frame handed to the encoder. Two disjoint shapes: the decode tap fills the geometry
// and leaves fence_fd/wb_index at -1; the writeback path fills those two and leaves the geometry
// zero, because writeback frames always use the encoder's fixed wb_* geometry instead.
struct enc_frame_info {
    int      prime_fd;
    uint32_t hor_stride;
    uint32_t ver_stride;
    uint32_t width;
    uint32_t height;
    size_t   buf_size;
    uint64_t pts;
    // Writeback only: capture-completion fence and pool slot to release once encoded.
    int      fence_fd = -1;
    int      wb_index = -1;
};

// Release a writeback buffer pool slot back to the display thread once the encoder has finished
// with it. Implemented in main.cpp; called from the encoder thread. Invalid index is ignored.
void pp_wb_release(int index);

#endif
