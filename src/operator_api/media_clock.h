#pragma once

#include <cstdint>

namespace vivid {

struct MediaClockV1 {
    float local_time_s = 0.0f;
    float duration_s = 0.0f;
    float speed = 1.0f;
    uint8_t playing = 0;
    uint8_t loop_enabled = 0;
    uint16_t reserved0 = 0;
    uint32_t loop_epoch = 0;
    uint64_t source_generation = 0;
    double monotonic_time_s = 0.0;
};

inline double media_clock_monotonic(double local_time_s, double duration_s, uint32_t loop_epoch) {
    if (duration_s > 0.0) return duration_s * static_cast<double>(loop_epoch) + local_time_s;
    return local_time_s;
}

} // namespace vivid

