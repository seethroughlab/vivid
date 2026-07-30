#pragma once
#include "ui/renderer_2d.h"
#include "ui/layout.h"   // Rect (pure geometry; no dependency back on this file)
#include <cmath>
#include <algorithm>
#include <string>

// Shared visual design language for the Vivid UI: a design-token palette (surfaces,
// a region/panel system, a spacing scale, a type ramp, domain accents) + reusable
// widget helpers (panel, knob, card, section header, text fitting) built on
// Renderer2D's primitives. Keeps colours/metrics out of the draw code so the
// interface reads cohesively — bounded regions on a disciplined grid ("clean pro-DAW").
namespace vivid::ui {

struct Style {
    // --- surfaces (vivid-classic "dark steel": flat, cool, near-black; framed by 1px rules, not fills) ---
    float bg[3]      = { 0.071f, 0.071f, 0.078f };   // window canvas        (#121214)
    float region[3]  = { 0.098f, 0.110f, 0.125f };   // a bounded panel's interior (#191C20)
    float region_hd[3]={ 0.141f, 0.153f, 0.188f };   // a panel's header strip (#242730)
    float panel[3]   = { 0.090f, 0.101f, 0.117f };   // legacy chrome / menus
    float card[3]    = { 0.122f, 0.122f, 0.141f };   // node/card body       (#1F1F24)
    float card_hi[3] = { 0.176f, 0.220f, 0.286f };   // hover / selected fill (#2D3849)
    float recess[3]  = { 0.078f, 0.090f, 0.098f };   // inset wells (inputs, sliders) (#141719)
    // --- lines ---
    float border[3]     = { 0.220f, 0.251f, 0.294f }; // visible 1px panel/cell frame (#38404B)
    float border_soft[3]= { 0.149f, 0.165f, 0.192f }; // internal dividers (#262A31)
    // --- text ---
    float text[3]    = { 0.900f, 0.920f, 0.950f };   // primary  (#E6EBEF)
    float body[3]    = { 0.700f, 0.730f, 0.780f };   // secondary
    float dim[3]     = { 0.550f, 0.580f, 0.620f };   // labels / hints (#8D9499; legible on steel)
    // --- domain accents (unchanged — the strict-zone identity system) ---
    float audio[3]   = { 0.94f, 0.63f, 0.19f };      // amber  (audio / instrument)
    float gpu[3]     = { 0.35f, 0.66f, 0.90f };      // cyan   (visual / gpu)
    float fx[3]      = { 0.60f, 0.45f, 0.85f };      // violet (effects)
    float control[3] = { 0.55f, 0.60f, 0.66f };      // gray   (control)
    float teal[3]    = { 0.31f, 0.80f, 0.75f };      // bridge (data sources)
    float mod[3]     = { 0.90f, 0.42f, 0.85f };      // magenta (ADR-0022 modulation: control edge arc + live dot)
    float sel[3]     = { 0.353f, 0.549f, 0.851f };   // selection / focus frame (#5A8CD9, classic blue)
    float gold[3]    = { 0.95f, 0.78f, 0.30f };      // queued / warn
    float green[3]   = { 0.30f, 0.80f, 0.50f };      // meter / level
    float red[3]     = { 0.90f, 0.24f, 0.28f };      // error / broken (dot, node border, record disc)
    // --- spacing scale (logical px) ---
    float s1 = 2.f, s2 = 4.f, s3 = 6.f, s4 = 8.f, s5 = 12.f, s6 = 16.f, s7 = 24.f;
    // --- type ramp (scale factor on the 15px base font) ---
    float fs_kicker = 0.66f;   // UPPERCASE region/section labels
    float fs_value  = 0.70f;   // numeric read-outs
    float fs_label  = 0.76f;   // control labels
    float fs_body   = 0.88f;   // names / body
    float fs_title  = 1.02f;   // panel titles
    float fs_brand  = 1.18f;   // the wordmark
    // --- radii / bars: hard 90° angles (serious-tool look). accent_bar = the edge identity stripe. ---
    float radius = 0.f, radius_lg = 0.f, accent_bar = 3.f;
    float panel_hd_h = 22.f;   // region header strip height
};
inline const Style& style() { static const Style s; return s; }

// The domain identity of a region (ADR-0013, UI-0). Every panel/zone carries a domain so
// the user always knows which world they're in — audio (amber), visual (cyan), the bridge
// (teal), or shared transport/chrome (gray). `domain_color` returns the accent to tint a
// region header / accent bar with; audio and visual never share a rectangle (strict zones).
enum class Domain { Audio, Visual, Bridge, Shared };
inline const float* domain_color(Domain d) {
    const Style& s = style();
    switch (d) {
        case Domain::Audio:  return s.audio;    // amber
        case Domain::Visual: return s.gpu;      // cyan
        case Domain::Bridge: return s.teal;     // teal
        case Domain::Shared: default: return s.control;  // gray
    }
}
inline const char* domain_label(Domain d) {
    switch (d) {
        case Domain::Audio:  return "AUDIO";
        case Domain::Visual: return "VISUAL";
        case Domain::Bridge: return "BRIDGE";
        case Domain::Shared: default: return "";
    }
}

// Truncate `str` with a trailing ellipsis so it fits within `max_w` at `scale`.
inline std::string fit_text(Renderer2D& r, const std::string& str, float max_w, float scale) {
    if (str.empty() || r.text_width(str.c_str(), scale) <= max_w) return str;
    std::string out = str;
    const std::string ell = "\xE2\x80\xA6";
    while (!out.empty() && r.text_width((out + ell).c_str(), scale) > max_w) out.pop_back();
    return out + ell;
}
// Right-align text so its right edge lands at `rx`.
inline void draw_text_r(Renderer2D& r, float rx, float y, const char* t, const float* c, float a, float scale) {
    r.draw_text(rx - r.text_width(t, scale), y, t, c[0], c[1], c[2], a, scale);
}

// A bounded region: interior fill + a 1px hairline border + a header strip with an
// accent tick and an UPPERCASE title. Returns the inner content rect (inside the
// header + one unit of padding), so callers lay content out relative to it.
inline Rect panel(Renderer2D& r, Rect b, const char* title, const float* accent) {
    const Style& s = style();
    r.draw_rect(b.x, b.y, b.w, b.h, s.region[0], s.region[1], s.region[2], 1.0f);                                 // flat interior
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);                    // hard 1px frame
    const float hh = s.panel_hd_h;
    r.draw_rect(b.x + 1.f, b.y + hh, b.w - 2.f, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);  // header rule
    if (title) {
        r.draw_rect(b.x + s.s4, b.y + 7.f, 3.f, hh - 13.f, accent[0], accent[1], accent[2], 1.0f);                 // accent tick
        r.draw_text(b.x + s.s4 + 8.f, b.y + 6.f, title, s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
    }
    return { b.x + s.s4, b.y + hh + s.s3, b.w - 2.f * s.s4, b.h - hh - 2.f * s.s3 };
}

// A region FRAME (1px border + a filled header strip) that does NOT fill its interior,
// for panels drawn over GPU content (the viewer) or the node graph, which paint the
// body themselves. Returns the inner content rect (below the header).
inline Rect panel_frame(Renderer2D& r, Rect b, const char* title, const float* accent) {
    const Style& s = style();
    const float hh = s.panel_hd_h;
    r.draw_rect(b.x, b.y, b.w, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    r.draw_rect(b.x, b.y + b.h - 1.f, b.w, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    r.draw_rect(b.x, b.y, 1.f, b.h, s.border[0], s.border[1], s.border[2], 1.0f);
    r.draw_rect(b.x + b.w - 1.f, b.y, 1.f, b.h, s.border[0], s.border[1], s.border[2], 1.0f);
    r.draw_rect(b.x + 1.f, b.y + 1.f, b.w - 2.f, hh - 1.f, s.region_hd[0], s.region_hd[1], s.region_hd[2], 1.0f);
    r.draw_rect(b.x + 1.f, b.y + hh, b.w - 2.f, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
    if (title) {
        r.draw_rect(b.x + s.s4, b.y + 7.f, 3.f, hh - 13.f, accent[0], accent[1], accent[2], 1.0f);
        r.draw_text(b.x + s.s4 + 8.f, b.y + 6.f, title, s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
    }
    return { b.x + s.s4, b.y + hh + s.s3, b.w - 2.f * s.s4, b.h - hh - 2.f * s.s3 };
}

// A card: a flat framed box — solid fill, a thin accent bar across the top, a hard 1px frame.
// Selected/hot swaps the fill up and the frame to the blue selection accent (not a nested ring).
inline void draw_card(Renderer2D& r, float x, float y, float w, float h,
                      const float* accent, bool hot = false) {
    const Style& s = style();
    const float* bg = hot ? s.card_hi : s.card;
    r.draw_rect(x, y, w, h, bg[0], bg[1], bg[2], 1.0f);
    r.draw_rect(x, y, w, s.accent_bar, accent[0], accent[1], accent[2], 1.0f);
    const float* fr = hot ? s.sel : s.border_soft;
    r.draw_rect_outline(x, y, w, h, 1.f, fr[0], fr[1], fr[2], 1.0f);
}

// A small left-aligned section label in dim text with an accent tick.
inline void section_header(Renderer2D& r, float x, float y, const char* label,
                           const float* accent) {
    const Style& s = style();
    r.draw_rect(x, y + 1.f, 3.f, 9.f, accent[0], accent[1], accent[2], 1.0f);
    r.draw_text(x + 8.f, y, label, s.dim[0], s.dim[1], s.dim[2], 1.0f, 0.78f);
}

enum class AccentEdge { None, Left, Top };

// One standard interactive item substrate: flat fill, optional accent edge, 1px frame.
// Use for clips, track headers, compact buttons, menu rows, graph palette rows, and chips.
inline void item_box(Renderer2D& r, Rect b, const float* accent,
                     bool hot = false, bool selected = false,
                     AccentEdge edge = AccentEdge::Left) {
    const Style& s = style();
    const float* bg = hot ? s.card_hi : s.card;
    r.draw_rect(b.x, b.y, b.w, b.h, bg[0], bg[1], bg[2], 1.0f);
    if (accent && edge == AccentEdge::Left)
        r.draw_rect(b.x, b.y, s.accent_bar, b.h, accent[0], accent[1], accent[2], 1.0f);
    else if (accent && edge == AccentEdge::Top)
        r.draw_rect(b.x, b.y, b.w, s.accent_bar, accent[0], accent[1], accent[2], 1.0f);
    const float* fr = selected ? s.sel : s.border_soft;
    r.draw_rect_outline(b.x, b.y, b.w, b.h, selected ? 2.f : 1.f, fr[0], fr[1], fr[2], 1.0f);
}

inline void item_box(Renderer2D& r, float x, float y, float w, float h, const float* accent,
                     bool hot = false, bool selected = false,
                     AccentEdge edge = AccentEdge::Left) {
    item_box(r, { x, y, w, h }, accent, hot, selected, edge);
}

// A dark inset well for content: previews, thumbnails, meters, piano-roll substrates, and pads.
inline void recess(Renderer2D& r, Rect b, bool framed = false) {
    const Style& s = style();
    r.draw_rect(b.x, b.y, b.w, b.h, s.recess[0], s.recess[1], s.recess[2], 1.0f);
    if (framed)
        r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
}

inline void recess(Renderer2D& r, float x, float y, float w, float h, bool framed = false) {
    recess(r, { x, y, w, h }, framed);
}

inline void separator(Renderer2D& r, float x, float y, float w) {
    const Style& s = style();
    r.draw_rect(x, y, w, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
}

// Session is a primary workspace, not a framed panel. These helpers draw
// structure with rules, accents, and state affordances instead of nested boxes.
inline void session_workspace_header(Renderer2D& r, Rect b, const char* title, const float* accent) {
    const Style& s = style();
    const float hh = s.panel_hd_h;
    if (title) {
        r.draw_rect(b.x + s.s4, b.y + 7.f, 3.f, hh - 13.f, accent[0], accent[1], accent[2], 1.0f);
        r.draw_text(b.x + s.s4 + 8.f, b.y + 6.f, title, s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
    }
    r.draw_rect(b.x + s.s4, b.y + hh, b.w - 2.f * s.s4, 1.f,
                s.border_soft[0], s.border_soft[1], s.border_soft[2], 0.9f);
}

inline void session_header_cell(Renderer2D& r, Rect b, const float* accent, bool hot = false) {
    const Style& s = style();
    if (hot)
        r.draw_rect(b.x, b.y, b.w, b.h, s.card_hi[0], s.card_hi[1], s.card_hi[2], 0.72f);
    if (accent)
        r.draw_rect(b.x, b.y + 3.f, s.accent_bar, b.h - 6.f, accent[0], accent[1], accent[2], 0.95f);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f,
                        hot ? s.border[0] : s.border_soft[0],
                        hot ? s.border[1] : s.border_soft[1],
                        hot ? s.border[2] : s.border_soft[2],
                        hot ? 0.95f : 0.75f);
}

inline void session_scene_button(Renderer2D& r, Rect b, const float* accent, bool hot = false) {
    const Style& s = style();
    if (hot)
        r.draw_rect(b.x, b.y, b.w, b.h, s.card_hi[0], s.card_hi[1], s.card_hi[2], 0.68f);
    if (accent)
        r.draw_rect(b.x, b.y, s.accent_bar, b.h, accent[0], accent[1], accent[2], hot ? 0.9f : 0.55f);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f,
                        hot ? s.border[0] : s.border_soft[0],
                        hot ? s.border[1] : s.border_soft[1],
                        hot ? s.border[2] : s.border_soft[2],
                        hot ? 0.9f : 0.7f);
}

inline void session_control_button(Renderer2D& r, Rect b, const float* accent,
                                   bool hot = false, bool selected = false) {
    const Style& s = style();
    if (hot || selected)
        r.draw_rect(b.x, b.y, b.w, b.h,
                    selected ? s.card_hi[0] : s.region[0],
                    selected ? s.card_hi[1] : s.region[1],
                    selected ? s.card_hi[2] : s.region[2],
                    selected ? 0.92f : 0.82f);
    r.draw_rect(b.x, b.y + b.h - 1.f, b.w, 1.f,
                selected ? accent[0] : s.border_soft[0],
                selected ? accent[1] : s.border_soft[1],
                selected ? accent[2] : s.border_soft[2],
                selected ? 1.0f : 0.65f);
    if (hot && !selected)
        r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border[0], s.border[1], s.border[2], 0.65f);
}

inline void session_meter_track(Renderer2D& r, Rect b) {
    const Style& s = style();
    r.draw_rect(b.x, b.y + b.h - 1.f, b.w, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 0.7f);
}

inline void session_clip_cell(Renderer2D& r, Rect b, const float* accent,
                              bool hot = false, bool active = false, bool queued = false,
                              float title_h = 14.f, bool empty = false) {
    const Style& s = style();
    // An EMPTY slot (no clip / generator) reads as empty: a plain recessed rectangle with NO accent
    // title strip, so it can't be mistaken for a clip whose thumbnail failed to render. Even an
    // active/queued empty slot drops the strip — its selection ring, ▶ and queued flash still mark
    // it as the live cell, so the "empty" read and the "active" read don't conflict.
    const bool blank = empty;
    const float br = active ? 0.115f : s.recess[0] * (blank ? 0.80f : 0.92f);
    const float bg = active ? 0.130f : s.recess[1] * (blank ? 0.76f : 0.88f);
    const float bb = active ? 0.155f : s.recess[2] * (blank ? 0.86f : 0.98f);
    r.draw_rect(b.x, b.y, b.w, b.h, br, bg, bb, 1.0f);
    const float k = (active ? 0.50f : 0.24f) + (hot ? 0.08f : 0.f);
    if (!blank)   // filled/generator/active cells carry the accent title strip; a blank slot does not
        r.draw_rect(b.x, b.y, b.w, title_h + 1.f, accent[0] * k, accent[1] * k, accent[2] * k, 1.0f);
    if (queued)
        r.draw_rect(b.x, b.y, b.w, 2.f, s.gold[0], s.gold[1], s.gold[2], 1.0f);
    if (active)
        r.draw_rect_outline(b.x, b.y, b.w, b.h, 2.f, s.sel[0], s.sel[1], s.sel[2], 0.95f);
    else
        r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f,
                            hot || queued ? s.border[0] : s.border_soft[0],
                            hot || queued ? s.border[1] : s.border_soft[1],
                            hot || queued ? s.border[2] : s.border_soft[2],
                            hot || queued ? 0.85f : 0.72f);
}

// Modal/menu shell: flat panel fill, 1px frame, standard accent/header rule.
inline Rect overlay_panel(Renderer2D& r, Rect b, const char* title, const float* accent,
                          bool scrim = false, Rect scrim_bounds = {}) {
    const Style& s = style();
    if (scrim)
        r.draw_rect(scrim_bounds.x, scrim_bounds.y, scrim_bounds.w, scrim_bounds.h, 0.f, 0.f, 0.f, 0.45f);
    r.draw_shadow(b.x, b.y, b.w, b.h);
    r.draw_rect(b.x, b.y, b.w, b.h, s.panel[0], s.panel[1], s.panel[2], 1.0f);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    const float hh = s.panel_hd_h;
    if (accent)
        r.draw_rect(b.x, b.y, b.w, s.accent_bar, accent[0], accent[1], accent[2], 1.0f);
    r.draw_rect(b.x + 1.f, b.y + hh, b.w - 2.f, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
    if (title)
        r.draw_text(b.x + s.s5, b.y + 6.f, title, s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
    return { b.x + s.s4, b.y + hh + s.s2, b.w - 2.f * s.s4, b.h - hh - s.s4 };
}

inline void toolbar_button(Renderer2D& r, Rect b, bool hot = false, bool selected = false) {
    item_box(r, b, nullptr, hot || selected, selected, AccentEdge::None);
}

// The dock's top-edge resize strip + centered grip (the "drag me" mark). detail_dock draws this inline
// for the param/audio views; it's exposed standalone so the docked clip editor — which paints its own
// chrome and skips detail_dock — can draw it too, keeping the dock resizable in every mode.
inline void dock_resize_strip(Renderer2D& r, float bx, float by, float bw, bool resize_hot) {
    const Style& s = style();
    const float* rc = resize_hot ? s.gpu : s.border_soft;
    r.draw_rect(bx, by - 1.f, bw, 2.f, rc[0], rc[1], rc[2], 1.0f);
    const float gw = 28.f, gx = bx + bw * 0.5f - gw * 0.5f;
    const float* gc = resize_hot ? s.gpu : s.border;
    r.draw_rect(gx, by - 2.f, gw, 4.f, s.recess[0], s.recess[1], s.recess[2], 1.0f);   // recessed well
    for (int i = 0; i < 3; ++i)   // three hard rules = the grip
        r.draw_rect(gx + gw * 0.5f - 5.f + i * 5.f, by - 2.f, 1.f, 4.f, gc[0], gc[1], gc[2], 1.0f);
}

// Full-width focused detail region, used by the bottom dock/editor area.
inline Rect detail_dock(Renderer2D& r, Rect b, const float* accent, bool resize_hot = false) {
    const Style& s = style();
    r.draw_rect(b.x, b.y, b.w, b.h, s.panel[0], s.panel[1], s.panel[2], 1.0f);
    // The top edge doubles as the horizontal resize splitter — styled to match the vertical
    // DAW|visuals splitter (gpu when hot, border_soft idle) + a centered grip, so the two
    // dividers read identically. See the vertical splitter in session_view.cpp.
    const float* rc = resize_hot ? s.gpu : s.border_soft;
    r.draw_rect(b.x, b.y - 1.f, b.w, 2.f, rc[0], rc[1], rc[2], 1.0f);
    const float hh = 20.f;
    r.draw_rect(b.x, b.y, b.w, hh, s.region_hd[0], s.region_hd[1], s.region_hd[2], 1.0f);
    r.draw_rect(b.x, b.y + hh, b.w, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
    if (accent)
        r.draw_rect(b.x, b.y, 4.f, hh, accent[0], accent[1], accent[2], 1.0f);
    // Centered grip = the "you can drag me" mark (mirror of the vertical splitter's grip).
    { const float gw = 28.f, gx = b.x + b.w * 0.5f - gw * 0.5f;
      const float* gc = resize_hot ? s.gpu : s.border;
      r.draw_rect(gx, b.y - 2.f, gw, 4.f, s.recess[0], s.recess[1], s.recess[2], 1.0f);   // recessed well
      for (int i = 0; i < 3; ++i)   // three hard rules = the grip
          r.draw_rect(gx + gw * 0.5f - 5.f + i * 5.f, b.y - 2.f, 1.f, 4.f, gc[0], gc[1], gc[2], 1.0f); }
    return { b.x + s.s4, b.y + hh + s.s3, b.w - 2.f * s.s4, b.h - hh - 2.f * s.s3 };
}

// Session clip cells have a compact title strip plus an optional queued bar.
inline void clip_cell_box(Renderer2D& r, Rect b, const float* accent,
                          bool hot = false, bool active = false, bool queued = false,
                          float title_h = 14.f) {
    const Style& s = style();
    const float br = active ? 0.115f : s.recess[0] * 0.90f;
    const float bg = active ? 0.130f : s.recess[1] * 0.86f;
    const float bb = active ? 0.155f : s.recess[2] * 0.96f;
    r.draw_rect(b.x, b.y, b.w, b.h, br, bg, bb, 1.0f);
    const float k = (active ? 0.50f : 0.22f) + (hot ? 0.06f : 0.f);
    r.draw_rect(b.x + 1.f, b.y + 1.f, b.w - 2.f, title_h, accent[0] * k, accent[1] * k, accent[2] * k, 1.0f);
    if (queued)
        r.draw_rect(b.x, b.y, b.w, 2.f, s.gold[0], s.gold[1], s.gold[2], 1.0f);
    const float* fr = active ? s.sel : s.border_soft;
    r.draw_rect_outline(b.x, b.y, b.w, b.h, active ? 2.f : 1.f, fr[0], fr[1], fr[2], active ? 0.95f : 0.75f);
}

inline Rect editor_panel(Renderer2D& r, Rect b, const char* title, const float* accent,
                         float header_h = 30.f) {
    const Style& s = style();
    r.draw_rect(b.x, b.y, b.w, b.h, s.region[0], s.region[1], s.region[2], 1.0f);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    if (accent)
        r.draw_rect(b.x, b.y, b.w, s.accent_bar, accent[0], accent[1], accent[2], 1.0f);
    r.draw_rect(b.x + 1.f, b.y + 1.f, b.w - 2.f, header_h - 1.f, s.region_hd[0], s.region_hd[1], s.region_hd[2], 1.0f);
    r.draw_rect(b.x + 1.f, b.y + header_h, b.w - 2.f, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
    if (title)
        r.draw_text(b.x + s.s5, b.y + 9.f, title, s.text[0], s.text[1], s.text[2], 1.0f, s.fs_title);
    return { b.x + s.s4, b.y + header_h + s.s4, b.w - 2.f * s.s4, b.h - header_h - 2.f * s.s4 };
}

// A rotary knob: dim track arc + accent value arc + pointer; name above, value
// below (both centred on cx). `mapped` tints the value arc toward the bridge teal.
inline void knob(Renderer2D& r, float cx, float cy, float rad, float v01,
                 const char* label, const char* valtext, const float* accent, bool mapped = false) {
    v01 = v01 < 0.f ? 0.f : (v01 > 1.f ? 1.f : v01);
    const Style& s = style();
    const float a0 = 2.3562f;                 // 135 deg (lower-left), sweeping CW (y-down)
    const float sweep = 4.7124f;              // 270 deg
    r.draw_arc(cx, cy, rad, a0, a0 + sweep, 2.6f, 0, s.recess[0] + 0.06f, s.recess[1] + 0.06f, s.recess[2] + 0.07f, 1.0f);  // 0 = auto (dpi-aware)
    const float vr = mapped ? s.teal[0] : accent[0];
    const float vg = mapped ? s.teal[1] : accent[1];
    const float vb = mapped ? s.teal[2] : accent[2];
    if (v01 > 0.001f)
        r.draw_arc(cx, cy, rad, a0, a0 + sweep * v01, 2.6f, 0, vr, vg, vb, 1.0f);
    const float pa = a0 + sweep * v01;        // pointer to the current value
    r.draw_line(cx, cy, cx + std::cos(pa) * (rad - 2.f), cy + std::sin(pa) * (rad - 2.f), 2.0f, s.text[0], s.text[1], s.text[2], 1.0f);
    if (label) {
        const float tw = r.text_width(label, 0.66f);
        r.draw_text(cx - tw * 0.5f, cy - rad - 12.f, label, s.dim[0], s.dim[1], s.dim[2], 1.0f, 0.66f);
    }
    if (valtext) {
        const float tw = r.text_width(valtext, 0.66f);
        r.draw_text(cx - tw * 0.5f, cy + rad + 3.f, valtext, s.text[0], s.text[1], s.text[2], 1.0f, 0.66f);
    }
}

// A horizontal slider: a framed recessed groove + inset accent fill + a real grabbable handle;
// name at the left, value at the right. v01 is the normalized 0..1 position. `mapped` tints the
// fill toward teal; `hot` brightens the handle on hover.
inline void slider(Renderer2D& r, float x, float y, float w, float h, float v01,
                   const char* label, const char* valtext, const float* accent, bool mapped = false, bool hot = false) {
    v01 = v01 < 0.f ? 0.f : (v01 > 1.f ? 1.f : v01);
    const Style& s = style();
    const float th = 6.f, ty = y + h - 9.f;                 // a chunkier groove near the row bottom
    r.draw_rect(x, ty, w, th, s.recess[0], s.recess[1], s.recess[2], 1.0f);
    r.draw_rect_outline(x, ty, w, th, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);   // 1px frame -> reads as a groove
    const float fr = mapped ? s.teal[0] : accent[0], fg = mapped ? s.teal[1] : accent[1], fb = mapped ? s.teal[2] : accent[2];
    if (v01 > 0.001f) r.draw_rect(x + 1.f, ty + 1.f, (w - 2.f) * v01, th - 2.f, fr, fg, fb, 1.0f);   // fill inside the frame
    // a real grabbable handle: a bright bordered block, kept inside the track; brighter on hover
    const float hw = 8.f, hh = th + 6.f, hx = x + (w - hw) * v01, hy = ty - 3.f;
    const float hb = hot ? 1.0f : 0.86f;
    r.draw_rect(hx, hy, hw, hh, s.text[0] * hb, s.text[1] * hb, s.text[2] * hb, 1.0f);
    r.draw_rect_outline(hx, hy, hw, hh, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    if (label) r.draw_text(x, y, label, s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
    if (valtext) {
        const float tw = r.text_width(valtext, s.fs_label);
        r.draw_text(x + w - tw, y, valtext, s.text[0], s.text[1], s.text[2], 1.0f, s.fs_label);
    }
}

// ADR-0022: the modulation overlay for a knob — the thing vivid-classic promised and never drew.
// The handle stays at the BASE (drawn by knob() above); this adds, just outside the knob's ring, a
// magenta ARC spanning where the modulation can push the value ([lo01, hi01] in the same normalized
// space as v01), and a bright DOT at where it IS right now (live01). So the user sees the knob they
// set, the range it can travel, and its live position — all at once. No-op when the param is not
// modulated (the caller gates on `wired`).
inline void knob_mod_overlay(Renderer2D& r, float cx, float cy, float rad,
                             float lo01, float hi01, float live01) {
    const Style& s = style();
    const float a0 = 2.3562f, sweep = 4.7124f;    // MUST match knob(): 135deg start, 270deg sweep
    auto clamp01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
    lo01 = clamp01(lo01); hi01 = clamp01(hi01); live01 = clamp01(live01);
    if (hi01 < lo01) std::swap(lo01, hi01);
    const float rr = rad + 3.5f;                   // sit just outside the value arc
    if (hi01 - lo01 > 0.002f)                      // the reachable range, dim magenta
        r.draw_arc(cx, cy, rr, a0 + sweep * lo01, a0 + sweep * hi01, 2.2f, 0, s.mod[0], s.mod[1], s.mod[2], 0.55f);
    const float la = a0 + sweep * live01;          // the live value, a bright dot on that ring
    r.draw_circle(cx + std::cos(la) * rr, cy + std::sin(la) * rr, 2.6f, 0, s.mod[0], s.mod[1], s.mod[2], 1.0f);
}

// ADR-0022: the modulation overlay for a horizontal slider — the arc's linear twin. A magenta band
// over the groove marks the reachable range; a bright notch marks the live value. Geometry mirrors
// slider() exactly so the band lines up with the fill.
inline void slider_mod_overlay(Renderer2D& r, float x, float y, float w, float h,
                               float lo01, float hi01, float live01) {
    const Style& s = style();
    auto clamp01 = [](float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); };
    lo01 = clamp01(lo01); hi01 = clamp01(hi01); live01 = clamp01(live01);
    if (hi01 < lo01) std::swap(lo01, hi01);
    const float th = 6.f, ty = y + h - 9.f;        // MUST match slider()
    const float my = ty - 3.f;                     // a thin band just above the groove
    if (hi01 - lo01 > 0.002f)
        r.draw_rect(x + (w) * lo01, my, (w) * (hi01 - lo01), 2.5f, s.mod[0], s.mod[1], s.mod[2], 0.55f);
    r.draw_rect(x + w * live01 - 1.f, my - 1.f, 2.f, 4.5f, s.mod[0], s.mod[1], s.mod[2], 1.0f);   // live notch
}

// A toggle switch: a hard rectangular track + a sliding square knob; on = accent, off = recess,
// with a 1px frame so it reads as a control, not a pill.
inline void toggle(Renderer2D& r, float x, float y, float w, float h, bool on, const float* accent) {
    const Style& s = style();
    const float* bg = on ? accent : s.recess;
    r.draw_rect(x, y, w, h, bg[0], bg[1], bg[2], 1.0f);
    r.draw_rect_outline(x, y, w, h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    const float kr = h - 4.f, kx = on ? (x + w - kr - 2.f) : (x + 2.f);
    r.draw_rect(kx, y + 2.f, kr, kr, s.text[0], s.text[1], s.text[2], 1.0f);
}

// A dropdown field: a bordered box showing the current choice + a chevron. The popup
// list itself is drawn separately by the caller (menu state lives with the view).
inline void dropdown_field(Renderer2D& r, float x, float y, float w, float h,
                           const char* current, const float* accent, bool hot) {
    const Style& s = style();
    const float* bg = hot ? s.card_hi : s.card;
    r.draw_rect(x, y, w, h, bg[0], bg[1], bg[2], 1.0f);
    r.draw_rect(x, y, s.accent_bar, h, accent[0], accent[1], accent[2], 1.0f);   // left edge identity
    r.draw_rect_outline(x, y, w, h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    if (current) r.draw_text(x + s.s3 + 2.f, y + (h - 10.f) * 0.5f, fit_text(r, current, w - 22.f, s.fs_label).c_str(),
                             s.text[0], s.text[1], s.text[2], 1.0f, s.fs_label);
    // chevron
    const float cx = x + w - 10.f, cy = y + h * 0.5f;
    r.draw_tri(cx - 3.f, cy - 2.f, cx + 3.f, cy - 2.f, cx, cy + 2.f, s.dim[0], s.dim[1], s.dim[2], 1.0f);
}

}  // namespace vivid::ui
