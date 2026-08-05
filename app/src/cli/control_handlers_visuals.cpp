#include "cli/control_handlers_internal.h"

#include "ui/node_graph.h"
#include "gpu/visual_graph.h"
#include "gpu/operator_scan.h"          // load_and_register_operator (live install)
#include "packages/package_manager.h"   // install_package
#include "app/app.h"                     // op_registry + op_loaders
#include "app/node_presets.h"            // ADR-0021/P4: node presets
#include "cli/control_json.h"            // ADR-0047: port_stream_name (typed rejection message)

#include <string>
#include <vector>

namespace vivid {
namespace {

int op_index_by_id(VisualGraph* vg, int id) {
    if (!vg) return -1;
    auto& ns = vg->nodes();
    for (int i = 0; i < static_cast<int>(ns.size()); ++i) if (ns[i].id == id) return i;
    return -1;
}
// Re-run the tidy layered auto-layout after a topology change made over the control server, so a
// graph BUILT via MCP (every demo, and any agent) doesn't stack its nodes on top of each other.
// Positions live in the NodeGraph (c.graph), not the VisualGraph model, and are seeded per-frame as
// a 60px overlap cascade otherwise; layout_nodes() is the same Sugiyama layout as the "Re-layout"
// button, is topology-driven + idempotent, and the result persists (persist.cpp saves node x/y).
// UI-thread-safe: the control server dispatches handlers from process_pending on the main thread.
void tidy_layout(const ControlCtx& c) { if (c.graph) c.graph->layout_nodes(); }
}  // namespace

// Node-graph construction: add/remove/connect nodes, set generator/asset/param, data nodes.
void register_visuals_handlers(Handlers& handlers_) {
    handlers_["add_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const std::string op = b.value("op", std::string());
        OpRegistry* reg = c.vgraph->registry();
        if (!reg || !reg->has(op)) {
            std::string types; for (const auto& t : (reg ? reg->type_names() : std::vector<std::string>{})) { if (!types.empty()) types += ", "; types += t; }
            return err(code::kBadArg, "unknown op '" + op + "' (valid: " + types + ")");
        }
        const int idx = c.vgraph->add_node(op);   // operator-driven: any registered op type
        tidy_layout(c);                            // keep the MCP-built graph organized, not stacked
        json r = ok(); r["id"] = c.vgraph->nodes()[idx].id; r["index"] = idx; return r;
    };
    handlers_["remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that id");
        c.vgraph->remove_node(idx);
        tidy_layout(c);
        return ok();
    };
    handlers_["connect_nodes"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        const int in_id = b.value("input_id", -1);
        const int in_idx = (in_id < 0) ? -1 : op_index_by_id(c.vgraph, in_id);
        if (in_id >= 0 && in_idx < 0) return err(code::kNotFound, "no node with that input_id");
        const int port = b.value("port", 0);
        const int nports = c.vgraph->nodes()[idx].inst.input_port_count;
        const bool output_primary = c.vgraph->nodes()[idx].is_output() && port == 0;
        if (port < 0 || (!output_primary && port >= nports))
            return err(code::kOutOfRange, "port " + std::to_string(port) + " out of range [0," + std::to_string(nports) + ")");
        const int src_port = b.value("src_port", 0);   // which OUTPUT of the source (multi-lane producers)
        // ADR-0047: typed connection validation — reject a wire whose stream types don't match (the
        // visual peer of the audio note-edge check). Only when actually connecting (in_idx>=0); a
        // disconnect (input_id<0) always passes.
        if (in_idx >= 0 && !c.vgraph->can_connect(idx, port, in_idx, src_port)) {
            const VividPortDescriptor* od = c.vgraph->output_port_desc(in_idx, src_port);
            const VividPortDescriptor* id = c.vgraph->input_port_desc(idx, port);
            const char* os = od ? control_json::port_stream_name(*od) : nullptr;
            const char* is = id ? control_json::port_stream_name(*id) : nullptr;
            return err(code::kBadArg, std::string("incompatible ports: ") + (os ? os : "?") + " -> " + (is ? is : "?"));
        }
        c.vgraph->set_input(idx, port, in_idx, src_port);   // N-input: wire src's out `src_port` -> node's input `port`
        tidy_layout(c);                            // re-tidy: node positions depend on the edges
        return ok();
    };
    // Point a node (e.g. a CustomShader) at a data asset — a project-relative .glsl
    // resolved against the loaded project dir. Empty clears it. The op (re)loads on the
    // next frame and degrades to a no-op if the file is missing or fails to compile.
    handlers_["set_node_asset"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that id");
        c.graph->set_op_asset_at(idx, b.value("asset", std::string()));
        json r = ok(); r["id"] = b.value("id", -1); r["asset"] = c.graph->op_asset_at(idx); return r;
    };
    handlers_["set_generator"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const std::string op = b.value("op", std::string());
        // Any registered SOURCE op is a valid answer now — not a hardcoded five-name enum.
        if (!c.vgraph->registry() || !c.vgraph->registry()->has(op))
            return err(code::kBadArg, "unknown operator '" + op + "'");
        if (!c.vgraph->type_is_source(op))
            return err(code::kBadArg, "'" + op + "' is not a source operator (it declares texture inputs, "
                                      "so it cannot head the chain)");
        if (!c.vgraph->set_generator(op))
            return err(code::kNotFound, "the graph has no source node to replace");
        return ok();
    };
    handlers_["set_active_output"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that id");
        c.vgraph->set_active_output(idx);
        return ok();
    };
    handlers_["set_node_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        const std::string name = b.value("name", std::string());
        int local = -1;
        for (int l = 0; l < c.graph->op_param_count_at(idx); ++l)
            if (name == c.graph->op_param_label_at(idx, l)) { local = l; break; }
        if (local < 0) return err(code::kNotFound, "no param '" + name + "' on that node");
        c.graph->set_op_param_base_at(idx, local, b.value("value", 0.f));
        return ok();   // undo capture happens centrally at the dispatch table (ADR-0017/G2)
    };
    // Set a FILE/TEXT param's string value (e.g. an Image node's file path).
    handlers_["set_node_file_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        const std::string name = b.value("name", std::string());
        int local = -1;
        for (int l = 0; l < c.graph->op_param_count_at(idx); ++l)
            if (name == c.graph->op_param_label_at(idx, l)) { local = l; break; }
        if (local < 0) return err(code::kNotFound, "no param '" + name + "' on that node");
        c.graph->set_op_file_param_at(idx, local, b.value("value", std::string()));
        json r = ok(); r["value"] = c.graph->op_file_param_at(idx, local); return r;
    };
    // ADR-0021/P4 — node presets: named param snapshots per node, distinct from the plugin
    // list_presets/load_preset flow (that one loads opaque per-instrument binary state).
    handlers_["save_node_preset"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        const std::string name = b.value("name", std::string());
        if (name.empty()) return err(code::kBadArg, "save_node_preset needs a \"name\"");
        const std::string op_type = c.graph->op_type_at(idx);
        std::string e2;
        const std::string path = node_presets::save(op_type, name,
                                                    node_presets::capture(*c.graph, idx), e2);
        if (path.empty()) return err(code::kBadArg, e2);
        json r = ok(); r["op_type"] = op_type; r["name"] = name; r["path"] = path; return r;
    };
    handlers_["list_node_presets"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        // Accept either a node_id (use its op type) or an explicit op_type.
        std::string op_type = b.value("op_type", std::string());
        if (op_type.empty()) {
            const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
            if (idx < 0) return err(code::kBadArg, "list_node_presets needs \"op_type\" or a valid \"node_id\"");
            op_type = c.graph->op_type_at(idx);
        }
        json arr = json::array();
        for (const auto& p : node_presets::list(op_type))
            arr.push_back({ {"name", p.name}, {"factory", p.factory} });
        json r = ok(); r["op_type"] = op_type; r["presets"] = arr; return r;
    };
    handlers_["load_node_preset"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        const std::string name = b.value("name", std::string());
        if (name.empty()) return err(code::kBadArg, "load_node_preset needs a \"name\"");
        const std::string op_type = c.graph->op_type_at(idx);
        const json preset = node_presets::load(op_type, name);
        if (preset.is_null()) return err(code::kNotFound, "no preset '" + name + "' for " + op_type);
        const int applied = node_presets::apply(*c.graph, idx, preset);
        json r = ok(); r["applied"] = applied; r["name"] = name; return r;
    };
    handlers_["add_data_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string src = b.value("source", std::string());
        const int cid = char_id_from_source(src);
        if (cid < 0) return err(code::kBadArg, "bad source (e.g. master.transient, track_2.low)");
        c.graph->add_data_node(src, cid);
        return ok();
    };

    // ADR-0033 P5: per-node label — a user rename shown instead of op_type (mirrors set_scene_name).
    handlers_["set_node_name"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph || !c.vgraph) return err(code::kNoGraph, "no graph");
        const int idx = op_index_by_id(c.vgraph, b.value("node_id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that node_id");
        c.graph->set_op_name_at(idx, b.value("name", std::string()));
        json r = ok(); r["node_id"] = b.value("node_id", -1); r["name"] = c.graph->op_name_at(idx); return r;
    };

    // ADR-0033 P5: sticky-note annotations — persisted, MCP-addressable explainability text.
    handlers_["add_annotation"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const int id = c.graph->add_annotation(b.value("x", 560.f), b.value("y", 488.f));
        if (b.contains("text")) c.graph->set_annotation_text(id, b.value("text", std::string()));
        json r = ok(); r["id"] = id; return r;
    };
    handlers_["set_annotation_text"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        if (!c.graph->set_annotation_text(b.value("id", -1), b.value("text", std::string())))
            return err(code::kNotFound, "no annotation with that id");
        return ok();
    };
    handlers_["move_annotation"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        if (!c.graph->move_annotation(b.value("id", -1), b.value("x", 0.f), b.value("y", 0.f)))
            return err(code::kNotFound, "no annotation with that id");
        return ok();
    };
    handlers_["remove_annotation"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        if (!c.graph->remove_annotation(b.value("id", -1)))
            return err(code::kNotFound, "no annotation with that id");
        return ok();
    };

    // ADR-0033 P2: duplicate a set of visual nodes. Each copy gets a fresh id at a small offset,
    // edges strictly between the copied nodes are recreated, edges to outside nodes are dropped, and
    // incoming audio→param mappings are replicated. Returns the new node ids.
    handlers_["duplicate_nodes"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        std::set<int> ids;
        if (b.contains("ids") && b["ids"].is_array())
            for (const auto& j : b["ids"]) if (j.is_number_integer()) ids.insert(j.get<int>());
        if (ids.empty()) return err(code::kBadArg, "ids: expected a non-empty array of node ids");
        const float dx = b.value("dx", 24.f), dy = b.value("dy", 24.f);
        const std::vector<int> new_ids = c.graph->spawn_clip(c.graph->capture_ids(ids), dx, dy, "Duplicate Nodes");
        if (new_ids.empty()) return err(code::kNotFound, "no duplicable nodes among those ids");
        json r = ok(); r["ids"] = new_ids; return r;
    };

}

}  // namespace vivid
