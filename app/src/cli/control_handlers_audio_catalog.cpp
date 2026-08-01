#include "cli/control_handlers_audio_domains.h"
#include "cli/control_handlers_internal.h"
#include "cli/control_json.h"
#include "cli/operator_catalog.h"

#include "audio/plugin_catalog.h"
#include "audio/vst3_host.h"
#include "gpu/visual_graph.h"
#include "ui/node_graph.h"

#include <string>

namespace vivid {

void register_audio_catalog_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
    // The native-audio back-compat surface: instruments + effects with the full schema, like
    // list_operators. ADR-0023 step 7: `list_operator_catalog` is the unified discovery endpoint;
    // this domain-scoped native-only view uses the same shared builder so the two cannot drift.
    handlers_["list_audio_operators"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        auto* reg = (c.vgraph && c.vgraph->registry()) ? c.vgraph->registry() : nullptr;
        json r = ok();
        r["instruments"] = control_json::native_audio_ops(c.session, reg, 1, "instrument");
        r["effects"]     = control_json::native_audio_ops(c.session, reg, 0, "audio_effect");
        return r;
    };

    // Everything that can START a signal: native instruments + every installed plugin the probe
    // classified as an instrument. (It used to return five hard-coded VST3 names, so a CLAP or a
    // native instrument could not begin a track at all.)
    handlers_["list_instruments"] = [](const ControlCtx& c, const json&) {
        json arr = json::array();
        for (int k = 0, n = c.session ? P::session_available_audio_op_count(c.session, 1) : 0; k < n; ++k)
            arr.push_back({ {"name", P::session_available_audio_op_name(c.session, 1, k)}, {"format", "native"} });
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            if (p.cls != P::kClassInstrument) continue;
            arr.push_back({ {"name", p.name}, {"path", p.path},
                            {"format", P::plugin_format_name(p.format)} });
        }
        json r = ok(); r["instruments"] = arr; return r;
    };
    // Create a track. kind "instrument" (default) needs an "instrument" — ANY name from
    // list_instruments (native / CLAP / VST3) or a .vst3 path; kind "audio" makes a sampler track.
    // Returns the new track index. (Phase-2 F2: `add_track` used to resolve VST3 only, so the native
    // and CLAP instruments list_instruments advertises — incl. the no-plugin-install ones — could
    // not begin a track. It now creates the instrument-shaped shell (session_add_graph_track) and
    // slots the right instrument in, matching how the project loader builds these tracks.)
    handlers_["add_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string kind = b.value("kind", std::string("instrument"));
        if (kind == "audio") {
            const int idx = P::session_add_audio_track(c.session);
            if (idx < 0) return err(code::kInternal, "add_track failed (kMaxTracks reached?)");
            json r = ok(); r["track"] = idx; return r;
        }
        const std::string inst = b.value("instrument", std::string());
        if (inst.empty())
            return err(code::kBadArg, "instrument track needs \"instrument\" (a list_instruments name or a .vst3 path)");

        // 1) A native instrument op (e.g. TestTone / Sampler): a graph-track shell + the native op.
        for (int k = 0, n = P::session_available_audio_op_count(c.session, 1); k < n; ++k) {
            if (inst != P::session_available_audio_op_name(c.session, 1, k)) continue;
            const int idx = P::session_add_graph_track(c.session, inst.c_str());
            if (idx < 0) return err(code::kInternal, "add_track failed (kMaxTracks reached?)");
            if (!P::session_set_track_audio_instrument(c.session, idx, inst.c_str())) {
                P::session_remove_track(c.session, idx);
                return err(code::kBadArg, "could not set native instrument '" + inst + "'");
            }
            json r = ok(); r["track"] = idx; r["format"] = "native"; return r;
        }
        // 2) A CLAP instrument by name (async load) — a graph-track shell + the queued CLAP.
        const bool is_vst3_path = inst.size() > 5 && inst.compare(inst.size() - 5, 5, ".vst3") == 0;
        if (!is_vst3_path) {
            for (int i = 0, n = P::plugin_count(); i < n; ++i) {
                const auto& p = P::plugin_at(i);
                if (p.cls != P::kClassInstrument || inst != p.name) continue;
                if (p.format == P::kFmtCLAP) {
                    const int idx = P::session_add_graph_track(c.session, inst.c_str());
                    if (idx < 0) return err(code::kInternal, "add_track failed (kMaxTracks reached?)");
                    if (!P::session_request_track_clap_instrument(c.session, idx, p.path.c_str())) {
                        P::session_remove_track(c.session, idx);
                        return err(code::kBadArg, "could not queue CLAP instrument '" + inst + "'");
                    }
                    json r = ok(); r["track"] = idx; r["format"] = "CLAP"; r["loading"] = true; return r;
                }
                break;   // a VST3 catalog match: fall through to the synchronous VST3 loader below
            }
        }
        // 3) VST3 (a catalog name or a .vst3 path): synchronous full instrument track.
        const int idx = P::session_add_instrument_track(c.session, inst.c_str());
        if (idx < 0)
            return err(code::kNotFound, "no instrument matched '" + inst +
                       "' — see list_instruments for valid names (native / CLAP / VST3), or pass a .vst3 path");
        json r = ok(); r["track"] = idx; r["format"] = "VST3"; return r;
    };
    // Append a scene (grid row): grows every track by one empty clip slot. Returns the new
    // scene index. Fails once the session reaches kMaxScenes.
    handlers_["add_scene"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int sc = P::session_add_scene(c.session);
        if (sc < 0) return err(code::kInternal, "add_scene failed (kMaxScenes reached?)");
        json r = ok(); r["scene"] = sc; return r;
    };
    // ADR-0022 P3.3: rename a scene (the "named" in "a scene is a named set of bindings").
    handlers_["set_scene_name"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int scene = b.value("scene", -1);
        if (scene < 0 || scene >= P::session_scene_count(c.session))
            return err(code::kBadArg, "scene out of range");
        P::session_set_scene_name(c.session, scene, b.value("name", std::string()).c_str());
        json r = ok(); r["scene"] = scene; r["name"] = P::session_scene_name(c.session, scene); return r;
    };
    // ADR-0022 P3.3: the note-generator ops that can be placed in a scene cell.
    handlers_["list_generators"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        json gens = json::array();
        for (int i = 0; i < P::session_available_generator_count(c.session); ++i)
            gens.push_back(std::string(P::session_available_generator_name(c.session, i)));
        json r = ok(); r["generators"] = gens; return r;
    };
    // Place a note generator (list_generators) into a scene cell — the cell voices the generator for
    // that scene instead of its clip. Replaces any generator already there.
    handlers_["place_generator"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1), scene = b.value("scene", -1);
        const std::string type = b.value("type", std::string());
        if (!P::session_place_generator(c.session, track, scene, type.c_str()))
            return err(code::kBadArg, "place_generator failed (bad track/scene/type, or audio track)");
        json r = ok(); r["track"] = track; r["scene"] = scene; r["type"] = type; return r;
    };
    // Revert a scene cell to a clip (remove its generator).
    handlers_["remove_generator"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1), scene = b.value("scene", -1);
        if (!P::session_remove_generator(c.session, track, scene))
            return err(code::kBadArg, "remove_generator failed (no generator in that cell)");
        json r = ok(); r["track"] = track; r["scene"] = scene; return r;
    };
    // Set a param on a scene cell's generator (by param name; see inspect_scene for the params).
    handlers_["set_generator_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1), scene = b.value("scene", -1);
        const std::string name = b.value("name", std::string());
        const float value = b.value("value", 0.f);
        if (!P::session_set_generator_param(c.session, track, scene, name.c_str(), value))
            return err(code::kBadArg, "set_generator_param failed (no generator, or unknown param)");
        json r = ok(); r["track"] = track; r["scene"] = scene; r["name"] = name; r["value"] = value; return r;
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
