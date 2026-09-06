#include "cli/control_handlers_internal.h"
#include "cli/control_handlers_audio_domains.h"
#include "audio/vst3_host.h"
#include "midi/midi_clip.h"
#include "midi/note_json.h"
#include "transport.h"                  // Transport (set_bpm / transport handlers)

#include <atomic>
#include <string>
#include <vector>

namespace vivid {

// Audio authoring: transport/BPM, per-track launch/gain/arm/note/record/metronome/params/clips,
// audio-clip warp + shaping, the clip pool, native audio operators (instrument/effects) + the
// per-track audio node graph (add/remove/connect/param), slicing, and discovery.
void register_audio_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
    // ---------------- audio authoring ----------------
    handlers_["set_bpm"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        const double bpm = b.value("bpm", 120.0);
        if (!(bpm > 0.0) || bpm > 1000.0) return err(code::kBadArg, "bpm out of range (0, 1000]");
        c.transport->bpm.store(bpm, std::memory_order_relaxed);
        return ok();
    };
    // Transport play/stop. set_playing{playing} sets it; toggle_play flips; reset_transport
    // returns to the top (bar 1). Pausing freezes the clock so clips stop advancing.
    handlers_["set_playing"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        c.transport->set_playing(b.value("playing", true));
        json r = ok(); r["playing"] = c.transport->is_playing(); return r;
    };
    handlers_["toggle_play"] = [](const ControlCtx& c, const json&) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        json r = ok(); r["playing"] = c.transport->toggle_playing(); return r;
    };
    handlers_["reset_transport"] = [](const ControlCtx& c, const json&) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        c.transport->reset();
        json r = ok(); r["beats"] = 0.0; return r;
    };
    handlers_["launch_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_launch_clip(c.session, track, scene);
        return ok();
    };
    handlers_["launch_scene"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int scene = b.value("scene", 0);
        json e; if (!need_scene(c.session, scene, e)) return e;
        P::session_launch_scene(c.session, scene);
        return ok();
    };
    // Stop a track's playing clip (the counterpart to launch_clip): the clip goes idle at the next
    // launch-quantize bar and stays silent until a clip/scene is launched. Distinct from set_track_mute,
    // which silences the mix while the clip keeps running.
    handlers_["stop_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_stop_track(c.session, track);
        return ok();
    };
    handlers_["stop_all"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        P::session_stop_all(c.session);
        return ok();
    };
    handlers_["set_track_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_gain(c.session, track, b.value("gain", 0.8f));
        return ok();
    };
    // ADR-0022 P1b: the master node's gain (the session's single sink). Default 1.0 (unity).
    handlers_["set_master_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        P::session_set_master_gain(c.session, b.value("gain", 1.0f));
        return ok();
    };
    // Scene-launch quantization in bars: a queued scene switch waits until the next N-bar boundary
    // (1 = next bar; typically 4 = let the current phrase finish). Persisted per project.
    handlers_["set_launch_quantize"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int bars = b.value("bars", 1);
        if (bars < 1) return err(code::kBadArg, "bars must be >= 1");
        P::session_set_launch_quantum_bars(c.session, bars);
        json r = ok(); r["bars"] = P::session_launch_quantum_bars(c.session); return r;
    };
    // ADR-0032 E1 (#4): opt-in playback plugin-delay compensation. {enabled} toggles it; the reply
    // reports the resulting state — L_max applied (samples + ms), how many tracks are exactly compensated
    // vs left live (unknown-latency / live-input / cross-track), and whether any latency was clamped.
    // Persisted per project (persist.cpp). Off by default so a live set stays low-latency.
    handlers_["set_pdc"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        P::session_set_pdc_enabled(c.session, b.value("enabled", false));
        const int sr = P::session_sample_rate(c.session);
        const int applied = P::session_pdc_applied_delay(c.session);
        json r = ok();
        r["enabled"]            = P::session_pdc_enabled(c.session);
        r["applied_delay"]      = applied;
        r["applied_delay_ms"]   = sr > 0 ? applied * 1000.0 / sr : 0.0;
        r["tracks_compensated"] = P::session_pdc_tracks_compensated(c.session);
        r["tracks_live"]        = P::session_pdc_tracks_live(c.session);
        r["clamped"]            = P::session_pdc_clamped(c.session) != 0;
        return r;
    };
    // Session music-theory context: root note + scale NAME (e.g. "C"/"minor"). The theory vocabulary
    // + validation live in the Python bridge (mcp/theory.py, ADR-0046); the core just persists the two
    // strings so the key/scale round-trips with the project. set_music_key is a classified edit
    // (undoable + marks the doc dirty; see edit_methods.cpp); get_music_key reads it back — the bridge
    // reads it to default quantize_to_scale/harmonize/get_scale after a project load or bridge restart.
    handlers_["set_music_key"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string root  = b.value("root",  std::string("C"));
        const std::string scale = b.value("scale", std::string("major"));
        P::session_set_music(c.session, root.c_str(), scale.c_str());
        json r = ok();
        r["root"]  = P::session_music_root(c.session);
        r["scale"] = P::session_music_scale(c.session);
        return r;
    };
    handlers_["get_music_key"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        json r = ok();
        r["root"]  = P::session_music_root(c.session);
        r["scale"] = P::session_music_scale(c.session);
        return r;
    };
    // ADR-0022 P1b.4: solo/mute. Silence a track in the master mix (mute), or hear only the
    // soloed track(s) (solo). The track's own meter stays pre-mute.
    handlers_["set_track_mute"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_mute(c.session, track, b.value("mute", true));
        return ok();
    };
    handlers_["set_track_solo"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_solo(c.session, track, b.value("solo", true));
        return ok();
    };
    handlers_["arm_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1);   // -1 disarms
        if (track >= 0) { json e; if (!need_track(c.session, track, e)) return e; }
        P::session_set_armed_track(c.session, track);
        json r = ok(); r["armed"] = P::session_armed_track(c.session); return r;
    };
    handlers_["note_on"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        if (P::session_armed_track(c.session) < 0) return err(code::kBadArg, "no armed track");
        const int pitch = b.value("pitch", -1);
        if (pitch < 0 || pitch > 127) return err(code::kBadArg, "pitch out of range [0,127]");
        P::session_note_on(c.session, pitch, b.value("vel", 0.8f));
        return ok();
    };
    handlers_["note_off"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int pitch = b.value("pitch", -1);
        if (pitch < 0 || pitch > 127) return err(code::kBadArg, "pitch out of range [0,127]");
        P::session_note_off(c.session, pitch);
        return ok();
    };
    handlers_["record"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const bool on = b.value("on", true);
        if (on && P::session_armed_track(c.session) < 0) return err(code::kBadArg, "no armed track");
        P::session_set_recording(c.session, on, b.value("count_in", 0.0));
        json r = ok(); r["recording"] = P::session_is_recording(c.session); return r;
    };
    handlers_["set_clip_loop"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_clip_loop(c.session, track, scene, b.value("loop_start", 0.0), b.value("loop_end", 0.0));
        return ok();
    };
    handlers_["metronome"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        P::session_set_metronome(c.session, b.value("on", true) ? 1 : 0);
        json r = ok(); r["metronome"] = P::session_get_metronome(c.session); return r;
    };
    handlers_["set_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), device = b.value("device", 0), index = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const int pc = P::session_param_count(c.session, track, device);
        if (pc == 0) return err(code::kOutOfRange, "device " + std::to_string(device) + " has no params (or out of range)");
        if (!in_range(index, pc)) return err(code::kOutOfRange, "param index " + std::to_string(index) + " out of range [0," + std::to_string(pc) + ")");
        P::session_set_param(c.session, track, device, P::session_param_id(c.session, track, device, index), b.value("value", 0.f));
        return ok();
    };
    handlers_["set_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        // Optimistic concurrency: a read-modify-write caller passes the `expected_rev` it read via
        // get_clip. If the clip's revision has advanced since, someone else wrote in between — reject
        // the stale write (conflict) instead of clobbering. Absent => an unconditional write (REPLACE
        // tools, and every pre-existing caller, are unchanged).
        if (b.contains("expected_rev")) {
            const uint64_t have = P::session_clip_rev(c.session, track, scene);
            const uint64_t want = b.value("expected_rev", static_cast<uint64_t>(0));
            if (have != want) {
                json cf = err(code::kConflict, "clip changed since read (expected rev " +
                              std::to_string(want) + ", have " + std::to_string(have) + ")");
                cf["rev"] = have; return cf;   // hand back the current rev so the caller can re-read+retry
            }
        }
        std::vector<P::ClipNote> notes;
        if (b.contains("notes"))
            for (const auto& jn : b["notes"]) {
                P::ClipNote cn{ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f), {} };
                P::expr_from_json(jn, cn);   // optional per-note bend/pressure/timbre curves
                notes.push_back(std::move(cn));
            }
        P::session_set_clip(c.session, track, scene,
                            notes.data(), static_cast<int>(notes.size()), b.value("length", 4.0));
        json r = ok(); r["notes"] = static_cast<int>(notes.size());
        r["rev"] = P::session_clip_rev(c.session, track, scene); return r;
    };
    // Read a MIDI clip back (the read half that read-modify-write authoring tools need).
    handlers_["get_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        // Sized to the clip, not a fixed cap: an imported drum groove or a long recorded take can
        // exceed any constant, and truncating a read silently is how a read-modify-write tool
        // deletes the tail of a clip on its next write.
        const int cap = P::session_clip_note_count(c.session, track, scene);
        std::vector<P::ClipNote> buf(static_cast<size_t>(cap > 0 ? cap : 1));
        const int n = P::session_get_clip(c.session, track, scene, buf.data(), cap);
        json notes = json::array();
        for (int i = 0; i < n; ++i) {
            json jn = { {"p", buf[i].pitch}, {"s", buf[i].start}, {"d", buf[i].dur}, {"v", buf[i].vel} };
            P::expr_to_json(buf[i], jn);
            notes.push_back(jn);
        }
        json r = ok(); r["notes"] = notes; r["length"] = P::session_clip_length(c.session, track, scene);
        r["rev"] = P::session_clip_rev(c.session, track, scene);   // for optimistic-concurrency RMW writes
        return r;
    };
    // ---------------- audio-clip warp / shaping (A2) ----------------
    handlers_["audio_set_warp"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const std::string m = b.value("mode", std::string("complex"));
        const int mode = (m == "beats") ? 1 : (m == "repitch") ? 2 : 0;
        P::session_set_audio_warp(c.session, track, scene, b.value("enabled", true) ? 1 : 0, mode);
        json r = ok(); r["warp"] = P::session_get_audio_warp(c.session, track, scene); return r;
    };
    handlers_["audio_set_pitch"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_pitch(c.session, track, scene, b.value("semitones", 0.0f));
        json r = ok(); r["semitones"] = P::session_get_audio_pitch(c.session, track, scene); return r;
    };
    handlers_["audio_set_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_gain(c.session, track, scene, b.value("gain", 1.0f));
        return ok();
    };
    handlers_["audio_set_reverse"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_reverse(c.session, track, scene, b.value("on", true) ? 1 : 0);
        return ok();
    };
    handlers_["audio_auto_warp"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const int npts = P::session_audio_auto_warp(c.session, track, scene, b.value("sensitivity", 0.5f));
        json r = ok(); r["markers"] = npts; return r;
    };
    register_audio_analysis_handlers(handlers_);
    register_audio_clip_pool_handlers(handlers_);
    register_audio_device_handlers(handlers_);
    register_audio_graph_handlers(handlers_);
    register_sampler_handlers(handlers_);
    register_audio_catalog_handlers(handlers_);
    register_music_eval_handlers(handlers_);

}

}  // namespace vivid
