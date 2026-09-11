#ifndef SHARED_SURFACE_HPP
#define SHARED_SURFACE_HPP

#include <atomic>
#include <cstdint>

static constexpr int SHM_BUFFERS_COUNT = 3;

struct SharedMemoryRegion {
    uint16_t width;       // Image width
    uint16_t height;      // Image height
    uint32_t stride;      // Number of bytes per image row
    uint8_t refresh_rate; // Refresh rate

    std::atomic<int32_t> front_index; // buffer index to read from
    std::atomic<int32_t> back_index;  // buffer index to write into
    std::atomic<int32_t> ready_index; // last fully written buffer (-1 = none)

    unsigned char data[]; // Three image buffers stored consecutively: [buffer0][buffer1][buffer2]
                          // Each buffer has size = stride * height
};

#endif
