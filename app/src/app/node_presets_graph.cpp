// Graph half of node presets: capture()/apply() over a live NodeGraph. Split from the storage half
// (node_presets.cpp) so the storage functions link into the headless test without pulling NodeGraph.
#include "app/node_presets.h"

#include "ui/node_graph.h"
#include "operator_api/types.h"     // VIVID_PARAM_FILE / VIVID_PARAM_TEXT

#include <string>
#include <utility>
#include <vector>

namespace vivid::node_presets {

namespace {
bool is_file_param(int type) { return type == VIVID_PARAM_FILE || type == VIVID_PARAM_TEXT; }
}  // namespace

nlohmann::json capture(const ui::NodeGraph& g, int idx) {
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json file_params = nlohmann::json::object();
    for (int l = 0; l < g.op_param_count_at(idx); ++l) {
        const char* name = g.op_param_label_at(idx, l);
        if (!name || !*name) continue;
        if (is_file_param(g.op_param_type_at(idx, l))) {
            const char* fv = g.op_file_param_at(idx, l);
            if (fv && fv[0]) file_params[name] = fv;
        } else {
            params[name] = g.op_param_base_at(idx, l);
        }
    }
    return { {"op_type", g.op_type_at(idx)}, {"params", params}, {"file_params", file_params} };
}

int apply(ui::NodeGraph& g, int idx, const nlohmann::json& preset) {
    if (!preset.is_object()) return 0;
    // Build a name -> local-index map once so applying is O(params), not O(params^2).
    std::vector<std::pair<std::string, int>> by_name;
    for (int l = 0; l < g.op_param_count_at(idx); ++l)
        if (const char* n = g.op_param_label_at(idx, l); n && *n) by_name.emplace_back(n, l);
    auto find_local = [&](const std::string& n) -> int {
        for (const auto& [nm, l] : by_name) if (nm == n) return l;
        return -1;   // a param the node no longer has: silently skipped (header edits are allowed)
    };
    int applied = 0;
    if (auto it = preset.find("params"); it != preset.end() && it->is_object())
        for (const auto& [name, val] : it->items()) {
            const int l = find_local(name);
            if (l >= 0 && val.is_number()) { g.set_op_param_base_at(idx, l, val.get<float>()); ++applied; }
        }
    if (auto it = preset.find("file_params"); it != preset.end() && it->is_object())
        for (const auto& [name, val] : it->items()) {
            const int l = find_local(name);
            if (l >= 0 && val.is_string()) { g.set_op_file_param_at(idx, l, val.get<std::string>()); ++applied; }
        }
    return applied;
}

}  // namespace vivid::node_presets
