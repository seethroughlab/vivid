#include "ui/session_view.h"

#include "app/app.h"
#include "app/window.h"
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include "ui/node_graph.h"
#include "ui/audio_node_graph.h"
#include "ui/compound_widget.h"   // UI-4a: host-composed compound inspector widgets
#include "ui/operator_draw_bridge.h"  // UI-4b: Renderer2D -> VividDrawAPI adapter
#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"
#include "transport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace vivid::ui {

// forward decls (definitions live below)
void draw_midi_preview(Renderer2D& ui, const vivid::session::ClipNote* buf, int n, double len,
                       const Rect& b, float ar, float ag, float ab, float alpha);
void draw_wave_preview(Renderer2D& ui, const float* bins, int n,
                       const Rect& b, float ar, float ag, float ab, float alpha);
void draw_sidebar(Renderer2D& ui, const Window& w, double mx, double my);

// UI-4b: bridge the host palette + node-graph param writes to the operator editor ABI.
namespace {
inline VividColor vc(const float* c, float a = 1.f) { return { c[0], c[1], c[2], a }; }
VividInspectorTheme editor_theme(const Style& s) {
    VividInspectorTheme t{};
    t.bg = vc(s.panel); t.accent = vc(s.gpu); t.dim_text = vc(s.dim); t.bright_text = vc(s.body);
    t.separator = vc(s.border_soft); t.dark_bg = vc(s.bg); t.slider_fill = vc(s.gpu); t.slider_track = vc(s.card);
    t.corner_radius = s.radius;
    return t;
}
// Command sink: an operator editor's set_param(name, value) → the node's base param by that name.
struct EditorCmd { NodeGraph* g; int node; };
void editor_set_param(void* o, const char* name, float v) {
    auto* c = static_cast<EditorCmd*>(o);
    if (!c || !c->g || !name) return;
    const int pc = c->g->op_param_count_at(c->node);
    for (int l = 0; l < pc; ++l)
        if (std::strcmp(c->g->op_param_label_at(c->node, l), name) == 0) { c->g->set_op_param_base_at(c->node, l, v); return; }
}
}  // namespace

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
    // Source mode (from "+ Src" in the audio-graph deep view) lists native INSTRUMENTS. Graph FX
    // mode is native effects only; the device chain lists the VST3 catalog first, then native ops.
    if (m.sources) {
        const int nsrc = s ? vivid::session::session_available_audio_op_count(s, 1) : 0;   // 1 = sources
        ui.draw_rect(m.x, m.y - 22.f, w, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
        ui.draw_text(m.x + 10.f, m.y - 18.f, "+ source", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
        for (int j = 0; j < nsrc; ++j) {
            const float iy = m.y + j * 24.f;
            ui.draw_rect(m.x, iy, w, 24.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
            ui.draw_rect(m.x, iy, 3.f, 24.f, sty.audio[0], sty.audio[1], sty.audio[2], 1.0f);
            ui.draw_text(m.x + 12.f, iy + 5.f, vivid::session::session_available_audio_op_name(s, 1, j),
                         sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.9f);
        }
        return;
    }
    const int nvst = m.graph ? 0 : vivid::session::session_available_effect_count();
    const int nnat = s ? vivid::session::session_available_audio_op_count(s, 0) : 0;   // 0 = effects
    overlay_panel(ui, { m.x, m.y - 22.f, w, 22.f + (nvst + nnat) * 24.f }, "+ effect", sty.fx);
    for (int j = 0; j < nvst + nnat; ++j) {
        const bool nat = (j >= nvst);
        const float iy = m.y + j * 24.f;
        const float* acc = nat ? sty.audio : sty.fx;
        const char* nm = nat ? vivid::session::session_available_audio_op_name(s, 0, j - nvst)
                             : vivid::session::session_available_effect_name(j);
        item_box(ui, { m.x, iy, w, 24.f }, acc);
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
    overlay_panel(ui, { m.x, m.y - 22.f, w, 22.f + (n + 1) * 24.f }, "+ track", sty.audio);
    for (int j = 0; j <= n; ++j) {   // n instrument rows + one "Audio track" row
        const float iy = m.y + j * 24.f;
        const bool isAudio = (j == n);
        const float* acc = isAudio ? sty.audio : sty.fx;
        item_box(ui, { m.x, iy, w, 24.f }, acc);
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
            item_box(ui, r, acc, hov);
            std::string nm = vivid::session::session_pool_name(s, i);
            if (nm.empty()) nm = "clip " + std::to_string(i + 1);
            ui.draw_text(r.x + 9.f, r.y + 4.f, fit_text(ui, nm, r.w - 28.f, sty.fs_value).c_str(), sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_value);
            const Rect pv = { r.x + 9.f, r.y + 18.f, r.w - 18.f, r.h - 22.f };
            recess(ui, pv);
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

}

// The bottom device-view dock: device chips for the selected track + a knob grid
// of the selected device's params. Full window width; resizable via its top edge.
void draw_device_dock(Renderer2D& ui, const Window& w, double mx, double my) {
    vivid::session::Session* s = w.app->session;
    if (!s) return;
    const Style& sty = style();
    const DockGeom d = w.dock_geom();
    const float y0 = d.y0;
    const bool rhov = hit(w.dock_resize_rect(), mx, my);
    // UI-1: the detail region always declares its domain (strict-zones principle) — an accent
    // edge + a badge, driven by the explicit focus, so audio vs visual is never ambiguous.
    const bool vis = (w.focus.dom == FocusContext::Dom::Visual);
    const float* dc = vis ? sty.gpu : sty.audio;
    detail_dock(ui, { 0.f, y0, static_cast<float>(w.win_w), w.dock_h }, dc, rhov);
    {
        draw_text_r(ui, w.win_w - 12.f, y0 + 6.f, vis ? "VISUAL" : "AUDIO", dc, 0.9f, sty.fs_kicker);
        // Close (x): exits a drilled-in VISUAL focus back to the output. The audio graph is now a
        // track's home detail view (not a drill-in), so it has no close-x.
        if (w.focus.kind == FocusContext::Kind::VisualNode || w.focus.kind == FocusContext::Kind::OpEditor) {
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
        // UI-4b: if this op exports a custom editor, offer an "Editor" drill-in button.
        if (w.app->graph->op_has_editor(selop)) {
            const Rect eb = dock_op_editor_button_rect(w.win_w, w.win_h, w.dock_h);
            const bool eh = hit(eb, mx, my);
            item_box(ui, eb, sty.gpu, eh);
            ui.draw_text(eb.x + 7.f, eb.y + 2.f, "Editor", sty.gpu[0], sty.gpu[1], sty.gpu[2], eh ? 1.0f : 0.85f, sty.fs_label);
        }
        auto* g = w.app->graph;
        const int pc = g->op_param_count_at(selop);
        for (int i = 0; i < pc; ++i) {
            const int hint = g->op_param_hint_at(selop, i);
            if (hint == VIVID_DISPLAY_HIDDEN || hint == VIVID_DISPLAY_EDITOR || hint == VIVID_DISPLAY_TRANSIENT) continue;
            // UI-4a: a compound widget claims several consecutive params and draws as one unit.
            if (is_compound_widget(hint)) {
                const int span = compound_span(hint);
                const Rect cr = node_param_compound_rect(i, span, w.win_w, w.win_h, w.dock_h);
                if (hint == VIVID_DISPLAY_XY_PAD) {
                    char lbl[48]; std::snprintf(lbl, sizeof lbl, "%s / %s",
                                                g->op_param_label_at(selop, i), g->op_param_label_at(selop, i + 1));
                    draw_xy_pad(ui, cr, g->op_param_base_at(selop, i), g->op_param_base_at(selop, i + 1), sty.gpu, lbl);
                } else if (hint == VIVID_DISPLAY_COLOR) {
                    const Rect r0 = node_param_row(i, w.win_w, w.win_h, w.dock_h);
                    const Rect r2 = node_param_row(i + 2, w.win_w, w.win_h, w.dock_h);
                    const Rect sw = { r0.x, r0.y + 2.f, kNodeLabelW - 12.f, (r2.y + r2.h) - r0.y - 4.f };
                    draw_color_swatch(ui, sw, g->op_param_base_at(selop, i), g->op_param_base_at(selop, i + 1), g->op_param_base_at(selop, i + 2));
                    static const char* ch[3] = { "R", "G", "B" };
                    for (int k = 0; k < 3; ++k) {
                        const Rect wr = node_param_widget_rect(i + k, w.win_w, w.win_h, w.dock_h);
                        const float b = g->op_param_base_at(selop, i + k);
                        char vt[8]; std::snprintf(vt, sizeof vt, "%.2f", b);
                        slider(ui, wr.x, wr.y, wr.w, wr.h, b, ch[k], vt, sty.gpu, false);
                    }
                }
                i += span - 1;
                continue;
            }
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
                case NodeWidget::File: {   // path field: show the basename, click to choose
                    const char* fp = g->op_file_param_at(selop, i);
                    std::string bn = (fp && fp[0]) ? std::string(fp) : std::string();
                    if (auto slash = bn.find_last_of("/\\"); slash != std::string::npos) bn = bn.substr(slash + 1);
                    dropdown_field(ui, wr.x, wr.y + 1.f, wr.w, wr.h - 2.f,
                                   bn.empty() ? "(choose file…)" : bn.c_str(), sty.gpu, hit(wr, mx, my));
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

    // UI-4b: an operator-exported custom editor, hosted in the detail region. The host builds a
    // VividEditorContext over the dock content rect (draw bridged to Renderer2D, param values from
    // the node's base, mouse from the live cursor + button state) and lets the op draw + self-edit
    // via commands.set_param. Drilled in from the visual-node "Editor" button; close x exits.
    if (w.focus.kind == FocusContext::Kind::OpEditor && w.app->graph) {
        auto* g = w.app->graph;
        const int selop = w.focus.node;
        char eh[64]; std::snprintf(eh, sizeof eh, "EDITOR \xC2\xB7 %s", g->op_kind_name(selop));
        section_header(ui, 12.f, y0 + 7.f, eh, sty.gpu);
        // UI-5: "Float" — pop the editor out into its own OS window.
        {
            const Rect fb = dock_op_float_button_rect(w.win_w, w.win_h, w.dock_h);
            const bool fh = hit(fb, mx, my);
            item_box(ui, fb, sty.gpu, fh);
            ui.draw_text(fb.x + 7.f, fb.y + 2.f, "Float", sty.gpu[0], sty.gpu[1], sty.gpu[2], fh ? 1.0f : 0.85f, sty.fs_label);
        }
        const float ex = 8.f, ey = y0 + 24.f, ew = w.win_w - 16.f, eht = (y0 + w.dock_h) - ey - 6.f;
        const int pc = g->op_param_count_at(selop);
        std::vector<float> pv(pc > 0 ? pc : 1, 0.f);
        for (int i = 0; i < pc; ++i) pv[i] = g->op_param_base_at(selop, i);
        DrawBridge db{ &ui, ex, ey, ex, ey, ew, eht };
        EditorCmd cmd{ g, selop };
        VividEditorContext ctx{};
        ctx.surface_width = ew; ctx.surface_height = eht; ctx.dpi_scale = 1.f;
        ctx.draw = make_op_draw_api(&db);
        ctx.theme = editor_theme(sty);
        ctx.commands.opaque = &cmd; ctx.commands.set_param = editor_set_param;
        ctx.param_values = pv.data(); ctx.param_count = static_cast<uint32_t>(pc);
        ctx.mouse.x = static_cast<float>(w.cur_x) - ex; ctx.mouse.y = static_cast<float>(w.cur_y) - ey;
        ctx.mouse.left_down = w.mouse_left_down ? 1 : 0;
        ui.push_clip_rect(ex, ey, ew, eht);
        g->op_draw_editor(selop, &ctx);
        ui.pop_clip_rect();
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
        ag.set_view(w.ag_zoom, w.ag_pan_x, w.ag_pan_y);
        ag.set_selection(w.sel_audio_node);   // sizes the param band for a compound preview
        ag.set_mapping(w.app->graph);         // lights the map dot on any bridge-driven node param
        ag.draw(ui, w.sel_audio_node, w.ag_wire_from,
                static_cast<float>(w.cur_x), static_cast<float>(w.cur_y));
        return;
    }

    // The linear device chip-row view was retired (G3): a track's primary detail view is now
    // its audio node graph, drawn above. Every focus kind returns before reaching here.
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
        toolbar_button(ui, { b.x - 2.f, b.y - 2.f, b.w + 4.f, b.h + 4.f }, hov, open);
        const float* ic = open ? sty.audio : sty.dim;
        for (int i = 0; i < 3; ++i) ui.draw_rect(b.x + 3.f, b.y + 3.f + i * 5.f, b.w - 6.f, 2.f, ic[0], ic[1], ic[2], 1.0f);
    }
    {
        const Rect p = transport_play_rect();
        const bool hov = hit(p, mx, my);
        toolbar_button(ui, { p.x - 3.f, p.y - 3.f, p.w + 6.f, p.h + 6.f }, hov);
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
        toolbar_button(ui, { r.x - 3.f, r.y - 3.f, r.w + 6.f, r.h + 6.f }, hov, recording);
        const float rc[3] = { 0.90f, 0.24f, 0.28f };
        const float* c = recording ? rc : sty.dim;
        ui.draw_rect(r.x + r.w * 0.5f - 6.f, r.y + r.h * 0.5f - 6.f, 12.f, 12.f, c[0], c[1], c[2], 1.0f);   // filled disc
    }
    {
        const Rect r = transport_metro_rect();
        const bool hov = hit(r, mx, my);
        toolbar_button(ui, { r.x - 3.f, r.y - 3.f, r.w + 6.f, r.h + 6.f }, hov, metro_on);
        const float* c = metro_on ? sty.gold : sty.dim;
        // a tiny triangular "metronome" glyph
        ui.draw_tri(r.x + r.w * 0.5f, r.y + 3.f, r.x + 3.f, r.y + r.h - 3.f, r.x + r.w - 3.f, r.y + r.h - 3.f, c[0], c[1], c[2], 1.0f);
    }
    if (w.typing) {
        ui.draw_rect(550.f, 12.f, 40.f, 16.f, sty.audio[0] * 0.35f, sty.audio[1] * 0.35f, sty.audio[2] * 0.35f, 1.0f);
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
    session_workspace_header(ui, spanel, "SESSION", sty.audio);
    const float contentR = dawW - kPaneMargin - kPanePad;                // right edge of the panel content
    // Clip the session workspace so extra track columns are cut cleanly at the
    // split gutter instead of bleeding into the visuals pane. (screen coords)
    ui.push_clip_rect(SW + spanel.x, spanel.y, spanel.w, spanel.h);

    // track headers (accent left edge, ellipsised name, remove ×) + a "+ Track" cell
    for (int t = 0; t < tracks; ++t) {
        const Rect h = track_header_rect(t);
        float ar, ag, ab; track_accent(t, ar, ag, ab);
        const bool hov = hit(h, mx, my);
        const float acc[3] = { ar, ag, ab };
        session_header_cell(ui, h, acc, hov);
        std::string nm = fit_text(ui, vivid::session::session_track_name(s, t), h.w - 28.f, sty.fs_label);
        ui.draw_text(h.x + 10.f, h.y + 6.f, nm.c_str(), sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_label);
        const Rect xb = track_header_x_rect(t);
        const bool xh = hit(xb, mx, my);
        ui.draw_text(xb.x + 1.f, xb.y - 2.f, "\xC3\x97", xh ? 0.82f : 0.46f, xh ? 0.42f : 0.49f, xh ? 0.42f : 0.55f, 1.0f, sty.fs_body);
    }
    if (tracks < vivid::session::kMaxTracks) {
        const Rect a = track_add_rect(tracks);
        const bool ah = hit(a, mx, my);
        session_header_cell(ui, a, sty.audio, ah);
        ui.draw_text(a.x + 10.f, a.y + 6.f, "+ Track", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
    }
    // scene rows + clip cells
    for (int sc = 0; sc < scenes; ++sc) {
        const Rect sb = scene_launch_rect(sc);
        const bool sh = hit(sb, mx, my);
        session_scene_button(ui, sb, sty.audio, sh);
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
            const float acc[3] = { ar, ag, ab };
            session_clip_cell(ui, r, acc, hov, on, q, tbh);
            float tx = r.x + 6.f;
            if (on) { ui.draw_tri(r.x + 5.f, r.y + 4.f, r.x + 5.f, r.y + 10.f, r.x + 10.f, r.y + 7.f, sty.green[0], sty.green[1], sty.green[2], 1.0f); tx = r.x + 14.f; }
            char cn[16];
            const int abpm = vivid::session::session_track_is_audio(s, t) ? vivid::session::session_audio_clip_bpm(s, t, sc) : 0;
            if (abpm > 0) std::snprintf(cn, sizeof cn, "%d BPM", abpm);
            else          std::snprintf(cn, sizeof cn, "Clip %c", 'A' + sc);
            ui.draw_text(tx, r.y + 2.f, cn, on ? 0.95f : 0.72f, on ? 0.97f : 0.74f, 1.0f, 1.0f, sty.fs_value);
            const Rect pv = { r.x + 4.f, r.y + tbh + 3.f, r.w - 8.f, r.h - tbh - 6.f };
            draw_clip_preview(ui, s, t, sc, pv, ar, ag, ab, on);
        }
    }
    // --- mixer strip ---
    const float my0 = mixer_y(scenes);
    ui.draw_rect(kSceneColX, mixer_divider_y(scenes), contentR - kSceneColX, 1.f, sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);
    section_header(ui, kSceneColX, my0 + 3.f, "MIX", sty.audio);
    auto viz_button = [&](const Rect& b) {
        const bool h = hit(b, mx, my);
        session_control_button(ui, b, sty.teal, h);
        const char* lbl = "VIZ";
        ui.draw_text(b.x + (b.w - ui.text_width(lbl, sty.fs_kicker)) * 0.5f, b.y + 3.f, lbl, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f, sty.fs_kicker);
    };
    for (int t = 0; t < tracks; ++t) {
        const Rect mr = track_meter_rect(t, scenes), gr = track_gain_rect(t, scenes);
        const float lvl = std::min(1.0f, vivid::session::session_track_level(s, t) * 4.0f);
        session_meter_track(ui, mr);
        ui.draw_rect(mr.x, mr.y, mr.w * lvl, mr.h, sty.green[0], sty.green[1], sty.green[2], 1.0f);
        const float g = vivid::session::session_track_gain(s, t);
        session_meter_track(ui, gr);
        ui.draw_rect(gr.x, gr.y, gr.w * g, gr.h, sty.gpu[0] * 0.7f, sty.gpu[1] * 0.7f, sty.gpu[2] * 0.75f, 1.0f);
        ui.draw_rect(gr.x + gr.w * g - 1.5f, gr.y - 1.f, 3.f, gr.h + 2.f, sty.text[0], sty.text[1], sty.text[2], 1.0f);
        // ARM: red when this track is record-armed. Audio tracks can't be armed (no instrument).
        {
            const Rect ar = track_arm_rect(t, scenes);
            const bool armed = vivid::session::session_armed_track(s) == t;
            const bool ah = hit(ar, mx, my);
            const float rc[3] = { 0.90f, 0.24f, 0.28f };
            session_control_button(ui, ar, rc, ah, armed);
            const char* al = "ARM";
            const float* ac = armed ? rc : sty.dim;
            ui.draw_text(ar.x + (ar.w - ui.text_width(al, sty.fs_kicker)) * 0.5f, ar.y + 3.f, al, ac[0], ac[1], ac[2], 1.0f, sty.fs_kicker);
        }
        viz_button(track_viz_rect(t, scenes));
    }
    const Rect mm = master_meter_rect(scenes);
    const float ml = w.app->transport ? std::min(1.0f, w.app->transport->level.load(std::memory_order_relaxed) * 4.0f) : 0.f;
    session_meter_track(ui, mm);
    ui.draw_rect(mm.x, mm.y, mm.w * ml, mm.h, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f);
    ui.draw_text(kSceneColX, mm.y + 6.f, "MAIN", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_kicker);
    viz_button(master_viz_rect(scenes));

    ui.pop_clip_rect();  // end the session workspace clip
    ui.set_transform(0.f, 0.f, 1.f);   // reset the DAW-pane shift
    ui.pop_clip_rect();  // end DAW pane
    // (clip drag feedback is drawn later as a screen-space overlay — it can cross the sidebar↔grid boundary)

    // ================= visuals zone (ADR-0014: the node graph IS the zone) =================
    // The graph draws itself (frame.cpp) into this column; no enclosing panel frame — the canvas
    // is the surface. Only the column's thin cyan domain accent + its corner chrome live here.
    ui.push_clip_rect(w.split_x, kTopBarH, W - w.split_x, w.dock_top() - kTopBarH);
    // (No edge accent bar here: hard against the splitter it reads as a stray line, not identity.
    // The visual domain announces itself through the graph's own cyan node/port coloring.)
    { const Rect rl = graph_relayout_rect(w.win_w, w.win_h, w.split_x, w.dock_h);
      const bool rlh = hit(rl, mx, my);
      item_box(ui, rl, sty.gpu, rlh);
      ui.draw_text(rl.x + 8.f, rl.y + 2.f, "Re-layout", sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label); }
    ui.pop_clip_rect();

    // DAW | visuals splitter (on top, unclipped): a full-height rule from the transport to the dock,
    // plus a centered grip so it reads as draggable rather than as a decorative divider. Hovering
    // (or dragging) lights the whole strip.
    const Rect sp = w.splitter_rect();
    const bool sph = hit(sp, mx, my) || w.split_drag;
    const float* sc = sph ? sty.gpu : sty.border_soft;
    ui.draw_rect(sp.x + 2.f, sp.y, 2.f, sp.h, sc[0], sc[1], sc[2], 1.0f);
    { const Rect gr = splitter_grip_rect(w.win_h, w.dock_h, w.split_x);
      const float* gc = sph ? sty.gpu : sty.border;
      ui.draw_rect(gr.x + 1.f, gr.y, 4.f, gr.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
      for (int i = 0; i < 3; ++i)   // three hard rules = the grip
          ui.draw_rect(gr.x + 1.f, gr.y + gr.h * 0.5f - 5.f + i * 5.f, 4.f, 1.f, gc[0], gc[1], gc[2], 1.0f); }

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
        ui.draw_rect(gx, gy, src.w, src.h, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 0.9f);
        ui.draw_rect(gx + 1.f, gy + 1.f, src.w - 2.f, 13.f, ar * 0.6f, ag * 0.6f, ab * 0.6f, 0.95f);
        char cn[24];
        if (fromPool) std::snprintf(cn, sizeof cn, "%.14s", vivid::session::session_pool_name(s, w.clip_drag_from_pool));
        else          std::snprintf(cn, sizeof cn, "Clip %c", 'A' + w.clip_drag_sc);
        ui.draw_text(gx + 6.f, gy + 2.f, cn, sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_value);
    }

}

// The "map this param from a source" picker (the return path).
void draw_map_menu(Renderer2D& ui, const CtxMenu& m) {
    if (!m.open) return;
    const Style& sty = style();
    const float w = 168.f;
    overlay_panel(ui, { m.x, m.y - 22.f, w, 22.f + kNumMapSources * 24.f }, "map param from:", sty.gold);
    for (int j = 0; j < kNumMapSources; ++j) {
        const float iy = m.y + j * 24.f;
        item_box(ui, { m.x, iy, w, 24.f }, sty.gold);  // gold = return path
        ui.draw_text(m.x + 12.f, iy + 5.f, kMapSources[j].label, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.88f);
    }
}

// The characteristic context menu (the bridge entry point).
void draw_menu(Renderer2D& ui, const CtxMenu& m, const char* track) {
    if (!m.open) return;
    const Style& sty = style();
    const float w = 184.f;
    char hdr[96]; std::snprintf(hdr, sizeof hdr, "%s  \xE2\x86\x92  visuals", track && *track ? track : "track");
    overlay_panel(ui, { m.x, m.y - 22.f, w, 22.f + kNumChars * 26.f }, hdr, sty.teal);
    for (int j = 0; j < kNumChars; ++j) {
        const float iy = m.y + j * 26.f;
        item_box(ui, { m.x, iy, w, 26.f }, sty.teal);  // teal = audio->visual
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
    const std::string title = fit_text(ui, nm, ww - 16.f, 0.82f);
    overlay_panel(ui, { m.x, m.y - 22.f, ww, 44.f }, title.c_str(), sty.gpu);
    item_box(ui, { m.x, m.y, ww, 22.f }, sty.gpu);
    if (m.has_source)
        ui.draw_text(m.x + 12.f, m.y + 5.f, "Open source in editor", sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.88f);
    else if (m.cloneable)
        ui.draw_text(m.x + 12.f, m.y + 5.f, "Clone & Edit", sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.88f);
    else
        ui.draw_text(m.x + 12.f, m.y + 5.f, "built-in \xC2\xB7 no editable source", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
}

// ADR-0014: the floating OUTPUT preview's chrome. The body is NOT filled here — the output FBO was
// already blitted into it by the GPU pass — so this only draws the frame, header and handles over
// it. Shadowed, because unlike a region panel this thing floats above the graph canvas.
void draw_output_preview(Renderer2D& ui, const Window& w, double mx, double my) {
    const Style& sty = style();
    const Rect p = w.preview_panel();
    ui.draw_shadow(p.x, p.y, p.w, p.h);
    char title[64];
    std::snprintf(title, sizeof title, "OUTPUT \xC2\xB7 %d\xC3\x97%d",
                  w.app && w.app->vgraph ? static_cast<int>(w.app->vgraph->rt_w()) : 0,
                  w.app && w.app->vgraph ? static_cast<int>(w.app->vgraph->rt_h()) : 0);
    panel_frame(ui, p, title, sty.gpu);
    // Pop the output out to its own window (second display / performance screen).
    { const Rect pb = preview_popout_rect(w.preview_x, w.preview_y, w.preview_w);
      const bool pbh = hit(pb, mx, my);
      item_box(ui, pb, sty.gpu, pbh, w.popout != nullptr);
      ui.draw_text(pb.x + 6.f, pb.y + 2.f, w.popout ? "\xE2\x87\xB2 In" : "\xE2\x87\xB1 Out",
                   sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label); }
    // Close × (hides the preview; the output keeps rendering — the graph and the pop-out still show it).
    { const Rect cb = w.preview_close();
      const bool cbh = hit(cb, mx, my);
      ui.draw_text(cb.x + 2.f, cb.y - 1.f, "\xC3\x97", cbh ? sty.body[0] : sty.dim[0],
                   cbh ? sty.body[1] : sty.dim[1], cbh ? sty.body[2] : sty.dim[2], 1.0f, sty.fs_label); }
    // Bottom-right resize grip: two hard rules, no rounding.
    { const Rect g = w.preview_grip();
      const bool gh = hit(g, mx, my);
      const float* c = gh ? sty.gpu : sty.border;
      for (int i = 1; i <= 2; ++i) {
          const float o = i * 4.f;
          ui.draw_rect(g.x + g.w - o, g.y + g.h - 3.f, o, 1.f, c[0], c[1], c[2], 1.0f);
          ui.draw_rect(g.x + g.w - 3.f, g.y + g.h - o, 1.f, o, c[0], c[1], c[2], 1.0f);
      } }
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
