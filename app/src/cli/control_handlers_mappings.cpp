#include "cli/control_handlers_internal.h"
#include "cli/mapping_request.h"

#include "ui/node_graph.h"   // NodeGraph::add_mapping / disconnect_dest
#include "gpu/visual_graph.h"

#include <string>

namespace vivid {
namespace {

namespace P = vivid::session;

const char* safe_cstr(const char* s) { return s ? s : ""; }

int track_index_from_request(P::Session* s, const json& b, json& e) {
    if (!s) { e = err(code::kNoSession, "no session"); return -1; }
    const int n = P::session_track_count(s);
    if (b.contains("track")) {
        const int t = b.value("track", -1);
        if (in_range(t, n)) return t;
        e = err(code::kOutOfRange, "track " + std::to_string(t) + " out of range [0," + std::to_string(n) + ")");
        return -1;
    }
    if (b.contains("track_id")) {
        const int id = b.value("track_id", -1);
        for (int t = 0; t < n; ++t) if (P::session_track_id(s, t) == id) return t;
        e = err(code::kNotFound, "no track with stable id " + std::to_string(id));
        return -1;
    }
    if (b.contains("track_name")) {
        const std::string want = control_mapping::lower_copy(b.value("track_name", std::string()));
        for (int t = 0; t < n; ++t) {
            if (control_mapping::lower_copy(safe_cstr(P::session_track_name(s, t))) == want) return t;
        }
        e = err(code::kNotFound, "no track named '" + b.value("track_name", std::string()) + "'");
        return -1;
    }
    e = err(code::kBadArg, "track source needs track, track_id, or track_name");
    return -1;
}

int op_index_by_id(VisualGraph* vg, int id) {
    if (!vg) return -1;
    auto& ns = vg->nodes();
    for (int i = 0; i < static_cast<int>(ns.size()); ++i) if (ns[i].id == id) return i;
    return -1;
}

}  // namespace

// The bridge: wire a characteristic source to a param destination (with amount/curve/invert/range),
// or tear a destination's mapping down.
void register_mappings_handlers(Handlers& handlers_) {
    handlers_["connect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string src = b.value("src", std::string()), dst = b.value("dst", std::string());
        if (src.empty() || dst.empty()) return err(code::kBadArg, "need src and dst");
        c.graph->add_mapping(src, dst, b.value("amount", 1.0f), b.value("curve", 0.0f),
                             b.value("invert", false), b.value("lo", 0.0f), b.value("hi", 1.0f),
                             b.value("attack", 0.0f), b.value("release", 0.0f));
        return ok();
    };
    // First-class tutorial/agent path for the common bridge move: "map this audio
    // characteristic to this visual param." It validates the source and destination, then writes
    // the same canonical mapping strings as connect_mapping.
    handlers_["map_audio_to_visual_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string source_scope = control_mapping::lower_copy(b.value("source", std::string("track")));
        const bool master = source_scope == "master";
        if (!master && source_scope != "track")
            return err(code::kBadArg, "source must be 'track' or 'master'");

        const std::string characteristic = control_mapping::lower_copy(b.value("characteristic", b.value("kind", std::string())));
        if (characteristic.empty()) return err(code::kBadArg, "need characteristic (try list_mapping_sources)");
        if (!control_mapping::valid_audio_characteristic(characteristic, master))
            return err(code::kBadArg, master
                ? "master characteristic must be one of: level, transient, low, mid, high"
                : "track characteristic must be one of: level, transient, low, mid, high, note, velocity, gate");

        std::string src;
        json source_info = json::object();
        if (master) {
            src = control_mapping::master_source(characteristic);
            source_info = { {"source", "master"}, {"characteristic", characteristic}, {"label", "master " + characteristic} };
        } else {
            json e;
            const int track = track_index_from_request(c.session, b, e);
            if (track < 0) return e;
            const int stable_id = P::session_track_id(c.session, track);
            const std::string track_name = safe_cstr(P::session_track_name(c.session, track));
            src = control_mapping::track_source(stable_id, characteristic);
            source_info = { {"source", "track"}, {"track", track}, {"track_id", stable_id},
                            {"track_name", track_name},
                            {"characteristic", characteristic},
                            {"label", track_name + " " + characteristic} };
        }

        const int node_id = b.value("node_id", b.value("id", -1));
        const int idx = op_index_by_id(c.vgraph, node_id);
        if (idx < 0) return err(code::kNotFound, "no visual node with node_id " + std::to_string(node_id));
        const std::string param = b.value("param", std::string());
        if (param.empty()) return err(code::kBadArg, "need visual param name (try list_mapping_destinations with scope=visual)");
        int local = -1;
        for (int l = 0; l < c.graph->op_param_count_at(idx); ++l)
            if (param == c.graph->op_param_label_at(idx, l)) { local = l; break; }
        if (local < 0) {
            json params = json::array();
            for (int l = 0; l < c.graph->op_param_count_at(idx); ++l)
                params.push_back(c.graph->op_param_label_at(idx, l));
            json r = err(code::kNotFound, "visual node " + std::to_string(node_id) + " has no param '" + param + "'");
            r["available_params"] = params;
            return r;
        }

        const std::string dst = "node:" + std::to_string(node_id) + "." + param;
        const float amount = b.value("amount", 1.0f);
        const float curve = b.value("curve", 0.0f);
        const bool invert = b.value("invert", false);
        const float lo = b.value("lo", 0.0f);
        const float hi = b.value("hi", 1.0f);
        c.graph->add_mapping(src, dst, amount, curve, invert, lo, hi);

        json r = ok();
        r["src"] = src;
        r["dst"] = dst;
        r["source"] = source_info;
        r["destination"] = { {"dest", dst}, {"node_id", node_id}, {"op", c.graph->op_kind_name(idx)},
                             {"param", param}, {"range", { c.graph->op_param_min_at(idx, local),
                                                            c.graph->op_param_max_at(idx, local) }} };
        r["amount"] = amount;
        r["curve"] = curve;
        r["invert"] = invert;
        r["lo"] = lo;
        r["hi"] = hi;
        r["summary"] = source_info.value("label", src) + " drives " +
                       std::string(c.graph->op_kind_name(idx)) + "." + param;
        return r;
    };
    handlers_["disconnect_mapping"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string dst = b.value("dst", std::string());
        if (dst.empty()) return err(code::kBadArg, "need dst");
        c.graph->disconnect_dest(dst);
        return ok();
    };
}

}  // namespace vivid
