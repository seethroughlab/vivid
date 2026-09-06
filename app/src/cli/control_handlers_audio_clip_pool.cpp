#include "cli/control_handlers_audio_domains.h"
#include "cli/control_handlers_internal.h"

#include "audio/vst3_host.h"
#include "midi/midi_file.h"    // SMF import/export (import_midi / export_midi)
#include "transport.h"           // export_midi writes the session tempo into the file

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace vivid {

void register_audio_clip_pool_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
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
        const int cap = P::session_clip_note_count(c.session, track, scene);   // sized to the clip, never truncated
        std::vector<P::ClipNote> buf(static_cast<size_t>(cap > 0 ? cap : 1));
        const int n = P::session_get_clip(c.session, track, scene, buf.data(), cap);
        if (n <= 0) return err(code::kBadArg, "clip is empty");
        const double len = P::session_clip_length(c.session, track, scene);
        const int idx = P::session_pool_add(c.session, buf.data(), n, len, name.c_str());
        {   // carry the clip's controller automation with it (P4)
            const int nc = P::session_clip_cc_count(c.session, track, scene);
            if (nc > 0 && idx >= 0) {
                std::vector<P::CcLane> lanes(static_cast<size_t>(nc));
                const int got = P::session_get_clip_cc(c.session, track, scene, lanes.data(), nc);
                P::session_pool_set_cc(c.session, idx, lanes.data(), got);
            }
        }
        P::session_set_clip(c.session, track, scene, nullptr, 0, len);   // take it out of the grid
        P::session_set_clip_cc(c.session, track, scene, nullptr, 0);
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
        const int cap = P::session_pool_note_count(c.session, index);
        std::vector<P::ClipNote> buf(static_cast<size_t>(cap > 0 ? cap : 1));
        const int n = P::session_pool_get(c.session, index, buf.data(), cap);
        P::session_set_clip(c.session, track, scene, buf.data(), n, P::session_pool_length(c.session, index));
        {   // and back again
            const int nc = P::session_pool_cc_count(c.session, index);
            std::vector<P::CcLane> lanes(static_cast<size_t>(nc > 0 ? nc : 1));
            const int got = nc > 0 ? P::session_pool_get_cc(c.session, index, lanes.data(), nc) : 0;
            P::session_set_clip_cc(c.session, track, scene, lanes.data(), got);
        }
        json r = ok(); r["notes"] = n; r["kind"] = "midi"; return r;
    };
    handlers_["pool_remove"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int index = b.value("index", -1);
        if (!in_range(index, P::session_pool_count(c.session))) return err(code::kOutOfRange, "pool index " + std::to_string(index) + " out of range");
        P::session_pool_remove(c.session, index);
        return ok();
    };

    // Import an audio file (.wav/.aif/.flac/.mp3) straight into a sampler track's scene clip,
    // decoding + resampling to the device rate and swapping it in under the audio lock. This is
    // the MCP-native path to get REAL recorded audio into the grid (previously only session-load
    // called the underlying engine op); it's what makes a warp/glitch example possible from a
    // script. `src_bpm` (0 = unknown) seeds warp/BPM estimation for the imported loop.
    handlers_["import_audio_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        if (!P::session_track_is_audio(c.session, track)) return err(code::kBadArg, "import needs an audio (sampler) track");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");
        const double src_bpm = b.value("src_bpm", 0.0);
        if (!P::session_load_audio_clip(c.session, track, scene, path.c_str(), src_bpm))
            return err(code::kIoError, "could not decode audio file: " + path);
        json r = ok(); r["track"] = track; r["scene"] = scene;
        r["length"] = P::session_audio_loop_beats(c.session, track, scene); return r;
    };

    // ---------------- MIDI file import / export ----------------
    // The way third-party MIDI gets in. Drum plugins (EZdrummer, Superior Drummer, Addictive Drums)
    // are built around dragging a groove out of the plugin's own browser, and their VST3 param
    // surface exposes nothing but a few macros — so without this the entire groove library those
    // instruments exist for is unreachable from Vivid. Also the path for any .mid on disk.
    handlers_["import_midi"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        if (P::session_track_is_audio(c.session, track))
            return err(code::kBadArg, "import_midi needs an instrument track (this is an audio/sampler track)");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");

        P::MidiFileData mf; std::string perr;
        if (!P::read_midi_file(path, mf, &perr)) return err(code::kIoError, perr);

        // Optional filters — a format-1 file keeps parts on separate tracks, and a drum groove is
        // usually on channel 9 (GM channel 10) alongside other parts.
        const int  want_track   = b.value("file_track", -1);       // -1 = every track
        const int  want_channel = b.value("channel", -1);          // -1 = every channel
        const int  transpose    = b.value("transpose", 0);
        const bool append       = b.value("append", false);

        std::vector<P::ClipNote> notes;
        double src_end = 0.0;
        if (append) {   // read the existing clip first (import overdubs onto it)
            const int cap = P::session_clip_note_count(c.session, track, scene);
            notes.resize(static_cast<size_t>(cap > 0 ? cap : 1));
            const int have = P::session_get_clip(c.session, track, scene, notes.data(), cap);
            notes.resize(static_cast<size_t>(have > 0 ? have : 0));
        }
        int skipped = 0;
        for (const P::MidiFileNote& n : mf.notes) {
            if (want_track >= 0 && n.track != want_track) { ++skipped; continue; }
            if (want_channel >= 0 && n.channel != want_channel) { ++skipped; continue; }
            const int p = n.pitch + transpose;
            if (p < 0 || p > 127) { ++skipped; continue; }   // transposed out of range: drop, and say so
            P::ClipNote cn{};
            cn.pitch = p; cn.start = n.start; cn.dur = n.dur; cn.vel = n.vel;
            notes.push_back(cn);
            if (n.start + n.dur > src_end) src_end = n.start + n.dur;
        }
        if (notes.empty())
            return err(code::kBadArg, "no notes matched (file has " + std::to_string(mf.notes.size()) +
                                      " note(s); check file_track/channel)");

        // Clip length: an explicit `length`, else round the content up to a whole bar so an
        // imported groove loops musically instead of at its last note-off.
        double length = b.value("length", 0.0);
        if (length <= 0.0) {
            const double bar = 4.0;   // beats_per_bar is fixed at 4 across the engine today
            length = std::max(bar, std::ceil((src_end - 1e-9) / bar) * bar);
            if (append) length = std::max(length, P::session_clip_length(c.session, track, scene));
        }
        P::session_set_clip(c.session, track, scene, notes.data(), static_cast<int>(notes.size()), length);

        json r = ok();
        r["track"] = track; r["scene"] = scene;
        r["notes"] = static_cast<int>(notes.size());
        r["skipped"] = skipped;
        r["length"] = length;
        r["file_tracks"] = mf.ntracks;
        r["file_format"] = mf.format;
        if (mf.initial_bpm > 0.0) r["file_bpm"] = mf.initial_bpm;   // informational: import does NOT retempo
        r["track_names"] = mf.track_names;
        return r;
    };

    // The symmetric half: a clip back out to .mid, so a part authored or recorded here can go to a
    // plugin's browser or another DAW. Per-note expression curves have no SMF equivalent and are
    // not written — say so rather than implying a lossless round-trip.
    handlers_["export_midi"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        if (P::session_track_is_audio(c.session, track))
            return err(code::kBadArg, "export_midi needs an instrument track (this is an audio/sampler track)");
        const std::string path = b.value("path", std::string());
        if (path.empty() || path.size() < 5 || path.compare(path.size() - 4, 4, ".mid") != 0)
            return err(code::kBadArg, "need an absolute path ending in .mid");

        const int cap = P::session_clip_note_count(c.session, track, scene);
        if (cap <= 0) return err(code::kBadArg, "clip is empty");
        std::vector<P::ClipNote> buf(static_cast<size_t>(cap));
        const int n = P::session_get_clip(c.session, track, scene, buf.data(), cap);
        const double bpm = c.transport ? c.transport->bpm.load(std::memory_order_relaxed) : 120.0;
        std::string werr;
        if (!P::write_midi_file(path, buf.data(), n, bpm, &werr)) return err(code::kIoError, werr);

        json r = ok(); r["path"] = path; r["notes"] = n; r["bpm"] = bpm;
        bool dropped_expr = false;
        for (int i = 0; i < n; ++i) if (buf[i].has_expr()) { dropped_expr = true; break; }
        if (dropped_expr) r["note"] = "per-note expression curves are not representable in SMF and were not written";
        return r;
    };
}

}  // namespace vivid
