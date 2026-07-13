#include "cli/control_handlers_internal.h"
#include "cli/control_json.h"           // operator_to_json (operator catalog)

#include "audio/vst3_host.h"
#include "gpu/visual_graph.h"           // op registry / descriptors
#include "ui/node_graph.h"              // graph queries + mappings
#include "gpu/operator_scan.h"          // load_and_register_operator (live install)
#include "packages/package_manager.h"   // install_package
#include "app/app.h"                    // op_registry + op_loaders + health
#include "app/runtime_health.h"         // collect_health
#include "transport.h"                  // status (bpm/beats/playing)
#include "version.h"                    // VIVID_VERSION
#include "operator_api/types.h"         // VIVID_OPERATOR_ABI_VERSION
#include "persist.h"                    // kSessionSchemaVersion

#include <string>
#include <vector>

namespace vivid {

// Read-only discovery + status for agents: status/version/health, the operator catalog,
// package install, get_session/list_tracks/list_params, get_graph/layout_graph, get_mappings,
// list_mapping_sources.
void register_introspection_handlers(Handlers& handlers_) {
    using vivid::session::Session;
    namespace P = vivid::session;
    handlers_["status"] = [](const ControlCtx& c, const json&) {
        json r = ok();
        if (c.session) { r["tracks"] = P::session_track_count(c.session); r["scenes"] = P::session_scene_count(c.session); }
        if (c.transport) { r["bpm"] = c.transport->bpm.load(std::memory_order_relaxed);
                           r["beats"] = c.transport->beats.load(std::memory_order_relaxed);
                           r["playing"] = c.transport->is_playing(); }
        r["ops"] = c.graph ? c.graph->op_count() : 0;
        if (c.vgraph && c.vgraph->registry()) r["op_types"] = c.vgraph->registry()->type_names();  // spawnable ops
        return r;
    };
    // Version surface for agents/clients + the version-guard check: the app version,
    // the operator ABI a loaded dylib must match, and the session schema version a
    // saved file is gated against. One place to read all three compatibility numbers.
    handlers_["get_version"] = [](const ControlCtx&, const json&) {
        json r = ok();
        r["app_version"]    = VIVID_VERSION;
        r["operator_abi"]   = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
        r["session_schema"] = kSessionSchemaVersion;
#ifdef NDEBUG
        r["build_type"] = "release";
#else
        r["build_type"] = "debug";
#endif
        return r;
    };
    // Runtime health: a rolled-up snapshot (severity ok|warning|error) of the engine —
    // gpu device/error state, operator/graph counts + any missing ops, loaded packages,
    // control liveness. Cheap; meant for agents/monitors to poll.
    handlers_["get_health"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        json r = ok();
        r["health"] = to_json(collect_health(*c.app));
        return r;
    };
    // Full operator catalog for agent discovery: every registered op (built-in AND
    // loaded dylib) with its display_name/summary/keywords + param + port schema.
    handlers_["list_operators"] = [](const ControlCtx& c, const json&) {
        if (!c.vgraph || !c.vgraph->registry()) return err(code::kNoVgraph, "no visuals graph");
        auto* reg = c.vgraph->registry();
        json arr = json::array();
        for (const auto& name : reg->type_names()) {
            const VividOperatorDescriptor* d = reg->descriptor_for(name);
            if (!d) continue;
            // Rich shared schema (params + ports + semantic metadata) so agents pick/wire by intent.
            json jo = control_json::operator_to_json(*d);
            jo["gpu"] = d->has_process_gpu != 0;   // back-compat flag
            arr.push_back(jo);
        }
        json r = ok(); r["operators"] = arr; return r;
    };
    // Install an operator package from a directory (its vivid-package.json + sources):
    // compile each operator to a .dylib (managed dir) and register it LIVE — the new
    // op is immediately spawnable, no restart. (Compile blocks the frame briefly; a
    // background compile is P2.4.) New ops appear in list_operators afterwards.
    handlers_["install_operator_package"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.vgraph || !c.vgraph->registry()) return err(code::kNoVgraph, "no visuals graph");
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "install requires \"path\" (a package directory)");
        PackageInstallResult ir = install_package(path);
        if (!ir.ok) return err(code::kBadArg, ir.error);
        int registered = 0;
        json ops = json::array();
        for (const auto& ci : ir.compiles) {
            json jo = { {"name", ci.op_name}, {"compiled", ci.success} };
            if (!ci.success) {
                jo["error"] = ci.error_output;
            } else {
                const std::string regname = load_and_register_operator(
                    ci.dylib_path, c.app->op_registry, c.app->op_loaders);
                jo["registered"] = !regname.empty();
                if (!regname.empty()) { jo["op"] = regname; ++registered; }
                else jo["note"] = "compiled but not registered (name already in use)";
            }
            ops.push_back(jo);
        }
        json r = ok(); r["package"] = ir.name; r["registered"] = registered; r["operators"] = ops;
        return r;
    };
    handlers_["get_session"] = [](const ControlCtx& c, const json&) {
        if (!c.session || !c.graph) return err(code::kNoSession, "no session");
        json r = ok();
        r["session"] = session_to_json(c.session, *c.graph, *c.win_w, *c.win_h, *c.split_x, *c.dock_h);
        return r;
    };
    handlers_["list_tracks"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        Session* s = c.session;
        json arr = json::array();
        for (int t = 0; t < P::session_track_count(s); ++t) {
            json jt;
            jt["index"] = t;
            jt["id"] = P::session_track_id(s, t);   // stable id — use in mapping sources "track_<id>.<kind>"
            jt["name"] = P::session_track_name(s, t);
            jt["gain"] = P::session_track_gain(s, t);
            jt["is_audio"] = P::session_track_is_audio(s, t);
            jt["active_clip"] = P::session_active_clip(s, t);
            jt["queued_clip"] = P::session_queued_clip(s, t);
            jt["level"] = P::session_track_level(s, t);
            jt["transient"] = P::session_track_transient(s, t);
            jt["bands"] = { P::session_track_band(s, t, 0), P::session_track_band(s, t, 1), P::session_track_band(s, t, 2) };
            json devs = json::array();
            if (!P::session_track_is_audio(s, t)) devs.push_back({ {"device", 0}, {"kind", "instrument"}, {"name", P::session_track_name(s, t)} });
            for (int e = 0; e < P::session_effect_count(s, t); ++e)
                devs.push_back({ {"device", e + 1}, {"kind", "fx"}, {"name", P::session_effect_name(s, t, e)} });
            jt["devices"] = devs;
            arr.push_back(jt);
        }
        json r = ok(); r["tracks"] = arr; return r;
    };
    handlers_["list_params"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        Session* s = c.session;
        const int track = b.value("track", 0), device = b.value("device", 0);
        json e; if (!need_track(s, track, e)) return e;
        const int limit = b.value("limit", 64);
        std::string filter = b.value("filter", std::string());
        for (auto& ch : filter) ch = static_cast<char>(std::tolower((unsigned char)ch));
        const int pc = P::session_param_count(s, track, device);
        json arr = json::array();
        for (int i = 0; i < pc && static_cast<int>(arr.size()) < limit; ++i) {
            std::string nm = P::session_param_name(s, track, device, i);
            if (!filter.empty()) {
                std::string low = nm; for (auto& ch : low) ch = static_cast<char>(std::tolower((unsigned char)ch));
                if (low.find(filter) == std::string::npos) continue;
            }
            arr.push_back({ {"index", i}, {"id", P::session_param_id(s, track, device, i)},
                            {"name", nm}, {"value", P::session_param_value(s, track, device, i)} });
        }
        json r = ok(); r["count"] = pc; r["params"] = arr; return r;
    };
    handlers_["get_graph"] = [](const ControlCtx& c, const json&) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        auto& g = *c.graph;
        json nodes = json::array();
        for (int i = 0; i < g.op_count(); ++i) {
            int op = 0, in = -1, id = 0; float x = 0, y = 0; g.get_op(i, op, in, id, x, y);
            json params = json::array();
            for (int l = 0; l < g.op_param_count_at(i); ++l)
                params.push_back({ {"name", g.op_param_label_at(i, l)}, {"base", g.op_param_base_at(i, l)},
                                   {"value", g.op_param_value_at(i, l)}, {"wired", g.op_param_wired_at(i, l)} });
            nodes.push_back({ {"id", id}, {"op", g.op_kind_name(i)}, {"input", in},
                              {"inputs", g.op_inputs_at(i)},   // all texture input edges (port order)
                              {"x", x}, {"y", y}, {"params", params} });
        }
        json dnodes = json::array();
        for (int i = 0; i < g.node_count(); ++i) {
            float x = 0, y = 0; int cid = 0; std::string title; g.get_node(i, x, y, cid, title);
            dnodes.push_back({ {"char_id", cid}, {"title", title} });
        }
        json r = ok();
        r["nodes"] = nodes;
        r["data_nodes"] = dnodes;
        if (c.vgraph) { r["active_output"] = c.vgraph->active_output_id(); r["generator"] = vop_name(c.vgraph->generator()); }
        return r;
    };
    // Auto-arrange the op nodes into a tidy layered layout (the "Re-layout" button).
    handlers_["layout_graph"] = [](const ControlCtx& c, const json&) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        c.graph->layout_nodes();
        json r = ok(); r["nodes"] = c.graph->op_count(); return r;
    };
    handlers_["get_mappings"] = [](const ControlCtx& c, const json&) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        json arr = json::array();
        for (const auto& m : c.graph->mappings())
            arr.push_back({ {"src", m.source}, {"dst", m.dest}, {"amount", m.amount},
                            {"curve", m.curve}, {"invert", m.invert}, {"lo", m.out_lo}, {"hi", m.out_hi} });
        json r = ok(); r["mappings"] = arr; return r;
    };

    // Mapping affordances: the valid bridge SOURCES an agent can wire to a dest (previously only
    // documented in a docstring). Every audio characteristic — master + per live track (by stable
    // id) — as a ready-to-use source string, its 0..1 range, and a description. Dests are the
    // params from list_operators / list_audio_ops (build "node:<id>.<param>" or "param:t:d:i").
    handlers_["list_mapping_sources"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        static const char* kKinds[] = { "level", "transient", "low", "mid", "high" };
        static const char* kDescs[] = { "overall loudness", "attack/onset energy",
                                        "low-band energy", "mid-band energy", "high-band energy" };
        json sources = json::array();
        auto emit = [&](const std::string& prefix, const std::string& label) {
            for (int k = 0; k < 5; ++k)
                sources.push_back({ {"source", prefix + "." + kKinds[k]}, {"kind", kKinds[k]},
                                    {"label", label + " " + kKinds[k]}, {"range", {0.0, 1.0}},
                                    {"description", kDescs[k]} });
        };
        emit("master", "master");
        for (int t = 0; t < P::session_track_count(c.session); ++t)
            emit("track_" + std::to_string(P::session_track_id(c.session, t)),
                 P::session_track_name(c.session, t));
        json r = ok(); r["sources"] = sources; return r;
    };

}

}  // namespace vivid
