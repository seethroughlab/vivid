#include "cli/control_handlers_internal.h"
#include "cli/control_json.h"           // operator_to_json (native audio-op discovery)

#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"
#include "midi/midi_clip.h"
#include "midi/note_json.h"
#include "transport.h"                  // Transport (set_bpm / transport handlers)
#include "gpu/visual_graph.h"           // VisualGraph (op registry for audio-op discovery)
#include "ui/node_graph.h"              // NodeGraph (bridge return path)

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
    handlers_["set_track_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_gain(c.session, track, b.value("gain", 0.8f));
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
        std::vector<P::ClipNote> notes;
        if (b.contains("notes"))
            for (const auto& jn : b["notes"]) {
                P::ClipNote cn{ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f), {} };
                P::expr_from_json(jn, cn);   // optional per-note bend/pressure/timbre curves
                notes.push_back(std::move(cn));
            }
        P::session_set_clip(c.session, track, scene,
                            notes.data(), static_cast<int>(notes.size()), b.value("length", 4.0));
        json r = ok(); r["notes"] = static_cast<int>(notes.size()); return r;
    };
    // Read a MIDI clip back (the read half that read-modify-write authoring tools need).
    handlers_["get_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::ClipNote buf[1024];
        const int n = P::session_get_clip(c.session, track, scene, buf, 1024);
        json notes = json::array();
        for (int i = 0; i < n; ++i) {
            json jn = { {"p", buf[i].pitch}, {"s", buf[i].start}, {"d", buf[i].dur}, {"v", buf[i].vel} };
            P::expr_to_json(buf[i], jn);
            notes.push_back(jn);
        }
        json r = ok(); r["notes"] = notes; r["length"] = P::session_clip_length(c.session, track, scene); return r;
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
    // ---------------- clip pool (loose clips that live outside the grid) ----------------
    // The pool is UI-thread-only storage; these handlers run on the UI thread (like all others).
    handlers_["list_pool"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        json arr = json::array();
        for (int i = 0, n = P::session_pool_count(c.session); i < n; ++i)
            arr.push_back({ {"index", i}, {"name", P::session_pool_name(c.session, i)},
                            {"length", P::session_pool_length(c.session, i)},
                            {"kind", P::session_pool_is_audio(c.session, i) ? "audio" : "midi"} });
        json r = ok(); r["pool"] = arr; return r;
    };
    // Move a grid clip into the pool (MIDI or audio): the source cell is cleared (the clip
    // leaves the session). Returns the new pool index.
    handlers_["pool_stash"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        std::string name = b.value("name", std::string());
        if (name.empty()) { char nm[28]; std::snprintf(nm, sizeof nm, "%.12s %c", P::session_track_name(c.session, track), 'A' + scene); name = nm; }
        if (P::session_track_is_audio(c.session, track)) {
            const int idx = P::session_pool_stash_audio(c.session, track, scene, name.c_str());
            if (idx < 0) return err(code::kBadArg, "clip is empty");
            json r = ok(); r["index"] = idx; r["kind"] = "audio"; return r;
        }
        P::ClipNote buf[1024];
        const int n = P::session_get_clip(c.session, track, scene, buf, 1024);
        if (n <= 0) return err(code::kBadArg, "clip is empty");
        const double len = P::session_clip_length(c.session, track, scene);
        const int idx = P::session_pool_add(c.session, buf, n, len, name.c_str());
        P::session_set_clip(c.session, track, scene, nullptr, 0, len);   // take it out of the grid
        json r = ok(); r["index"] = idx; r["kind"] = "midi"; return r;
    };
    // Place a pool clip into a grid cell, overwriting it. Types must match: an audio clip
    // goes on an audio track, a MIDI clip on an instrument track.
    handlers_["pool_place"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int index = b.value("index", -1), track = b.value("track", 0), scene = b.value("scene", 0);
        if (!in_range(index, P::session_pool_count(c.session))) return err(code::kOutOfRange, "pool index " + std::to_string(index) + " out of range");
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const bool poolAudio = P::session_pool_is_audio(c.session, index);
        const bool trackAudio = P::session_track_is_audio(c.session, track);
        if (poolAudio != trackAudio)
            return err(code::kBadArg, poolAudio ? "audio clip needs an audio track" : "MIDI clip needs an instrument track");
        if (poolAudio) {
            if (!P::session_pool_place_audio(c.session, index, track, scene)) return err(code::kInternal, "place failed");
            json r = ok(); r["kind"] = "audio"; return r;
        }
        P::ClipNote buf[1024];
        const int n = P::session_pool_get(c.session, index, buf, 1024);
        P::session_set_clip(c.session, track, scene, buf, n, P::session_pool_length(c.session, index));
        json r = ok(); r["notes"] = n; r["kind"] = "midi"; return r;
    };
    handlers_["pool_remove"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int index = b.value("index", -1);
        if (!in_range(index, P::session_pool_count(c.session))) return err(code::kOutOfRange, "pool index " + std::to_string(index) + " out of range");
        P::session_pool_remove(c.session, index);
        return ok();
    };
    handlers_["add_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = b.value("name", std::string());
        // A ".vst3" path (from list_plugins) loads that bundle directly as an effect.
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".vst3") == 0) {
            const bool okk = P::session_add_effect(c.session, track, name.c_str());
            return okk ? ok() : err(code::kInternal, "add failed (not an effect, or load error)");
        }
        for (int k = 0; k < P::session_available_effect_count(); ++k)
            if (name == P::session_available_effect_name(k)) {
                const bool okk = P::session_add_effect_by_index(c.session, track, k);
                return okk ? ok() : err(code::kInternal, "add failed");
            }
        return err(code::kNotFound, "unknown effect '" + name + "'");
    };
    // Every installed plugin (VST3 today), for the browser. A path from here works as
    // an "instrument" for add_track or a "name" for add_effect. CLAP/AU hosts TBD.
    handlers_["list_plugins"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            arr.push_back({ {"name", p.name}, {"path", p.path}, {"format", "vst3"} });
        }
        json r = ok(); r["plugins"] = arr; return r;
    };
    handlers_["remove_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), effect = b.value("effect", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!in_range(effect, P::session_effect_count(c.session, track)))
            return err(code::kOutOfRange, "effect index " + std::to_string(effect) + " out of range");
        P::session_remove_effect(c.session, track, effect);
        return ok();
    };
    handlers_["list_effects"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int k = 0; k < P::session_available_effect_count(); ++k) arr.push_back(P::session_available_effect_name(k));
        json r = ok(); r["effects"] = arr; return r;
    };
    // --- Native audio operators (AO-1). index -1 = instrument slot, >=0 = effect. ---
    handlers_["add_audio_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int idx = P::session_add_audio_effect(c.session, track, op.c_str());
        if (idx < 0) return err(code::kBadArg, "not a valid audio effect operator: '" + op + "'");
        json r = ok(); r["index"] = idx; return r;
    };
    handlers_["remove_audio_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_remove_audio_effect(c.session, track, index);
        return ok();
    };
    // A bare native-instrument track (no VST3 handle) — the home for a native audio node graph.
    // This is the only programmatic way to create a graph-capable track (add_track's instrument
    // path builds a VST3/plugin track; slice_to_midi needs an audio clip). Optional "instrument"
    // sets the native instrument op in the same call so the track is graph-ok immediately.
    handlers_["add_graph_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string name = b.value("name", std::string());
        const int idx = P::session_add_graph_track(c.session, name.c_str());
        if (idx < 0) return err(code::kInternal, "add_graph_track failed (kMaxTracks reached?)");
        const std::string inst = b.value("instrument", std::string());
        if (!inst.empty() && !P::session_set_track_audio_instrument(c.session, idx, inst.c_str()))
            return err(code::kBadArg, "track created but instrument invalid: '" + inst + "'");
        json r = ok(); r["track"] = idx; return r;
    };
    handlers_["set_track_audio_instrument"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        if (!P::session_set_track_audio_instrument(c.session, track, op.c_str()))
            return err(code::kBadArg, "not a valid audio instrument operator: '" + op + "'");
        return ok();
    };
    handlers_["set_audio_op_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", -1), param = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_op_param_set(c.session, track, index, param, b.value("value", 0.f));
        return ok();
    };
    // AG-1 step 2: authoritative topology edits. The first flips the track's audio graph to the
    // editable source of truth; get_audio_graph reflects the result (nodes/edges/output_id).
    handlers_["audio_graph_add_op"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_op(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add audio effect node: '" + op + "'");
        json r = ok(); r["node"] = nid; return r;
    };
    handlers_["audio_graph_remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!P::session_audio_graph_remove_node(c.session, track, node))
            return err(code::kBadArg, "node not removable (unknown, or an instrument/output)");
        return ok();
    };
    handlers_["audio_graph_connect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!P::session_audio_graph_connect(c.session, track, from, to))
            return err(code::kBadArg, "edge rejected (duplicate, self-loop, unknown node, or would create a cycle)");
        return ok();
    };
    handlers_["audio_graph_disconnect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_disconnect(c.session, track, from, to);
        return ok();
    };
    handlers_["audio_graph_set_node_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1), param = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_node_param_set(c.session, track, node, param, b.value("value", 0.f));
        return ok();
    };
    handlers_["slice_to_midi"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        const int mode = b.value("mode", 1);   // 1=transients, 3=16-grid
        json e; if (!need_track(c.session, track, e)) return e;
        const int nt = P::session_slice_to_midi(c.session, track, scene, mode);
        if (nt < 0) return err(code::kBadArg, "slice-to-MIDI failed (not an audio clip, or no slices)");
        json r = ok(); r["track"] = nt; return r;
    };
    handlers_["list_audio_ops"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        auto op_json = [&](int index) {
            json jo; jo["type"] = P::session_audio_op_type(c.session, track, index);
            json ps = json::array();
            for (int p = 0; p < P::session_audio_op_param_count(c.session, track, index); ++p)
                ps.push_back({ {"name", P::session_audio_op_param_name(c.session, track, index, p)},
                               {"value", P::session_audio_op_param_get(c.session, track, index, p)} });
            jo["params"] = ps; return jo;
        };
        json r = ok();
        if (*P::session_audio_op_type(c.session, track, -1)) r["instrument"] = op_json(-1);
        json fx = json::array();
        for (int i = 0; i < P::session_audio_effect_count(c.session, track); ++i) fx.push_back(op_json(i));
        r["effects"] = fx; return r;
    };

    // AG-1: the track's authoritative audio graph (nodes + edges the RT executor runs). Distinct
    // from list_audio_ops (the linear device view): this reports the persistent topology model —
    // stable node ids, kinds, and (from_id -> to_id) edges — that the audio-graph UI + future
    // rewiring build on. graph_ok is false for VST3 / inline tracks (empty graph).
    handlers_["get_audio_graph"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        static const char* kKind[] = { "instrument", "effect", "output" };
        json r = ok();
        r["graph_ok"]   = P::session_track_audio_graph_ok(c.session, track) != 0;
        r["output_id"]  = P::session_track_audio_graph_output_id(c.session, track);
        json nodes = json::array();
        for (int i = 0; i < P::session_track_audio_graph_node_count(c.session, track); ++i) {
            const int k = P::session_track_audio_graph_node_kind(c.session, track, i);
            nodes.push_back({ {"id",   P::session_track_audio_graph_node_id(c.session, track, i)},
                              {"kind", (k >= 0 && k < 3) ? kKind[k] : "unknown"},
                              {"type", P::session_track_audio_graph_node_type(c.session, track, i)} });
        }
        r["nodes"] = nodes;
        json edges = json::array();
        for (int i = 0; i < P::session_track_audio_graph_edge_count(c.session, track); ++i)
            edges.push_back({ {"from", P::session_track_audio_graph_edge_from(c.session, track, i)},
                              {"to",   P::session_track_audio_graph_edge_to(c.session, track, i)} });
        r["edges"] = edges;
        return r;
    };

    // The catalog of NATIVE audio operators (the audio peer of list_operators, which is
    // visual): instruments (sources, no audio input) + effects (audio in->out). Names are
    // stable registry keys for set_track_audio_instrument / add_audio_effect.
    handlers_["list_audio_operators"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        auto* reg = (c.vgraph && c.vgraph->registry()) ? c.vgraph->registry() : nullptr;
        // Each entry carries the full schema (params + semantic metadata), like list_operators —
        // so an agent can pick an op AND know its params without a second call.
        auto emit = [&](int want_source, const char* kind) {
            json arr = json::array();
            for (int i = 0; i < P::session_available_audio_op_count(c.session, want_source); ++i) {
                const char* nm = P::session_available_audio_op_name(c.session, want_source, i);
                const VividOperatorDescriptor* d = reg ? reg->descriptor_for(nm ? nm : "") : nullptr;
                if (d) arr.push_back(control_json::operator_to_json(*d, kind));
                else   arr.push_back({ {"name", nm ? nm : ""}, {"kind", kind} });
            }
            return arr;
        };
        json r = ok();
        r["instruments"] = emit(1, "instrument");
        r["effects"]     = emit(0, "audio_effect");
        return r;
    };

    // The instrument catalog offered when creating a track (a label or a .vst3 path on add).
    handlers_["list_instruments"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int k = 0; k < P::session_available_instrument_count(); ++k) arr.push_back(P::session_available_instrument_name(k));
        json r = ok(); r["instruments"] = arr; return r;
    };
    // Create a track. kind "instrument" (default) needs an "instrument" (catalog label or a
    // .vst3 path); kind "audio" makes a sampler track. Returns the new track index.
    handlers_["add_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string kind = b.value("kind", std::string("instrument"));
        int idx = -1;
        if (kind == "audio") {
            idx = P::session_add_audio_track(c.session);
        } else {
            const std::string inst = b.value("instrument", std::string());
            if (inst.empty()) return err(code::kBadArg, "instrument track needs \"instrument\" (a catalog label or .vst3 path)");
            idx = P::session_add_instrument_track(c.session, inst.c_str());
            if (idx < 0) return err(code::kNotFound, "no instrument matched '" + inst + "' (or kMaxTracks reached)");
        }
        if (idx < 0) return err(code::kInternal, "add_track failed (kMaxTracks reached?)");
        json r = ok(); r["track"] = idx; return r;
    };
    // Delete a track. Also drops audio->visual mappings whose source encodes the removed
    // track's stable id; surviving mappings do not need index renumbering.
    handlers_["remove_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const int rid = P::session_track_id(c.session, track);   // capture the stable id before removal
        if (!P::session_remove_track(c.session, track)) return err(code::kInternal, "remove_track failed");
        int dropped = 0;
        if (c.graph) dropped = c.graph->drop_track_sources(rid);   // drop this track's mappings (id-based; survivors untouched)
        json r = ok(); r["removed"] = track; r["mappings_dropped"] = dropped; return r;
    };

}

}  // namespace vivid
