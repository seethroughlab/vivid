#pragma once
//
// Compact editor-toolbar layout helpers.
//
// These helpers sit above Rect/LayoutCursor but below the widgets in
// editor_ui.h. They deliberately do not own widget state and do not emit
// commands; operators still place ui_button/ui_toggle/ui_radio/ui_text_field
// into the returned rects.

#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_ui/geometry.h"
#include "operator_api/editor_ui/introspection.h"
#include "operator_api/types.h"

#include <algorithm>
#include <cstring>

namespace vivid::ui {

struct ToolbarRow {
    Rect bounds{};
    float cursor_x = 0.0f;
    float end_x = 0.0f;
    float gap = 6.0f;
};

struct ToolbarSection {
    Rect bounds{};
    Rect content{};
    float cursor_x = 0.0f;
    float gap = 6.0f;
};

inline ToolbarRow toolbar_row(Rect bounds, float pad = 8.0f, float gap = 8.0f) {
    ToolbarRow row{};
    row.bounds = bounds;
    row.cursor_x = bounds.x + pad;
    row.end_x = bounds.x + std::max(0.0f, bounds.w) - pad;
    row.gap = gap;
    return row;
}

inline Rect toolbar_reserve_right(ToolbarRow& row, float width) {
    const float w = std::clamp(width, 0.0f, std::max(0.0f, row.end_x - row.cursor_x));
    Rect out{row.end_x - w, row.bounds.y, w, row.bounds.h};
    row.end_x = std::max(row.cursor_x, out.x - row.gap);
    return out;
}

inline ToolbarSection toolbar_section(VividEditorContext& ctx,
                                      ToolbarRow& row,
                                      const char* label,
                                      float requested_w,
                                      float min_w = 0.0f,
                                      float label_w = 42.0f) {
    const float available = std::max(0.0f, row.end_x - row.cursor_x);
    float w = std::min(requested_w, available);
    if (available >= min_w) w = std::max(w, min_w);
    if (available < min_w) w = available;

    ToolbarSection section{};
    section.bounds = Rect{row.cursor_x, row.bounds.y, w, row.bounds.h};
    const float content_x = section.bounds.x + std::max(0.0f, label_w);
    section.content = Rect{
        content_x,
        section.bounds.y,
        std::max(0.0f, section.bounds.x + section.bounds.w - content_x),
        section.bounds.h
    };
    section.cursor_x = section.content.x;
    section.gap = row.gap;
    row.cursor_x += w + row.gap;

    auto& d = ctx.draw;
    void* o = d.opaque;
    const auto& th = ctx.theme;
    if (d.draw_rounded_rect) {
        d.draw_rounded_rect(o, section.bounds.x, section.bounds.y,
                            section.bounds.w, section.bounds.h, 4.0f,
                            {0.10f, 0.105f, 0.12f, 0.78f});
    } else if (d.draw_rect) {
        d.draw_rect(o, section.bounds.x, section.bounds.y,
                    section.bounds.w, section.bounds.h,
                    {0.10f, 0.105f, 0.12f, 0.78f});
    }
    if (label && *label && d.draw_text) {
        d.draw_text(o, section.bounds.x + 6.0f, section.bounds.y + 7.0f,
                    label, {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.56f}, 0.68f);
    }

    VividIntrospectWidget iw{};
    iw.kind = "toolbar_section";
    iw.x = section.bounds.x;
    iw.y = section.bounds.y;
    iw.w = section.bounds.w;
    iw.h = section.bounds.h;
    iw.label = label;
    introspect_emit(ctx, iw);

    return section;
}

inline Rect toolbar_item(ToolbarSection& section, float width, float inset_y = 1.0f) {
    const float right = section.bounds.x + section.bounds.w - 6.0f;
    const float w = std::min(width, std::max(0.0f, right - section.cursor_x));
    Rect out{section.cursor_x, section.bounds.y + inset_y,
             w, std::max(0.0f, section.bounds.h - inset_y * 2.0f)};
    section.cursor_x += w + section.gap;
    return out;
}

inline Rect toolbar_remaining(ToolbarSection& section, float inset_y = 1.0f) {
    const float right = section.bounds.x + section.bounds.w - 6.0f;
    return Rect{section.cursor_x, section.bounds.y + inset_y,
                std::max(0.0f, right - section.cursor_x),
                std::max(0.0f, section.bounds.h - inset_y * 2.0f)};
}

inline void toolbar_text(VividEditorContext& ctx, Rect r, const char* text,
                         VividColor color, float scale = 0.82f,
                         float align = 0.0f) {
    if (!text || !*text || r.w <= 0.0f || r.h <= 0.0f) return;
    auto& d = ctx.draw;
    void* o = d.opaque;
    if (d.push_clip_rect) d.push_clip_rect(o, r.x, r.y, r.w, r.h);
    const float lh = d.line_height ? d.line_height(o) * scale : 12.0f * scale;
    vivid::draw_ui::draw_text_aligned(d, o, r.x, r.y + std::max(0.0f, (r.h - lh) * 0.5f - 1.0f),
                                      r.w, text, color, scale, align);
    if (d.pop_clip_rect) d.pop_clip_rect(o);
}

inline void toolbar_value_pill(VividEditorContext& ctx, Rect r, const char* text,
                               bool active = false) {
    auto& d = ctx.draw;
    void* o = d.opaque;
    vivid::draw_ui::draw_value_badge(
        d, o, r.x, r.y, r.w, r.h, text ? text : "",
        active ? VividColor{0.22f, 0.34f, 0.45f, 0.95f}
               : VividColor{0.16f, 0.16f, 0.18f, 0.9f},
        VividColor{0.86f, 0.88f, 0.92f, 0.95f},
        4.0f, 0.82f);
}

inline bool toolbar_rects_overlap(Rect a, Rect b, float epsilon = 0.25f) {
    return a.x + a.w > b.x + epsilon && b.x + b.w > a.x + epsilon &&
           a.y + a.h > b.y + epsilon && b.y + b.h > a.y + epsilon;
}

} // namespace vivid::ui
