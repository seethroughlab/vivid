#include "ui/session_view.h"

#include "app/app_state.h"
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include "ui/node_graph.h"
#include "audio/vst3_host.h"
#include "transport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

// The "+ FX" effect picker for the device chain.
void draw_fx_menu(Renderer2D& ui, const CtxMenu& m) {
    if (!m.open) return;
    const Style& sty = style();
    const float w = 150.f;
    const int n = vivid_poc::session_available_effect_count();
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, "+ effect", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
    for (int j = 0; j < n; ++j) {
        const float iy = m.y + j * 24.f;
        ui.draw_rect(m.x, iy, w, 24.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 24.f, sty.fx[0], sty.fx[1], sty.fx[2], 1.0f);
        ui.draw_text(m.x + 12.f, iy + 5.f, vivid_poc::session_available_effect_name(j), sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.9f);
    }
}

// Mini clip preview inside a session cell: a piano-roll for MIDI clips, a
// waveform for audio clips. Drawn faintly so the clip name reads on top.
void draw_clip_preview(Renderer2D& ui, vivid_poc::Session* s, int t, int sc,
                       const Rect& b, float ar, float ag, float ab, bool on) {
    const float alpha = on ? 0.85f : 0.5f;
    if (vivid_poc::session_track_is_audio(s, t)) {
        float bins[48];
        const int n = vivid_poc::session_audio_waveform(s, t, sc, bins, 48);
        if (n <= 0) return;
        const float midy = b.y + b.h * 0.5f, colw = b.w / n;
        for (int i = 0; i < n; ++i) {
            const float a = std::min(1.f, std::max(0.f, bins[i])) * (b.h * 0.5f - 1.f);
            ui.draw_rect(b.x + colw * i, midy - a, std::max(1.f, colw - 0.4f), a * 2.f + 1.f, ar, ag, ab, alpha);
        }
    } else {
        vivid_poc::ClipNote buf[256];
        const int n = vivid_poc::session_get_clip(s, t, sc, buf, 256);
        const double len = vivid_poc::session_clip_length(s, t, sc);
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
}

// The bottom device-view dock: device chips for the selected track + a knob grid
// of the selected device's params. Full window width; resizable via its top edge.
void draw_device_dock(Renderer2D& ui, const AudioState& st, double mx, double my,
                      int win_w, int win_h, float dock_h) {
    vivid_poc::Session* s = st.session;
    if (!s) return;
    const Style& sty = style();
    const DockGeom d = dock_geom(win_w, win_h, dock_h);
    const float y0 = d.y0;
    ui.draw_rect(0.f, y0, static_cast<float>(win_w), dock_h, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    const bool rhov = hit(dock_resize_rect(win_w, win_h, dock_h), mx, my);
    ui.draw_rect(0.f, y0 - 1.f, static_cast<float>(win_w), 2.f,
                 rhov ? 0.40f : sty.sep[0], rhov ? 0.46f : sty.sep[1], rhov ? 0.52f : sty.sep[2], 1.0f);

    // When a visual node is selected in the graph, the dock becomes its inspector.
    const int selop = st.graph ? st.graph->selected_op() : -1;
    if (selop >= 0) {
        char nh[64]; std::snprintf(nh, sizeof nh, "NODE \xC2\xB7 %s", st.graph->op_kind_name(selop));
        section_header(ui, 12.f, y0 + 7.f, nh, sty.gpu);
        ui.draw_text(120.f, y0 + 7.f, "drag knobs to set the base value \xC2\xB7 teal = wired (modulated) \xC2\xB7 click a track header for devices",
                     sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.7f);
        const int pc = st.graph->op_param_count_at(selop);
        for (int i = 0; i < pc; ++i) {
            float cx, cy; dock_knob(i, d, cx, cy);
            const float base = st.graph->op_param_base_at(selop, i);
            const bool wired = st.graph->op_param_wired_at(selop, i);
            char vt[8]; std::snprintf(vt, sizeof vt, "%.2f", base);
            knob(ui, cx, cy, 15.f, base, st.graph->op_param_label_at(selop, i), vt, sty.gpu, wired);
        }
        if (pc == 0) ui.draw_text(12.f, y0 + 40.f, "this node has no parameters", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.8f);
        return;
    }

    const int tracks = vivid_poc::session_track_count(s);
    const int seltr = std::min(std::max(st.sel_track, 0), tracks - 1);
    const bool aud = vivid_poc::session_track_is_audio(s, seltr);
    char hdr[80]; std::snprintf(hdr, sizeof hdr, "DEVICE \xC2\xB7 %.40s", vivid_poc::session_track_name(s, seltr));
    section_header(ui, 12.f, y0 + 7.f, hdr, sty.audio);

    // device chips: instrument (0) + effects (1..nfx) + "+ FX"
    const int nfx = vivid_poc::session_effect_count(s, seltr);
    for (int i = 0; i <= nfx + 1; ++i) {
        const bool isInst = (i == 0), isAdd = (i == nfx + 1);
        if (isInst && aud) continue;  // sampler track has no instrument plugin
        const Rect b = dock_chip(i, win_h, dock_h);
        const bool sel = !isAdd && st.sel_device == (isInst ? 0 : i);
        const float* acc = isAdd ? sty.control : (isInst ? sty.audio : sty.fx);
        draw_card(ui, b.x, b.y, b.w, b.h, acc, hit(b, mx, my) || sel);
        if (sel) ui.draw_rect(b.x, b.y + b.h - 2.f, b.w, 2.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);
        if (isAdd) { ui.draw_text(b.x + 10.f, b.y + 11.f, "+ FX", 0.62f, 0.80f, 0.72f, 1.0f, 0.9f); continue; }
        ui.draw_text(b.x + 8.f, b.y + 6.f, isInst ? "INST" : "FX", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.64f);
        char nm[24]; std::snprintf(nm, sizeof nm, "%.13s", isInst ? vivid_poc::session_track_name(s, seltr)
                                                                  : vivid_poc::session_effect_name(s, seltr, i - 1));
        ui.draw_text(b.x + 8.f, b.y + 17.f, nm, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.82f);
        if (!isInst) {
            const Rect xb = dock_chip_x(i, win_h, dock_h);
            ui.draw_rect(xb.x, xb.y, xb.w, xb.h, 0.4f, 0.18f, 0.18f, 1.0f);
            ui.draw_text(xb.x + 3.f, xb.y, "x", 0.85f, 0.6f, 0.6f, 1.0f, 0.8f);
        }
    }

    // knob grid for the selected device's params
    const int seldev = std::max(0, st.sel_device);
    const int pc = vivid_poc::session_param_count(s, seltr, seldev);
    const float* pacc = (seldev == 0) ? sty.audio : sty.fx;
    const int shown = std::min(pc, d.cols * d.maxRows);
    for (int i = 0; i < shown; ++i) {
        float cx, cy; dock_knob(i, d, cx, cy);
        const float v = vivid_poc::session_param_value(s, seltr, seldev, i);
        char nm[12]; std::snprintf(nm, sizeof nm, "%.10s", vivid_poc::session_param_name(s, seltr, seldev, i));
        char vt[8]; std::snprintf(vt, sizeof vt, "%.2f", v);
        const bool mapped = st.graph && st.graph->source_of(param_dest(seltr, seldev, i)) != nullptr;
        knob(ui, cx, cy, 15.f, v, nm, vt, pacc, mapped);
        const Rect mb = dock_knob_map(i, d);   // small map affordance (amber=unmapped, teal=mapped)
        ui.draw_rect(mb.x, mb.y, mb.w, mb.h, mapped ? sty.teal[0] : 0.55f, mapped ? sty.teal[1] : 0.45f,
                     mapped ? sty.teal[2] : 0.22f, mapped ? 1.0f : 0.7f);
    }
    if (pc > shown) {
        char more[48]; std::snprintf(more, sizeof more, "+%d more \xE2\x80\x94 drag the dock taller", pc - shown);
        ui.draw_text(12.f, y0 + dock_h - 13.f, more, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.68f);
    }
}

// The Session view on Renderer2D: transport, a tracks×scenes clip grid, a mixer.
void draw_ui(Renderer2D& ui, const AudioState& st, double beats, double mx, double my,
             int win_w, int win_h, float split_x, float dock_h, int visual_source) {
    const Style& sty = style();
    ui.draw_rect(0, 0, static_cast<float>(win_w), 40, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_rect(0, 39, static_cast<float>(win_w), 1, sty.sep[0], sty.sep[1], sty.sep[2], 1.0f);  // header rule
    ui.draw_text(20, 12, "VIVID \xE2\x80\x94 Session", sty.text[0], sty.text[1], sty.text[2], 1.0f, 1.15f);
    const double bpm = st.transport ? st.transport->bpm.load(std::memory_order_relaxed) : 120.0;
    char tb[64]; std::snprintf(tb, sizeof tb, "%.0f BPM   4/4", bpm);
    ui.draw_text(190, 13, tb, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.95f);
    int beat = static_cast<int>(std::floor(beats)) % 4; if (beat < 0) beat += 4;
    for (int i = 0; i < 4; ++i) {
        const bool ob = (i == beat);
        ui.draw_rect(360 + i * 22.f, 13, 14, 14, ob ? sty.gold[0] : sty.card[0],
                     ob ? sty.gold[1] : sty.card[1], ob ? sty.gold[2] : sty.card[2], 1.0f);
    }
    if (!st.session) return;
    auto* s = st.session;
    const int tracks = vivid_poc::session_track_count(s);
    const int scenes = vivid_poc::session_scene_count(s);

    ui.push_clip_rect(0.f, 40.f, split_x, dock_top(win_h, dock_h) - 40.f);  // DAW pane (above the dock)
    ui.draw_rect(0.f, 40.f, split_x, dock_top(win_h, dock_h) - 40.f, sty.bg[0], sty.bg[1], sty.bg[2], 1.0f);  // pane bg
    // track headers
    for (int t = 0; t < tracks; ++t) {
        const Rect h = track_header_rect(t);
        float ar, ag, ab; track_accent(t, ar, ag, ab);
        const bool hov = hit(h, mx, my);  // hover: clickable -> editor
        draw_card(ui, h.x, h.y, h.w, h.h, sty.control, hov);
        ui.draw_rect(h.x, h.y, h.w, 3.f, ar, ag, ab, 1.0f);  // track accent overrides the card bar
        char nm[40]; std::snprintf(nm, sizeof nm, "%.18s", vivid_poc::session_track_name(s, t));
        ui.draw_text(h.x + 8.f, h.y + 9.f, nm, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.95f);
    }
    // scene rows + clip cells
    for (int sc = 0; sc < scenes; ++sc) {
        const Rect sb = scene_launch_rect(sc);
        const bool sh = hit(sb, mx, my);
        ui.draw_rect(sb.x, sb.y, sb.w, sb.h, sh ? sty.card_hi[0] : sty.card[0], sh ? sty.card_hi[1] : sty.card[1], sh ? sty.card_hi[2] : sty.card[2], 1.0f);
        char sl[4]; std::snprintf(sl, sizeof sl, "%c", 'A' + sc);
        ui.draw_text(sb.x + 10.f, sb.y + 8.f, sl, sty.text[0], sty.text[1], sty.text[2], 1.0f);
        ui.draw_tri(sb.x + 12.f, sb.y + 30.f, sb.x + 12.f, sb.y + 42.f, sb.x + 23.f, sb.y + 36.f, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f);
        for (int t = 0; t < tracks; ++t) {
            const Rect r = clip_cell_rect(t, sc);
            const bool on = vivid_poc::session_active_clip(s, t) == sc;
            const bool q  = vivid_poc::session_queued_clip(s, t) == sc;
            const bool hov = hit(r, mx, my);
            float ar, ag, ab; track_accent(t, ar, ag, ab);
            const float tbh = 15.f;  // title-bar height
            // cell body (the preview well sits below the title bar)
            ui.draw_rounded_rect(r.x, r.y, r.w, r.h, 4.f, on ? 0.12f : 0.085f, on ? 0.135f : 0.095f, on ? 0.16f : 0.11f, 1.0f);
            // title bar: accent-tinted, brighter when active
            ui.draw_rect(r.x + 1.f, r.y + 1.f, r.w - 2.f, tbh, ar * (on ? 0.55f : 0.28f) + (hov ? 0.05f : 0.f),
                         ag * (on ? 0.55f : 0.28f) + (hov ? 0.05f : 0.f), ab * (on ? 0.55f : 0.28f) + (hov ? 0.05f : 0.f), 1.0f);
            if (q) ui.draw_rect(r.x, r.y, r.w, 2.f, 0.95f, 0.75f, 0.20f, 1.0f);  // queued = gold top edge
            // play glyph (active) then the clip name in the title bar
            float tx = r.x + 6.f;
            if (on) { ui.draw_tri(r.x + 5.f, r.y + 4.f, r.x + 5.f, r.y + 11.f, r.x + 11.f, r.y + 7.5f, 0.6f, 0.95f, 0.6f, 1.0f); tx = r.x + 15.f; }
            char cn[16];
            const int abpm = vivid_poc::session_track_is_audio(s, t) ? vivid_poc::session_audio_clip_bpm(s, t, sc) : 0;
            if (abpm > 0) std::snprintf(cn, sizeof cn, "%d BPM", abpm);
            else          std::snprintf(cn, sizeof cn, "Clip %c", 'A' + sc);
            ui.draw_text(tx, r.y + 3.f, cn, on ? 0.95f : 0.72f, on ? 0.97f : 0.74f, 1.0f, 1.0f, 0.72f);
            // preview fills the body beneath the title bar
            const Rect pv = { r.x + 5.f, r.y + tbh + 4.f, r.w - 10.f, r.h - tbh - 8.f };
            ui.draw_rect(pv.x, pv.y, pv.w, pv.h, 0.03f, 0.035f, 0.045f, 1.0f);
            draw_clip_preview(ui, s, t, sc, pv, ar, ag, ab, on);
        }
    }
    // mixer
    const float my0 = mixer_y(scenes);
    section_header(ui, kSceneColX, my0, "MIX", sty.audio);
    // A teal "+ VIZ" button = send this source into the visuals graph as a node.
    auto viz_button = [&](const Rect& b, bool small) {
        const bool h = hit(b, mx, my);
        draw_card(ui, b.x, b.y, b.w, b.h, sty.teal, h);
        ui.draw_text(b.x + (small ? 8.f : 10.f), b.y + 5.f, small ? "+VIZ" : "+ VIZ",
                     sty.teal[0], sty.teal[1], sty.teal[2], 1.0f, 0.82f);
    };
    for (int t = 0; t < tracks; ++t) {
        const Rect mr = track_meter_rect(t, scenes), gr = track_gain_rect(t, scenes);
        const float lvl = std::min(1.0f, vivid_poc::session_track_level(s, t) * 4.0f);
        ui.draw_rect(mr.x, mr.y, mr.w, mr.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
        ui.draw_rect(mr.x, mr.y, mr.w * lvl, mr.h, 0.30f, 0.80f, 0.50f, 1.0f);  // green level
        const float g = vivid_poc::session_track_gain(s, t);
        ui.draw_rect(gr.x, gr.y, gr.w, gr.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
        ui.draw_rect(gr.x, gr.y, gr.w * g, gr.h, sty.gpu[0] * 0.8f, sty.gpu[1] * 0.8f, sty.gpu[2] * 0.85f, 1.0f);
        ui.draw_rect(gr.x + gr.w * g - 2.f, gr.y - 2.f, 4.f, gr.h + 4.f, sty.text[0], sty.text[1], sty.text[2], 1.0f);
        viz_button(track_viz_rect(t, scenes), false);
    }
    // master meter + its viz button
    const Rect mm = master_meter_rect(scenes);
    const float ml = st.transport ? std::min(1.0f, st.transport->level.load(std::memory_order_relaxed) * 4.0f) : 0.f;
    ui.draw_rect(mm.x, mm.y, mm.w, mm.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
    ui.draw_rect(mm.x, mm.y, mm.w * ml, mm.h, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f);
    ui.draw_text(kSceneColX, mm.y + 8.f, "MASTER", sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.78f);
    viz_button(master_viz_rect(scenes), true);
    ui.draw_text(kSceneColX, my0 + 78.f,
                 "+VIZ \xE2\x86\x92 add a node to the visuals graph (then drag its port onto a shader input)",
                 sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);

    ui.pop_clip_rect();  // end DAW pane (device chain + params now live in the bottom dock)
    // Visuals pane label (clipped to the right pane)
    const Rect vp = viewer_rect(win_w, split_x);
    ui.push_clip_rect(split_x, 40.f, static_cast<float>(win_w) - split_x, dock_top(win_h, dock_h) - 40.f);
    ui.draw_text(vp.x, 80.f, visual_source ? "VISUALS — video source  ·  V: plasma  ·  N: next clip"
                                           : "VISUALS — plasma shader  ·  V: video",
                 0.55f, 0.78f, 0.85f, 1.0f, 0.95f);
    ui.pop_clip_rect();
    // DAW | visuals splitter (on top, unclipped)
    const Rect sp = splitter_rect(win_h, dock_h, split_x);
    const bool sph = hit(sp, mx, my);
    ui.draw_rect(sp.x, sp.y, sp.w, sp.h, sph ? 0.30f : 0.16f, sph ? 0.34f : 0.17f, sph ? 0.40f : 0.20f, 1.0f);
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

// Which visuals source is under (mx,my): -1 = master, >=0 = track, -2 = none.
// The +VIZ button (or its meter) is the click target.
int meter_hit(int tracks, int scenes, double mx, double my) {
    if (hit(master_viz_rect(scenes), mx, my) || hit(master_meter_rect(scenes), mx, my)) return -1;
    for (int t = 0; t < tracks; ++t)
        if (hit(track_viz_rect(t, scenes), mx, my) || hit(track_meter_rect(t, scenes), mx, my)) return t;
    return -2;
}

}  // namespace vivid::ui
