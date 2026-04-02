#pragma once

#include "operator_api/draw_ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::draw_plot {

inline void draw_thumb_background(VividDrawAPI& d, void* o,
                                  float w, float h,
                                  VividColor bg = {0.07f, 0.08f, 0.09f, 0.9f}) {
    if (d.draw_rect) d.draw_rect(o, 0.0f, 0.0f, w, h, bg);
}

inline void draw_thumb_label(VividDrawAPI& d, void* o,
                             float x, float y,
                             const char* text,
                             VividColor color = {0.45f, 0.55f, 0.65f, 0.9f},
                             float scale = 0.85f) {
    if (d.draw_text) d.draw_text(o, x, y, text, color, scale);
}

inline void draw_thumb_value(VividDrawAPI& d, void* o,
                             float x, float y, float w,
                             const char* text,
                             VividColor color = {1.0f, 0.78f, 0.31f, 0.85f},
                             float scale = 0.8f) {
    vivid::draw_ui::draw_text_aligned(d, o, x, y, w, text, color, scale, 1.0f);
}

inline void draw_threshold_line(VividDrawAPI& d, void* o,
                                float x0, float x1, float y,
                                VividColor color,
                                float thickness = 2.0f) {
    if (d.draw_line) d.draw_line(o, x0, y, x1, y, thickness, color);
}

inline void draw_playhead_line(VividDrawAPI& d, void* o,
                               float x0, float y0, float x1, float y1,
                               VividColor color,
                               float thickness = 2.0f) {
    if (d.draw_line) d.draw_line(o, x0, y0, x1, y1, thickness, color);
}

inline void draw_scalar_meter(VividDrawAPI& d, void* o,
                              float x, float y, float w, float h,
                              float value_norm,
                              VividColor track,
                              VividColor low,
                              VividColor high,
                              float radius = 2.0f,
                              float unity_norm = -1.0f,
                              VividColor unity = {0.78f, 0.82f, 0.86f, 0.7f}) {
    value_norm = std::clamp(value_norm, 0.0f, 1.0f);
    vivid::draw_ui::draw_panel(d, o, x, y, w, h, track, {0, 0, 0, 0}, radius);

    float fill_h = h * value_norm;
    float fill_y = y + h - fill_h;
    constexpr int kSlices = 8;
    float slice_h = (fill_h <= 0.0f) ? 0.0f : fill_h / static_cast<float>(kSlices);
    for (int i = 0; i < kSlices; ++i) {
        if (slice_h <= 0.0f) break;
        float t = (kSlices <= 1) ? 1.0f : static_cast<float>(i) / static_cast<float>(kSlices - 1);
        VividColor c{
            low.r + (high.r - low.r) * t,
            low.g + (high.g - low.g) * t,
            low.b + (high.b - low.b) * t,
            low.a + (high.a - low.a) * t,
        };
        float sy = fill_y + i * slice_h;
        if (d.draw_rect) d.draw_rect(o, x + 1.0f, sy, std::max(0.0f, w - 2.0f), slice_h + 0.5f, c);
    }

    if (unity_norm >= 0.0f && unity_norm <= 1.0f && d.draw_line) {
        float uy = y + h * (1.0f - unity_norm);
        d.draw_line(o, x - 3.0f, uy, x + w + 3.0f, uy, 1.5f, unity);
    }
}

inline void draw_step_grid(VividDrawAPI& d, void* o,
                           float x, float y, float w, float h,
                           int steps,
                           const int* active_steps,
                           int current_step,
                           VividColor fill,
                           VividColor dim,
                           VividColor highlight,
                           float gap = 2.0f,
                           float radius = 2.0f) {
    if (steps <= 0) return;
    float cell_w = (w - gap * static_cast<float>(steps - 1)) / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        float cx = x + i * (cell_w + gap);
        bool is_active = active_steps ? (active_steps[i] != 0) : false;
        bool is_current = (i == current_step);
        if (is_current) {
            vivid::draw_ui::draw_panel(d, o, cx - 1.5f, y - 1.5f, cell_w + 3.0f, h + 3.0f,
                                       highlight, {0, 0, 0, 0}, radius + 1.0f);
        }
        vivid::draw_ui::draw_panel(d, o, cx, y, cell_w, h,
                                   is_active ? fill : dim, {0, 0, 0, 0}, radius);
    }
}

template <typename SampleFn>
inline void draw_waveform_plot(VividDrawAPI& d, void* o,
                               float x, float y, float w, float h,
                               SampleFn sample_fn,
                               VividColor fill,
                               VividColor line,
                               VividColor center_line = {0.24f, 0.25f, 0.29f, 0.7f},
                               bool bipolar = true,
                               float cycles = 1.0f,
                               float pad = 0.0f) {
    if (!d.draw_rect) return;
    float px = x + pad;
    float py = y + pad;
    float pw = std::max(1.0f, w - 2.0f * pad);
    float ph = std::max(1.0f, h - 2.0f * pad);
    float center = py + ph * 0.5f;
    if (bipolar && d.draw_line) d.draw_line(o, px, center, px + pw, center, 1.0f, center_line);

    constexpr int kCols = 28;
    float col_w = pw / static_cast<float>(kCols);
    float prev_y = center;
    for (int i = 0; i < kCols; ++i) {
        float u0 = static_cast<float>(i) / static_cast<float>(kCols);
        float u1 = static_cast<float>(i + 1) / static_cast<float>(kCols);
        float sx = px + u0 * pw;
        float sample = std::clamp(sample_fn(u0 * cycles), -1.0f, 1.0f);
        float sy = center - sample * (ph * 0.45f);
        float lo = std::min(center, sy);
        float hh = std::fabs(sy - center);
        if (hh > 0.0f) d.draw_rect(o, sx, lo, std::max(1.0f, col_w - 1.0f), hh, fill);
        if (i > 0 && d.draw_line) d.draw_line(o, sx - col_w, prev_y, sx, sy, 1.5f, line);
        prev_y = sy;
        (void)u1;
    }
}

template <typename SampleFn>
inline void draw_envelope_plot(VividDrawAPI& d, void* o,
                               float x, float y, float w, float h,
                               SampleFn env_fn,
                               VividColor fill,
                               VividColor line,
                               float current_value = -1.0f,
                               VividColor playhead = {1.0f, 0.78f, 0.31f, 0.85f},
                               float pad = 0.0f) {
    if (!d.draw_rect) return;
    float px = x + pad;
    float py = y + pad;
    float pw = std::max(1.0f, w - 2.0f * pad);
    float ph = std::max(1.0f, h - 2.0f * pad);

    constexpr int kCols = 28;
    float col_w = pw / static_cast<float>(kCols);
    float prev_x = px;
    float prev_y = py + ph;
    for (int i = 0; i <= kCols; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(kCols);
        float sample = std::clamp(env_fn(u), 0.0f, 1.0f);
        float sx = px + u * pw;
        float sy = py + (1.0f - sample) * ph;
        if (i > 0) {
            float col_x = prev_x;
            float col_h = (py + ph) - prev_y;
            if (col_h > 0.0f) d.draw_rect(o, col_x, prev_y, std::max(1.0f, col_w), col_h, fill);
            if (d.draw_line) d.draw_line(o, prev_x, prev_y, sx, sy, 1.5f, line);
        }
        prev_x = sx;
        prev_y = sy;
    }

    if (current_value >= 0.0f && current_value <= 1.0f && d.draw_line) {
        float cy = py + (1.0f - current_value) * ph;
        d.draw_line(o, px, cy, px + pw, cy, 2.0f, playhead);
    }
}

} // namespace vivid::draw_plot
