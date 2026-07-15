#include "cli/control_handlers_internal.h"

#include "ui/node_graph.h"
#include "gpu/visual_graph.h"
#include "gpu/operator_scan.h"          // load_and_register_operator (live install)
#include "packages/package_manager.h"   // install_package
#include "app/app.h"                     // op_registry + op_loaders
#include "app/edit_gateway.h"            // ADR-0017 note_edit (G1: set_node_param)

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
        json r = ok(); r["id"] = c.vgraph->nodes()[idx].id; r["index"] = idx; return r;
    };
    handlers_["remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no vgraph");
        const int idx = op_index_by_id(c.vgraph, b.value("id", -1));
        if (idx < 0) return err(code::kNotFound, "no node with that id");
        c.vgraph->remove_node(idx);
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
        if (port < 0 || port >= nports)
            return err(code::kOutOfRange, "port " + std::to_string(port) + " out of range [0," + std::to_string(nports) + ")");
        c.vgraph->set_input(idx, port, in_idx);   // N-input: wire src -> node's texture input `port`
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
    handlers_["add_data_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.graph) return err(code::kNoGraph, "no graph");
        const std::string src = b.value("source", std::string());
        const int cid = char_id_from_source(src);
        if (cid < 0) return err(code::kBadArg, "bad source (e.g. master.transient, track_2.low)");
        c.graph->add_data_node(src, cid);
        return ok();
    };

}

}  // namespace vivid
