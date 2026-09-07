#pragma once
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"          // Style, Rect, hit(), section_header, draw_text_r, fit_text
#include "ui/editor_controls.h"   // icon_button, segmented, stepper, menu_button, hover_status
#include "ui/editor_shell.h"      // the shared detail-view shell: zones, title strip, strip packer
#include "ui/waveform_view.h"     // the shared waveform language (ADR-0048 slice 3b)
#include "ui/layout.h"            // segmented_hit / stepper_hit (pure geometry)
#include "audio/vst3_host.h"      // session_sampler_* + node param accessors
#include "audio/sampler_op.h"     // SamplerInfo / SamplerSlice
#include "platform/file_dialog.h" // open_file_dialog (Load / Replace)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    // ADR-0017: every edit this editor makes must reach the undo gateway. The gateway lives in `app/`,
    // which `ui/` may not include (ADR-0043 layering), so the host passes a plain function sink —
    // the same shape session_view.cpp already uses for the operator-editor param sink.
    struct EditSink {
        void (*fn)(void* opaque, const char* label, const char* coalesce_key);
        void* opaque;
        void operator()(const char* label, const char* key = "") const { if (fn) fn(opaque, label, key); }
    };

    // Draw + interact for the selected Sampler node, filling `body` (the dock area below the header).
    // Immediate-mode: reads the latest cursor + button; `click` is derived from the caller's edge latch.
    void draw(Renderer2D& r, vivid::session::Session* s, Rect body,
              int track, int node_id, float mx, float my, bool mouse_down, EditSink edited) {
        namespace S = vivid::session;
        const Style& sty = style();
        if (node_id != node_) { node_ = node_id; sel_ = -1; drag_ = -1; }  // reset per-node UI state
        const bool down_edge = mouse_down && !prev_down_;                 // grab / click
        const bool up_edge   = !mouse_down && prev_down_;                 // release (commit a drag)
        prev_down_ = mouse_down;
        const bool click = down_edge;
        auto hov = [&](Rect rr) { return hit(rr, mx, my); };

        // ---- read the sample + its edit state ---------------------------------------------------------
        SamplerInfo info{};
        const bool loaded = S::session_sampler_info(s, track, node_id, &info) != 0;
        const char* src = S::session_sampler_source(s, track, node_id);
        SamplerSlice slc[128]; const int nsl = loaded ? S::session_sampler_slices(s, track, node_id, slc, 128) : 0;
        const unsigned long long sframes = loaded ? S::session_sampler_source_frames(s, track, node_id) : 0;
        uint32_t bs[64], be[64];
        const int nb = sframes ? S::session_sampler_edit_boundaries(s, track, node_id, bs, be, 64) : 0;

        // ---- ZONE 1: the SHARED title strip (editor_shell.h) -------------------------------------------
        // The same component the clip editor paints: name · ident · MODE CHIP · readout. A docked
        // Sampler is a track's HOME detail view, not a drill-in, so its TitleSpec asks for no
        // close/float/follow/fit — the affordances differ, the implementation does not.
        const char* name = (src && *src) ? base_name(src) : (loaded ? "(sample)" : nullptr);
        char ident[64], readout[96];
        std::snprintf(ident, sizeof ident, "Sampler \xC2\xB7 %s", S::session_track_name(s, track));
        if (loaded && name) {
            const double secs = info.sample_rate ? (double)info.frames / info.sample_rate : 0.0;
            std::snprintf(readout, sizeof readout, "%.2fs \xC2\xB7 %d Hz \xC2\xB7 %dch \xC2\xB7 %d slice%s \xC2\xB7 %s",
                          secs, info.sample_rate, info.channels,
                          nsl, nsl == 1 ? "" : "s", info.gate ? "gated" : "one-shot");
        } else {
            std::snprintf(readout, sizeof readout, "no sample loaded");
        }
        TitleSpec tspec;
        tspec.name = name ? name : "(no sample)";
        tspec.ident = ident;
        tspec.readout = readout;
        tspec.mode = EditorMode::Sampler;
        shell_chrome(r, body, tspec, mx, my);

        if (!loaded || !name) {
            // Nothing loaded: an ACTION, not just a placeholder. ADR-0049 asks for load/replace as a
            // clear action; telling the user to go do it somewhere else was the gap.
            const Rect cv = shell_canvas_rect(body);
            const float cy = cv.y + cv.h * 0.5f;
            const Rect rLoad{ cv.x + 4.f, cy - 12.f, 132.f, 24.f };
            icon_button(r, rLoad, "\xE2\x97\x89 Load sample", hov(rLoad));
            if (click && hit(rLoad, mx, my)) load_replace(s, track, node_id, 60, edited);
            r.draw_text(rLoad.x + rLoad.w + 12.f, cy - 6.f,
                        "or drop an audio file on this node", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_value);
            return;
        }

        // ---- ZONE 2: the inspector strip, packed by the SHARED packer ---------------------------------
        InspectorStrip strip = InspectorStrip::begin(body);
        auto place = [&](float w) { return strip.take(w); };

        const int   root = pget_i(s, track, node_id, "base_note", info.base_note);
        const int   gate = pget_i(s, track, node_id, "gate", info.gate);
        const int   tr   = pget_i(s, track, node_id, "transpose", 0);
        const float gn   = pget_f(s, track, node_id, "gain", 1.f);

        const Rect rRoot = place(112.f);
        stepper(r, rRoot, "ROOT", note_name(root).s, stepper_hit(rRoot, mx, my));
        const Rect rGate = place(150.f);
        segmented(r, rGate, { "One-shot", "Gated" }, gate ? 1 : 0, segmented_hit(rGate, 2, mx, my), sty.audio);
        const Rect rTr = place(132.f);
        { char v[16]; std::snprintf(v, sizeof v, "%+d st", tr); stepper(r, rTr, "TRANS", v, stepper_hit(rTr, mx, my)); }
        const Rect rGn = place(112.f);
        { char v[16]; std::snprintf(v, sizeof v, "%.2f", gn); stepper(r, rGn, "GAIN", v, stepper_hit(rGn, mx, my)); }
        const Rect rSlc = place(112.f);   // number of trigger slices: 1 = whole/trimmed; N = drum-rack
        { char v[16]; std::snprintf(v, sizeof v, "%d", nb); stepper(r, rSlc, "SLICES", v, stepper_hit(rSlc, mx, my)); }
        const Rect rDet = place(84.f);    // auto-slice at detected onsets
        icon_button(r, rDet, "Detect", hov(rDet));
        const Rect rClr = place(74.f);    // collapse back to ONE region spanning [in,out] (melodic trim)
        icon_button(r, rClr, "Clear", hov(rClr), false, nb > 1 ? nullptr : sty.dim);
        // Per-slice TUNE appears only when a drum-rack slice is selected (tune == trigger note - root note).
        const bool slice_sel = (sel_ >= 0 && sel_ < nsl && nb > 1);
        const int  stune = slice_sel ? (slc[sel_].lo_note - slc[sel_].root_note) : 0;
        Rect rTune{};
        if (slice_sel) {
            rTune = place(120.f);
            char v[16]; std::snprintf(v, sizeof v, "%+d st", stune);
            stepper(r, rTune, "TUNE", v, stepper_hit(rTune, mx, my));
        }
        const Rect rMidi = place(120.f);  // write a MIDI clip that triggers these slices (drum rack only)
        icon_button(r, rMidi, "Slice \xE2\x86\x92 MIDI", hov(rMidi), false, nb > 1 ? nullptr : sty.dim);
        const Rect rLoad = place(104.f);  // replace the loaded sample in place (keeps the node + wiring)
        icon_button(r, rLoad, "\xE2\x97\x89 Replace", hov(rLoad));
        const Rect rEnv = place(120.f);   // the disclosure for the set-and-forget envelope/voice params
        menu_button(r, rEnv, "\xE2\x8B\xAF Envelope", hov(rEnv), env_open_);

        if (click) {
            // Every mutation below notes an edit (ADR-0017). Param steppers coalesce per param so a run
            // of clicks is one undo entry; the structural actions (reslice / detect / clear / load) do not.
            char ck[64];
            auto param_edit = [&](const char* nm) {
                std::snprintf(ck, sizeof ck, "sampler:%d/%s", node_id, nm);
                edited("Sampler Param", ck);
            };
            if (int h = stepper_hit(rRoot, mx, my)) {
                pset(s, track, node_id, "base_note", std::clamp(root + h, 0, 127)); param_edit("base_note"); }
            if (int c = segmented_hit(rGate, 2, mx, my); c >= 0) {
                pset(s, track, node_id, "gate", (float)c); param_edit("gate"); }
            if (int h = stepper_hit(rTr, mx, my)) {
                pset(s, track, node_id, "transpose", std::clamp(tr + h, -48, 48)); param_edit("transpose"); }
            if (int h = stepper_hit(rGn, mx, my)) {
                pset(s, track, node_id, "gain", std::clamp(gn + 0.05f * h, 0.f, 2.f)); param_edit("gain"); }
            if (int h = stepper_hit(rSlc, mx, my); h && nb > 0) {  // re-cut into N EQUAL slices over [in,out]
                reslice_equal(s, track, node_id, bs[0], be[nb - 1], std::clamp(nb + h, 1, 32), root);
                sel_ = -1; edited("Sampler Slices"); }
            if (hit(rDet, mx, my)) {
                S::session_sampler_detect_slices(s, track, node_id, 0.5f); sel_ = -1; edited("Detect Slices"); }
            if (hit(rClr, mx, my) && nb > 1) {                    // back to one region: the melodic trim
                reslice_equal(s, track, node_id, bs[0], be[nb - 1], 1, root);
                sel_ = -1; edited("Clear Slices"); }
            if (hit(rMidi, mx, my) && nb > 1) slices_to_midi(s, track, slc, nsl, edited);
            if (hit(rLoad, mx, my)) load_replace(s, track, node_id, root, edited);
            if (slice_sel) if (int h = stepper_hit(rTune, mx, my)) {
                S::session_sampler_set_slice_tune(s, track, node_id, sel_, std::clamp(stune + h, -48, 48));
                std::snprintf(ck, sizeof ck, "sampler:%d/tune%d", node_id, sel_);
                edited("Slice Tune", ck); }
            if (hit(rEnv, mx, my)) env_open_ = !env_open_;
        }

        // The ⋯ Envelope popover holds the secondary params (ADSR / voices / tune) — disclosed on demand,
        // NOT a permanent second row. Its bounds are known now so the waveform below can ignore clicks it
        // owns; a click anywhere else (not the button, not the popover) dismisses it.
        const Rect pop = env_popover_rect(rEnv, body);
        const bool pop_hit = env_open_ && hit(pop, mx, my);
        if (env_open_ && click && !pop_hit && !hit(rEnv, mx, my)) env_open_ = false;

        // ---- ZONE 3: the canvas — the WHOLE source with draggable in/out + slice edges (SOURCE space) --
        const Rect canvas = shell_canvas_rect(body);               // the shell's zone-3 rect
        const float ksH = 26.f;                                    // key-zone strip height (below the waveform)
        const Rect wf{ canvas.x, canvas.y, canvas.w, std::max(30.f, canvas.h - ksH - 4.f) };
        recess(r, wf);
        r.push_clip_rect(wf.x, wf.y, wf.w, wf.h);
        static float speaks[512];
        const int np = sframes ? S::session_sampler_source_peaks(s, track, node_id, speaks, 512) : 0;
        // The marker colors come from the shared language (waveform_view.h) — the clip editor's
        // waveform draws the same trim/divider/region/playhead colors from the same constants.
        const WaveformView wv{ { wf.x, wf.y, wf.w, wf.h }, 0.0, wf.w, 1.f };   // whole SOURCE, fit-to-width
        // A drag in progress paints from the live copy; otherwise from the committed boundaries.
        const bool dragging = (drag_ >= 0) && !ds_.empty();
        const uint32_t* S0 = dragging ? ds_.data() : bs;
        const uint32_t* E0 = dragging ? de_.data() : be;
        const int NB = dragging ? static_cast<int>(ds_.size()) : nb;
        auto n_of = [&](uint32_t f) { return sframes ? static_cast<double>(f) / sframes : 0.0; };
        const double inN = NB ? n_of(S0[0]) : 0.0, outN = NB ? n_of(E0[NB - 1]) : 1.0;
        if (np > 0) wv.bins(r, speaks, np);
        wv.center_line(r);
        wv.dim_outside(r, inN, outN);                              // darken the un-played head/tail
        if (sel_ >= 0 && sel_ < NB) wv.region(r, n_of(S0[sel_]), n_of(E0[sel_]));
        float divs[64]; int nd = 0;                                // internal slice edges (shared boundaries)
        for (int i = 0; i + 1 < NB; ++i) divs[nd++] = static_cast<float>(n_of(E0[i]));
        wv.dividers(r, divs, nd, kWaveDivider, 0.85f, /*grab_tab*/true);
        wv.handle(r, inN); wv.handle(r, outN);
        // The playhead is drawn for a DRUM RACK too — the old NB==1 gate hid it in exactly the sliced
        // case this editor exists for. The concat->source conversion is pure geometry, so it lives in
        // layout.h and is unit-tested (test_editor_controls).
        if (const float ph = S::session_audio_graph_node_sampler_playhead(s, track, node_id); ph >= 0.f) {
            const double pn = sampler_playhead_norm(S0, E0, NB, ph, sframes);
            if (pn >= 0.0) wv.playhead(r, pn);
        }
        r.pop_clip_rect();

        // ---- drag: grab an in/out handle or a divider, or select a slice; commit on release -----------
        const float grab = 7.f;
        auto at_x  = [&](double n) { return std::abs((wf.x + static_cast<float>(n) * wf.w) - mx) < grab; };
        if (down_edge && !pop_hit && hit(wf, mx, my) && nb > 0) {
            ds_.assign(bs, bs + nb); de_.assign(be, be + nb);      // snapshot for the live drag
            if      (at_x(n_of(bs[0])))        drag_ = 0;          // in handle
            else if (at_x(n_of(be[nb - 1])))   drag_ = 1;          // out handle
            else {
                drag_ = -1;
                for (int i = 0; i + 1 < nb; ++i) if (at_x(n_of(be[i]))) { drag_ = 100 + i; break; }
                if (drag_ < 0) {                                   // empty space → select + audition the slice
                    const double n = static_cast<double>(mx - wf.x) / wf.w;
                    sel_ = -1;
                    for (int i = 0; i < nb; ++i) if (n >= n_of(bs[i]) && n < n_of(be[i])) { sel_ = i; break; }
                    if (sel_ >= 0) audition(s, track, (sel_ < nsl ? slc[sel_].root_note : root));
                }
            }
        }
        if (drag_ >= 0 && mouse_down && !ds_.empty()) {            // live-update the grabbed edge
            const double n = std::clamp(static_cast<double>(mx - wf.x) / wf.w, 0.0, 1.0);
            const long f = std::llround(n * static_cast<double>(sframes));
            const long pad = 16;
            if (drag_ == 0)      ds_[0] = static_cast<uint32_t>(std::clamp<long>(f, 0, static_cast<long>(de_[0]) - pad));
            else if (drag_ == 1) de_.back() = static_cast<uint32_t>(std::clamp<long>(f, static_cast<long>(ds_.back()) + pad, static_cast<long>(sframes)));
            else { const int i = drag_ - 100;
                   const long lo = static_cast<long>(ds_[i]) + pad;
                   const long hi = (i + 1 < static_cast<int>(de_.size()) ? static_cast<long>(de_[i + 1]) : static_cast<long>(sframes)) - pad;
                   const uint32_t cf = static_cast<uint32_t>(std::clamp<long>(f, lo, std::max(lo, hi)));
                   de_[i] = cf; if (i + 1 < static_cast<int>(ds_.size())) ds_[i + 1] = cf; }
        }
        if (up_edge && drag_ >= 0) {                               // commit the edit
            if (!ds_.empty()) {
                if (ds_.size() == 1) S::session_sampler_set_trim(s, track, node_id, ds_[0], de_[0]);
                else S::session_sampler_reslice(s, track, node_id, ds_.data(), de_.data(), static_cast<int>(ds_.size()), root);
                edited(drag_ >= 100 ? "Move Slice Point" : "Trim Sample");   // one entry per completed drag
            }
            drag_ = -1; ds_.clear(); de_.clear();
        }

        // ---- key-zone strip: the VISIBLE note mapping (click a zone to audition it) --------------------
        const Rect ks{ wf.x, wf.y + wf.h + 4.f, wf.w, ksH - 4.f };
        const int prev_kz = sel_;
        draw_key_zones(r, ks, slc, nsl, root, sel_, mx, my, (click && !pop_hit) ? &sel_ : nullptr);
        if (sel_ != prev_kz && sel_ >= 0 && sel_ < nsl && hit(ks, mx, my))
            audition(s, track, slc[sel_].root_note);

        // ---- hover status: name the thing under the cursor, not a generic instruction ------------------
        char sbuf[128];
        std::string status;
        if (drag_ == 0)            status = "drag \xE2\x86\x92 sample start (in)";
        else if (drag_ == 1)       status = "drag \xE2\x86\x92 sample end (out)";
        else if (drag_ >= 100)     status = "drag \xE2\x86\x92 slice point";
        else if (hit(wf, mx, my)) {
            // Which slice is the cursor over, and what would a click do to it? (Clicking DOES audition —
            // the old generic string never said so.) Near an edge, the drag verb wins.
            const double hn = static_cast<double>(mx - wf.x) / wf.w;
            int hovi = -1;
            for (int i = 0; i < nb; ++i) if (hn >= n_of(bs[i]) && hn < n_of(be[i])) { hovi = i; break; }
            const bool near_edge = at_x(n_of(bs[0])) || at_x(n_of(be[nb ? nb - 1 : 0]));
            if (near_edge)
                status = "drag \xE2\x86\x92 trim in/out";
            else if (nb > 1 && hovi >= 0) {
                std::snprintf(sbuf, sizeof sbuf, "click \xE2\x86\x92 audition slice %d / %d  \xC2\xB7  drag an edge \xE2\x86\x92 move the slice point",
                              hovi + 1, nb);
                status = sbuf;
            } else {
                status = nb > 1 ? "drag an edge \xE2\x86\x92 move the slice point  \xC2\xB7  SLICES to add/remove"
                                : "click \xE2\x86\x92 audition  \xC2\xB7  drag the amber handles \xE2\x86\x92 trim in/out";
            }
        }
        else if (hit(ks, mx, my)) {
            // Over a zone, name the slice + the notes that trigger it; otherwise explain the strip.
            int hz = -1;
            for (int i = 0; i < nsl; ++i) {
                const float zl = ks.x + (std::clamp(slc[i].lo_note, 0, 127) / 127.f) * ks.w;
                const float zr = ks.x + (std::clamp(std::min(slc[i].hi_note + 1, 127), 0, 127) / 127.f) * ks.w;
                if (mx >= zl && mx < std::max(zl + 2.f, zr)) { hz = i; break; }
            }
            if (hz >= 0) {
                std::snprintf(sbuf, sizeof sbuf, "click \xE2\x86\x92 audition slice %d / %d  \xC2\xB7  keys %s\xE2\x80\x93%s",
                              hz + 1, nsl, note_name(slc[hz].lo_note).s, note_name(slc[hz].hi_note).s);
                status = sbuf;
            } else status = "the key map \xC2\xB7 each zone = a slice's note range";
        }
        if (!status.empty()) hover_status(r, body.x + 10.f, body.y + body.h - 22.f, status.c_str(), sty.audio);

        // ---- ⋯ Envelope popover (drawn LAST so it overlays the canvas) --------------------------------
        if (env_open_) draw_env_popover(r, s, track, node_id, pop, mx, my, click, edited);
    }

private:
    int  node_ = -1;
    int  sel_ = -1;          // selected slice (-1 none)
    bool prev_down_ = false; // click-edge latch
    bool env_open_ = false;  // ⋯ Envelope popover open
    int  drag_ = -1;         // -1 none · 0 in-handle · 1 out-handle · 100+i divider between slice i / i+1
    std::vector<uint32_t> ds_, de_;   // live boundaries (SOURCE frames) during a drag

    // Play a slice's mapped note through the track's instrument so you can HEAR a slice as you cut it.
    static void audition(vivid::session::Session* s, int t, int pitch) {
        vivid::session::session_preview_note(s, t, std::clamp(pitch, 0, 127), 0.85f);
    }

    // ADR-0049 "clear actions": replace the loaded sample in place — the node, its wiring, and its
    // envelope/voice params all survive; only the PCM changes. Cancelling the dialog is a no-op.
    static void load_replace(vivid::session::Session* s, int t, int nid, int base, const EditSink& edited) {
        const std::string p = vivid::platform::open_file_dialog(
            "Load a sample", { "wav", "aif", "aiff", "flac", "mp3", "ogg" });
        if (p.empty()) return;
        if (vivid::session::session_audio_graph_load_sampler(s, t, nid, p.c_str(), base) > 0)
            edited("Load Sample");
    }

    // ADR-0049 "no magic conversion": write a MIDI clip whose notes trigger these slices in order, so
    // the mapping shown in the key strip becomes something you can hear and edit. Writes to the first
    // EMPTY scene on this track — never overwrites an existing clip.
    static void slices_to_midi(vivid::session::Session* s, int t, const SamplerSlice* slc, int n,
                               const EditSink& edited) {
        namespace S = vivid::session;
        if (n <= 1) return;
        int scene = -1;
        for (int i = 0, sc = S::session_scene_count(s); i < sc; ++i)
            if (S::session_clip_note_count(s, t, i) == 0) { scene = i; break; }
        if (scene < 0) return;                       // every scene is taken: refuse rather than clobber
        // The note layout is the SHARED one (sampler_op.h) — the sampler_slices_to_midi control method
        // calls the same helper, so the button and an agent produce an identical clip.
        std::vector<S::ClipNote> notes(n);
        const int wrote = sampler_slices_to_notes(slc, n, notes.data(), n);
        notes.resize(wrote > 0 ? wrote : 0);
        if (notes.empty()) return;
        const double len = std::max(4.0, std::ceil(n * 0.25));
        S::session_set_clip(s, t, scene, notes.data(), static_cast<int>(notes.size()), len);
        edited("Slices to MIDI");
    }

    // Re-cut into N equal slices spanning [in,out) of the source (N==1 => a plain melodic trim).
    static void reslice_equal(vivid::session::Session* s, int t, int nid, uint32_t in, uint32_t out, int n, int base) {
        if (out <= in) return;
        if (n <= 1) { vivid::session::session_sampler_set_trim(s, t, nid, in, out); return; }
        std::vector<uint32_t> starts(n), ends(n);
        const double span = static_cast<double>(out - in);
        for (int i = 0; i < n; ++i) {
            starts[i] = in + static_cast<uint32_t>(span * i / n);
            ends[i]   = in + static_cast<uint32_t>(span * (i + 1) / n);
        }
        ends[n - 1] = out;
        vivid::session::session_sampler_reslice(s, t, nid, starts.data(), ends.data(), n, base);
    }

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
                                 Rect pop, float mx, float my, bool click, const EditSink& edited) {
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
            if (click) if (int h = stepper_hit(er, mx, my)) {
                pset(s, t, nid, e[i].key, std::clamp(cur + e[i].step * h, e[i].lo, e[i].hi));
                char ck[64]; std::snprintf(ck, sizeof ck, "sampler:%d/%s", nid, e[i].key);
                edited("Sampler Param", ck);   // ADR-0017: the popover's steppers are edits too
            }
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
        for (int i = 0; i < n; ++i) {
            const float xl = xof(slc[i].lo_note), xr = xof(std::min(slc[i].hi_note + 1, 127));
            const bool selc = (i == sel);
            // Same amber/selected-blue the waveform above uses, so a zone and its slice read as one thing.
            const float* c = selc ? kWaveRegion : kWaveTrim;
            r.draw_rect(xl, b.y + 2.f, std::max(2.f, xr - xl), b.h - 4.f, c[0], c[1], c[2], selc ? 0.42f : 0.24f);
            r.draw_rect_outline(xl, b.y + 2.f, std::max(2.f, xr - xl), b.h - 4.f, 1.f, c[0], c[1], c[2], 0.8f);
            const float rx = xof(slc[i].root_note);   // root tick within the zone
            r.draw_rect(rx - 0.5f, b.y + 2.f, 1.f, b.h - 4.f, kWaveTrim[0], kWaveTrim[1], kWaveTrim[2], 1.0f);
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
    // Returns the name BY VALUE (not a shared static buffer) so two notes can be formatted in one
    // expression — e.g. a zone's "keys C1–D#1" — without the second call clobbering the first.
    struct NoteName { char s[8]; };
    static NoteName note_name(int n) {
        static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        NoteName o{};
        n = std::clamp(n, 0, 127);
        std::snprintf(o.s, sizeof o.s, "%s%d", names[n % 12], n / 12 - 1);
        return o;
    }
};

}  // namespace vivid::ui
