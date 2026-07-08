#pragma once
// UI-4b: host implementation of the operator draw ABI (VividDrawAPI). Wraps a host Renderer2D
// into the C callback struct an operator-exported inspector/editor draws through, translating the
// operator's LOCAL pixel space (0,0 = top-left of its surface) to screen space by a fixed offset,
// and clamping clip rects to the hosting region. Header-only + inline: the trampolines are tiny
// and there's no per-frame state beyond the caller-owned DrawBridge.
//
// Reusable for both the custom-editor detail-region host and (later) a floated editor window.
#include "operator_api/types.h"
#include "ui/renderer_2d.h"

#include <algorithm>
#include <vector>

namespace vivid::ui {

// The binding an operator draw call resolves against: the renderer, the local→screen offset, and
// the screen-space region the operator owns (used to clamp its clip rects so it can't draw over
// the rest of the host). One per draw_editor/draw_inspector dispatch; lives on the caller's stack.
struct DrawBridge {
    Renderer2D* r  = nullptr;
    float ox = 0.f, oy = 0.f;             // add to local coords → screen coords
    float rx = 0.f, ry = 0.f, rw = 0.f, rh = 0.f;   // screen-space region bounds (clip clamp)
};

namespace opdraw_detail {
inline DrawBridge* B(void* o) { return static_cast<DrawBridge*>(o); }

inline void t_rect(void* o, float x, float y, float w, float h, VividColor c) {
    auto* b = B(o); b->r->draw_rect(b->ox + x, b->oy + y, w, h, c.r, c.g, c.b, c.a);
}
inline void t_rrect(void* o, float x, float y, float w, float h, float rad, VividColor c) {
    auto* b = B(o); b->r->draw_rounded_rect(b->ox + x, b->oy + y, w, h, rad, c.r, c.g, c.b, c.a);
}
inline void t_text(void* o, float x, float y, const char* s, VividColor c, float sc) {
    auto* b = B(o); b->r->draw_text(b->ox + x, b->oy + y, s ? s : "", c.r, c.g, c.b, c.a, sc);
}
inline void t_line(void* o, float x1, float y1, float x2, float y2, float th, VividColor c) {
    auto* b = B(o); b->r->draw_line(b->ox + x1, b->oy + y1, b->ox + x2, b->oy + y2, th, c.r, c.g, c.b, c.a);
}
inline float t_twidth(void* o, const char* s, float sc) { return B(o)->r->text_width(s ? s : "", sc); }
inline float t_lheight(void* o) { return B(o)->r->line_height(); }

inline void t_clip(void* o, float x, float y, float w, float h) {
    auto* b = B(o);
    // Intersect the operator's (offset) clip rect with the region it owns.
    const float x0 = std::max(b->ox + x, b->rx), y0 = std::max(b->oy + y, b->ry);
    const float x1 = std::min(b->ox + x + w, b->rx + b->rw), y1 = std::min(b->oy + y + h, b->ry + b->rh);
    b->r->push_clip_rect(x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
}
inline void t_unclip(void* o) { B(o)->r->pop_clip_rect(); }

inline void t_tri(void* o, float x0, float y0, float x1, float y1, float x2, float y2, VividColor c) {
    auto* b = B(o); b->r->draw_tri(b->ox + x0, b->oy + y0, b->ox + x1, b->oy + y1, b->ox + x2, b->oy + y2, c.r, c.g, c.b, c.a);
}
inline void t_arc(void* o, float cx, float cy, float rad, float a0, float a1, float th, int seg, VividColor c) {
    auto* b = B(o); b->r->draw_arc(b->ox + cx, b->oy + cy, rad, a0, a1, th, seg, c.r, c.g, c.b, c.a);
}
inline float t_wrapped(void* o, float x, float y, const char* s, float mw, VividColor c, float sc) {
    auto* b = B(o); return b->r->draw_text_wrapped(b->ox + x, b->oy + y, s ? s : "", mw, c.r, c.g, c.b, c.a, sc);
}
inline void t_circle(void* o, float cx, float cy, float rad, float th, VividColor c) {
    auto* b = B(o); b->r->draw_circle(b->ox + cx, b->oy + cy, rad, th, c.r, c.g, c.b, c.a);
}
inline void t_dashed(void* o, float x1, float y1, float x2, float y2, float th, float dl, float gl, VividColor c) {
    auto* b = B(o); b->r->draw_dashed_line(b->ox + x1, b->oy + y1, b->ox + x2, b->oy + y2, th, dl, gl, c.r, c.g, c.b, c.a);
}
inline void t_polyline(void* o, const float* xs, const float* ys, uint32_t n, float th, VividColor c) {
    auto* b = B(o);
    if (!xs || !ys || n == 0) return;
    std::vector<float> px(n), py(n);   // offset into screen space (UI-rate, not RT)
    for (uint32_t i = 0; i < n; ++i) { px[i] = b->ox + xs[i]; py[i] = b->oy + ys[i]; }
    b->r->draw_polyline(px.data(), py.data(), n, th, c.r, c.g, c.b, c.a);
}
}  // namespace opdraw_detail

// Build a VividDrawAPI whose callbacks render through `b` (which must outlive the returned struct
// and every call made on it — i.e. the whole draw_editor/draw_inspector dispatch).
inline VividDrawAPI make_op_draw_api(DrawBridge* b) {
    VividDrawAPI d{};
    d.opaque           = b;
    d.draw_rect        = opdraw_detail::t_rect;
    d.draw_rounded_rect= opdraw_detail::t_rrect;
    d.draw_text        = opdraw_detail::t_text;
    d.draw_line        = opdraw_detail::t_line;
    d.text_width       = opdraw_detail::t_twidth;
    d.line_height      = opdraw_detail::t_lheight;
    d.push_clip_rect   = opdraw_detail::t_clip;
    d.pop_clip_rect    = opdraw_detail::t_unclip;
    d.draw_tri         = opdraw_detail::t_tri;
    d.draw_arc         = opdraw_detail::t_arc;
    d.draw_text_wrapped= opdraw_detail::t_wrapped;
    d.draw_circle      = opdraw_detail::t_circle;
    d.draw_dashed_line = opdraw_detail::t_dashed;
    d.draw_polyline    = opdraw_detail::t_polyline;
    return d;
}

}  // namespace vivid::ui
