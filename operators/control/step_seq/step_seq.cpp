#include "step_seq.h"

void StepSeq::draw_inspector(VividInspectorContext* ctx) {
    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    float px = ctx->content_x;
    float py = ctx->content_y;
    float w = ctx->content_width;
    constexpr float h = 100.0f;
    constexpr float pad = 4.0f;

    py += 4;

    // Background
    d.draw_rect(o, px, py, w, h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

    // Read params from inspector context
    // Param order: num_steps=0, frequency=1, rate_mode=2, glide=3, amplitude=4, offset=5, polarity=6
    // step_value[0..31] at indices 7..38, step_gate[0..31] at indices 39..70
    int ns = 8;
    if (ctx->param_count > 0) ns = std::max(1, std::min(kMaxSteps, static_cast<int>(ctx->param_values[0])));

    float plot_x = px + pad;
    float plot_y = py + pad;
    float plot_w = w - 2.0f * pad;
    float plot_h = h - 2.0f * pad;

    float bar_w = plot_w / static_cast<float>(ns);
    constexpr float bar_gap = 1.0f;

    // Determine current playing step from output (trigger)
    int playing_step = -1;
    if (ctx->output_count > 0) {
        // Reconstruct current step from output value — approximate via param matching
        // We can't directly read internal state, so check which step's value best matches
        // Instead, use a simpler approach: track via the value output
        float cur_out = ctx->output_values[0];
        float amp = (ctx->param_count > 4) ? ctx->param_values[4] : 1.0f;
        float off = (ctx->param_count > 5) ? ctx->param_values[5] : 0.0f;
        int pol = (ctx->param_count > 6) ? static_cast<int>(ctx->param_values[6]) : 0;

        // Reverse the output transform to get raw value
        if (amp > 0.0001f) {
            float raw = (cur_out - off) / amp;
            if (pol == 0) raw = (raw + 1.0f) * 0.5f; // bipolar → [0,1]
            raw = std::max(0.0f, std::min(1.0f, raw));

            // Find closest matching active step
            float best_dist = 999.0f;
            for (int i = 0; i < ns; ++i) {
                float sv = (ctx->param_count > static_cast<uint32_t>(7 + i)) ? ctx->param_values[7 + i] : 0.5f;
                float dist = std::abs(sv - raw);
                if (dist < best_dist) {
                    best_dist = dist;
                    playing_step = i;
                }
            }
        }
    }

    // Draw step bars
    for (int i = 0; i < ns; ++i) {
        float sv = (ctx->param_count > static_cast<uint32_t>(7 + i)) ? ctx->param_values[7 + i] : 0.5f;
        float sg = (ctx->param_count > static_cast<uint32_t>(39 + i)) ? ctx->param_values[39 + i] : 1.0f;

        float bx = plot_x + static_cast<float>(i) * bar_w + bar_gap;
        float bw = bar_w - 2.0f * bar_gap;
        if (bw < 1.0f) bw = 1.0f;

        float bar_h = sv * plot_h;
        float by = plot_y + plot_h - bar_h;

        // Full bar fill
        float alpha = (i == playing_step) ? 0.7f : 0.4f;
        d.draw_rect(o, bx, by, bw, bar_h,
                    {th.accent.r, th.accent.g, th.accent.b, alpha});

        // Gate-off portion: dimmer overlay on top of bar where gate is off
        if (sg < 0.99f) {
            float gate_off_h = bar_h * (1.0f - sg);
            d.draw_rect(o, bx, by, bw, gate_off_h,
                        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.5f});
        }
    }

    // --- Drag interaction ---
    if (ctx->mouse.left_clicked) {
        dragged_step_ = -1;
        float mx = ctx->mouse.x;
        if (mx >= plot_x && mx <= plot_x + plot_w) {
            int hit = static_cast<int>((mx - plot_x) / bar_w);
            if (hit >= 0 && hit < ns) {
                dragged_step_ = hit;
            }
        }
    }

    if (ctx->mouse.left_down && dragged_step_ >= 0 && dragged_step_ < ns) {
        float my = ctx->mouse.y;
        float new_value = 1.0f - (my - plot_y) / plot_h;
        new_value = std::max(0.0f, std::min(1.0f, new_value));

        char name_buf[32];
        std::snprintf(name_buf, sizeof(name_buf), "step_value_%d", dragged_step_);
        ctx->commands.set_param(ctx->commands.opaque, name_buf, new_value);
    }

    if (!ctx->mouse.left_down) {
        dragged_step_ = -1;
    }

    ctx->consumed_height = 4.0f + h + 4.0f;
}

VIVID_REGISTER(StepSeq)
VIVID_BINDABLE(StepSeq)
VIVID_INSPECTOR(StepSeq)
