// Dedicated editor window for Euclidean. Single horizontal strip of
// step cells (filled = hit, dim = rest), live playhead, and a side
// panel of density presets. Pattern is algorithmic — cells aren't
// individually editable. Authoring is via hits / steps / rotation +
// quick-pick presets.

#include "euclidean_core.h"
#include "euclidean_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"
#include "operator_api/thumbnail.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

// Thumbnail body lives here (co-located with draw_editor so tests that
// compile this file resolve EuclideanCore's vtable without pulling in
// the entry cpp's VIVID_REGISTER). Reads pattern from the shared
// Bjorklund helper — same code path as compute() and the editor.
void EuclideanCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    const auto& d = ctx->draw;
    void* o = d.opaque;

    int h   = (ctx->param_count > 0) ? std::clamp(static_cast<int>(ctx->param_values[0]), 0, 32) : 3;
    int n   = (ctx->param_count > 1) ? std::clamp(static_cast<int>(ctx->param_values[1]), 1, 32) : 8;
    int rot = (ctx->param_count > 2) ? std::clamp(static_cast<int>(ctx->param_values[2]), 0, 31) : 0;

    int pat[vivid::euclidean_editor::kMaxSteps] = {};
    vivid::euclidean_editor::compute_pattern(h, n, rot, pat);

    float cur_step = (ctx->output_count > 2) ? ctx->output_values[2] : 0.0f;
    float gate     = (ctx->output_count > 1) ? ctx->output_values[1] : 0.0f;
    int cur = static_cast<int>(cur_step);

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float height = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

    d.draw_rect(o, 0, 0, w, height, {0.08f, 0.08f, 0.1f, 0.9f});

    char label[16];
    std::snprintf(label, sizeof(label), "%d/%d", h, n);
    d.draw_text(o, 6, 3, label, {0.45f, 0.55f, 0.65f, 1.0f}, 1.0f);

    float margin_x = 6.0f;
    float top_y = 24.0f;
    float bot_pad = 6.0f;
    float cell_area_w = w - 2 * margin_x;
    float cell_h = height - top_y - bot_pad;
    float gap = (n <= 16) ? 2.0f : 1.0f;
    float cell_w = (cell_area_w - gap * (n - 1)) / n;
    cell_w = std::min(cell_w, 16.0f);
    float cr = std::min(2.0f, cell_w * 0.2f);

    float grid_w = cell_w * n + gap * (n - 1);
    float grid_x0 = margin_x + (cell_area_w - grid_w) * 0.5f;

    VividColor accent = {0.45f, 0.55f, 0.65f, 1.0f};
    VividColor dim    = {0.15f, 0.17f, 0.2f, 1.0f};

    for (int i = 0; i < n; ++i) {
        bool is_hit = pat[i] != 0;
        bool is_current = (i == cur);
        bool is_gating = is_current && (gate > 0.5f);
        float cx = grid_x0 + i * (cell_w + gap);

        if (is_current) {
            d.draw_rounded_rect(o, cx - 1.5f, top_y - 1.5f, cell_w + 3, cell_h + 3, cr + 1,
                                {accent.r * 1.3f, accent.g * 1.3f, accent.b * 1.3f, 0.9f});
        }

        VividColor fill;
        if (is_hit) {
            float a = is_gating ? 1.0f : 0.55f;
            fill = {accent.r, accent.g, accent.b, a};
        } else {
            float a = is_current ? 0.4f : 0.2f;
            fill = {dim.r, dim.g, dim.b, a};
        }

        d.draw_rounded_rect(o, cx, top_y, cell_w, cell_h, cr, fill);
    }
}

namespace euc_ed {

constexpr float kInset      = 8.0f;
constexpr float kTopBarH    = 28.0f;
constexpr float kSidePanelW = 220.0f;

// Param / output indices (must stay in sync with EuclideanCore).
constexpr int kHitsIdx       = 0;
constexpr int kStepsIdx      = 1;
constexpr int kRotationIdx   = 2;

constexpr int kStepOutputIdx = 2;

} // namespace euc_ed


VividEditorMetadata EuclideanCore::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 820;
    m.default_height = 260;
    m.min_width      = 560;
    m.min_height     = 200;
    m.title_suffix   = "Euclidean Editor";
    return m;
}

void EuclideanCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed  = ::euc_ed;
    namespace ek  = ::vivid::editor_keys;
    namespace eu  = ::vivid::euclidean_editor;

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    auto get_param = [&](int idx, float fallback) -> float {
        if (idx < 0) return fallback;
        if (static_cast<uint32_t>(idx) >= ctx->param_count) return fallback;
        return ctx->param_values[idx];
    };
    auto set_named = [&](const char* name, float v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name, v);
    };

    // ---- Live state ----
    const int hits  = std::clamp(
        static_cast<int>(std::lround(get_param(ed::kHitsIdx,     3.0f))), 0, 32);
    const int steps = std::clamp(
        static_cast<int>(std::lround(get_param(ed::kStepsIdx,    8.0f))), 1, 32);
    const int rotation = std::clamp(
        static_cast<int>(std::lround(get_param(ed::kRotationIdx, 0.0f))), 0, 31);
    const int current_step = (ctx->output_count > ed::kStepOutputIdx)
        ? static_cast<int>(ctx->output_values[ed::kStepOutputIdx]) : -1;

    // Rebuild the pattern from (hits, steps, rotation) — same helper
    // compute() uses, so the preview and playback can never diverge.
    int pattern[eu::kMaxSteps] = {};
    eu::compute_pattern(hits, steps, rotation, pattern);

    // ---- Layout ----
    const float surf_w = ctx->surface_width;
    const float surf_h = ctx->surface_height;
    const float top_y  = ed::kInset;
    const float top_h  = ed::kTopBarH;

    const float strip_x = ed::kInset;
    const float strip_y = top_y + top_h + ed::kInset;
    const float strip_w = std::max(0.0f,
        surf_w - 3.0f * ed::kInset - ed::kSidePanelW);
    const float strip_h = std::max(0.0f, surf_h - strip_y - ed::kInset);

    const float side_x = strip_x + strip_w + ed::kInset;
    const float side_y = strip_y;
    const float side_w = ed::kSidePanelW;
    const float side_h = strip_h;

    // ---- Keyboard ----
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ek::kPress && e.action != ek::kRepeat) continue;

        const bool shift = (e.modifiers & ek::kModShift) != 0;

        if (e.key == ek::kLeft) {
            set_named("rotation", static_cast<float>(std::max(0, rotation - 1)));
            continue;
        }
        if (e.key == ek::kRight) {
            set_named("rotation",
                      static_cast<float>(std::min(std::max(0, steps - 1), rotation + 1)));
            continue;
        }
        if (e.key == ek::kUp) {
            if (shift) {
                set_named("steps", static_cast<float>(std::min(32, steps + 1)));
            } else {
                set_named("hits", static_cast<float>(std::min(steps, hits + 1)));
            }
            continue;
        }
        if (e.key == ek::kDown) {
            if (shift) {
                set_named("steps", static_cast<float>(std::max(1, steps - 1)));
            } else {
                set_named("hits", static_cast<float>(std::max(0, hits - 1)));
            }
            continue;
        }
        if (e.key == ek::kR) {
            set_named("rotation", 0.0f);
            continue;
        }
        if (e.key == ek::kD) {
            // Cycle through density presets.
            editor_preset_cursor_ =
                (editor_preset_cursor_ + 1) % eu::kDensityPresetCount;
            const auto& p = eu::kDensityPresets[editor_preset_cursor_];
            set_named("hits",  static_cast<float>(p.hits));
            set_named("steps", static_cast<float>(p.steps));
            continue;
        }
    }

    // ---- Mouse: horizontal drag on strip → rotation; scroll → hits; alt+scroll → steps ----
    const auto& mouse = ctx->mouse;
    const bool mouse_in_strip =
        mouse.x >= strip_x && mouse.x < strip_x + strip_w &&
        mouse.y >= strip_y && mouse.y < strip_y + strip_h;

    if (mouse.left_clicked && mouse_in_strip) {
        editor_drag_rotation_ = true;
    }
    if (!mouse.left_down) {
        editor_drag_rotation_ = false;
    }
    if (editor_drag_rotation_ && strip_w > 0.0f) {
        const float rel = std::clamp((mouse.x - strip_x) / strip_w, 0.0f, 1.0f);
        const int new_rot = std::clamp(
            static_cast<int>(std::lround(rel * static_cast<float>(steps - 1))),
            0, std::max(0, steps - 1));
        if (new_rot != rotation)
            set_named("rotation", static_cast<float>(new_rot));
    }

    // Wheel: ups/downs hits; alt+wheel: steps.
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_MOUSE_SCROLL) continue;
        if (!mouse_in_strip) continue;
        const int tick = (e.scroll_dy > 0) ? +1 : (e.scroll_dy < 0 ? -1 : 0);
        if (tick == 0) continue;
        const bool alt = (e.modifiers & ek::kModAlt) != 0;
        if (alt) {
            set_named("steps",
                      static_cast<float>(std::clamp(steps + tick, 1, 32)));
        } else {
            set_named("hits",
                      static_cast<float>(std::clamp(hits + tick, 0, steps)));
        }
    }

    // Side-panel density-preset click.
    const float preset_row_h = 22.0f;
    const float preset_y0    = side_y + 44.0f;
    if (mouse.left_clicked &&
        mouse.x >= side_x && mouse.x < side_x + side_w &&
        mouse.y >= preset_y0 &&
        mouse.y <  preset_y0 + preset_row_h * eu::kDensityPresetCount) {
        const int idx = static_cast<int>((mouse.y - preset_y0) / preset_row_h);
        if (idx >= 0 && idx < eu::kDensityPresetCount) {
            editor_preset_cursor_ = idx;
            const auto& p = eu::kDensityPresets[idx];
            set_named("hits",  static_cast<float>(p.hits));
            set_named("steps", static_cast<float>(p.steps));
        }
    }

    // ---- Drawing ----
    // Backdrop.
    vivid::draw_ui::draw_panel(d, o, strip_x, strip_y, strip_w, strip_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.92f});

    // Top bar text.
    if (d.draw_text) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "Hits %d  /  Steps %d  /  Rotation %d",
            hits, steps, rotation);
        d.draw_text(o, strip_x, top_y + 6.0f, buf,
            {th.bright_text.r, th.bright_text.g,
             th.bright_text.b, 0.95f}, 1.0f);

        const char* hints =
            "←/→ rotation  ·  ↑/↓ hits  ·  Shift+↑/↓ steps  ·  "
            "R reset  ·  D preset  ·  drag=rotate  ·  scroll=hits";
        const float scale = 0.7f;
        const float hints_w = d.text_width
            ? d.text_width(o, hints, scale) : 560.0f;
        d.draw_text(o, strip_x + strip_w - hints_w, top_y + 8.0f, hints,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, scale);
    }

    // Step strip — 32-slot grid, but only draw `steps` cells live.
    const float cell_pad = 3.0f;
    const float cell_w   = (strip_w - cell_pad * 2.0f)
                         / static_cast<float>(std::max(1, steps));
    const float cell_h   = strip_h - cell_pad * 2.0f;
    const float cells_x  = strip_x + cell_pad;
    const float cells_y  = strip_y + cell_pad;

    for (int i = 0; i < steps; ++i) {
        const float cx = cells_x + static_cast<float>(i) * cell_w;
        const bool is_hit  = pattern[i] != 0;
        const bool is_now  = (i == current_step);
        const bool is_start = (i == 0);  // fold origin marker

        const float inner_pad = 2.0f;
        const float ix = cx + inner_pad;
        const float iy = cells_y + inner_pad;
        const float iw = std::max(0.0f, cell_w - 2.0f * inner_pad);
        const float ih = std::max(0.0f, cell_h - 2.0f * inner_pad);

        // Cell backdrop.
        const VividColor bg{0.11f, 0.12f, 0.14f, 0.95f};
        vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih, bg,
            {0, 0, 0, 0}, 3.0f);

        // Hit fill.
        if (is_hit) {
            const float a = is_now ? 1.0f : 0.72f;
            const VividColor fill{
                th.accent.r, th.accent.g, th.accent.b, a};
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih, fill,
                {0, 0, 0, 0}, 3.0f);
        }

        // Current-step outline (even if it's a rest).
        if (is_now && d.draw_rect) {
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih,
                {0, 0, 0, 0},
                {th.bright_text.r, th.bright_text.g,
                 th.bright_text.b, 0.95f}, 3.0f, 1.5f);
        }

        // Step 0 origin marker — a small tick below the cell so users can
        // see where rotation=0 is.
        if (is_start && d.draw_rect) {
            d.draw_rect(o, ix + iw * 0.5f - 1.0f,
                        iy + ih + 1.0f, 2.0f, 3.0f,
                        {th.dim_text.r, th.dim_text.g,
                         th.dim_text.b, 0.8f});
        }
    }

    // ---- Side panel ----
    vivid::draw_ui::draw_panel(d, o, side_x, side_y, side_w, side_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.85f},
        {th.separator.r, th.separator.g, th.separator.b, 0.8f}, 4.0f, 1.0f);

    if (d.draw_text) {
        constexpr float kSpPad = 10.0f;
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad, "Density presets",
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f}, 1.0f);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 18.0f,
            "press D to cycle, or click a row",
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, 0.75f);

        // Find the active preset (if hits/steps match one exactly).
        int active_preset = -1;
        for (int i = 0; i < eu::kDensityPresetCount; ++i) {
            if (eu::kDensityPresets[i].hits == hits &&
                eu::kDensityPresets[i].steps == steps) {
                active_preset = i;
                break;
            }
        }

        for (int i = 0; i < eu::kDensityPresetCount; ++i) {
            const float row_y = preset_y0 + static_cast<float>(i) * preset_row_h;
            const bool hover  = (mouse.x >= side_x && mouse.x < side_x + side_w &&
                                 mouse.y >= row_y && mouse.y < row_y + preset_row_h);
            const bool active = (i == active_preset);

            if ((hover || active) && d.draw_rect) {
                const VividColor bg = active
                    ? VividColor{th.accent.r, th.accent.g, th.accent.b, 0.35f}
                    : VividColor{th.accent.r, th.accent.g, th.accent.b, 0.12f};
                d.draw_rect(o, side_x + 2.0f, row_y,
                            side_w - 4.0f, preset_row_h - 1.0f, bg);
            }
            d.draw_text(o, side_x + kSpPad, row_y + 4.0f,
                eu::kDensityPresets[i].label,
                active
                    ? VividColor{th.bright_text.r, th.bright_text.g,
                                 th.bright_text.b, 0.95f}
                    : VividColor{th.dim_text.r, th.dim_text.g,
                                 th.dim_text.b, 0.85f},
                0.9f);
        }
    }
}
