#pragma once

#include "operator_api/types.h"
#include <algorithm>
#include <cmath>

namespace vivid {
namespace adsr_inspector {

inline void draw(VividInspectorContext* ctx,
                 float attack, float decay, float sustain, float release,
                 bool bypassed) {
    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    float px = ctx->content_x;
    float py = ctx->content_y;
    float w = ctx->content_width;
    constexpr float h = 80.0f;
    constexpr float pad = 6.0f;

    if (attack < 0.0001f) attack = 0.0001f;
    if (decay < 0.001f) decay = 0.001f;
    if (release < 0.001f) release = 0.001f;
    sustain = std::max(0.0f, std::min(1.0f, sustain));

    float alpha_mult = bypassed ? 0.35f : 1.0f;

    py += 4;

    d.draw_rect(o, px, py, w, h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

    float sustain_width = 0.3f * (attack + decay + release);
    float total_time = attack + decay + sustain_width + release;

    auto env_at = [&](float t) -> float {
        if (t <= attack) return t / attack;
        t -= attack;
        if (t <= decay) return 1.0f - (1.0f - sustain) * (t / decay);
        t -= decay;
        if (t <= sustain_width) return sustain;
        t -= sustain_width;
        if (t <= release) return sustain * (1.0f - t / release);
        return 0.0f;
    };

    auto time_to_x = [&](float t) -> float {
        return px + pad + (t / total_time) * (w - 2.0f * pad);
    };
    auto env_to_y = [&](float e) -> float {
        return py + pad + (1.0f - e) * (h - 2.0f * pad);
    };

    float plot_w = w - 2.0f * pad;
    int cols = static_cast<int>(plot_w / 3.0f);
    float col_w = plot_w / static_cast<float>(cols);
    float bottom_y = env_to_y(0.0f);

    for (int i = 0; i < cols; ++i) {
        float fx = px + pad + static_cast<float>(i) * col_w;
        float t = (static_cast<float>(i) / static_cast<float>(cols)) * total_time;
        float e = env_at(t);
        float ey = env_to_y(e);
        float fill_h = bottom_y - ey;
        if (fill_h > 0.0f) {
            d.draw_rect(o, fx, ey, col_w, fill_h,
                        {th.accent.r, th.accent.g, th.accent.b, 0.15f * alpha_mult});
        }
    }

    int segments = std::max(4, cols / 2);
    float lx = time_to_x(0.0f);
    float ly = env_to_y(env_at(0.0f));
    for (int i = 1; i <= segments; ++i) {
        float t = (static_cast<float>(i) / static_cast<float>(segments)) * total_time;
        float cx = time_to_x(t);
        float cy = env_to_y(env_at(t));
        d.draw_line(o, lx, ly, cx, cy, 1.5f,
                    {th.accent.r, th.accent.g, th.accent.b, 0.9f * alpha_mult});
        lx = cx;
        ly = cy;
    }

    float marker_times[3] = { attack, attack + decay, attack + decay + sustain_width };
    for (float mt : marker_times) {
        float mx = time_to_x(mt);
        float top_y = py + pad;
        for (float dy = top_y; dy < bottom_y; dy += 8.0f) {
            float dash_end = std::min(dy + 4.0f, bottom_y);
            d.draw_line(o, mx, dy, mx, dash_end, 1.0f,
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.3f * alpha_mult});
        }
    }

    if (bypassed) {
        float tw = d.text_width(o, "bypassed", 1.0f);
        d.draw_text(o, px + w - tw - 4, py + 2, "bypassed",
                    {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f}, 1.0f);
    }

    ctx->consumed_height = 4.0f + h + 4.0f;
}

} // namespace adsr_inspector
} // namespace vivid
