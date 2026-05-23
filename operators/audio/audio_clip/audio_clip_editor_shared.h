#pragma once
// Pure math helpers for audio clip editor hit-testing and geometry.
// No GLFW, no WGPU, no runtime headers — testable in isolation.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace audio_clip_ed {

struct LoopBounds {
    float start = 0.0f;
    float end = 1.0f;
};

// Returns true if mx is within grab_px of handle_x (for a vertical handle line).
inline bool hit_handle_x(float mx, float handle_x, float grab_px = 6.0f) {
    return (mx >= handle_x - grab_px) && (mx < handle_x + grab_px);
}

// Clamp a normalized value to [0, 1].
inline float norm_clamp(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float min_region() {
    return 0.01f;
}

inline LoopBounds effective_loop_bounds(float clip_start, float clip_end,
                                        float loop_start, float loop_end) {
    const float cs = norm_clamp(clip_start);
    const float ce = std::max(cs + 1e-4f, norm_clamp(clip_end));
    const float ls = std::max(cs, std::min(loop_start, ce - 1e-4f));
    const float le = std::max(ls + 1e-4f, std::min(loop_end, ce));
    return {ls, le};
}

inline float pixel_delta_to_norm(float dx, float viewport_size, float screen_width) {
    const float safe_w = screen_width > 1.0f ? screen_width : 1.0f;
    return dx * viewport_size / safe_w;
}

inline float drag_clip_start(float original, float delta_norm, float clip_end) {
    return std::max(0.0f, std::min(original + delta_norm, clip_end - min_region()));
}

inline float drag_clip_end(float original, float delta_norm, float clip_start) {
    return std::max(clip_start + min_region(), std::min(original + delta_norm, 1.0f));
}

inline float drag_loop_start(float original, float delta_norm,
                             float clip_start, float loop_end) {
    return std::max(clip_start, std::min(original + delta_norm, loop_end - min_region()));
}

inline float drag_loop_end(float original, float delta_norm,
                           float loop_start, float clip_end) {
    return std::max(loop_start + min_region(), std::min(original + delta_norm, clip_end));
}

inline LoopBounds drag_loop_body(float original_start, float original_end,
                                 float delta_norm, float clip_start, float clip_end) {
    const float clip_len = std::max(1e-4f, clip_end - clip_start);
    const float length = std::min(clip_len, std::max(1e-4f, original_end - original_start));
    const float max_start = clip_end - length;
    const float new_start = std::max(clip_start, std::min(original_start + delta_norm, max_start));
    return {new_start, std::min(clip_end, new_start + length)};
}

// Format time as mm:ss.mm
inline void format_time(char* buf, size_t n, double sec) {
    if (sec < 0.0) sec = 0.0;
    const int   m  = static_cast<int>(sec) / 60;
    const float s  = static_cast<float>(sec - m * 60.0);
    std::snprintf(buf, n, "%d:%05.2f", m, s);
}

} // namespace audio_clip_ed
