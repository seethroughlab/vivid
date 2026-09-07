#include "cli/control_handlers_audio_domains.h"
#include "cli/control_handlers_internal.h"

#include "audio/vst3_host.h"
#include "audio/sampler_op.h"   // SamplerInfo / SamplerSlice / sampler_slices_to_notes

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// ADR-0049 (MCP parity): the Sampler's SAMPLE-editing surface. The Sampler editor could trim the played
// window, cut/move slices, auto-detect onsets, tune a slice, and write the slice map out as a MIDI clip
// — but none of it was reachable from the control server, so an agent could load a sample and then do
// nothing with it. These handlers expose exactly the operations the editor performs, through the same
// session_sampler_* API the editor calls, so the two surfaces stay in step.
//
// Deliberately NOT re-exposed here: base_note / gate / gain / transpose / tune / ADSR / voices are plain
// node params — `graph_set_node_param` already sets them. This file covers only what params can't.
namespace vivid {

namespace {

// Validate that `node` on `track` is a Sampler with a sample loaded, filling the error reply on
// failure. A wrong node id is the most likely agent mistake, so name the type it actually found.
// The caller parses track/node_id from the body itself (inline, like every other handler family) —
// the arg-shape parity guard reads those `b.value(...)` calls out of the handler body.
bool need_sampler(const ControlCtx& c, int track, int node, json& e) {
    namespace P = vivid::session;
    if (!c.session) { e = err(code::kNoSession, "no session"); return false; }
    if (!need_track(c.session, track, e)) return false;
    const int nc = P::session_track_audio_graph_node_count(c.session, track);
    for (int i = 0; i < nc; ++i) {
        if (P::session_track_audio_graph_node_id(c.session, track, i) != node) continue;
        const char* ty = P::session_track_audio_graph_node_type(c.session, track, i);
        if (!ty || std::string(ty) != "Sampler") {
            e = err(code::kBadArg, "node " + std::to_string(node) + " is a '" + (ty ? ty : "?") +
                                   "', not a Sampler");
            return false;
        }
        if (P::session_sampler_source_frames(c.session, track, node) == 0) {
            e = err(code::kBadArg, "Sampler node " + std::to_string(node) +
                                   " has no sample loaded (use load_sampler first)");
            return false;
        }
        return true;
    }
    e = err(code::kOutOfRange, "no audio-graph node " + std::to_string(node) + " on track " +
                               std::to_string(track));
    return false;
}

// The current played window / slice edges in SOURCE frames (what the editor drags).
int read_boundaries(vivid::session::Session* s, int t, int n,
                    std::vector<uint32_t>& starts, std::vector<uint32_t>& ends) {
    starts.assign(64, 0); ends.assign(64, 0);
    const int nb = vivid::session::session_sampler_edit_boundaries(s, t, n, starts.data(), ends.data(), 64);
    starts.resize(nb > 0 ? nb : 0); ends.resize(nb > 0 ? nb : 0);
    return nb;
}

// Re-cut [in,out) into N equal regions (N==1 => a plain melodic trim). Mirrors the editor's SLICES
// stepper, which is also how "clear the slices" is expressed: cut back to one region.
void reslice_equal(vivid::session::Session* s, int t, int n, uint32_t in, uint32_t out, int count, int base) {
    if (out <= in) return;
    if (count <= 1) { vivid::session::session_sampler_set_trim(s, t, n, in, out); return; }
    std::vector<uint32_t> st(count), en(count);
    const double span = static_cast<double>(out - in);
    for (int i = 0; i < count; ++i) {
        st[i] = in + static_cast<uint32_t>(span * i / count);
        en[i] = in + static_cast<uint32_t>(span * (i + 1) / count);
    }
    en[count - 1] = out;
    vivid::session::session_sampler_reslice(s, t, n, st.data(), en.data(), count, base);
}

// The slice map as JSON — shared by get_sampler and every mutating reply, so a caller always gets the
// resulting mapping back and never has to re-read to see what it did.
json slices_json(vivid::session::Session* s, int t, int n) {
    SamplerSlice sl[128];
    const int nsl = vivid::session::session_sampler_slices(s, t, n, sl, 128);
    std::vector<uint32_t> bs, be;
    const int nb = read_boundaries(s, t, n, bs, be);
    json arr = json::array();
    for (int i = 0; i < nsl; ++i) {
        json j;
        j["index"]     = i;
        j["root_note"] = sl[i].root_note;
        j["lo_note"]   = sl[i].lo_note;
        j["hi_note"]   = sl[i].hi_note;
        if (i < nb) { j["start"] = bs[i]; j["end"] = be[i]; }   // SOURCE frames (what you trim/drag)
        arr.push_back(std::move(j));
    }
    return arr;
}

}  // namespace

void register_sampler_handlers(Handlers& handlers_) {
    namespace P = vivid::session;

    // ---- READ: everything the editor shows, in one call ------------------------------------------
    handlers_["get_sampler"] = [](const ControlCtx& c, const json& b) {
        const int track = b.value("track", 0);
        const int node  = b.value("node_id", -1);
        json e; if (!need_sampler(c, track, node, e)) return e;
        SamplerInfo info{};
        P::session_sampler_info(c.session, track, node, &info);
        const char* src = P::session_sampler_source(c.session, track, node);
        json r = ok();
        r["track"] = track; r["node_id"] = node;
        r["source"] = src ? src : "";
        r["source_frames"] = P::session_sampler_source_frames(c.session, track, node);
        r["frames"] = info.frames;
        r["sample_rate"] = info.sample_rate;
        r["channels"] = info.channels;
        r["base_note"] = info.base_note;
        r["gate"] = info.gate != 0;
        r["slice_count"] = info.slice_count;
        r["slices"] = slices_json(c.session, track, node);
        r["summary"] = std::string("sampler ") + (src ? src : "(sample)") + " — " +
                       std::to_string(info.slice_count) + (info.slice_count == 1 ? " region" : " slices") +
                       (info.gate ? ", gated" : ", one-shot");
        return r;
    };

    // ---- WRITE: trim the played window (the editor's amber in/out handles) ------------------------
    handlers_["sampler_set_trim"] = [](const ControlCtx& c, const json& b) {
        const int track = b.value("track", 0);
        const int node  = b.value("node_id", -1);
        json e; if (!need_sampler(c, track, node, e)) return e;
        const unsigned long long sf = P::session_sampler_source_frames(c.session, track, node);
        const long long in  = b.value("start", 0LL);
        const long long out = b.value("end", static_cast<long long>(sf));
        if (in < 0 || out > static_cast<long long>(sf) || out - in < 16)
            return err(code::kBadArg, "start/end must be source frames with end-start >= 16 (source_frames=" +
                                      std::to_string(sf) + ")");
        P::session_sampler_set_trim(c.session, track, node, static_cast<uint32_t>(in), static_cast<uint32_t>(out));
        json r = ok();
        r["start"] = in; r["end"] = out; r["slices"] = slices_json(c.session, track, node);
        r["summary"] = "trimmed to [" + std::to_string(in) + "," + std::to_string(out) + ") frames";
        return r;
    };

    // ---- WRITE: cut into N equal slices. count=1 clears the slicing (back to a melodic trim). -----
    handlers_["sampler_slice_equal"] = [](const ControlCtx& c, const json& b) {
        const int track = b.value("track", 0);
        const int node  = b.value("node_id", -1);
        json e; if (!need_sampler(c, track, node, e)) return e;
        const int count = b.value("count", 0);
        if (count < 1 || count > 32) return err(code::kBadArg, "count must be 1..32 (1 = clear slices)");
        std::vector<uint32_t> bs, be;
        const int nb = read_boundaries(c.session, track, node, bs, be);
        if (nb <= 0) return err(code::kInternal, "sampler has no regions to re-cut");
        SamplerInfo info{}; P::session_sampler_info(c.session, track, node, &info);
        reslice_equal(c.session, track, node, bs.front(), be.back(), count, info.base_note);
        json r = ok();
        r["slice_count"] = count;
        r["slices"] = slices_json(c.session, track, node);
        r["summary"] = count == 1 ? "cleared slices — one melodic region across the keyboard"
                                  : "cut into " + std::to_string(count) + " equal slices";
        return r;
    };

    // ---- WRITE: auto-slice at detected onsets (the editor's Detect button) ------------------------
    handlers_["sampler_detect_slices"] = [](const ControlCtx& c, const json& b) {
        const int track = b.value("track", 0);
        const int node  = b.value("node_id", -1);
        json e; if (!need_sampler(c, track, node, e)) return e;
        const double sens = b.value("sensitivity", 0.5);
        if (sens < 0.0 || sens > 1.0) return err(code::kBadArg, "sensitivity must be 0..1");
        const int n = P::session_sampler_detect_slices(c.session, track, node, static_cast<float>(sens));
        json r = ok();
        r["slice_count"] = n;
        r["slices"] = slices_json(c.session, track, node);
        r["summary"] = "detected " + std::to_string(n) + (n == 1 ? " onset" : " onsets");
        return r;
    };

    // ---- WRITE: set explicit slice edges (the editor's divider drags) -----------------------------
    handlers_["sampler_set_slices"] = [](const ControlCtx& c, const json& b) {
        const int track = b.value("track", 0);
        const int node  = b.value("node_id", -1);
        json e; if (!need_sampler(c, track, node, e)) return e;
        if (!b.contains("starts") || !b["starts"].is_array())
            return err(code::kBadArg, "sampler_set_slices needs \"starts\" (source frames, ascending)");
        const unsigned long long sf = P::session_sampler_source_frames(c.session, track, node);
        std::vector<uint32_t> st, en;
        for (const auto& v : b["starts"]) {
            if (!v.is_number()) return err(code::kBadArg, "starts must be numbers (source frames)");
            const long long f = v.get<long long>();
            if (f < 0 || f >= static_cast<long long>(sf))
                return err(code::kBadArg, "a start is outside the source (source_frames=" + std::to_string(sf) + ")");
            st.push_back(static_cast<uint32_t>(f));
        }
        if (st.empty() || st.size() > 32) return err(code::kBadArg, "starts must hold 1..32 positions");
        std::sort(st.begin(), st.end());
        // Ends default to the next start (contiguous slices); an explicit "ends" array overrides.
        if (b.contains("ends") && b["ends"].is_array()) {
            for (const auto& v : b["ends"]) en.push_back(static_cast<uint32_t>(std::max<long long>(0, v.get<long long>())));
            if (en.size() != st.size()) return err(code::kBadArg, "ends must be the same length as starts");
        } else {
            for (size_t i = 0; i + 1 < st.size(); ++i) en.push_back(st[i + 1]);
            en.push_back(static_cast<uint32_t>(sf));
        }
        for (size_t i = 0; i < st.size(); ++i)
            if (en[i] <= st[i]) return err(code::kBadArg, "slice " + std::to_string(i) + " has end <= start");
        SamplerInfo info{}; P::session_sampler_info(c.session, track, node, &info);
        const int base = b.value("base_note", info.base_note);
        if (st.size() == 1) P::session_sampler_set_trim(c.session, track, node, st[0], en[0]);
        else P::session_sampler_reslice(c.session, track, node, st.data(), en.data(),
                                        static_cast<int>(st.size()), base);
        json r = ok();
        r["slice_count"] = static_cast<int>(st.size());
        r["slices"] = slices_json(c.session, track, node);
        r["summary"] = "set " + std::to_string(st.size()) + " slice region(s)";
        return r;
    };

    // ---- WRITE: per-slice tune (keeps the trigger note, shifts the pitch) -------------------------
    handlers_["sampler_set_slice_tune"] = [](const ControlCtx& c, const json& b) {
        const int track = b.value("track", 0);
        const int node  = b.value("node_id", -1);
        json e; if (!need_sampler(c, track, node, e)) return e;
        const int slice = b.value("slice", -1);
        const int semis = b.value("semitones", 0);
        SamplerInfo info{}; P::session_sampler_info(c.session, track, node, &info);
        if (slice < 0 || slice >= info.slice_count)
            return err(code::kOutOfRange, "slice must be 0.." + std::to_string(info.slice_count - 1));
        if (semis < -48 || semis > 48) return err(code::kBadArg, "semitones must be -48..48");
        P::session_sampler_set_slice_tune(c.session, track, node, slice, semis);
        json r = ok();
        r["slice"] = slice; r["semitones"] = semis;
        r["slices"] = slices_json(c.session, track, node);
        r["summary"] = "slice " + std::to_string(slice) + " tuned " + std::to_string(semis) + " st";
        return r;
    };

    // ---- WRITE: the slice map as a playable MIDI clip (the editor's Slice -> MIDI button) ---------
    handlers_["sampler_slices_to_midi"] = [](const ControlCtx& c, const json& b) {
        const int track = b.value("track", 0);
        const int node  = b.value("node_id", -1);
        json e; if (!need_sampler(c, track, node, e)) return e;
        SamplerSlice sl[128];
        const int nsl = P::session_sampler_slices(c.session, track, node, sl, 128);
        if (nsl <= 1)
            return err(code::kBadArg, "sampler has no slices — detect or cut some first "
                                      "(sampler_detect_slices / sampler_slice_equal)");
        // Default target: the first EMPTY scene on this track, so the call never clobbers a clip.
        // An explicit `scene` overwrites that scene deliberately.
        const int scenes = P::session_scene_count(c.session);
        int scene = b.value("scene", -1);
        if (scene < 0) {
            for (int i = 0; i < scenes; ++i)
                if (P::session_clip_note_count(c.session, track, i) == 0) { scene = i; break; }
            if (scene < 0) return err(code::kBadArg, "every scene on this track already has a clip — "
                                                     "pass \"scene\" to choose one to overwrite");
        } else if (scene >= scenes) {
            return err(code::kOutOfRange, "scene must be 0.." + std::to_string(scenes - 1));
        }
        // The SAME note layout the editor's button produces (sampler_op.h), so both agree.
        std::vector<P::ClipNote> notes(nsl);
        const int wrote = sampler_slices_to_notes(sl, nsl, notes.data(), nsl);
        notes.resize(wrote > 0 ? wrote : 0);
        if (notes.empty()) return err(code::kInternal, "no notes produced");
        const double len = std::max(4.0, std::ceil(wrote * 0.25));
        P::session_set_clip(c.session, track, scene, notes.data(), static_cast<int>(notes.size()), len);
        json r = ok();
        r["track"] = track; r["scene"] = scene; r["notes"] = wrote; r["length"] = len;
        r["summary"] = "wrote " + std::to_string(wrote) + " notes (one per slice) to track " +
                       std::to_string(track) + " scene " + std::to_string(scene);
        return r;
    };
}

}  // namespace vivid
