#pragma once
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"        // Style, Rect, editor_panel, fit_text, draw_text_r
#include "ui/editor_controls.h" // icon_button
#include "ui/layout.h"          // hit(), the pure geometry home
#include <string>               // fit_text's return — spelled out; gcc won't take it transitively

// ADR-0048/0049: THE DETAIL-VIEW SHELL — "one shell, three modes".
//
// The control substrate (editor_controls.h) and the waveform language (waveform_view.h) were shared
// from the start, but the SHELL around them was not: the clip editor owned its own zone metrics and
// its own take()/gap() strip packer, while the Sampler editor re-derived a different strip (24px
// controls at inset 2 vs 22px at inset 10) and had no title strip at all. Nothing kept them aligned.
//
// This is that missing piece. It owns the three stable zones the ADR names —
//   1. title / transport strip   (name · ident · MODE CHIP · readout · optional buttons)
//   2. inspector strip           (real bounded controls, packed left-to-right)
//   3. canvas                    (piano roll / waveform)
// — as ONE set of metrics and ONE title-strip painter, so a third mode cannot drift from the other
// two. Geometry is pure (Rect math only); the painters are thin.
//
// Modes differ in CONTENT, not in structure. Which affordances a mode's title strip offers is a
// per-mode fact (a docked Sampler is a track's home view, not a drill-in, so it has no close-x —
// see session_view.cpp), expressed as `TitleSpec` flags rather than a second implementation.
namespace vivid::ui {

// Which editor is filling the shell. Drives the mode chip and nothing else structural.
enum class EditorMode { Midi, Audio, Sampler };

inline const char* mode_chip_label(EditorMode m) {
    switch (m) {
        case EditorMode::Midi:    return "MIDI";
        case EditorMode::Audio:   return "AUDIO";
        case EditorMode::Sampler: return "SAMPLER";
    }
    return "";
}

// --- Zone metrics -------------------------------------------------------------------------------
// One source of truth for the band heights. `title_h` matches Style.panel_hd_h's editor variant
// (30px) and `insp_h` the inspector strip; both editors previously hardcoded their own.
constexpr float kShellTitleH = 30.f;   // zone 1
constexpr float kShellInspH  = 32.f;   // zone 2
constexpr float kShellPadX   = 10.f;   // canvas inset from the panel edge
constexpr float kShellPadY   = 8.f;    // gap under the inspector strip

// The inspector strip's interior band (zone 2), given the panel rect.
inline Rect shell_inspector_rect(Rect panel) {
    return { panel.x + 1.f, panel.y + kShellTitleH + 1.f, panel.w - 2.f, kShellInspH - 1.f };
}
// The canvas (zone 3), given the panel rect.
inline Rect shell_canvas_rect(Rect panel) {
    return { panel.x + kShellPadX,
             panel.y + kShellTitleH + kShellInspH + kShellPadY,
             panel.w - 2.f * kShellPadX,
             panel.h - kShellTitleH - kShellInspH - kShellPadY - kShellPadX };
}

// --- Inspector-strip packer ---------------------------------------------------------------------
// Packs controls left-to-right on the strip's baseline, returning ONE Rect per control that the
// caller uses for BOTH draw and hit-test (the ADR-0048 rule). Replaces the clip editor's take()/gap()
// lambdas and the Sampler's place() lambda, which had drifted to different heights and insets.
struct InspectorStrip {
    float x = 0.f, y = 0.f, h = 22.f;

    // Start packing at the strip's left edge for `panel`.
    static InspectorStrip begin(Rect panel) {
        return { panel.x + kShellPadX, panel.y + kShellTitleH + (kShellInspH - 22.f) * 0.5f, 22.f };
    }
    Rect take(float w) { const Rect r{ x, y, w, h }; x += w + 6.f; return r; }
    void gap(float extra = 8.f) { x += extra; }
};

// --- Zone 1: the title / transport strip --------------------------------------------------------
// What a mode's title strip offers. A mode omits an affordance by leaving its flag false — it does
// NOT get a second title-strip implementation.
struct TitleSpec {
    const char* name  = nullptr;   // the edited thing ("Bassline", "break90.wav")
    const char* ident = nullptr;   // where it lives ("Track 2 · Clip A", "Sampler · Track 1")
    const char* readout = nullptr; // right-aligned geometry/state ("1/16 · 3 sel", "2.7s · 8 slices")
    EditorMode  mode = EditorMode::Midi;
    bool follow = false, fit = false, dock = false, close = false;  // which buttons to draw
    bool follow_on = false, docked = false;                         // their state
};

// The four optional title-strip buttons, right-aligned. Laid out RIGHT to LEFT in the order
// [close][dock][fit][follow] so omitting one closes the gap instead of leaving a hole. Pure, so
// draw and hit agree (both call this).
struct TitleButtons { Rect follow{}, fit{}, dock{}, close{}; };
inline TitleButtons title_buttons(Rect panel, const TitleSpec& spec) {
    TitleButtons b;
    float right = panel.x + panel.w - 6.f;
    const float y = panel.y + 6.f, h = 18.f;
    auto take = [&](float w) { right -= w + 4.f; return Rect{ right, y, w, h }; };
    if (spec.close)  b.close  = take(20.f);
    if (spec.dock)   b.dock   = take(44.f);
    if (spec.fit)    b.fit    = take(26.f);
    if (spec.follow) b.follow = take(26.f);
    return b;
}
// Where the readout ends (so a caller can right-align its own extra text against the buttons).
inline float title_readout_right(Rect panel, const TitleSpec& spec) {
    const TitleButtons b = title_buttons(panel, spec);
    float left = panel.x + panel.w - 6.f;
    for (const Rect* r : { &b.follow, &b.fit, &b.dock, &b.close })
        if (r->w > 0.f && r->x < left) left = r->x;
    return left - 10.f;
}

// Paint zones 1+2's chrome: the panel frame + accent bar, the title strip contents, and the
// inspector strip's background. Returns the button rects so the caller hit-tests the SAME geometry.
inline TitleButtons shell_chrome(Renderer2D& r, Rect panel, const TitleSpec& spec,
                                 double hover_x, double hover_y) {
    const Style& s = style();
    // Panel + header band + accent bar (the existing editor chrome; title text drawn below instead,
    // so the name and the ident can carry different weights).
    editor_panel(r, panel, nullptr, s.audio, kShellTitleH);

    float x = panel.x + s.s5;
    if (spec.name && *spec.name) {
        const std::string n = fit_text(r, spec.name, panel.w * 0.35f, s.fs_body);
        r.draw_text(x, panel.y + 9.f, n.c_str(), s.text[0], s.text[1], s.text[2], 1.0f, s.fs_body);
        x += r.text_width(n.c_str(), s.fs_body) + 12.f;
    }
    if (spec.ident && *spec.ident) {
        const std::string id = fit_text(r, spec.ident, panel.w * 0.30f, s.fs_kicker);
        r.draw_text(x, panel.y + 11.f, id.c_str(), s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
        x += r.text_width(id.c_str(), s.fs_kicker) + 12.f;
    }
    // The MODE CHIP: a bounded amber-on-dark tag, so which editor you are in is stated, not inferred
    // from which controls happen to be showing.
    {
        const char* lbl = mode_chip_label(spec.mode);
        const float tw = r.text_width(lbl, s.fs_kicker);
        const Rect chip{ x, panel.y + 7.f, tw + 14.f, 16.f };
        r.draw_rect(chip.x, chip.y, chip.w, chip.h, 0.141f, 0.114f, 0.063f, 1.0f);   // #241D10
        r.draw_rect_outline(chip.x, chip.y, chip.w, chip.h, 1.f, 0.290f, 0.227f, 0.118f, 1.0f);
        r.draw_text(chip.x + 7.f, chip.y + (16.f - 15.f * s.fs_kicker) * 0.5f, lbl,
                    s.audio[0], s.audio[1], s.audio[2], 1.0f, s.fs_kicker);
    }

    const TitleButtons b = title_buttons(panel, spec);
    if (spec.readout && *spec.readout)
        draw_text_r(r, title_readout_right(panel, spec), panel.y + 9.f,
                    fit_text(r, spec.readout, panel.w * 0.3f, s.fs_value).c_str(), s.dim, 1.0f, s.fs_value);

    auto hov = [&](Rect rr) { return rr.w > 0.f && hit(rr, hover_x, hover_y); };
    if (spec.follow) icon_button(r, b.follow, "Flw", hov(b.follow), spec.follow_on);
    if (spec.fit)    icon_button(r, b.fit,    "Fit", hov(b.fit));
    if (spec.dock)   icon_button(r, b.dock,   spec.docked ? "float" : "dock", hov(b.dock));
    if (spec.close)  icon_button(r, b.close,  "\xE2\x9C\x95", hov(b.close), false, s.red);

    // Zone 2's background + its bottom rule.
    const Rect insp = shell_inspector_rect(panel);
    r.draw_rect(insp.x, insp.y, insp.w, insp.h, s.region[0], s.region[1], s.region[2], 1.0f);
    r.draw_rect(insp.x, insp.y + insp.h, insp.w, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
    return b;
}

}  // namespace vivid::ui
