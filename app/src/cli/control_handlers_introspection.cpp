#include "cli/control_handlers_internal.h"
#include "cli/control_json.h"           // operator_to_json (operator catalog)
#include "cli/operator_catalog.h"       // native_audio_ops — shared enumeration (ADR-0023 step 7)

#include "audio/vst3_host.h"
#include "gpu/visual_graph.h"           // op registry / descriptors
#include "ui/node_graph.h"              // graph queries + mappings
#include "gpu/operator_scan.h"          // load_and_register_operator (live install)
#include "gpu/shader_library.h"         // ADR-0016: the shader library (list/reload/fork)
#include "audio/plugin_catalog.h"       // unified operator catalog: installed plugins
#include "packages/package_manager.h"   // install_package
#include "app/app.h"                    // op_registry + op_loaders + health
#include "app/runtime_health.h"         // collect_health
#include "app/crash_recovery.h"         // ADR-0018 crash_dir()
#include "app/quarantine.h"             // ADR-0018 scan_quarantine / clear_crash_history
#include "transport.h"                  // status (bpm/beats/playing)
#include "version.h"                    // VIVID_VERSION
#include "operator_api/types.h"         // VIVID_OPERATOR_ABI_VERSION
#include "persist.h"                    // kSessionSchemaVersion

#include <string>
#include <sstream>
#include <vector>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace vivid {
namespace {

namespace P = vivid::session;

const char* safe_cstr(const char* s) { return s ? s : ""; }

json audio_graph_summary(P::Session* s, int track) {
    static const char* kKind[] = { "instrument", "effect", "output", "midi_in", "note_effect", "modulator", "midi_clip", "selector", "generator" };
    json r;
    r["graph_ok"] = P::session_track_audio_graph_ok(s, track) != 0;
    r["output_id"] = P::session_track_audio_graph_output_id(s, track);
    r["nodes"] = P::session_track_audio_graph_node_count(s, track);
    r["edges"] = P::session_track_audio_graph_edge_count(s, track);

    json kinds = json::object();
    for (int i = 0; i < P::session_track_audio_graph_node_count(s, track); ++i) {
        const int k = P::session_track_audio_graph_node_kind(s, track, i);
        const std::string key = (k >= 0 && k < 9) ? kKind[k] : "unknown";
        kinds[key] = kinds.value(key, 0) + 1;
    }
    r["node_kinds"] = kinds;
    return r;
}

json track_summary(P::Session* s, int track, bool include_scenes) {
    json jt;
    jt["index"] = track;
    jt["id"] = P::session_track_id(s, track);
    jt["name"] = safe_cstr(P::session_track_name(s, track));
    jt["kind"] = P::session_track_is_audio(s, track) ? "audio" : "instrument";
    jt["gain"] = P::session_track_gain(s, track);
    jt["active_clip"] = P::session_active_clip(s, track);
    jt["queued_clip"] = P::session_queued_clip(s, track);
    jt["analysis"] = {
        {"level", P::session_track_level(s, track)},
        {"transient", P::session_track_transient(s, track)},
        {"low", P::session_track_band(s, track, 0)},
        {"mid", P::session_track_band(s, track, 1)},
        {"high", P::session_track_band(s, track, 2)}
    };

    json devices = json::array();
    if (!P::session_track_is_audio(s, track)) {
        devices.push_back({ {"device", 0}, {"kind", "instrument"}, {"name", safe_cstr(P::session_track_name(s, track))} });
    }
    for (int e = 0; e < P::session_effect_count(s, track); ++e)
        devices.push_back({ {"device", e + 1}, {"kind", "fx"}, {"name", safe_cstr(P::session_effect_name(s, track, e))} });
    jt["devices"] = devices;

    if (P::session_track_audio_graph_ok(s, track)) {
        jt["audio_graph"] = audio_graph_summary(s, track);
        // ADR-0022 P2b.3c: the track-out node's session-global id (is_track_out — the per-track sink,
        // complement of the master's is_master gnid). -1 on a derived-chain track (not cross-addressable).
        jt["track_out_gnid"] = P::session_track_out_gnid(s, track);
    }

    if (include_scenes) {
        json clips = json::array();
        for (int sc = 0; sc < P::session_scene_count(s); ++sc) {
            json jc = { {"scene", sc} };
            if (P::session_track_is_audio(s, track)) {
                jc["kind"] = "audio";
                jc["length"] = P::session_audio_loop_beats(s, track, sc);
                jc["source_bpm"] = P::session_audio_clip_bpm(s, track, sc);
                jc["empty"] = jc.value("length", 0.0) <= 0.0;
            } else {
                jc["kind"] = "midi";
                jc["length"] = P::session_clip_length(s, track, sc);
                jc["notes"] = P::session_clip_note_count(s, track, sc);
                jc["empty"] = jc.value("notes", 0) <= 0;
            }
            double ls = 0, le = 0;
            P::session_get_clip_loop(s, track, sc, &ls, &le);
            if (le > ls) jc["loop"] = { {"start", ls}, {"end", le} };
            clips.push_back(jc);
        }
        jt["clips"] = clips;
    }
    return jt;
}

json visual_graph_summary(const ControlCtx& c) {
    json r;
    if (!c.graph) return r;
    r["nodes"] = c.graph->op_count();
    r["data_nodes"] = c.graph->node_count();
    r["mappings"] = static_cast<int>(c.graph->mappings().size());
    if (c.vgraph) {
        r["active_output"] = c.vgraph->active_output_id();
        r["generator"] = c.vgraph->generator();
    }
    json ops = json::array();
    for (int i = 0; i < c.graph->op_count(); ++i) {
        int in = -1, id = 0; float x = 0, y = 0;
        c.graph->get_op(i, in, id, x, y);
        ops.push_back({ {"id", id}, {"op", c.graph->op_kind_name(i)}, {"inputs", c.graph->op_inputs_at(i)} });
    }
    r["ops"] = ops;
    return r;
}

json mapping_summary(const ControlCtx& c) {
    json arr = json::array();
    if (!c.graph) return arr;
    for (const auto& m : c.graph->mappings()) {
        arr.push_back({ {"src", m.source}, {"dst", m.dest}, {"amount", m.amount},
                        {"curve", m.curve}, {"invert", m.invert}, {"lo", m.out_lo}, {"hi", m.out_hi} });
    }
    return arr;
}

json visual_mapping_destinations(const ControlCtx& c) {
    json arr = json::array();
    if (!c.graph) return arr;
    for (int i = 0; i < c.graph->op_count(); ++i) {
        int in = -1, id = 0; float x = 0, y = 0;
        c.graph->get_op(i, in, id, x, y);
        for (int p = 0; p < c.graph->op_param_count_at(i); ++p) {
            const std::string name = safe_cstr(c.graph->op_param_label_at(i, p));
            const std::string dest = "node:" + std::to_string(id) + "." + name;
            json jd = { {"dest", dest}, {"domain", "visual"}, {"target", "node"},
                        {"node_id", id}, {"op", c.graph->op_kind_name(i)}, {"param", name},
                        {"base", c.graph->op_param_base_at(i, p)},
                        {"value", c.graph->op_param_value_at(i, p)},
                        {"wired", c.graph->op_param_wired_at(i, p)},
                        {"range", { c.graph->op_param_min_at(i, p), c.graph->op_param_max_at(i, p) }} };
            if (const std::string* src = c.graph->source_of(dest)) jd["mapped_from"] = *src;
            arr.push_back(jd);
        }
    }
    return arr;
}

json hosted_audio_destinations(P::Session* s, int track) {
    json arr = json::array();
    const int devs = P::session_track_is_audio(s, track) ? 0 : (1 + P::session_effect_count(s, track));
    for (int d = 0; d < devs; ++d) {
        const std::string device_name = d == 0 ? safe_cstr(P::session_track_name(s, track))
                                               : safe_cstr(P::session_effect_name(s, track, d - 1));
        for (int p = 0; p < P::session_param_count(s, track, d); ++p) {
            const std::string dest = "param:" + std::to_string(track) + ":" + std::to_string(d) + ":" + std::to_string(p);
            arr.push_back({ {"dest", dest}, {"domain", "audio"}, {"target", "hosted_device"},
                            {"track", track}, {"device", d}, {"device_name", device_name},
                            {"param_index", p}, {"param", safe_cstr(P::session_param_name(s, track, d, p))},
                            {"value", P::session_param_value(s, track, d, p)},
                            {"normalized", true}, {"range", {0.0, 1.0}} });
        }
    }
    return arr;
}

json native_audio_destinations(P::Session* s, int track) {
    json arr = json::array();
    auto emit = [&](int index, const char* role) {
        const char* type = P::session_audio_op_type(s, track, index);
        if (!type || !*type) return;
        for (int p = 0; p < P::session_audio_op_param_count(s, track, index); ++p) {
            const std::string dest = "aparam:" + std::to_string(track) + ":" + std::to_string(index) + ":" + std::to_string(p);
            arr.push_back({ {"dest", dest}, {"domain", "audio"}, {"target", "native_audio_op"},
                            {"track", track}, {"index", index}, {"role", role}, {"op", type},
                            {"param_index", p}, {"param", safe_cstr(P::session_audio_op_param_name(s, track, index, p))},
                            {"value", P::session_audio_op_param_get(s, track, index, p)},
                            {"range", { P::session_audio_op_param_min(s, track, index, p),
                                         P::session_audio_op_param_max(s, track, index, p) }} });
        }
    };
    emit(-1, "instrument");
    for (int i = 0; i < P::session_audio_effect_count(s, track); ++i) emit(i, "effect");
    return arr;
}

json audio_graph_destinations(P::Session* s, int track) {
    json arr = json::array();
    if (!P::session_track_audio_graph_ok(s, track)) return arr;
    for (int i = 0; i < P::session_track_audio_graph_node_count(s, track); ++i) {
        const int nid = P::session_track_audio_graph_node_id(s, track, i);
        const std::string node_type = safe_cstr(P::session_track_audio_graph_node_type(s, track, i));
        for (int p = 0; p < P::session_audio_graph_node_param_count(s, track, nid); ++p) {
            const std::string dest = "gnode:" + std::to_string(track) + ":" + std::to_string(nid) + ":" + std::to_string(p);
            arr.push_back({ {"dest", dest}, {"domain", "audio"}, {"target", "audio_graph_node"},
                            {"track", track}, {"node", nid}, {"op", node_type},
                            {"param_index", p}, {"param", safe_cstr(P::session_audio_graph_node_param_name(s, track, nid, p))},
                            {"base", P::session_audio_graph_node_param_get(s, track, nid, p)},
                            {"value", P::session_audio_graph_node_param_resolved(s, track, nid, p)},
                            {"wired", P::session_audio_graph_node_param_wired(s, track, nid, p) != 0},
                            {"range", { P::session_audio_graph_node_param_min(s, track, nid, p),
                                         P::session_audio_graph_node_param_max(s, track, nid, p) }} });
        }
    }
    return arr;
}

json audio_mapping_destinations(P::Session* s) {
    json arr = json::array();
    if (!s) return arr;
    for (int t = 0; t < P::session_track_count(s); ++t) {
        for (auto& d : hosted_audio_destinations(s, t)) arr.push_back(std::move(d));
        for (auto& d : native_audio_destinations(s, t)) arr.push_back(std::move(d));
        for (auto& d : audio_graph_destinations(s, t)) arr.push_back(std::move(d));
    }
    return arr;
}

std::string source_label(P::Session* s, const std::string& src) {
    if (src.rfind("master.", 0) == 0) return "master " + src.substr(7);
    if (src.rfind("track_", 0) == 0) {
        const auto dot = src.find('.');
        if (dot != std::string::npos) {
            const int stable = std::atoi(src.c_str() + 6);
            for (int t = 0; s && t < P::session_track_count(s); ++t) {
                if (P::session_track_id(s, t) == stable)
                    return std::string(safe_cstr(P::session_track_name(s, t))) + " " + src.substr(dot + 1);
            }
        }
    }
    return src;
}

std::string dest_label(const ControlCtx& c, const std::string& dest) {
    if (dest.rfind("node:", 0) == 0) {
        const size_t dot = dest.find('.', 5);
        const std::string id = dot == std::string::npos ? dest.substr(5) : dest.substr(5, dot - 5);
        const std::string param = dot == std::string::npos ? "" : dest.substr(dot + 1);
        return "visual node " + id + (param.empty() ? "" : " " + param);
    }
    int T = -1, D = 0, I = 0;
    if (dest.rfind("param:", 0) == 0 && std::sscanf(dest.c_str(), "param:%d:%d:%d", &T, &D, &I) == 3 && c.session) {
        const char* pn = P::session_param_name(c.session, T, D, I);
        return "track " + std::to_string(T) + " device " + std::to_string(D) + " " + safe_cstr(pn);
    }
    if (dest.rfind("aparam:", 0) == 0 && std::sscanf(dest.c_str(), "aparam:%d:%d:%d", &T, &D, &I) == 3 && c.session) {
        const char* pn = P::session_audio_op_param_name(c.session, T, D, I);
        return "track " + std::to_string(T) + " native op " + std::to_string(D) + " " + safe_cstr(pn);
    }
    int NID = -1;
    if (dest.rfind("gnode:", 0) == 0 && std::sscanf(dest.c_str(), "gnode:%d:%d:%d", &T, &NID, &I) == 3 && c.session) {
        const char* pn = P::session_audio_graph_node_param_name(c.session, T, NID, I);
        return "track " + std::to_string(T) + " audio node " + std::to_string(NID) + " " + safe_cstr(pn);
    }
    return dest;
}

json binding_json(const ControlCtx& c, const Mapping& m) {
    return { {"src", m.source}, {"src_label", source_label(c.session, m.source)},
             {"dst", m.dest}, {"dst_label", dest_label(c, m.dest)},
             {"amount", m.amount}, {"curve", m.curve}, {"invert", m.invert},
             {"lo", m.out_lo}, {"hi", m.out_hi} };
}

std::string lower_copy(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool text_match(const json& j, const std::string& query) {
    if (query.empty()) return true;
    const std::string q = lower_copy(query);
    std::string hay;
    auto add = [&](const char* key) {
        if (j.contains(key) && j[key].is_string()) {
            hay += " ";
            hay += j[key].get<std::string>();
        }
    };
    add("name"); add("display_name"); add("summary"); add("kind"); add("domain"); add("format"); add("class");
    if (j.contains("keywords") && j["keywords"].is_array())
        for (const auto& kw : j["keywords"]) if (kw.is_string()) { hay += " "; hay += kw.get<std::string>(); }
    if (j.contains("params") && j["params"].is_array()) {
        for (const auto& p : j["params"]) {
            if (!p.is_object()) continue;
            for (const char* key : {"name", "description", "semantic_tag", "semantic_intent", "semantic_unit"})
                if (p.contains(key) && p[key].is_string()) { hay += " "; hay += p[key].get<std::string>(); }
        }
    }
    return lower_copy(hay).find(q) != std::string::npos;
}

json unified_operator_catalog(const ControlCtx& c, const std::string& domain, const std::string& kind) {
    json arr = json::array();
    auto accept = [&](const char* d, const char* k) {
        return (domain.empty() || domain == "all" || domain == d) &&
               (kind.empty() || kind == "all" || kind == k);
    };
    auto* reg = (c.vgraph && c.vgraph->registry()) ? c.vgraph->registry() : nullptr;
    if (reg && accept("visual", "gpu_visual")) {
        for (const auto& name : reg->type_names()) {
            const VividOperatorDescriptor* d = reg->descriptor_for(name);
            if (!d || !d->has_process_gpu) continue;
            json jo = control_json::operator_to_json(*d, "gpu_visual");
            jo["domain"] = "visual";
            jo["spawn"] = { {"tool", "add_node"}, {"op_arg", name} };
            arr.push_back(jo);
        }
    }
    if (c.session && reg) {
        auto emit_audio = [&](int want_source, const char* k, const char* spawn_tool) {
            if (!accept("audio", k)) return;
            for (json& jo : control_json::native_audio_ops(c.session, reg, want_source, k)) {
                jo["domain"] = "audio";
                jo["format"] = "native";
                jo["spawn"] = { {"tool", spawn_tool}, {"op_arg", jo.value("name", std::string())} };
                arr.push_back(std::move(jo));
            }
        };
        emit_audio(1, "instrument", "set_track_audio_instrument");
        emit_audio(0, "audio_effect", "add_audio_effect");
        if (accept("audio", "note_effect")) {
            for (int i = 0; i < P::session_available_note_op_count(c.session); ++i) {
                const char* nm = P::session_available_note_op_name(c.session, i);
                const VividOperatorDescriptor* d = reg->descriptor_for(nm ? nm : "");
                json jo = d ? control_json::operator_to_json(*d, "note_effect")
                            : json({ {"name", nm ? nm : ""}, {"kind", "note_effect"} });
                jo["domain"] = "audio"; jo["format"] = "native";
                jo["spawn"] = { {"tool", "audio_graph_add_note_op"}, {"op_arg", nm ? nm : ""} };
                arr.push_back(jo);
            }
        }
        if (accept("audio", "modulator")) {
            for (int i = 0; i < P::session_available_mod_op_count(c.session); ++i) {
                const char* nm = P::session_available_mod_op_name(c.session, i);
                const VividOperatorDescriptor* d = reg->descriptor_for(nm ? nm : "");
                json jo = d ? control_json::operator_to_json(*d, "modulator")
                            : json({ {"name", nm ? nm : ""}, {"kind", "modulator"} });
                jo["domain"] = "audio"; jo["format"] = "native";
                jo["spawn"] = { {"tool", "audio_graph_add_mod_op"}, {"op_arg", nm ? nm : ""} };
                arr.push_back(jo);
            }
        }
    }
    if (accept("audio", "plugin") || accept("audio", "instrument") || accept("audio", "audio_effect") || accept("audio", "note_effect")) {
        for (int i = 0; i < P::plugin_count(); ++i) {
            const auto& p = P::plugin_at(i);
            const std::string cls = P::plugin_class_name(p.cls);
            std::string k = "plugin";
            if (cls == "instrument") k = "instrument";
            else if (cls == "effect") k = "audio_effect";
            else if (cls == "note-effect") k = "note_effect";
            if (!accept("audio", k.c_str()) && !accept("audio", "plugin")) continue;
            json jo = { {"name", p.name}, {"domain", "audio"}, {"kind", k},
                        {"format", P::plugin_format_name(p.format)}, {"class", cls},
                        {"path", p.path}, {"vendor", p.vendor}, {"uid", p.uid},
                        {"probed", p.probed} };
            jo["spawn"] = { {"tool", k == "instrument" ? "audio_graph_add_plugin" : "add_effect"},
                            {"path_arg", p.path} };
            arr.push_back(jo);
        }
    }
    return arr;
}

std::string session_overview_text(const ControlCtx& c) {
    std::ostringstream ss;
    const int tracks = c.session ? P::session_track_count(c.session) : 0;
    const int scenes = c.session ? P::session_scene_count(c.session) : 0;
    ss << tracks << " tracks, " << scenes << " scenes";
    if (c.transport) {
        ss << " at " << c.transport->bpm.load(std::memory_order_relaxed) << " BPM";
        ss << (c.transport->is_playing() ? ", playing" : ", stopped");
    }
    if (c.graph) ss << "; visuals have " << c.graph->op_count() << " op nodes and " << c.graph->mappings().size() << " mappings";
    return ss.str();
}

std::string signal_flow_text(const ControlCtx& c) {
    std::ostringstream ss;
    const int mappings = c.graph ? static_cast<int>(c.graph->mappings().size()) : 0;
    ss << mappings << " audio/control mapping" << (mappings == 1 ? "" : "s");
    if (c.vgraph) ss << "; active output " << c.vgraph->active_output_id() << " from generator " << c.vgraph->generator();
    return ss.str();
}

}  // namespace

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
    handlers_["inspect_session_overview"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string detail = b.value("detail", std::string("summary"));
        json r = ok();
        r["summary"] = session_overview_text(c);
        r["transport"] = json::object();
        if (c.transport) {
            r["transport"] = { {"bpm", c.transport->bpm.load(std::memory_order_relaxed)},
                               {"beats", c.transport->beats.load(std::memory_order_relaxed)},
                               {"playing", c.transport->is_playing()},
                               {"level", c.transport->level.load(std::memory_order_relaxed)},
                               {"transient", c.transport->transient.load(std::memory_order_relaxed)},
                               {"bands", { c.transport->band_low.load(std::memory_order_relaxed),
                                            c.transport->band_mid.load(std::memory_order_relaxed),
                                            c.transport->band_high.load(std::memory_order_relaxed) }} };
        }
        r["counts"] = { {"tracks", P::session_track_count(c.session)},
                        {"scenes", P::session_scene_count(c.session)},
                        {"visual_nodes", c.graph ? c.graph->op_count() : 0},
                        {"mappings", c.graph ? static_cast<int>(c.graph->mappings().size()) : 0} };
        if (c.app) {
            r["project"] = { {"path", c.app->project.current_project_path},
                             {"media_root", c.app->project.media_root},
                             {"missing_media", c.app->project.missing_media} };
        }
        json tracks = json::array();
        const bool full = detail == "full";
        for (int t = 0; t < P::session_track_count(c.session); ++t)
            tracks.push_back(track_summary(c.session, t, full));
        r["tracks"] = tracks;
        r["visuals"] = visual_graph_summary(c);
        if (detail != "summary") r["mappings"] = mapping_summary(c);
        json warnings = json::array();
        if (!c.graph) warnings.push_back("visual graph unavailable");
        if (!c.transport) warnings.push_back("transport unavailable");
        if (!warnings.empty()) r["warnings"] = warnings;
        return r;
    };
    handlers_["inspect_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string detail = b.value("detail", std::string("normal"));
        json r = ok();
        r["track"] = track_summary(c.session, track, detail != "summary");
        std::ostringstream ss;
        ss << "Track " << track << " (" << safe_cstr(P::session_track_name(c.session, track)) << ") is "
           << (P::session_track_is_audio(c.session, track) ? "audio" : "instrument")
           << " with " << P::session_effect_count(c.session, track) << " hosted FX";
        if (P::session_track_audio_graph_ok(c.session, track))
            ss << " and an audio graph of " << P::session_track_audio_graph_node_count(c.session, track) << " nodes";
        r["summary"] = ss.str();
        return r;
    };
    handlers_["inspect_scene"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int scene = b.value("scene", 0);
        json e; if (!need_scene(c.session, scene, e)) return e;
        json tracks = json::array();
        int filled = 0;
        for (int t = 0; t < P::session_track_count(c.session); ++t) {
            json jt = { {"track", t}, {"id", P::session_track_id(c.session, t)},
                        {"name", safe_cstr(P::session_track_name(c.session, t))},
                        {"kind", P::session_track_is_audio(c.session, t) ? "audio" : "instrument"},
                        {"active", P::session_active_clip(c.session, t) == scene},
                        {"queued", P::session_queued_clip(c.session, t) == scene} };
            if (P::session_track_is_audio(c.session, t)) {
                const double len = P::session_audio_loop_beats(c.session, t, scene);
                jt["length"] = len;
                jt["source_bpm"] = P::session_audio_clip_bpm(c.session, t, scene);
                jt["empty"] = len <= 0.0;
                if (len > 0.0) ++filled;
            } else if (P::session_cell_is_generator(c.session, t, scene)) {
                // ADR-0022 P3.3: a generator cell — report its type + live params instead of clip notes.
                jt["cell"] = "generator";
                jt["generator"] = safe_cstr(P::session_generator_type(c.session, t, scene));
                json params = json::object();
                const int np = P::session_generator_param_count(c.session, t, scene);
                for (int i = 0; i < np; ++i)
                    params[safe_cstr(P::session_generator_param_name(c.session, t, scene, i))] =
                        P::session_generator_param_value(c.session, t, scene, i);
                jt["params"] = params;
                jt["empty"] = false;
                ++filled;
            } else {
                const int notes = P::session_clip_note_count(c.session, t, scene);
                jt["cell"] = "clip";
                jt["length"] = P::session_clip_length(c.session, t, scene);
                jt["notes"] = notes;
                jt["empty"] = notes <= 0;
                if (notes > 0) ++filled;
            }
            tracks.push_back(jt);
        }
        json r = ok();
        std::ostringstream ss;
        ss << "Scene " << scene << " has material on " << filled << " of "
           << P::session_track_count(c.session) << " tracks";
        r["summary"] = ss.str();
        r["scene"] = scene;
        r["name"] = safe_cstr(P::session_scene_name(c.session, scene));   // ADR-0022 P3.3
        r["tracks"] = tracks;
        return r;
    };
    handlers_["explain_scene"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int scene = b.value("scene", 0);
        json e; if (!need_scene(c.session, scene, e)) return e;
        json lines = json::array();
        int filled = 0;
        for (int t = 0; t < P::session_track_count(c.session); ++t) {
            std::ostringstream ss;
            ss << "Track " << t << " (" << safe_cstr(P::session_track_name(c.session, t)) << "): ";
            if (P::session_track_is_audio(c.session, t)) {
                const double len = P::session_audio_loop_beats(c.session, t, scene);
                if (len > 0.0) {
                    ++filled;
                    ss << "audio clip, " << len << " beats";
                    const int bpm = P::session_audio_clip_bpm(c.session, t, scene);
                    if (bpm > 0) ss << ", source " << bpm << " BPM";
                } else {
                    ss << "empty audio slot";
                }
            } else {
                const int notes = P::session_clip_note_count(c.session, t, scene);
                if (notes > 0) {
                    ++filled;
                    ss << notes << " MIDI notes over " << P::session_clip_length(c.session, t, scene) << " beats";
                } else {
                    ss << "empty MIDI slot";
                }
            }
            if (P::session_active_clip(c.session, t) == scene) ss << " (active)";
            else if (P::session_queued_clip(c.session, t) == scene) ss << " (queued)";
            lines.push_back(ss.str());
        }
        json r = ok();
        std::ostringstream summary;
        summary << "Scene " << scene << " has material on " << filled << " of "
                << P::session_track_count(c.session) << " tracks";
        if (c.graph && !c.graph->mappings().empty())
            summary << " and " << c.graph->mappings().size() << " session-level mappings can react to it";
        r["summary"] = summary.str();
        r["scene"] = scene;
        r["explanation"] = lines;
        if (c.graph) r["mappings"] = mapping_summary(c);
        return r;
    };
    handlers_["inspect_signal_flow"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph) return err(code::kNoGraph, "no session/graph");
        const std::string scope = b.value("scope", std::string("session"));
        json r = ok();
        r["scope"] = scope;
        r["summary"] = signal_flow_text(c);
        r["visuals"] = visual_graph_summary(c);
        r["mappings"] = mapping_summary(c);
        json audio = json::array();
        for (int t = 0; t < P::session_track_count(c.session); ++t) {
            json jt = { {"track", t}, {"id", P::session_track_id(c.session, t)},
                        {"name", safe_cstr(P::session_track_name(c.session, t))},
                        {"analysis_sources", {
                            "track_" + std::to_string(P::session_track_id(c.session, t)) + ".level",
                            "track_" + std::to_string(P::session_track_id(c.session, t)) + ".transient",
                            "track_" + std::to_string(P::session_track_id(c.session, t)) + ".low",
                            "track_" + std::to_string(P::session_track_id(c.session, t)) + ".mid",
                            "track_" + std::to_string(P::session_track_id(c.session, t)) + ".high"
                        }} };
            if (P::session_track_audio_graph_ok(c.session, t)) jt["audio_graph"] = audio_graph_summary(c.session, t);
            audio.push_back(jt);
        }
        r["audio"] = audio;
        return r;
    };
    handlers_["explain_signal_flow"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph) return err(code::kNoGraph, "no session/graph");
        const std::string scope = b.value("scope", std::string("session"));
        json r = ok();
        r["scope"] = scope;
        r["summary"] = signal_flow_text(c);
        json lines = json::array();
        lines.push_back(session_overview_text(c));
        if (c.graph->mappings().empty()) {
            lines.push_back("No audio/control mappings are currently driving visual or audio parameters.");
        } else {
            for (const auto& m : c.graph->mappings()) {
                std::ostringstream ss;
                ss << m.source << " drives " << m.dest << " amount=" << m.amount;
                if (m.invert) ss << " inverted";
                if (m.out_lo != 0.f || m.out_hi != 1.f) ss << " range=[" << m.out_lo << "," << m.out_hi << "]";
                lines.push_back(ss.str());
            }
        }
        r["explanation"] = lines;
        r["mappings"] = mapping_summary(c);
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
    // ADR-0018 (R3): the operators quarantined this launch (repeat crashers, disabled by default),
    // and a way to clear an operator's crash history so it re-enables on the next launch.
    handlers_["list_quarantine"] = [](const ControlCtx& c, const json&) {
        if (!c.app || !c.app->crash_recovery) return err(code::kInternal, "no crash recovery");
        json list = json::array();
        for (const auto& q : vivid::scan_quarantine(c.app->crash_recovery->crash_dir()))
            list.push_back({ {"operator", q.type_name}, {"crash_count", q.crash_count}, {"last_seen", q.last_seen} });
        json r = ok(); r["quarantined"] = list; return r;
    };
    handlers_["unquarantine"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.app->crash_recovery) return err(code::kInternal, "no crash recovery");
        const std::string op = b.value("op", std::string());
        if (op.empty()) return err(code::kBadArg, "need op");
        const int removed = vivid::clear_crash_history(c.app->crash_recovery->crash_dir(), op);
        json r = ok(); r["removed"] = removed; r["note"] = "restart to re-enable"; return r;
    };
    // Full operator catalog for agent discovery: every registered op (built-in AND
    // loaded dylib) with its display_name/summary/keywords + param + port schema.
    // The raw visual-operator registry dump (back-compat). ADR-0023 step 7: `list_operator_catalog`
    // is the unified, domain/kind-aware discovery surface across visual + audio + plugins — prefer it
    // for new agents; this stays for compatibility until callers migrate.
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
    handlers_["list_operator_catalog"] = [](const ControlCtx& c, const json& b) {
        const std::string domain = lower_copy(b.value("domain", std::string("all")));
        const std::string kind = lower_copy(b.value("kind", std::string("all")));
        const std::string detail = lower_copy(b.value("detail", std::string("summary")));
        if (domain != "all" && domain != "visual" && domain != "audio")
            return err(code::kBadArg, "domain must be all, visual, or audio");
        if (detail != "summary" && detail != "full")
            return err(code::kBadArg, "detail must be summary or full");
        json ops = unified_operator_catalog(c, domain, kind);
        if (detail == "summary")   // compact listing — drop the heavy per-op schema (params/ports/keywords)
            for (auto& op : ops) { op.erase("params"); op.erase("ports"); op.erase("keywords"); }
        json r = ok();
        r["domain"] = domain;
        r["kind"] = kind;
        r["detail"] = detail;
        r["operators"] = std::move(ops);
        r["count"] = static_cast<int>(r["operators"].size());
        return r;
    };
    handlers_["find_operators"] = [](const ControlCtx& c, const json& b) {
        const std::string query = b.value("query", std::string());
        const std::string domain = lower_copy(b.value("domain", std::string("all")));
        const std::string kind = lower_copy(b.value("kind", std::string("all")));
        json matches = json::array();
        for (const auto& op : unified_operator_catalog(c, domain, kind)) {
            if (text_match(op, query)) matches.push_back(op);
        }
        json r = ok();
        r["query"] = query;
        r["matches"] = matches;
        r["count"] = static_cast<int>(matches.size());
        return r;
    };
    handlers_["find_params"] = [](const ControlCtx& c, const json& b) {
        const std::string query = b.value("query", std::string());
        const std::string scope = lower_copy(b.value("scope", std::string("all")));
        json matches = json::array();
        if (scope == "all" || scope == "visual") {
            for (const auto& d : visual_mapping_destinations(c)) {
                if (text_match(d, query)) matches.push_back(d);
            }
        }
        if (scope == "all" || scope == "audio") {
            for (const auto& d : audio_mapping_destinations(c.session)) {
                if (text_match(d, query)) matches.push_back(d);
            }
        }
        if (scope != "all" && scope != "visual" && scope != "audio")
            return err(code::kBadArg, "scope must be all, visual, or audio");
        json r = ok();
        r["query"] = query;
        r["scope"] = scope;
        r["params"] = matches;
        r["count"] = static_cast<int>(matches.size());
        return r;
    };
    // ADR-0016: the shader library. Every .wgsl/.glsl the app found, registered or not — a
    // malformed file appears here WITH its error rather than vanishing, so an agent (or a user)
    // can see why their shader is missing. The registered ones are ordinary operators, so their
    // params/ports are in list_operators like everything else.
    handlers_["list_shaders"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        json arr = json::array();
        for (const auto& e : c.app->shader_library.entries()) {
            json je = { {"name", e.name}, {"path", e.path}, {"tier", e.tier},
                        {"summary", e.summary}, {"registered", e.registered} };
            if (!e.error.empty()) je["error"] = e.error;
            arr.push_back(std::move(je));
        }
        json r = ok();
        r["shaders"] = arr;
        for (const auto& [dir, tier] : shader_search_path()) r["search_path"].push_back({{"dir", dir}, {"tier", tier}});
        return r;
    };
    // Re-walk the search path for shader files the app has not seen yet. (Edits to files it
    // already knows about are picked up automatically — this is for new files, when you would
    // rather not wait for the folder's mtime to land.)
    handlers_["reload_shaders"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kInternal, "no app context");
        const int added = c.app->shader_library.rescan(c.app->op_registry);
        json r = ok();
        r["added"] = added;
        r["count"] = static_cast<int>(c.app->shader_library.entries().size());
        return r;
    };
    // Fork-to-edit: copy a shader into the user tier under a new name and register it live, so
    // it is immediately spawnable and editable (edits hot-reload). Mirrors clone_operator for
    // compiled ops — except that here there is nothing to compile.
    handlers_["fork_shader"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kInternal, "no app context");
        const std::string src = b.value("op", std::string());
        const std::string name = b.value("new_name", std::string());
        if (src.empty() || name.empty())
            return err(code::kBadArg, "fork_shader needs \"op\" (the shader to fork) and \"new_name\"");
        std::string error;
        const std::string path = c.app->shader_library.fork(src, name, c.app->op_registry, error);
        if (path.empty()) return err(code::kBadArg, error);
        json r = ok(); r["op"] = name; r["path"] = path; return r;
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
        if (registered > 0) c.app->file_drops.rebuild(c.app->op_loaders);  // pick up any new drop handlers
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
            jt["mute"] = P::session_track_mute(s, t);   // ADR-0022 P1b.4
            jt["solo"] = P::session_track_solo(s, t);
            jt["track_out_gnid"] = P::session_track_out_gnid(s, t);   // ADR-0022 P2b.3c: this track's sink in the global id space (is_track_out; -1 if none)
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
        json r = ok(); r["tracks"] = arr;
        // ADR-0022 P1b: the master node (the session's single sink) — its gain + meters.
        r["master"] = { {"gain", P::session_master_gain(s)}, {"level", P::session_master_level(s)},
                        {"transient", P::session_master_transient(s)},
                        {"gnid", P::session_master_gnid(s)}, {"is_master", true},   // ADR-0022 P2b.3c: the sink's global node id
                        {"bands", { P::session_master_band(s, 0), P::session_master_band(s, 1), P::session_master_band(s, 2) }} };
        return r;
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
            int in = -1, id = 0; float x = 0, y = 0; g.get_op(i, in, id, x, y);
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
        if (c.vgraph) { r["active_output"] = c.vgraph->active_output_id(); r["generator"] = c.vgraph->generator(); }
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
    handlers_["inspect_bindings"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string detail = b.value("detail", std::string("summary"));
        json bindings = json::array();
        for (const auto& m : c.graph->mappings()) bindings.push_back(binding_json(c, m));
        json r = ok();
        std::ostringstream ss;
        ss << bindings.size() << " binding" << (bindings.size() == 1 ? "" : "s");
        r["summary"] = ss.str();
        r["bindings"] = bindings;
        if (detail != "summary") {
            r["sources"] = json::array({"master.level", "master.transient", "master.low", "master.mid", "master.high"});
            r["destinations"] = { {"visual", visual_mapping_destinations(c)},
                                  {"audio", audio_mapping_destinations(c.session)} };
        }
        return r;
    };
    handlers_["explain_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string src = b.value("src", std::string());
        const std::string dst = b.value("dst", std::string());
        json matches = json::array();
        for (const auto& m : c.graph->mappings()) {
            if (!src.empty() && m.source != src) continue;
            if (!dst.empty() && m.dest != dst) continue;
            json jb = binding_json(c, m);
            std::ostringstream ss;
            ss << jb["src_label"].get<std::string>() << " drives " << jb["dst_label"].get<std::string>()
               << " with amount " << m.amount;
            if (m.curve != 0.f) ss << ", curve " << m.curve;
            if (m.invert) ss << ", inverted";
            if (m.out_lo != 0.f || m.out_hi != 1.f) ss << ", output range " << m.out_lo << " to " << m.out_hi;
            jb["explanation"] = ss.str();
            matches.push_back(jb);
        }
        if (matches.empty()) return err(code::kNotFound, "no mapping matched");
        json r = ok();
        r["summary"] = matches.size() == 1 ? matches[0]["explanation"].get<std::string>()
                                           : std::to_string(matches.size()) + " mappings matched";
        r["mappings"] = matches;
        return r;
    };
    handlers_["list_mapping_destinations"] = [](const ControlCtx& c, const json& b) {
        const std::string scope = b.value("scope", std::string("all"));
        json destinations = json::array();
        if ((scope == "all" || scope == "visual") && c.graph) {
            for (auto& d : visual_mapping_destinations(c)) destinations.push_back(std::move(d));
        }
        if ((scope == "all" || scope == "audio") && c.session) {
            for (auto& d : audio_mapping_destinations(c.session)) destinations.push_back(std::move(d));
        }
        if (scope != "all" && scope != "visual" && scope != "audio")
            return err(code::kBadArg, "scope must be visual, audio, or all");
        json r = ok();
        r["scope"] = scope;
        r["destinations"] = destinations;
        r["count"] = static_cast<int>(destinations.size());
        return r;
    };
    handlers_["suggest_mappings"] = [](const ControlCtx& c, const json& b) {
        if (!c.session || !c.graph) return err(code::kNoGraph, "no session/graph");
        std::string intent = b.value("intent", std::string());
        for (auto& ch : intent) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        json suggestions = json::array();
        auto add = [&](const std::string& source, const json& dest, float score, const std::string& reason) {
            suggestions.push_back({ {"src", source}, {"src_label", source_label(c.session, source)},
                                    {"dst", dest.value("dest", std::string())},
                                    {"dst_label", dest_label(c, dest.value("dest", std::string()))},
                                    {"score", score}, {"reason", reason},
                                    {"amount", 1.0}, {"curve", 0.0}, {"invert", false} });
        };
        json visual_dests = visual_mapping_destinations(c);
        for (const auto& d : visual_dests) {
            std::string param = d.value("param", std::string());
            std::string op = d.value("op", std::string());
            std::string low = param + " " + op;
            for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (low.find("size") != std::string::npos || low.find("warp") != std::string::npos ||
                low.find("flash") != std::string::npos || low.find("glow") != std::string::npos) {
                add("master.transient", d, 0.82f, "Transient/onset energy is a good match for impact-like visual params.");
            } else if (low.find("hue") != std::string::npos || low.find("color") != std::string::npos ||
                       low.find("tint") != std::string::npos) {
                add("master.mid", d, 0.66f, "Mid-band energy is a conservative color/palette driver until richer chroma analysis exists.");
            } else if (low.find("density") != std::string::npos || low.find("amount") != std::string::npos ||
                       low.find("feedback") != std::string::npos) {
                add("master.level", d, 0.62f, "Overall loudness is a stable source for continuous visual density or amount.");
            } else if (intent.empty() || low.find(intent) != std::string::npos) {
                add("master.level", d, 0.35f, "Fallback suggestion: use overall loudness for a continuous visual response.");
            }
        }
        json r = ok();
        r["summary"] = std::to_string(suggestions.size()) + " conservative mapping suggestions";
        r["suggestions"] = suggestions;
        return r;
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
