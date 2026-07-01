#include "ui/session_view.h"

#include "app/app.h"
#include "app/window.h"
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
    const int n = vivid::session::session_available_effect_count();
    ui.draw_rect(m.x, m.y - 22.f, w, 22.f, sty.panel[0], sty.panel[1], sty.panel[2], 1.0f);
    ui.draw_text(m.x + 10.f, m.y - 18.f, "+ effect", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.82f);
    for (int j = 0; j < n; ++j) {
        const float iy = m.y + j * 24.f;
        ui.draw_rect(m.x, iy, w, 24.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
        ui.draw_rect(m.x, iy, 3.f, 24.f, sty.fx[0], sty.fx[1], sty.fx[2], 1.0f);
        ui.draw_text(m.x + 12.f, iy + 5.f, vivid::session::session_available_effect_name(j), sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.9f);
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
        if (n <= 0) return;
        const float midy = b.y + b.h * 0.5f, colw = b.w / n;
        for (int i = 0; i < n; ++i) {
            const float a = std::min(1.f, std::max(0.f, bins[i])) * (b.h * 0.5f - 1.f);
            ui.draw_rect(b.x + colw * i, midy - a, std::max(1.f, colw - 0.4f), a * 2.f + 1.f, ar, ag, ab, alpha);
        }
    } else {
        vivid::session::ClipNote buf[256];
        const int n = vivid::session::session_get_clip(s, t, sc, buf, 256);
        const double len = vivid::session::session_clip_length(s, t, sc);
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

    // When a visual node is selected in the graph, the dock becomes its inspector.
    const int selop = w.app->graph ? w.app->graph->selected_op() : -1;
    if (selop >= 0) {
        char nh[64]; std::snprintf(nh, sizeof nh, "NODE \xC2\xB7 %s", w.app->graph->op_kind_name(selop));
        section_header(ui, 12.f, y0 + 7.f, nh, sty.gpu);
        ui.draw_text(120.f, y0 + 7.f, "drag knobs to set the base value \xC2\xB7 teal = wired (modulated) \xC2\xB7 click a track header for devices",
                     sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.7f);
        const int pc = w.app->graph->op_param_count_at(selop);
        for (int i = 0; i < pc; ++i) {
            float cx, cy; dock_knob(i, d, cx, cy);
            const float base = w.app->graph->op_param_base_at(selop, i);
            const bool wired = w.app->graph->op_param_wired_at(selop, i);
            char vt[8]; std::snprintf(vt, sizeof vt, "%.2f", base);
            knob(ui, cx, cy, 15.f, base, w.app->graph->op_param_label_at(selop, i), vt, sty.gpu, wired);
        }
        if (pc == 0) ui.draw_text(12.f, y0 + 40.f, "this node has no parameters", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.8f);
        return;
    }

    const int tracks = vivid::session::session_track_count(s);
    const int seltr = std::min(std::max(w.sel_track, 0), tracks - 1);
    const bool aud = vivid::session::session_track_is_audio(s, seltr);
    char hdr[80]; std::snprintf(hdr, sizeof hdr, "DEVICE \xC2\xB7 %.40s", vivid::session::session_track_name(s, seltr));
    section_header(ui, 12.f, y0 + 7.f, hdr, sty.audio);

    // device chips: instrument (0) + effects (1..nfx) + "+ FX"
    const int nfx = vivid::session::session_effect_count(s, seltr);
    for (int i = 0; i <= nfx + 1; ++i) {
        const bool isInst = (i == 0), isAdd = (i == nfx + 1);
        if (isInst && aud) continue;  // sampler track has no instrument plugin
        const Rect b = w.dock_chip(i);
        const bool sel = !isAdd && w.sel_device == (isInst ? 0 : i);
        const float* acc = isAdd ? sty.control : (isInst ? sty.audio : sty.fx);
        draw_card(ui, b.x, b.y, b.w, b.h, acc, hit(b, mx, my) || sel);
        if (sel) ui.draw_rect(b.x, b.y + b.h - 2.f, b.w, 2.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);
        if (isAdd) { ui.draw_text(b.x + 10.f, b.y + 11.f, "+ FX", 0.62f, 0.80f, 0.72f, 1.0f, 0.9f); continue; }
        ui.draw_text(b.x + 8.f, b.y + 6.f, isInst ? "INST" : "FX", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.64f);
        char nm[24]; std::snprintf(nm, sizeof nm, "%.13s", isInst ? vivid::session::session_track_name(s, seltr)
                                                                  : vivid::session::session_effect_name(s, seltr, i - 1));
        ui.draw_text(b.x + 8.f, b.y + 17.f, nm, sty.text[0], sty.text[1], sty.text[2], 1.0f, 0.82f);
        if (!isInst) {
            const Rect xb = w.dock_chip_x(i);
            ui.draw_rect(xb.x, xb.y, xb.w, xb.h, 0.4f, 0.18f, 0.18f, 1.0f);
            ui.draw_text(xb.x + 3.f, xb.y, "x", 0.85f, 0.6f, 0.6f, 1.0f, 0.8f);
        }
    }

    // knob grid for the selected device's params
    const int seldev = std::max(0, w.sel_device);
    const int pc = vivid::session::session_param_count(s, seltr, seldev);
    const float* pacc = (seldev == 0) ? sty.audio : sty.fx;
    const int shown = std::min(pc, d.cols * d.maxRows);
    for (int i = 0; i < shown; ++i) {
        float cx, cy; dock_knob(i, d, cx, cy);
        const float v = vivid::session::session_param_value(s, seltr, seldev, i);
        char nm[12]; std::snprintf(nm, sizeof nm, "%.10s", vivid::session::session_param_name(s, seltr, seldev, i));
        char vt[8]; std::snprintf(vt, sizeof vt, "%.2f", v);
        const bool mapped = w.app->graph && w.app->graph->source_of(param_dest(seltr, seldev, i)) != nullptr;
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

    if (!w.app->session) return;
    auto* s = w.app->session;
    const int tracks = vivid::session::session_track_count(s);
    const int scenes = vivid::session::session_scene_count(s);

    // ================= DAW pane =================
    ui.push_clip_rect(0.f, kTopBarH, w.split_x, w.dock_top() - kTopBarH);
    ui.draw_rect(0.f, kTopBarH, w.split_x, w.dock_top() - kTopBarH, sty.bg[0], sty.bg[1], sty.bg[2], 1.0f);
    panel(ui, session_panel(w.split_x, w.win_h, w.dock_h), "SESSION", sty.audio);   // the bounded region
    const float contentR = w.split_x - kPaneMargin - kPanePad;           // right edge of the panel content

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
        viz_button(track_viz_rect(t, scenes));
    }
    const Rect mm = master_meter_rect(scenes);
    const float ml = w.app->transport ? std::min(1.0f, w.app->transport->level.load(std::memory_order_relaxed) * 4.0f) : 0.f;
    ui.draw_rect(mm.x, mm.y, mm.w, mm.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
    ui.draw_rect(mm.x, mm.y, mm.w * ml, mm.h, sty.teal[0], sty.teal[1], sty.teal[2], 1.0f);
    ui.draw_text(kSceneColX, mm.y + 6.f, "MAIN", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_kicker);
    viz_button(master_viz_rect(scenes));

    ui.pop_clip_rect();  // end DAW pane

    // ================= visuals pane (Output + Signal regions; content drawn by the GPU / node graph) =================
    ui.push_clip_rect(w.split_x, kTopBarH, W - w.split_x, w.dock_top() - kTopBarH);
    char oh[48]; std::snprintf(oh, sizeof oh, "OUTPUT \xC2\xB7 %s", w.app->visual_source ? "VIDEO" : "SHADER");
    panel_frame(ui, w.output_panel(), oh, sty.gpu);
    panel_frame(ui, w.signal_panel(), "SIGNAL \xC2\xB7 VISUALS", sty.gpu);
    ui.pop_clip_rect();

    // DAW | visuals splitter (on top, unclipped)
    const Rect sp = w.splitter_rect();
    const bool sph = hit(sp, mx, my);
    ui.draw_rect(sp.x, sp.y, sp.w, sp.h, sph ? sty.border[0] : sty.border_soft[0], sph ? sty.border[1] : sty.border_soft[1], sph ? sty.border[2] : sty.border_soft[2], 1.0f);
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
