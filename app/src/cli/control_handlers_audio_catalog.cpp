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
    // Append a scene (grid row): grows every track by one empty clip slot. Returns the new
    // scene index. Fails once the session reaches kMaxScenes.
    handlers_["add_scene"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int sc = P::session_add_scene(c.session);
        if (sc < 0) return err(code::kInternal, "add_scene failed (kMaxScenes reached?)");
        json r = ok(); r["scene"] = sc; return r;
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
