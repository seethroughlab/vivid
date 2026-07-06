#include "ui/session_view.h"

#include "app/app.h"
#include "app/window.h"
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include "ui/node_graph.h"
#include "ui/audio_node_graph.h"
#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"
#include "transport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

// forward decls (definitions live below)
void draw_midi_preview(Renderer2D& ui, const vivid::session::ClipNote* buf, int n, double len,
                       const Rect& b, float ar, float ag, float ab, float alpha);
void draw_wave_preview(Renderer2D& ui, const float* bins, int n,
                       const Rect& b, float ar, float ag, float ab, float alpha);
void draw_sidebar(Renderer2D& ui, const Window& w, double mx, double my);

// --- Unified device chain resolution (VST3 + native audio operators) ---
// Chip layout: slot 0 = instrument; slots [1..nfx] = VST3 effects; slots
// [nfx+1..nfx+nnat] = native audio effects; the "+ FX" tile follows at dock_device_count.
int dock_device_count(vivid::session::Session* s, int track) {
    if (!s) return 1;
    return 1 + vivid::session::session_effect_count(s, track)
             + vivid::session::session_audio_effect_count(s, track);
}
DevSlot dock_resolve(vivid::session::Session* s, int track, int slot) {
    DevSlot d;
    if (!s || slot < 0) return d;
    const int nfx = vivid::session::session_effect_count(s, track);
    if (slot == 0) {                       // instrument slot
        d.valid = true; d.is_instrument = true;
        d.native = vivid::session::session_audio_op_type(s, track, -1)[0] != '\0';
        d.api_index = d.native ? -1 : 0;
        return d;
    }
    if (slot <= nfx) {                     // VST3 effect
        d.valid = true; d.native = false; d.api_index = slot;   // VST3 device index (0=inst,1+=fx)
        return d;
    }
    const int ne = slot - nfx - 1;         // native effect index
    if (ne < vivid::session::session_audio_effect_count(s, track)) {
        d.valid = true; d.native = true; d.api_index = ne;
    }
    return d;
}
int dock_param_count(vivid::session::Session* s, int track, const DevSlot& d) {
    if (!s || !d.valid) return 0;
    return d.native ? vivid::session::session_audio_op_param_count(s, track, d.api_index)
                    : vivid::session::session_param_count(s, track, d.api_index);
}
const char* dock_param_name(vivid::session::Session* s, int track, const DevSlot& d, int i) {
    if (!s || !d.valid) return "";
    return d.native ? vivid::session::session_audio_op_param_name(s, track, d.api_index, i)
                    : vivid::session::session_param_name(s, track, d.api_index, i);
}
float dock_param_norm(vivid::session::Session* s, int track, const DevSlot& d, int i) {
    if (!s || !d.valid) return 0.f;
    if (!d.native) return vivid::session::session_param_value(s, track, d.api_index, i);
    const float lo = vivid::session::session_audio_op_param_min(s, track, d.api_index, i);
    const float hi = vivid::session::session_audio_op_param_max(s, track, d.api_index, i);
    const float v  = vivid::session::session_audio_op_param_get(s, track, d.api_index, i);
    return (hi > lo) ? std::clamp((v - lo) / (hi - lo), 0.f, 1.f) : 0.f;
}
void dock_param_set_norm(vivid::session::Session* s, int track, const DevSlot& d, int i, float norm) {
    if (!s || !d.valid) return;
    if (!d.native) {
        vivid::session::session_set_param(s, track, d.api_index,
            vivid::session::session_param_id(s, track, d.api_index, i), norm);
        return;
    }
    const float lo = vivid::session::session_audio_op_param_min(s, track, d.api_index, i);
    const float hi = vivid::session::session_audio_op_param_max(s, track, d.api_index, i);
    vivid::session::session_audio_op_param_set(s, track, d.api_index, i, lo + std::clamp(norm, 0.f, 1.f) * (hi - lo));
}
std::string dock_param_dest(int track, const DevSlot& d, int i) {
    if (d.native)   // native: "aparam:T:IDX:I" (IDX = -1 for instrument)
        return "aparam:" + std::to_string(track) + ":" + std::to_string(d.api_index) + ":" + std::to_string(i);
    return param_dest(track, d.api_index, i);
}

// The "+ FX" effect picker for the device chain. Rows: VST3 effects first (accent
// sty.fx), then native audio operators (accent sty.audio). Row j in [0,nvst) adds a
// VST3 effect; j in [nvst, nvst+nnat) adds the native audio op (j-nvst). The input
// handler in input.cpp mirrors this ordering.
void draw_fx_menu(Renderer2D& ui, vivid::session::Session* s, const CtxMenu& m) {
    if (!m.open) return;
    const Style& sty = style();
    const float w = 150.f;
    const int nvst = vivid::session::session_available_effect_count();
    const int nnat = s ? vivid::session::session_available_audio_op_count(s, 0) : 0;   // 0 = effects
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, "+ effect", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
    for (int j = 0; j < nvst + nnat; ++j) {
        const bool nat = (j >= nvst);
        const float iy = m.y + j * 24.f;
        const float* acc = nat ? sty.audio : sty.fx;
        const char* nm = nat ? vivid::session::session_available_audio_op_name(s, 0, j - nvst)
                             : vivid::session::session_available_effect_name(j);
        ui.draw_rect(m.x, iy, w, 24.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 24.f, acc[0], acc[1], acc[2], 1.0f);
        ui.draw_text(m.x + 12.f, iy + 5.f, nm, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.9f);
    }
}

// The "+ Track" picker: the instrument catalog, then an "Audio track" entry last.
// Row j in [0,n) adds instrument j; row n adds an audio (sampler) track.
void draw_track_menu(Renderer2D& ui, const CtxMenu& m) {
    if (!m.open) return;
    const Style& sty = style();
    const float w = 150.f;
    const int n = vivid::session::session_available_instrument_count();
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, "+ track", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
    for (int j = 0; j <= n; ++j) {   // n instrument rows + one "Audio track" row
        const float iy = m.y + j * 24.f;
        const bool isAudio = (j == n);
        ui.draw_rect(m.x, iy, w, 24.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
        const float* acc = isAudio ? sty.audio : sty.fx;
        ui.draw_rect(m.x, iy, 3.f, 24.f, acc[0], acc[1], acc[2], 1.0f);
        ui.draw_text(m.x + 12.f, iy + 5.f, isAudio ? "Audio track" : vivid::session::session_available_instrument_name(j),
                     sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.9f);
    }
}

// (The File menu is a native OS menu now — platform/menu_bar.*.)

// Mini clip preview inside a session cell: a piano-roll for MIDI clips, a
// waveform for audio clips. Drawn faintly so the clip name reads on top.
void draw_clip_preview(Renderer2D& ui, vivid::session::Session* s, int t, int sc,
                       const Rect& b, float ar, float ag, float ab, bool on) {
    const float alpha = on ? 0.85f : 0.5f;
    if (vivid::session::session_track_is_audio(s, t)) {
        float bins[48];
        const int n = vivid::session::session_audio_waveform(s, t, sc, bins, 48);
        draw_wave_preview(ui, bins, n, b, ar, ag, ab, alpha);
    } else {
        vivid::session::ClipNote buf[256];
        const int n = vivid::session::session_get_clip(s, t, sc, buf, 256);
        draw_midi_preview(ui, buf, n, vivid::session::session_clip_length(s, t, sc), b, ar, ag, ab, alpha);
    }
}

// A mini piano-roll of a note buffer inside `b` (reused by grid cells + the clip pool).
void draw_midi_preview(Renderer2D& ui, const vivid::session::ClipNote* buf, int n, double len,
                       const Rect& b, float ar, float ag, float ab, float alpha) {
    if (n <= 0 || len <= 0.0) return;
    int lo = 127, hi = 0;
    for (int i = 0; i < n; ++i) { lo = std::min(lo, buf[i].pitch); hi = std::max(hi, buf[i].pitch); }
    const int span = std::max(12, hi - lo + 1);            // at least an octave
    const int base = lo - (span - (hi - lo + 1)) / 2;      // vertically centered
    for (int i = 0; i < n; ++i) {
        const float x0 = b.x + b.w * static_cast<float>(buf[i].start / len);
        const float ww = b.w * static_cast<float>(buf[i].dur / len);
        const float ny = b.y + b.h * (1.f - (static_cast<float>(buf[i].pitch - base) + 0.5f) / span);
        ui.draw_rect(x0, ny - 1.f, std::max(1.5f, std::min(ww, b.x + b.w - x0)), 2.4f, ar, ag, ab, alpha);
    }
}

// A mini waveform (peak-per-bin) inside `b` (reused by grid cells + the clip pool).
void draw_wave_preview(Renderer2D& ui, const float* bins, int n,
                       const Rect& b, float ar, float ag, float ab, float alpha) {
    if (n <= 0) return;
    const float midy = b.y + b.h * 0.5f, colw = b.w / n;
    for (int i = 0; i < n; ++i) {
        const float a = std::min(1.f, std::max(0.f, bins[i])) * (b.h * 0.5f - 1.f);
        ui.draw_rect(b.x + colw * i, midy - a, std::max(1.f, colw - 0.4f), a * 2.f + 1.f, ar, ag, ab, alpha);
    }
}

// The browser sidebar: a CLIPS pool panel (top) over a PLUGINS browser panel (bottom).
void draw_sidebar(Renderer2D& ui, const Window& w, double mx, double my) {
    const Style& sty = style();
    auto* s = w.app->session;

    // --- CLIPS: the session clip pool ---
    panel(ui, sidebar_clips_panel(w.sidebar_w, w.win_h, w.dock_h), "CLIPS", sty.audio);
    const int nc = s ? vivid::session::session_pool_count(s) : 0;
    if (nc == 0) {
        ui.draw_text(sidebar_content_x(), sidebar_content_top() + 4.f, "drag a clip here to stash it",
                     sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
    } else {
        vivid::session::ClipNote buf[256];
        float wbins[128];
        for (int i = 0; i < nc; ++i) {
            if (!pool_item_visible(i, w.sidebar_w, w.win_h, w.dock_h)) break;   // clip to the panel (no scroll yet)
            const Rect r = pool_item_rect(i, w.sidebar_w);
            const bool hov = hit(r, mx, my);
            const bool aud = vivid::session::session_pool_is_audio(s, i);
            const float* acc = aud ? sty.fx : sty.teal;   // audio = violet, MIDI = teal
            ui.draw_rounded_rect(r.x, r.y, r.w, r.h, sty.radius, hov ? sty.card_hi[0] : sty.card[0], hov ? sty.card_hi[1] : sty.card[1], hov ? sty.card_hi[2] : sty.card[2], 1.0f);
            ui.draw_rect(r.x, r.y + 2.f, 3.f, r.h - 4.f, acc[0], acc[1], acc[2], 1.0f);
            std::string nm = vivid::session::session_pool_name(s, i);
            if (nm.empty()) nm = "clip " + std::to_string(i + 1);
            ui.draw_text(r.x + 9.f, r.y + 4.f, fit_text(ui, nm, r.w - 28.f, sty.fs_value).c_str(), sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_value);
            const Rect pv = { r.x + 9.f, r.y + 18.f, r.w - 18.f, r.h - 22.f };
            ui.draw_rect(pv.x, pv.y, pv.w, pv.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
            if (aud) {
                const int nb = vivid::session::session_pool_audio_waveform(s, i, wbins, 128);
                draw_wave_preview(ui, wbins, nb, pv, acc[0], acc[1], acc[2], 0.85f);
            } else {
                const int n = vivid::session::session_pool_get(s, i, buf, 256);
                draw_midi_preview(ui, buf, n, vivid::session::session_pool_length(s, i), pv, acc[0], acc[1], acc[2], 0.85f);
            }
            const Rect xb = pool_item_x_rect(i, w.sidebar_w);
            ui.draw_text(xb.x + 1.f, xb.y - 2.f, "\xC3\x97", hit(xb, mx, my) ? 0.82f : 0.46f, 0.46f, 0.5f, 1.0f, sty.fs_body);
        }
    }

    // --- PLUGINS: every installed VST3 (double-click a row to add it) ---
    const Rect pp = sidebar_plugins_panel(w.sidebar_w, w.win_h, w.dock_h);
    char phdr[24]; std::snprintf(phdr, sizeof phdr, "PLUGINS \xC2\xB7 %d", vivid::session::plugin_count());
    panel(ui, pp, phdr, sty.fx);
    ui.push_clip_rect(pp.x + 1.f, plugins_body_top(w.sidebar_w, w.win_h, w.dock_h),
                      pp.w - 2.f, pp.y + pp.h - plugins_body_top(w.sidebar_w, w.win_h, w.dock_h) - 1.f);
    const int np = vivid::session::plugin_count();
    for (int i = 0; i < np; ++i) {
        const Rect r = plugin_row_rect(i, w.sidebar_w, w.win_h, w.dock_h, w.plugin_scroll);
        if (r.y + r.h < plugins_body_top(w.sidebar_w, w.win_h, w.dock_h) || r.y > pp.y + pp.h) continue;  // offscreen
        const bool hov = hit(r, mx, my);
        if (hov) ui.draw_rounded_rect(r.x, r.y, r.w, r.h, sty.radius, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 1.0f);
        ui.draw_text(r.x + 6.f, r.y + 3.f, fit_text(ui, vivid::session::plugin_at(i).name, r.w - 12.f, sty.fs_label).c_str(),
                     hov ? sty.text[0] : sty.body[0], hov ? sty.text[1] : sty.body[1], hov ? sty.text[2] : sty.body[2], 1.0f, sty.fs_label);
    }
    ui.pop_clip_rect();
}

// The bottom device-view dock: device chips for the selected track + a knob grid
// of the selected device's params. Full window width; resizable via its top edge.
void draw_device_dock(Renderer2D& ui, const Window& w, double mx, double my) {
    vivid::session::Session* s = w.app->session;
    if (!s) return;
    const Style& sty = style();
    const DockGeom d = w.dock_geom();
    const float y0 = d.y0;
    ui.draw_rect(0.f, y0, static_cast<float>(w.win_w), w.dock_h, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    const bool rhov = hit(w.dock_resize_rect(), mx, my);
    ui.draw_rect(0.f, y0 - 1.f, static_cast<float>(w.win_w), 2.f,   // strong top border (drag to resize)
                 rhov ? sty.control[0] : sty.border[0], rhov ? sty.control[1] : sty.border[1], rhov ? sty.control[2] : sty.border[2], 1.0f);
    // header strip (title) — a defined band above the device chain / params
    ui.draw_rect(0.f, y0, static_cast<float>(w.win_w), 20.f, sty.region_hd[0], sty.region_hd[1], sty.region_hd[2], 1.0f);
    ui.draw_rect(0.f, y0 + 20.f, static_cast<float>(w.win_w), 1.f, sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);
    // UI-1: the detail region always declares its domain (strict-zones principle) — an accent
    // edge + a badge, driven by the explicit focus, so audio vs visual is never ambiguous.
    {
        const bool vis = (w.focus.dom == FocusContext::Dom::Visual);
        const float* dc = vis ? sty.gpu : sty.audio;
        ui.draw_rect(0.f, y0, 4.f, 20.f, dc[0], dc[1], dc[2], 1.0f);
        draw_text_r(ui, w.win_w - 12.f, y0 + 6.f, vis ? "VISUAL" : "AUDIO", dc, 0.9f, sty.fs_kicker);
        // Close (x): exits a drilled-in focus back to the device view (progressive disclosure).
        // Shown for the drilled-in deep views (visual-node inspector, audio graph).
        if (w.focus.kind == FocusContext::Kind::VisualNode || w.focus.kind == FocusContext::Kind::AudioGraph) {
            const Rect cb = dock_close_rect(w.win_w, w.win_h, w.dock_h);
            const bool ch = hit(cb, mx, my);
            ui.draw_text(cb.x, cb.y - 2.f, "\xC3\x97", ch ? 0.9f : 0.55f, ch ? 0.6f : 0.5f, ch ? 0.6f : 0.55f, 1.0f, sty.fs_body);
        }
    }

    // The detail region's explicit focus (UI-1) decides device (audio) vs visual-node (visual).
    if (w.focus.kind == FocusContext::Kind::VisualNode && w.app->graph) {
        const int selop = w.focus.node;
        char nh[64]; std::snprintf(nh, sizeof nh, "NODE \xC2\xB7 %s", w.app->graph->op_kind_name(selop));
        section_header(ui, 12.f, y0 + 7.f, nh, sty.gpu);
        ui.draw_text(120.f, y0 + 7.f, "teal = wired (modulated) \xC2\xB7 click a track header for devices",
                     sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.7f);
        auto* g = w.app->graph;
        const int pc = g->op_param_count_at(selop);
        for (int i = 0; i < pc; ++i) {
            const int hint = g->op_param_hint_at(selop, i);
            if (hint == VIVID_DISPLAY_HIDDEN || hint == VIVID_DISPLAY_EDITOR || hint == VIVID_DISPLAY_TRANSIENT) continue;
            const Rect row = node_param_row(i, w.win_w, w.win_h, w.dock_h);
            const Rect wr  = node_param_widget_rect(i, w.win_w, w.win_h, w.dock_h);
            const float base = g->op_param_base_at(selop, i);
            const bool wired = g->op_param_wired_at(selop, i);
            const char* label = g->op_param_label_at(selop, i);
            const NodeWidget kind = node_widget_kind(g->op_param_type_at(selop, i), hint, g->op_param_choice_count_at(selop, i));
            // label + small wire affordance
            ui.draw_text(row.x, row.y + 6.f, fit_text(ui, label, kNodeLabelW - 16.f, sty.fs_label).c_str(), sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label);
            const Rect mb = node_param_map_rect(i, w.win_w, w.win_h, w.dock_h);
            ui.draw_rect(mb.x, mb.y, mb.w, mb.h, wired ? sty.teal[0] : 0.55f, wired ? sty.teal[1] : 0.45f, wired ? sty.teal[2] : 0.22f, wired ? 1.0f : 0.7f);
            switch (kind) {
                case NodeWidget::Toggle:
                    toggle(ui, wr.x, wr.y + 3.f, 34.f, wr.h - 6.f, base >= 0.5f, sty.gpu);
                    ui.draw_text(wr.x + 42.f, wr.y + 4.f, base >= 0.5f ? "on" : "off", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
                    break;
                case NodeWidget::Enum: {
                    const int cc = g->op_param_choice_count_at(selop, i);
                    const int idx = cc > 1 ? std::clamp(int(std::lround(base * (cc - 1))), 0, cc - 1) : 0;
                    dropdown_field(ui, wr.x, wr.y + 1.f, wr.w, wr.h - 2.f, g->op_param_choice_label_at(selop, i, idx), sty.gpu, hit(wr, mx, my));
                    break;
                }
                case NodeWidget::Knob: {
                    char vt[8]; std::snprintf(vt, sizeof vt, "%.2f", base);
                    knob(ui, wr.x + 14.f, wr.y + wr.h * 0.5f, 11.f, base, nullptr, vt, sty.gpu, wired);
                    break;
                }
                default: {  // Slider
                    const float mn = g->op_param_min_at(selop, i), mx2 = g->op_param_max_at(selop, i);
                    char vt[12]; std::snprintf(vt, sizeof vt, "%.2f", mn + base * (mx2 - mn));
                    slider(ui, wr.x, wr.y, wr.w, wr.h, base, nullptr, vt, sty.gpu, wired);
                    break;
                }
            }
        }
        if (pc == 0) ui.draw_text(12.f, y0 + 40.f, "this node has no parameters", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.8f);
        return;
    }

    // UI-3: the audio node graph deep view — the authoritative per-track audio topology the RT
    // engine runs (instrument -> FX -> output), drawn as a node graph. Drilled in from the dock
    // "Graph" button; the close x (above) returns to the device chain.
    if (w.focus.kind == FocusContext::Kind::AudioGraph) {
        const int tr = std::min(std::max(w.focus.track, 0), vivid::session::session_track_count(s) - 1);
        char ah[80]; std::snprintf(ah, sizeof ah, "AUDIO GRAPH \xC2\xB7 %.40s", vivid::session::session_track_name(s, tr));
        section_header(ui, 12.f, y0 + 7.f, ah, sty.audio);
        AudioNodeGraph ag;
        ag.set_source(s, tr);
        const Rect gp = audio_graph_panel(w.win_w, w.win_h, w.dock_h);
        ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
        ag.draw(ui);
        return;
    }

    const int tracks = vivid::session::session_track_count(s);
    const int seltr = std::min(std::max(w.sel_track, 0), tracks - 1);
    const bool aud = vivid::session::session_track_is_audio(s, seltr);
    char hdr[80]; std::snprintf(hdr, sizeof hdr, "DEVICE \xC2\xB7 %.40s", vivid::session::session_track_name(s, seltr));
    section_header(ui, 12.f, y0 + 7.f, hdr, sty.audio);
    // UI-3: a "Graph" toggle to drill into this track's audio node graph (a deep view).
    { const Rect gb = audio_graph_button_rect(w.win_w, w.win_h, w.dock_h);
      draw_card(ui, gb.x, gb.y, gb.w, gb.h, sty.audio, hit(gb, mx, my));
      ui.draw_text(gb.x + 9.f, gb.y + 2.f, "Graph", sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label); }

    // CHAIN rack — a bounded, recessed shelf that visually holds the device chips
    // as their own section (distinct from the params grid below). Present only in
    // track mode: inspecting a visual node hides it (handled by the early return above).
    const Rect rack = dock_chain_rect(w.win_w, w.win_h, w.dock_h);
    ui.draw_rounded_rect(rack.x, rack.y, rack.w, rack.h, sty.radius, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
    ui.draw_rect(rack.x, rack.y, rack.w, 1.f, sty.border[0], sty.border[1], sty.border[2], 1.0f);                    // top hairline
    ui.draw_rect(rack.x, rack.y + rack.h - 1.f, rack.w, 1.f, sty.border[0], sty.border[1], sty.border[2], 1.0f);     // bottom hairline
    ui.draw_rect(rack.x, rack.y, 3.f, rack.h, sty.audio[0], sty.audio[1], sty.audio[2], 1.0f);                        // domain accent tick
    ui.draw_text(rack.x + rack.w - 46.f, rack.y + 3.f, "CHAIN", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_kicker);

    // device chips: instrument (0) + VST3 fx + native audio-op fx + "+ FX".
    // Native chips carry the audio-domain accent + an "AFX"/"INST" kicker.
    const int ndev = dock_device_count(s, seltr);
    for (int i = 0; i <= ndev; ++i) {
        const bool isAdd = (i == ndev);
        const DevSlot slot = isAdd ? DevSlot{} : dock_resolve(s, seltr, i);
        if (!isAdd && slot.is_instrument && aud && !slot.native) continue;  // sampler track: no VST3 instrument
        const Rect b = w.dock_chip(i);
        const bool sel = !isAdd && w.sel_device == i;
        const float* acc = isAdd ? sty.control : ((slot.native || slot.is_instrument) ? sty.audio : sty.fx);
        draw_card(ui, b.x, b.y, b.w, b.h, acc, hit(b, mx, my) || sel);
        if (sel) ui.draw_rect(b.x, b.y + b.h - 2.f, b.w, 2.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);
        if (isAdd) { ui.draw_text(b.x + 10.f, b.y + 11.f, "+ FX", 0.62f, 0.80f, 0.72f, 1.0f, 0.9f); continue; }
        const char* kicker = slot.is_instrument ? "INST" : (slot.native ? "AFX" : "FX");
        ui.draw_text(b.x + 8.f, b.y + 6.f, kicker, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.64f);
        const char* dispname = slot.is_instrument
            ? (slot.native ? vivid::session::session_audio_op_type(s, seltr, -1) : vivid::session::session_track_name(s, seltr))
            : (slot.native ? vivid::session::session_audio_op_type(s, seltr, slot.api_index)
                           : vivid::session::session_effect_name(s, seltr, slot.api_index - 1));
        char nm[24]; std::snprintf(nm, sizeof nm, "%.13s", dispname);
        ui.draw_text(b.x + 8.f, b.y + 17.f, nm, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.82f);
        if (!slot.is_instrument) {   // effects (VST3 or native) get a remove ×
            const Rect xb = w.dock_chip_x(i);
            ui.draw_rect(xb.x, xb.y, xb.w, xb.h, 0.4f, 0.18f, 0.18f, 1.0f);
            ui.draw_text(xb.x + 3.f, xb.y, "x", 0.85f, 0.6f, 0.6f, 1.0f, 0.8f);
        }
    }

    // knob grid for the selected device's params (its own zone below the rack)
    const DevSlot seldev = dock_resolve(s, seltr, std::max(0, w.sel_device));
    const int pc = dock_param_count(s, seltr, seldev);
    const float* pacc = (seldev.is_instrument || seldev.native) ? sty.audio : sty.fx;
    const int shown = std::min(pc, d.cols * d.maxRows);
    for (int i = 0; i < shown; ++i) {
        float cx, cy; dock_knob(i, d, cx, cy);
        const float v = dock_param_norm(s, seltr, seldev, i);
        char nm[12]; std::snprintf(nm, sizeof nm, "%.10s", dock_param_name(s, seltr, seldev, i));
        char vt[8]; std::snprintf(vt, sizeof vt, "%.2f", v);
        const bool mapped = w.app->graph && w.app->graph->source_of(dock_param_dest(seltr, seldev, i)) != nullptr;
        knob(ui, cx, cy, 15.f, v, nm, vt, pacc, mapped);
        const Rect mb = dock_knob_map(i, d);   // small map affordance (amber=unmapped, teal=mapped)
        ui.draw_rect(mb.x, mb.y, mb.w, mb.h, mapped ? sty.teal[0] : 0.55f, mapped ? sty.teal[1] : 0.45f,
                     mapped ? sty.teal[2] : 0.22f, mapped ? 1.0f : 0.7f);
    }
    if (pc > shown) {
        char more[48]; std::snprintf(more, sizeof more, "+%d more \xE2\x80\x94 drag the dock taller", pc - shown);
        ui.draw_text(12.f, y0 + w.dock_h - 13.f, more, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.68f);
    }
}

// The Session view on Renderer2D: transport, a tracks×scenes clip grid, a mixer.
void draw_ui(Renderer2D& ui, const Window& w, double beats, double mx, double my) {
    const Style& sty = style();
    const float W = static_cast<float>(w.win_w);

    // --- transport bar: wordmark · play/pause · beat · tempo ---
    ui.draw_rect(0, 0, W, kTopBarH, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_rect(0, kTopBarH - 1.f, W, 1.f, sty.border[0], sty.border[1], sty.border[2], 1.0f);
    ui.draw_text(sty.s6, 11.f, "Vivid", sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_brand);
    const double bpm = w.app->transport ? w.app->transport->bpm.load(std::memory_order_relaxed) : 120.0;
    const bool playing = w.app->transport && w.app->transport->is_playing();
    {   // browser sidebar toggle (three stacked lines when open)
        const Rect b = sidebar_toggle_rect();
        const bool open = w.sidebar_w > 0.f, hov = hit(b, mx, my);
        ui.draw_rounded_rect(b.x - 2.f, b.y - 2.f, b.w + 4.f, b.h + 4.f, sty.radius,
                             (open || hov) ? sty.card_hi[0] : sty.card[0], (open || hov) ? sty.card_hi[1] : sty.card[1], (open || hov) ? sty.card_hi[2] : sty.card[2], 1.0f);
        const float* ic = open ? sty.audio : sty.dim;
        for (int i = 0; i < 3; ++i) ui.draw_rect(b.x + 3.f, b.y + 3.f + i * 5.f, b.w - 6.f, 2.f, ic[0], ic[1], ic[2], 1.0f);
    }
    {
        const Rect p = transport_play_rect();
        const bool hov = hit(p, mx, my);
        ui.draw_rounded_rect(p.x - 3.f, p.y - 3.f, p.w + 6.f, p.h + 6.f, sty.radius,
                             hov ? sty.card_hi[0] : sty.card[0], hov ? sty.card_hi[1] : sty.card[1], hov ? sty.card_hi[2] : sty.card[2], 1.0f);
        if (playing) {
            ui.draw_rect(p.x + 4.f, p.y + 3.f, 3.5f, 12.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);
            ui.draw_rect(p.x + 10.5f, p.y + 3.f, 3.5f, 12.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);
        } else {
            ui.draw_tri(p.x + 5.f, p.y + 3.f, p.x + 5.f, p.y + 15.f, p.x + 15.f, p.y + 9.f, sty.green[0], sty.green[1], sty.green[2], 1.0f);
        }
    }
    int beat = static_cast<int>(std::floor(beats)) % 4; if (beat < 0) beat += 4;
    for (int i = 0; i < 4; ++i) {
        const bool ob = playing && (i == beat);   // dots freeze when stopped
        ui.draw_rounded_rect(330.f + i * 13.f, 15.f, 8.f, 8.f, 2.f,
                             ob ? sty.gold[0] : sty.card_hi[0], ob ? sty.gold[1] : sty.card_hi[1], ob ? sty.gold[2] : sty.card_hi[2], 1.0f);
    }
    char tb[48]; std::snprintf(tb, sizeof tb, "%.0f BPM     4 / 4", bpm);
    ui.draw_text(398.f, 13.f, tb, sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label);
    // Record + metronome toggles (M6). Record glows red while armed-and-rolling; the
    // metronome pip glows gold when on. A "TYPE" pill lights when musical typing is active.
    const bool recording = w.app->session && vivid::session::session_is_recording(w.app->session);
    const bool metro_on  = w.app->session && vivid::session::session_get_metronome(w.app->session);
    {
        const Rect r = transport_record_rect();
        const bool hov = hit(r, mx, my);
        ui.draw_rounded_rect(r.x - 3.f, r.y - 3.f, r.w + 6.f, r.h + 6.f, sty.radius,
                             hov ? sty.card_hi[0] : sty.card[0], hov ? sty.card_hi[1] : sty.card[1], hov ? sty.card_hi[2] : sty.card[2], 1.0f);
        const float rc[3] = { 0.90f, 0.24f, 0.28f };
        const float* c = recording ? rc : sty.dim;
        ui.draw_rounded_rect(r.x + r.w * 0.5f - 6.f, r.y + r.h * 0.5f - 6.f, 12.f, 12.f, 6.f, c[0], c[1], c[2], 1.0f);   // filled disc
    }
    {
        const Rect r = transport_metro_rect();
        const bool hov = hit(r, mx, my);
        ui.draw_rounded_rect(r.x - 3.f, r.y - 3.f, r.w + 6.f, r.h + 6.f, sty.radius,
                             hov ? sty.card_hi[0] : sty.card[0], hov ? sty.card_hi[1] : sty.card[1], hov ? sty.card_hi[2] : sty.card[2], 1.0f);
        const float* c = metro_on ? sty.gold : sty.dim;
        // a tiny triangular "metronome" glyph
        ui.draw_tri(r.x + r.w * 0.5f, r.y + 3.f, r.x + 3.f, r.y + r.h - 3.f, r.x + r.w - 3.f, r.y + r.h - 3.f, c[0], c[1], c[2], 1.0f);
    }
    if (w.typing) {
        ui.draw_rounded_rect(550.f, 12.f, 40.f, 16.f, sty.radius, sty.audio[0] * 0.35f, sty.audio[1] * 0.35f, sty.audio[2] * 0.35f, 1.0f);
        ui.draw_text(556.f, 14.f, "TYPE", sty.audio[0], sty.audio[1], sty.audio[2], 1.0f, sty.fs_kicker);
    }

    if (!w.app->session) return;
    auto* s = w.app->session;
    const int tracks = vivid::session::session_track_count(s);
    const int scenes = vivid::session::session_scene_count(s);

    // ================= browser sidebar (screen space, left of the DAW pane) =================
    if (w.sidebar_w > 0.f)
        draw_sidebar(ui, w, mx, my);

    // ================= DAW pane (shifted right past the browser sidebar via a view transform) =================
    const float SW = w.sidebar_w;
    const float dawW = w.split_x - SW;
    ui.push_clip_rect(SW, kTopBarH, dawW, w.dock_top() - kTopBarH);
    ui.set_transform(SW, 0.f, 1.f);   // world x=0 is the DAW pane's left edge; content shifts past the sidebar
    ui.draw_rect(0.f, kTopBarH, dawW, w.dock_top() - kTopBarH, sty.bg[0], sty.bg[1], sty.bg[2], 1.0f);
    const Rect spanel = session_panel(dawW, w.win_h, w.dock_h);
    panel(ui, spanel, "SESSION", sty.audio);   // the bounded region (draw the container first)
    const float contentR = dawW - kPaneMargin - kPanePad;                // right edge of the panel content
    // Clip the grid to the panel's interior so extra track columns are cut cleanly at the
    // panel border instead of bleeding into the gutter / the visuals pane. (screen coords)
    ui.push_clip_rect(SW + spanel.x + 1.f, spanel.y + 1.f, spanel.w - 2.f, spanel.h - 2.f);

    // track headers (accent left edge, ellipsised name, remove ×) + a "+ Track" cell
    for (int t = 0; t < tracks; ++t) {
        const Rect h = track_header_rect(t);
        float ar, ag, ab; track_accent(t, ar, ag, ab);
        const bool hov = hit(h, mx, my);
        ui.draw_rounded_rect(h.x, h.y, h.w, h.h, sty.radius, hov ? sty.card_hi[0] : sty.card[0], hov ? sty.card_hi[1] : sty.card[1], hov ? sty.card_hi[2] : sty.card[2], 1.0f);
        ui.draw_rect(h.x, h.y + 3.f, 3.f, h.h - 6.f, ar, ag, ab, 1.0f);   // accent edge
        std::string nm = fit_text(ui, vivid::session::session_track_name(s, t), h.w - 28.f, sty.fs_label);
        ui.draw_text(h.x + 10.f, h.y + 6.f, nm.c_str(), sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_label);
        const Rect xb = track_header_x_rect(t);
        const bool xh = hit(xb, mx, my);
        ui.draw_text(xb.x + 1.f, xb.y - 2.f, "\xC3\x97", xh ? 0.82f : 0.46f, xh ? 0.42f : 0.49f, xh ? 0.42f : 0.55f, 1.0f, sty.fs_body);
    }
    if (tracks < vivid::session::kMaxTracks) {
        const Rect a = track_add_rect(tracks);
        const bool ah = hit(a, mx, my);
        ui.draw_rounded_rect(a.x, a.y, a.w, a.h, sty.radius, ah ? sty.card[0] : sty.region[0], ah ? sty.card[1] : sty.region[1], ah ? sty.card[2] : sty.region[2], 1.0f);
        ui.draw_text(a.x + 10.f, a.y + 6.f, "+ Track", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
    }
    // scene rows + clip cells
    for (int sc = 0; sc < scenes; ++sc) {
        const Rect sb = scene_launch_rect(sc);
        const bool sh = hit(sb, mx, my);
        ui.draw_rounded_rect(sb.x, sb.y, sb.w, sb.h, sty.radius, sh ? sty.card_hi[0] : sty.card[0], sh ? sty.card_hi[1] : sty.card[1], sh ? sty.card_hi[2] : sty.card[2], 1.0f);
        char sl[4]; std::snprintf(sl, sizeof sl, "%c", 'A' + sc);
        ui.draw_text(sb.x + (sb.w - ui.text_width(sl, sty.fs_body)) * 0.5f, sb.y + 5.f, sl, sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_body);
        ui.draw_tri(sb.x + sb.w * 0.5f - 4.f, sb.y + sb.h - 15.f, sb.x + sb.w * 0.5f - 4.f, sb.y + sb.h - 7.f, sb.x + sb.w * 0.5f + 4.f, sb.y + sb.h - 11.f, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f);
        for (int t = 0; t < tracks; ++t) {
            const Rect r = clip_cell_rect(t, sc);
            const bool on = vivid::session::session_active_clip(s, t) == sc;
            const bool q  = vivid::session::session_queued_clip(s, t) == sc;
            const bool hov = hit(r, mx, my);
            float ar, ag, ab; track_accent(t, ar, ag, ab);
            const float tbh = 14.f;  // title bar
            ui.draw_rounded_rect(r.x, r.y, r.w, r.h, sty.radius, on ? 0.115f : 0.07f, on ? 0.13f : 0.078f, on ? 0.155f : 0.095f, 1.0f);
            const float k = (on ? 0.5f : 0.22f) + (hov ? 0.06f : 0.f);
            ui.draw_rect(r.x + 1.f, r.y + 1.f, r.w - 2.f, tbh, ar * k, ag * k, ab * k, 1.0f);
            if (q) ui.draw_rect(r.x, r.y, r.w, 2.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);
            float tx = r.x + 6.f;
            if (on) { ui.draw_tri(r.x + 5.f, r.y + 4.f, r.x + 5.f, r.y + 10.f, r.x + 10.f, r.y + 7.f, sty.green[0], sty.green[1], sty.green[2], 1.0f); tx = r.x + 14.f; }
            char cn[16];
            const int abpm = vivid::session::session_track_is_audio(s, t) ? vivid::session::session_audio_clip_bpm(s, t, sc) : 0;
            if (abpm > 0) std::snprintf(cn, sizeof cn, "%d BPM", abpm);
            else          std::snprintf(cn, sizeof cn, "Clip %c", 'A' + sc);
            ui.draw_text(tx, r.y + 2.f, cn, on ? 0.95f : 0.72f, on ? 0.97f : 0.74f, 1.0f, 1.0f, sty.fs_value);
            const Rect pv = { r.x + 4.f, r.y + tbh + 3.f, r.w - 8.f, r.h - tbh - 6.f };
            ui.draw_rect(pv.x, pv.y, pv.w, pv.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
            draw_clip_preview(ui, s, t, sc, pv, ar, ag, ab, on);
        }
    }
    // --- mixer sub-region (inside the Session panel) ---
    const float my0 = mixer_y(scenes);
    ui.draw_rect(kSceneColX, mixer_divider_y(scenes), contentR - kSceneColX, 1.f, sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);
    section_header(ui, kSceneColX, my0 + 3.f, "MIX", sty.audio);
    auto viz_button = [&](const Rect& b) {
        const bool h = hit(b, mx, my);
        ui.draw_rounded_rect(b.x, b.y, b.w, b.h, 3.f, h ? sty.card_hi[0] : sty.card[0], h ? sty.card_hi[1] : sty.card[1], h ? sty.card_hi[2] : sty.card[2], 1.0f);
        const char* lbl = "VIZ";
        ui.draw_text(b.x + (b.w - ui.text_width(lbl, sty.fs_kicker)) * 0.5f, b.y + 3.f, lbl, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f, sty.fs_kicker);
    };
    for (int t = 0; t < tracks; ++t) {
        const Rect mr = track_meter_rect(t, scenes), gr = track_gain_rect(t, scenes);
        const float lvl = std::min(1.0f, vivid::session::session_track_level(s, t) * 4.0f);
        ui.draw_rect(mr.x, mr.y, mr.w, mr.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
        ui.draw_rect(mr.x, mr.y, mr.w * lvl, mr.h, sty.green[0], sty.green[1], sty.green[2], 1.0f);
        const float g = vivid::session::session_track_gain(s, t);
        ui.draw_rect(gr.x, gr.y, gr.w, gr.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
        ui.draw_rect(gr.x, gr.y, gr.w * g, gr.h, sty.gpu[0] * 0.7f, sty.gpu[1] * 0.7f, sty.gpu[2] * 0.75f, 1.0f);
        ui.draw_rect(gr.x + gr.w * g - 1.5f, gr.y - 1.f, 3.f, gr.h + 2.f, sty.text[0], sty.text[1], sty.text[2], 1.0f);
        // ARM: red when this track is record-armed. Audio tracks can't be armed (no instrument).
        {
            const Rect ar = track_arm_rect(t, scenes);
            const bool armed = vivid::session::session_armed_track(s) == t;
            const bool ah = hit(ar, mx, my);
            const float rc[3] = { 0.90f, 0.24f, 0.28f };
            ui.draw_rounded_rect(ar.x, ar.y, ar.w, ar.h, 3.f,
                                 armed ? rc[0] * 0.5f : (ah ? sty.card_hi[0] : sty.card[0]),
                                 armed ? rc[1] * 0.5f : (ah ? sty.card_hi[1] : sty.card[1]),
                                 armed ? rc[2] * 0.5f : (ah ? sty.card_hi[2] : sty.card[2]), 1.0f);
            const char* al = "ARM";
            const float* ac = armed ? rc : sty.dim;
            ui.draw_text(ar.x + (ar.w - ui.text_width(al, sty.fs_kicker)) * 0.5f, ar.y + 3.f, al, ac[0], ac[1], ac[2], 1.0f, sty.fs_kicker);
        }
        viz_button(track_viz_rect(t, scenes));
    }
    const Rect mm = master_meter_rect(scenes);
    const float ml = w.app->transport ? std::min(1.0f, w.app->transport->level.load(std::memory_order_relaxed) * 4.0f) : 0.f;
    ui.draw_rect(mm.x, mm.y, mm.w, mm.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
    ui.draw_rect(mm.x, mm.y, mm.w * ml, mm.h, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f);
    ui.draw_text(kSceneColX, mm.y + 6.f, "MAIN", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_kicker);
    viz_button(master_viz_rect(scenes));

    ui.pop_clip_rect();  // end the panel-interior grid clip
    ui.set_transform(0.f, 0.f, 1.f);   // reset the DAW-pane shift
    ui.pop_clip_rect();  // end DAW pane
    // (clip drag feedback is drawn later as a screen-space overlay — it can cross the sidebar↔grid boundary)

    // ================= visuals pane (Output + Signal regions; content drawn by the GPU / node graph) =================
    ui.push_clip_rect(w.split_x, kTopBarH, W - w.split_x, w.dock_top() - kTopBarH);
    char oh[48]; std::snprintf(oh, sizeof oh, "OUTPUT \xC2\xB7 %s", w.app->visual_source ? "VIDEO" : "SHADER");
    panel_frame(ui, w.output_panel(), oh, sty.gpu);
    // Pop-out toggle in the OUTPUT header (second window / performance screen).
    { const Rect pb = popout_button_rect(w.win_w, w.split_x);
      const bool pbh = hit(pb, mx, my);
      ui.draw_rounded_rect(pb.x, pb.y, pb.w, pb.h, sty.radius, pbh ? sty.card_hi[0] : sty.card[0], pbh ? sty.card_hi[1] : sty.card[1], pbh ? sty.card_hi[2] : sty.card[2], 1.0f);
      ui.draw_rect(pb.x, pb.y, sty.accent_bar, pb.h, sty.gpu[0], sty.gpu[1], sty.gpu[2], 1.0f);
      ui.draw_text(pb.x + 8.f, pb.y + 2.f, w.popout ? "\xE2\x87\xB2 Pop in" : "\xE2\x87\xB1 Pop out",
                   sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label); }
    // UI-2: toggle the visuals node graph (a deep view under the output). Lit when open.
    { const Rect gb = graph_button_rect(w.win_w, w.split_x);
      const bool gbh = hit(gb, mx, my) || w.show_graph;
      ui.draw_rounded_rect(gb.x, gb.y, gb.w, gb.h, sty.radius, gbh ? sty.card_hi[0] : sty.card[0], gbh ? sty.card_hi[1] : sty.card[1], gbh ? sty.card_hi[2] : sty.card[2], 1.0f);
      ui.draw_rect(gb.x, gb.y, sty.accent_bar, gb.h, sty.gpu[0], sty.gpu[1], sty.gpu[2], 1.0f);
      ui.draw_text(gb.x + 8.f, gb.y + 2.f, "Graph", w.show_graph ? sty.gpu[0] : sty.body[0],
                   w.show_graph ? sty.gpu[1] : sty.body[1], w.show_graph ? sty.gpu[2] : sty.body[2], 1.0f, sty.fs_label); }
    if (w.show_graph) panel_frame(ui, w.signal_panel(), "SIGNAL \xC2\xB7 VISUALS", sty.gpu);
    ui.pop_clip_rect();

    // DAW | visuals splitter (on top, unclipped)
    const Rect sp = w.splitter_rect();
    const bool sph = hit(sp, mx, my);
    ui.draw_rect(sp.x, sp.y, sp.w, sp.h, sph ? sty.border[0] : sty.border_soft[0], sph ? sty.border[1] : sty.border_soft[1], sph ? sty.border[2] : sty.border_soft[2], 1.0f);

    // ---- clip drag feedback: a screen-space overlay (can cross the sidebar↔grid boundary) ----
    if ((w.clip_drag_t >= 0 || w.clip_drag_from_pool >= 0) && w.clip_dragging) {
        const bool fromPool = w.clip_drag_from_pool >= 0;
        const bool audioSrc = !fromPool && vivid::session::session_track_is_audio(s, w.clip_drag_t);
        int tt = -1, ts = -1;
        for (int a = 0; a < tracks && tt < 0; ++a)
            for (int b = 0; b < scenes; ++b)
                if (hit(clip_cell_rect(a, b), mx - SW, my)) { tt = a; ts = b; break; }   // grid is shifted
        if (tt >= 0) {   // highlight the target cell (screen rect = world + sidebar offset)
            const Rect r = clip_cell_rect(tt, ts);
            const bool dstAudio = vivid::session::session_track_is_audio(s, tt);
            // ok to drop here: pool clip type must match the track; a grid move needs both MIDI.
            const bool ok = fromPool ? (vivid::session::session_pool_is_audio(s, w.clip_drag_from_pool) == dstAudio)
                                     : (!audioSrc && !dstAudio);
            const float* hl = ok ? sty.gold : sty.control;
            const float rx = r.x + SW;
            ui.draw_rect(rx, r.y, r.w, 2.f, hl[0], hl[1], hl[2], 1.0f);
            ui.draw_rect(rx, r.y + r.h - 2.f, r.w, 2.f, hl[0], hl[1], hl[2], 1.0f);
            ui.draw_rect(rx, r.y, 2.f, r.h, hl[0], hl[1], hl[2], 1.0f);
            ui.draw_rect(rx + r.w - 2.f, r.y, 2.f, r.h, hl[0], hl[1], hl[2], 1.0f);
        } else if (!fromPool && in_sidebar(SW, w.win_h, w.dock_h, mx, my)) {   // grid clip -> pool (MIDI or audio)
            const Rect sb = sidebar_panel(SW, w.win_h, w.dock_h);   // grid -> pool stash target
            ui.draw_rect(sb.x, sb.y, sb.w, 2.f, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f);
            ui.draw_rect(sb.x, sb.y + sb.h - 2.f, sb.w, 2.f, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f);
        }
        Rect src = fromPool ? pool_item_rect(w.clip_drag_from_pool, SW) : clip_cell_rect(w.clip_drag_t, w.clip_drag_sc);
        if (!fromPool) src.x += SW;
        const float gx = static_cast<float>(mx) - (static_cast<float>(w.clip_drag_x0) - src.x);
        const float gy = static_cast<float>(my) - (static_cast<float>(w.clip_drag_y0) - src.y);
        float ar = sty.teal[0], ag = sty.teal[1], ab = sty.teal[2];
        if (!fromPool) track_accent(w.clip_drag_t, ar, ag, ab);
        ui.draw_rounded_rect(gx, gy, src.w, src.h, sty.radius, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 0.9f);
        ui.draw_rect(gx + 1.f, gy + 1.f, src.w - 2.f, 13.f, ar * 0.6f, ag * 0.6f, ab * 0.6f, 0.95f);
        char cn[24];
        if (fromPool) std::snprintf(cn, sizeof cn, "%.14s", vivid::session::session_pool_name(s, w.clip_drag_from_pool));
        else          std::snprintf(cn, sizeof cn, "Clip %c", 'A' + w.clip_drag_sc);
        ui.draw_text(gx + 6.f, gy + 2.f, cn, sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_value);
    }

    // ---- plugin drag feedback: highlight the drop target (a track = effect, +Track = instrument) ----
    if (w.plugin_drag_i >= 0 && w.plugin_dragging) {
        const float dmx = static_cast<float>(mx) - SW;   // DAW-pane x (grid shifted by the sidebar)
        int tgt = -1;   // -2 = +Track slot (new instrument); >=0 = track (effect)
        if (tracks < vivid::session::kMaxTracks && hit(track_add_rect(tracks), dmx, my)) tgt = -2;
        else {
            for (int t = 0; t < tracks && tgt < 0; ++t)
                if (hit(track_header_rect(t), dmx, my) ||
                    (dmx >= track_x(t) && dmx < track_x(t) + kTrackW && my >= kHeaderY && my < w.dock_top())) tgt = t;
            if (tgt < 0 && my >= w.dock_top()) tgt = std::min(std::max(w.sel_track, 0), tracks - 1);
        }
        auto outline = [&](Rect r, const float* c) {
            const float rx = r.x + SW;
            ui.draw_rect(rx, r.y, r.w, 2.f, c[0], c[1], c[2], 1.0f);
            ui.draw_rect(rx, r.y + r.h - 2.f, r.w, 2.f, c[0], c[1], c[2], 1.0f);
            ui.draw_rect(rx, r.y, 2.f, r.h, c[0], c[1], c[2], 1.0f);
            ui.draw_rect(rx + r.w - 2.f, r.y, 2.f, r.h, c[0], c[1], c[2], 1.0f);
        };
        if (tgt == -2) outline(track_add_rect(tracks), sty.audio);                                   // new instrument
        else if (tgt >= 0) outline({ track_x(tgt), kHeaderY, kTrackW, w.dock_top() - kPaneMargin - kHeaderY }, sty.fx);  // effect on this track

        const std::string& pn = vivid::session::plugin_at(w.plugin_drag_i).name;
        const bool inst = (tgt == -2);
        const float gw = 156.f, gh = 20.f, gx = static_cast<float>(mx) + 10.f, gy = static_cast<float>(my) + 6.f;
        ui.draw_rounded_rect(gx, gy, gw, gh, sty.radius, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 0.95f);
        const float* bar = inst ? sty.audio : sty.fx;
        ui.draw_rect(gx, gy, 3.f, gh, bar[0], bar[1], bar[2], 1.0f);
        char pt[40]; std::snprintf(pt, sizeof pt, "%s %.24s", inst ? "\xE2\x86\x92 track:" : "\xE2\x86\x92 fx:", pn.c_str());
        ui.draw_text(gx + 8.f, gy + 3.f, fit_text(ui, pt, gw - 14.f, sty.fs_label).c_str(), sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_label);
    }
}

// The "map this param from a source" picker (the return path).
void draw_map_menu(Renderer2D& ui, const CtxMenu& m) {
    if (!m.open) return;
    const Style& sty = style();
    const float w = 168.f;
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, "map param from:", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.8f);
    for (int j = 0; j < kNumMapSources; ++j) {
        const float iy = m.y + j * 24.f;
        ui.draw_rect(m.x, iy, w, 24.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 24.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);  // gold = return path
        ui.draw_text(m.x + 12.f, iy + 5.f, kMapSources[j].label, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.88f);
    }
}

// The characteristic context menu (the bridge entry point).
void draw_menu(Renderer2D& ui, const CtxMenu& m, const char* track) {
    if (!m.open) return;
    const Style& sty = style();
    const float w = 184.f;
    char hdr[96]; std::snprintf(hdr, sizeof hdr, "%s  \xE2\x86\x92  visuals", track && *track ? track : "track");
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, hdr, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
    for (int j = 0; j < kNumChars; ++j) {
        const float iy = m.y + j * 26.f;
        ui.draw_rect(m.x, iy, w, 26.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 26.f, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f);  // teal = audio->visual
        ui.draw_text(m.x + 14.f, iy + 6.f, kChars[j].label, sty.text[0], sty.text[1], sty.text[2], 1.0f);
    }
}

// Right-click op-node menu: open its editable source (custom nodes) — a built-in
// shows a disabled hint (Clone & Edit lands in P2b).
void draw_node_menu(Renderer2D& ui, const Window& w) {
    const NodeMenu& m = w.node_menu;
    if (!m.open) return;
    const Style& sty = style();
    const float ww = 172.f;
    const char* nm = (w.app && w.app->graph) ? w.app->graph->op_kind_name(m.node) : "node";
    ui.draw_rect(m.x, m.y - 22.f, ww, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, fit_text(ui, nm, ww - 16.f, 0.82f).c_str(), sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
    ui.draw_rect(m.x, m.y, ww, 22.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
    ui.draw_rect(m.x, m.y, 3.f, 22.f, sty.gpu[0], sty.gpu[1], sty.gpu[2], 1.0f);
    if (m.has_source)
        ui.draw_text(m.x + 12.f, m.y + 5.f, "Open source in editor", sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.88f);
    else if (m.cloneable)
        ui.draw_text(m.x + 12.f, m.y + 5.f, "Clone & Edit", sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.88f);
    else
        ui.draw_text(m.x + 12.f, m.y + 5.f, "built-in \xC2\xB7 no editable source", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
}

// Which visuals source is under (mx,my): -1 = master, >=0 = track, -2 = none.
// The +VIZ button (or its meter) is the click target.
int meter_hit(int tracks, int scenes, double mx, double my) {
    if (hit(master_viz_rect(scenes), mx, my) || hit(master_meter_rect(scenes), mx, my)) return -1;
    for (int t = 0; t < tracks; ++t)
        if (hit(track_viz_rect(t, scenes), mx, my) || hit(track_meter_rect(t, scenes), mx, my)) return t;
    return -2;
}

}  // namespace vivid::ui
