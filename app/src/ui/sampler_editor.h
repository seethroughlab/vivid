#pragma once
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"          // Style, Rect, hit(), section_header, draw_text_r, fit_text
#include "ui/editor_controls.h"   // icon_button, segmented, stepper, menu_button, hover_status
#include "ui/waveform_view.h"     // the shared waveform language (ADR-0048 slice 3b)
#include "ui/layout.h"            // segmented_hit / stepper_hit (pure geometry)
#include "audio/vst3_host.h"      // session_sampler_* + node param accessors
#include "audio/sampler_op.h"     // SamplerInfo / SamplerSlice
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

// ADR-0049 slice 5: the Sampler editor. The Sampler node's home detail view — the same dark-steel shell
// and waveform language as the clip editor (slices 1-3b), specialized to a sampler: sample identity,
// the waveform with slice markers + playhead, the VISIBLE key-zone mapping (does this span the keyboard
// or is it one slice per key?), and the high-frequency controls (root, one-shot/gated, transpose, gain)
// as real bounded controls — with the fuller envelope params on a secondary row. Amber accent (audio
// zone). It reads through the slice-4 read API + the generic node-param accessors, and edits params by
// name; it holds only UI state (which slice is selected + click-edge latch), so a file-static singleton
// in the dock is enough (one dock, one selection at a time), mirroring the persistent AudioNodeGraph.
namespace vivid::ui {

class SamplerEditor {
public:
    // Draw + interact for the selected Sampler node, filling `body` (the dock area below the header).
    // Immediate-mode: reads the latest cursor + button; `click` is derived from the caller's edge latch.
    void draw(Renderer2D& r, vivid::session::Session* s, Rect body,
              int track, int node_id, float mx, float my, bool mouse_down) {
        namespace S = vivid::session;
        const Style& sty = style();
        if (node_id != node_) { node_ = node_id; sel_ = -1; }             // reset selection on node change
        const bool click = mouse_down && !prev_down_;                     // rising edge
        prev_down_ = mouse_down;
        auto hov = [&](Rect rr) { return hit(rr, mx, my); };

        // ---- read the sample --------------------------------------------------------------------------
        SamplerInfo info{};
        const bool loaded = S::session_sampler_info(s, track, node_id, &info) != 0;
        const char* src = S::session_sampler_source(s, track, node_id);
        SamplerSlice slc[128]; const int nsl = loaded ? S::session_sampler_slices(s, track, node_id, slc, 128) : 0;

        // ---- identity line (name + geometry) ----------------------------------------------------------
        const char* name = (src && *src) ? base_name(src) : (loaded ? "(sample)" : nullptr);
        if (!loaded || !name) {
            // Nothing loaded: a single legible placeholder rather than an empty panel.
            const float cy = body.y + body.h * 0.5f;
            const std::string t = "no sample \xC2\xB7 drop a file on this node, or load in the graph";
            r.draw_text(body.x + 14.f, cy - 8.f, t.c_str(), sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_value);
            return;
        }
        {
            char id[160];
            const double secs = info.sample_rate ? (double)info.frames / info.sample_rate : 0.0;
            std::snprintf(id, sizeof id, "%s   \xC2\xB7   %.2fs \xC2\xB7 %d Hz \xC2\xB7 %dch \xC2\xB7 %d slice%s \xC2\xB7 %s",
                          name, secs, info.sample_rate, info.channels,
                          nsl, nsl == 1 ? "" : "s", info.gate ? "gated" : "one-shot");
            r.draw_text(body.x + 2.f, body.y, fit_text(r, id, body.w - 12.f, sty.fs_value).c_str(),
                        sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_value);
        }

        // ---- inspector strip: the high-frequency controls ---------------------------------------------
        const float IY = body.y + 20.f, IH = 24.f;
        float x = body.x + 2.f;
        auto place = [&](float w) { Rect rr{ x, IY, w, IH }; x += w + 6.f; return rr; };

        const int   root = pget_i(s, track, node_id, "base_note", info.base_note);
        const int   gate = pget_i(s, track, node_id, "gate", info.gate);
        const int   tr   = pget_i(s, track, node_id, "transpose", 0);
        const float gn   = pget_f(s, track, node_id, "gain", 1.f);

        const Rect rRoot = place(112.f);
        { char v[16]; std::snprintf(v, sizeof v, "%s", note_name(root)); stepper(r, rRoot, "ROOT", v, stepper_hit(rRoot, mx, my)); }
        const Rect rGate = place(150.f);
        segmented(r, rGate, { "One-shot", "Gated" }, gate ? 1 : 0, segmented_hit(rGate, 2, mx, my), sty.audio);
        const Rect rTr = place(132.f);
        { char v[16]; std::snprintf(v, sizeof v, "%+d st", tr); stepper(r, rTr, "TRANS", v, stepper_hit(rTr, mx, my)); }
        const Rect rGn = place(112.f);
        { char v[16]; std::snprintf(v, sizeof v, "%.2f", gn); stepper(r, rGn, "GAIN", v, stepper_hit(rGn, mx, my)); }
        const Rect rEnv = place(120.f);   // the disclosure for the set-and-forget envelope/voice params
        menu_button(r, rEnv, "\xE2\x8B\xAF Envelope", hov(rEnv), env_open_);

        if (click) {
            if (int h = stepper_hit(rRoot, mx, my)) pset(s, track, node_id, "base_note", std::clamp(root + h, 0, 127));
            if (int c = segmented_hit(rGate, 2, mx, my); c >= 0) pset(s, track, node_id, "gate", (float)c);
            if (int h = stepper_hit(rTr, mx, my)) pset(s, track, node_id, "transpose", std::clamp(tr + h, -48, 48));
            if (int h = stepper_hit(rGn, mx, my)) pset(s, track, node_id, "gain", std::clamp(gn + 0.05f * h, 0.f, 2.f));
            if (hit(rEnv, mx, my)) env_open_ = !env_open_;
        }

        // The ⋯ Envelope popover holds the secondary params (ADSR / voices / tune) — disclosed on demand,
        // NOT a permanent second row. Its bounds are known now so the waveform below can ignore clicks it
        // owns; a click anywhere else (not the button, not the popover) dismisses it.
        const Rect pop = env_popover_rect(rEnv, body);
        const bool pop_hit = env_open_ && hit(pop, mx, my);
        if (env_open_ && click && !pop_hit && !hit(rEnv, mx, my)) env_open_ = false;

        // ---- waveform canvas (one control row above → the canvas takes the full remaining height) -------
        const float canvas_top = IY + IH + 8.f;
        const float ksH = 26.f;                                    // key-zone strip height (below the waveform)
        const Rect wf{ body.x + 4.f, canvas_top, body.w - 8.f, std::max(30.f, body.y + body.h - canvas_top - ksH - 6.f) };
        recess(r, wf);
        r.push_clip_rect(wf.x, wf.y, wf.w, wf.h);
        static float peaks[512];
        const int np = S::session_audio_graph_node_sampler_peaks(s, track, node_id, peaks, 512);
        const WaveformView wv{ { wf.x, wf.y, wf.w, wf.h }, 0.0, wf.w, 1.f };   // whole sample, fit-to-width
        static const float kAmber[3] = { 0.94f, 0.63f, 0.19f };
        static const float kSel[3]   = { 0.35f, 0.55f, 0.85f };
        static const float kPlay[3]  = { 0.95f, 0.35f, 0.35f };
        if (np > 0) { wv.bins(r, peaks, np); wv.center_line(r); }
        // slice dividers at each region boundary + selected-slice region highlight
        float divs[128]; int nd = 0;
        for (int i = 0; i < nsl && info.frames; ++i) {
            const double n0 = (double)slc[i].start / info.frames;
            if (i > 0) divs[nd++] = (float)n0;
            if (i == sel_) {
                const double n1 = (double)slc[i].end / info.frames;
                wv.region(r, n0, n1, kSel, 0.20f);
            }
        }
        wv.dividers(r, divs, nd, kAmber, 0.7f);
        { const float ph = S::session_audio_graph_node_sampler_playhead(s, track, node_id);
          if (ph >= 0.f) wv.playhead(r, ph, kPlay); }
        r.pop_clip_rect();
        // click a slice region in the waveform → select it (unless the popover owns this click)
        if (click && !pop_hit && hit(wf, mx, my) && info.frames) {
            const double n = (double)(mx - wf.x) / wf.w;
            for (int i = 0; i < nsl; ++i)
                if (n >= (double)slc[i].start / info.frames && n < (double)slc[i].end / info.frames) { sel_ = (sel_ == i ? -1 : i); break; }
        }

        // ---- key-zone strip: the VISIBLE mapping ------------------------------------------------------
        const Rect ks{ wf.x, wf.y + wf.h + 4.f, wf.w, ksH - 4.f };
        draw_key_zones(r, ks, slc, nsl, root, sel_, mx, my, (click && !pop_hit) ? &sel_ : nullptr);

        // ---- hover status -----------------------------------------------------------------------------
        std::string status;
        if (hit(wf, mx, my))      status = nsl > 1 ? "click a slice \xE2\x86\x92 select" : "one region \xC2\xB7 spans the map";
        else if (hit(ks, mx, my)) status = "the key map \xC2\xB7 each zone = a region's note range";
        if (!status.empty()) hover_status(r, body.x + 10.f, body.y + body.h - 22.f, status.c_str(), sty.audio);

        // ---- ⋯ Envelope popover (drawn LAST so it overlays the canvas) --------------------------------
        if (env_open_) draw_env_popover(r, s, track, node_id, pop, mx, my, click);
    }

private:
    int  node_ = -1;
    int  sel_ = -1;          // selected slice (-1 none)
    bool prev_down_ = false; // click-edge latch
    bool env_open_ = false;  // ⋯ Envelope popover open

    // The secondary param popover: a compact vertical stack of the envelope / voice / tune steppers,
    // anchored under the ⋯ button. Kept OFF the main strip (which stays a single high-frequency row).
    struct Env { const char* key; const char* kick; float lo, hi, step; bool secs; };
    static const Env* env_params(int& n) {
        static const Env k[] = {
            { "attack",  "ATTACK",  0.f,   4.f,   0.01f, true }, { "decay",   "DECAY",   0.f, 4.f, 0.01f, true },
            { "sustain", "SUSTAIN", 0.f,   1.f,   0.05f, false }, { "release", "RELEASE", 0.f, 8.f, 0.02f, true },
            { "voices",  "VOICES",  1.f,   32.f,  1.f,   false }, { "tune",    "TUNE",    -100.f, 100.f, 5.f, false },
        };
        n = 6; return k;
    }
    static Rect env_popover_rect(Rect anchor, Rect body) {
        int n = 0; env_params(n);
        const float w = 190.f, top = anchor.y + anchor.h + 4.f;
        // Drop below the inspector row (over the waveform); shrink the rows to stay within the dock so a
        // short dock never clips the last param off the bottom.
        const float avail = (body.y + body.h - 4.f) - top;
        const float rowh = std::clamp(avail / n, 17.f, 25.f);
        const float h = n * rowh + 8.f;
        float x = anchor.x;
        if (x + w > body.x + body.w) x = body.x + body.w - w;   // keep on-screen horizontally
        return { x, top, w, h };
    }
    static void draw_env_popover(Renderer2D& r, vivid::session::Session* s, int t, int nid,
                                 Rect pop, float mx, float my, bool click) {
        const Style& sty = style();
        r.draw_rect(pop.x, pop.y, pop.w, pop.h, sty.region_hd[0], sty.region_hd[1], sty.region_hd[2], 0.99f);
        r.draw_rect_outline(pop.x, pop.y, pop.w, pop.h, 1.f, sty.sel[0], sty.sel[1], sty.sel[2], 1.0f);
        int n = 0; const Env* e = env_params(n);
        const float rowh = (pop.h - 8.f) / n;
        float ry = pop.y + 4.f;
        for (int i = 0; i < n; ++i) {
            const Rect er{ pop.x + 6.f, ry, pop.w - 12.f, rowh - 3.f }; ry += rowh;
            const float cur = pget_f(s, t, nid, e[i].key, e[i].lo);
            char v[16];
            if (e[i].step >= 1.f)   std::snprintf(v, sizeof v, "%.0f", cur);
            else if (e[i].secs)     std::snprintf(v, sizeof v, "%.2fs", cur);
            else                    std::snprintf(v, sizeof v, "%.2f", cur);
            stepper(r, er, e[i].kick, v, stepper_hit(er, mx, my));
            if (click) if (int h = stepper_hit(er, mx, my))
                pset(s, t, nid, e[i].key, std::clamp(cur + e[i].step * h, e[i].lo, e[i].hi));
        }
    }

    // A mini keyboard bar: each region's [lo,hi] note range drawn as an amber zone with its root ticked;
    // the whole 0..127 span mapped across the strip width. This is the headline of ADR-0049 — the mapping
    // you can read at a glance instead of guessing from docs.
    static void draw_key_zones(Renderer2D& r, Rect b, const SamplerSlice* slc, int n,
                               int root, int sel, float mx, float my, int* pick) {
        const Style& sty = style();
        recess(r, b);
        auto xof = [&](int note) { return b.x + (std::clamp(note, 0, 127) / 127.f) * b.w; };
        static const float kAmber[3] = { 0.94f, 0.63f, 0.19f };
        static const float kSel[3]   = { 0.42f, 0.62f, 0.9f };
        for (int i = 0; i < n; ++i) {
            const float xl = xof(slc[i].lo_note), xr = xof(std::min(slc[i].hi_note + 1, 127));
            const bool selc = (i == sel);
            const float* c = selc ? kSel : kAmber;
            r.draw_rect(xl, b.y + 2.f, std::max(2.f, xr - xl), b.h - 4.f, c[0], c[1], c[2], selc ? 0.42f : 0.24f);
            r.draw_rect_outline(xl, b.y + 2.f, std::max(2.f, xr - xl), b.h - 4.f, 1.f, c[0], c[1], c[2], 0.8f);
            const float rx = xof(slc[i].root_note);   // root tick within the zone
            r.draw_rect(rx - 0.5f, b.y + 2.f, 1.f, b.h - 4.f, kAmber[0], kAmber[1], kAmber[2], 1.0f);
            if (pick && hit({ xl, b.y, std::max(2.f, xr - xl), b.h }, mx, my)) *pick = (sel == i ? -1 : i);
        }
        // the sampler-level root marker (base_note) as a taller tick
        const float brx = xof(root);
        r.draw_rect(brx - 1.f, b.y, 2.f, b.h, sty.text[0], sty.text[1], sty.text[2], 0.9f);
    }

    // ---- session param helpers (look up by name; a Sampler always has these) ----
    static int find_param(vivid::session::Session* s, int t, int nid, const char* nm) {
        const int c = vivid::session::session_audio_graph_node_param_count(s, t, nid);
        for (int p = 0; p < c; ++p) {
            const char* pn = vivid::session::session_audio_graph_node_param_name(s, t, nid, p);
            if (pn && std::strcmp(pn, nm) == 0) return p;
        }
        return -1;
    }
    static float pget_f(vivid::session::Session* s, int t, int nid, const char* nm, float dflt) {
        const int p = find_param(s, t, nid, nm);
        return p < 0 ? dflt : vivid::session::session_audio_graph_node_param_get(s, t, nid, p);
    }
    static int pget_i(vivid::session::Session* s, int t, int nid, const char* nm, int dflt) {
        const int p = find_param(s, t, nid, nm);
        return p < 0 ? dflt : (int)std::lround(vivid::session::session_audio_graph_node_param_get(s, t, nid, p));
    }
    static void pset(vivid::session::Session* s, int t, int nid, const char* nm, float v) {
        const int p = find_param(s, t, nid, nm);
        if (p >= 0) vivid::session::session_audio_graph_node_param_set(s, t, nid, p, v);
    }

    static const char* base_name(const char* path) {
        const char* b = path;
        for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
        return b;
    }
    static const char* note_name(int n) {
        static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        static char buf[8];
        n = std::clamp(n, 0, 127);
        std::snprintf(buf, sizeof buf, "%s%d", names[n % 12], n / 12 - 1);
        return buf;
    }
};

}  // namespace vivid::ui
