#pragma once

#include "runtime/simd/simd_config.h"

#include <algorithm>
#include <cstdint>

namespace vivid::audio_kernels {

enum class Backend {
    Scalar,
    Accelerate,
};

inline constexpr Backend selected_buffer_backend() {
#if VIVID_ACCELERATE_ENABLED
    return Backend::Accelerate;
#else
    return Backend::Scalar;
#endif
}

inline constexpr const char* backend_name(Backend backend = selected_buffer_backend()) {
    switch (backend) {
        case Backend::Accelerate: return "accelerate";
        case Backend::Scalar:
        default:                  return "scalar";
    }
}

inline void clear(float* out, uint32_t frames) {
    if (!out || frames == 0) return;
#if VIVID_ACCELERATE_ENABLED
    vDSP_vclr(out, 1, frames);
#else
    std::fill_n(out, frames, 0.0f);
#endif
}

inline void scale(const float* in, float* out, uint32_t frames, float gain) {
    if (!in || !out || frames == 0) return;
#if VIVID_ACCELERATE_ENABLED
    vDSP_vsmul(in, 1, &gain, out, 1, frames);
#else
    for (uint32_t i = 0; i < frames; ++i)
        out[i] = in[i] * gain;
#endif
}

inline void mix4(const float* in1, const float* in2, const float* in3, const float* in4,
                 float* out, uint32_t frames,
                 float g1, float g2, float g3, float g4) {
    if (!out || frames == 0) return;
#if VIVID_ACCELERATE_ENABLED
    clear(out, frames);
    if (in1) vDSP_vsma(in1, 1, &g1, out, 1, out, 1, frames);
    if (in2) vDSP_vsma(in2, 1, &g2, out, 1, out, 1, frames);
    if (in3) vDSP_vsma(in3, 1, &g3, out, 1, out, 1, frames);
    if (in4) vDSP_vsma(in4, 1, &g4, out, 1, out, 1, frames);
#else
    for (uint32_t i = 0; i < frames; ++i) {
        const float a = in1 ? in1[i] : 0.0f;
        const float b = in2 ? in2[i] : 0.0f;
        const float c = in3 ? in3[i] : 0.0f;
        const float d = in4 ? in4[i] : 0.0f;
        out[i] = a * g1 + b * g2 + c * g3 + d * g4;
    }
#endif
}

inline void stereo_pan_width(const float* left_in, const float* right_in,
                             float* left_out, float* right_out,
                             uint32_t frames,
                             float mid_gain, float side_gain,
                             float width, float pan_left, float pan_right) {
    if (!left_in || !right_in || !left_out || !right_out || frames == 0) return;

    // This kernel is intentionally scalar for now: it establishes the shared
    // call site while avoiding scratch-buffer allocation on the audio thread.
    const float side_scale = side_gain * width;
    for (uint32_t i = 0; i < frames; ++i) {
        const float mid  = (left_in[i] + right_in[i]) * 0.5f * mid_gain;
        const float side = (left_in[i] - right_in[i]) * 0.5f * side_scale;
        left_out[i] = (mid + side) * pan_left;
        right_out[i] = (mid - side) * pan_right;
    }
}

} // namespace vivid::audio_kernels
