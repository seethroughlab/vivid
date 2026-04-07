#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/style/ui_style.h"
#include "common/topo_sort.h"
#include "common/string_util.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace vivid::ui {

using vivid::format_float;
using vivid::format_int;
using vivid::format_uint;
using vivid::kahn_sort;

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
NodeGraphUI::NodeGraphUI(UICommandSink& commands)
    : commands_(commands), dialogs_(commands) {
    // Initialize with default Dark Steel style
    auto styles = builtin_styles();
    if (!styles.empty()) style_ = styles[0];
    // Give DialogManager access to live style and pan gesture for preferences
    dialogs_.set_style_ptr(&style_);
    dialogs_.set_pan_gesture_ptr(&pan_gesture_);
}

bool NodeGraphUI::select_single_node_for_review(const std::string& node_id) {
    if (!snap_.has_node(node_id))
        return false;
    selected_node_ids_.clear();
    selected_node_ids_.insert(node_id);
    selected_wire_idx_ = -1;
    pending_select_node_id_.clear();
    inspector_.insp_scroll_y = 0.0f;
    inspector_.insp_scroll_node_id.clear();
    return true;
}

std::string NodeGraphUI::next_available_node_id(
    const std::string& base,
    const std::unordered_set<std::string>& reserved) const {
    std::string candidate = base;
    if (candidate.empty())
        candidate = "node";
    auto id_available = [&](const std::string& id) {
        return !snap_.has_node(id) && reserved.count(id) == 0;
    };
    if (id_available(candidate))
        return candidate;
    for (int n = 1;; ++n) {
        candidate = base + "_copy";
        if (n > 1)
            candidate += std::to_string(n);
        if (id_available(candidate))
            return candidate;
    }
}

void NodeGraphUI::copy_selected_nodes() {
    clipboard_nodes_.clear();
    clipboard_connections_.clear();
    if (!snap_valid_ || selected_node_ids_.empty())
        return;

    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
    bool anchor_set = false;
    for (const auto& node_id : selected_node_ids_) {
        const auto* ns = snap_.find_node(node_id);
        if (!ns)
            continue;
        float x = ns->has_layout ? ns->layout_x : 0.0f;
        float y = ns->has_layout ? ns->layout_y : 0.0f;
        if (!anchor_set) {
            anchor_x = x;
            anchor_y = y;
            anchor_set = true;
        } else {
            anchor_x = std::min(anchor_x, x);
            anchor_y = std::min(anchor_y, y);
        }
    }
    if (!anchor_set)
        return;

    for (const auto& node_id : selected_node_ids_) {
        const auto* ns = snap_.find_node(node_id);
        if (!ns)
            continue;
        ClipboardNode entry;
        entry.node = *ns;
        float x = ns->has_layout ? ns->layout_x : 0.0f;
        float y = ns->has_layout ? ns->layout_y : 0.0f;
        entry.rel_x = x - anchor_x;
        entry.rel_y = y - anchor_y;
        clipboard_nodes_.push_back(std::move(entry));
    }

    for (const auto& conn : snap_.connections) {
        if (selected_node_ids_.count(conn.from_node) &&
            selected_node_ids_.count(conn.to_node)) {
            clipboard_connections_.push_back(conn);
        }
    }
}

void NodeGraphUI::paste_copied_nodes() {
    if (clipboard_nodes_.empty())
        return;

    float base_x = 0.0f;
    float base_y = 0.0f;
    if (!graph_position_for_screen(mouse_.x, mouse_.y, base_x, base_y))
        graph_center_position(base_x, base_y);

    std::unordered_map<std::string, std::string> id_map;
    std::unordered_set<std::string> reserved_ids;
    std::unordered_set<std::string> pasted_ids;
    for (const auto& copied : clipboard_nodes_) {
        std::string new_id = next_available_node_id(copied.node.node_id, reserved_ids);
        reserved_ids.insert(new_id);
        id_map[copied.node.node_id] = new_id;
        pasted_ids.insert(new_id);

        std::string add_error;
        if (!commands_.try_add_node(copied.node.type_name, new_id, &add_error)) {
            std::fprintf(stderr, "[vivid] Paste add node failed for '%s': %s\n",
                         copied.node.type_name.c_str(), add_error.c_str());
            continue;
        }

        // Keep pasted nodes visibly offset from the source selection so redo
        // restores a readable result instead of stacking copies on top.
        commands_.set_node_layout(new_id, base_x + copied.rel_x + 220.0f,
                                  base_y + copied.rel_y + 120.0f);

        if (copied.node.op_info) {
            for (const auto& pd : copied.node.op_info->params) {
                auto pi_it = copied.node.param_indices.find(pd.name);
                if (pd.type == VIVID_PARAM_TEXT) {
                    auto text_it = copied.node.file_param_values.find(pd.name);
                    if (text_it != copied.node.file_param_values.end())
                        commands_.set_string_param(new_id, pd.name, text_it->second);
                    continue;
                }
                if (pi_it != copied.node.param_indices.end() &&
                    pi_it->second < copied.node.param_values.size()) {
                    commands_.set_param(new_id, pd.name, copied.node.param_values[pi_it->second]);
                }
            }
        }
    }

    for (const auto& conn : clipboard_connections_) {
        auto from_it = id_map.find(conn.from_node);
        auto to_it = id_map.find(conn.to_node);
        if (from_it == id_map.end() || to_it == id_map.end())
            continue;
        commands_.connect(from_it->second + "/" + conn.from_port,
                          to_it->second + "/" + conn.to_port);
    }

    if (!pasted_ids.empty()) {
        selected_node_ids_ = std::move(pasted_ids);
        selected_wire_idx_ = -1;
    }
}

void NodeGraphUI::open_clone_confirm_dialog(const std::string& type_name, const std::string& node_id) {
    dialogs_.open_clone_confirm(type_name, node_id);
}

void NodeGraphUI::open_save_confirm_dialog(SaveConfirmAction action) {
    dialogs_.open_save_confirm(action);
}

float NodeGraphUI::graph_right() const {
    return has_selection() ? inspector_x() : static_cast<float>(win_w_);
}

float NodeGraphUI::graph_bottom() const {
    float h = static_cast<float>(win_h_);
    h -= build_console_panel_.panel_height();
    if (session_grid_open_) h -= kSessionStripH;
    return h;
}

float NodeGraphUI::session_strip_top() const {
    return static_cast<float>(win_h_) - kSessionStripH;
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
std::vector<std::pair<uint32_t, std::string>> NodeGraphUI::sorted_ports(
    const std::unordered_map<std::string, uint32_t>& port_indices) {
    std::vector<std::pair<uint32_t, std::string>> result;
    for (const auto& [name, idx] : port_indices)
        result.push_back({idx, name});
    std::sort(result.begin(), result.end());
    return result;
}

template<typename RectT>
int NodeGraphUI::hit_test_rect(const std::vector<RectT>& rects, float mx, float my) {
    for (int i = 0; i < static_cast<int>(rects.size()); ++i) {
        const auto& r = rects[i];
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h)
            return i;
    }
    return -1;
}

// Explicit instantiations for rect types used across translation units
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::InspectorRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::ResolutionRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::MidiRemoveRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::MidiRangeRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::XYPadRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::ColorSwatchRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::StatePresetRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorController::StateHeaderRect>& rects, float mx, float my);

// -----------------------------------------------------------------------
// Port visibility helpers
// -----------------------------------------------------------------------

// Check if a port has an active wire connection
static bool port_has_connection(const std::vector<ConnectionSnapshot>& conns,
                                const std::string& node_id,
                                const std::string& port_name,
                                bool is_output) {
    for (const auto& c : conns) {
        if (is_output) {
            if (c.from_node == node_id && c.from_port == port_name) return true;
        } else {
            if (c.to_node == node_id && c.to_port == port_name) return true;
        }
    }
    return false;
}

uint32_t NodeGraphUI::count_visible_input_ports(const NodeSnapshot& ns, bool show_params) const {
    // Signal input ports are always visible
    uint32_t count = static_cast<uint32_t>(ns.input_port_indices.size());
    // Param inputs only visible if connected (and param wires shown)
    if (show_params) {
        for (const auto& [name, idx] : ns.param_indices) {
            if (ns.input_port_indices.count(name)) continue; // already counted as signal port
            if (port_has_connection(snap_.connections, ns.node_id, name, false))
                count++;
        }
    }
    return count;
}

uint32_t NodeGraphUI::count_visible_output_ports(const NodeSnapshot& ns, bool show_params) const {
    size_t regular_count = ns.output_port_indices.size() - ns.analysis_output_port_indices.size();
    bool few_outputs = regular_count <= 3;
    bool expanded    = outputs_expanded_.count(ns.node_id) > 0;
    bool show_all    = few_outputs || expanded;

    uint32_t count = 0;
    for (const auto& [name, idx] : ns.output_port_indices) {
        if (ns.analysis_output_port_indices.count(name)) continue;
        if (show_all || port_has_connection(snap_.connections, ns.node_id, name, true))
            count++;
    }
    // Affordance row always reserves one row for nodes with >3 outputs
    if (!few_outputs)
        count++;
    // Param sources — visible only if connected as source (and param wires shown)
    if (show_params) {
        for (const auto& [name, idx] : ns.param_indices) {
            if (ns.output_port_indices.count(name)) continue;
            if (port_has_connection(snap_.connections, ns.node_id, name, true))
                count++;
        }
    }
    // Analysis ports (rms/peak/waveform) — visible only if connected
    for (const auto& [name, idx] : ns.analysis_output_port_indices) {
        if (port_has_connection(snap_.connections, ns.node_id, name, true))
            count++;
    }
    return count;
}

// -----------------------------------------------------------------------
// Port position helper
// -----------------------------------------------------------------------
void NodeGraphUI::recompute_ports(NodeRect& rect, const NodeSnapshot& ns) {
    bool has_ct = custom_thumb_nodes_.count(rect.node_id) > 0;
    float body_h = node_body_height(rect.is_gpu, rect.active_cadence, has_ct);

    rect.inputs.clear();
    rect.outputs.clear();

    auto sorted_inputs = sorted_ports(ns.input_port_indices);
    auto sorted_outputs_vec = sorted_ports(ns.output_port_indices);
    size_t regular_output_count = ns.output_port_indices.size() - ns.analysis_output_port_indices.size();
    bool few_outputs = regular_output_count <= 3;
    bool expanded    = outputs_expanded_.count(ns.node_id) > 0;
    bool show_all    = few_outputs || expanded;

    float port_start_y = rect.y + kAccentBarH + body_h + kNodePadY + kLineH * 2;

    // Input signal ports — always visible
    size_t pi = 0;
    for (; pi < sorted_inputs.size(); ++pi) {
        float py = port_start_y + pi * kLineH + kLineH * 0.5f;
        rect.inputs.push_back({sorted_inputs[pi].second, rect.x, py, false});
    }

    // Parameter inputs — only visible if connected and param wires shown
    if (show_param_wires_) {
        std::vector<std::pair<uint32_t, std::string>> sorted_params;
        for (const auto& [name, idx] : ns.param_indices)
            if (!ns.input_port_indices.count(name)) sorted_params.push_back({idx, name});
        std::sort(sorted_params.begin(), sorted_params.end());
        for (const auto& [idx, name] : sorted_params) {
            if (!port_has_connection(snap_.connections, ns.node_id, name, false))
                continue;
            float py = port_start_y + pi * kLineH + kLineH * 0.5f;
            rect.inputs.push_back({name, rect.x, py, true});
            ++pi;
        }
    }

    // Output ports — show all when few or expanded, otherwise only connected
    size_t oi = 0;
    for (const auto& [idx, name] : sorted_outputs_vec) {
        if (ns.analysis_output_port_indices.count(name)) continue;
        bool connected = port_has_connection(snap_.connections, ns.node_id, name, true);
        if (!show_all && !connected)
            continue;
        float py = port_start_y + oi * kLineH + kLineH * 0.5f;
        rect.outputs.push_back({name, rect.x + rect.w, py, false});
        ++oi;
    }

    // Affordance row: present for all nodes with >3 outputs (collapsed or expanded)
    rect.outputs_expandable  = !few_outputs;
    rect.outputs_expanded    = expanded;
    rect.hidden_output_count = 0;
    rect.affordance_gy       = 0;
    if (!few_outputs) {
        uint32_t total = static_cast<uint32_t>(regular_output_count);
        rect.hidden_output_count = expanded ? 0 : total - static_cast<uint32_t>(oi);
        rect.affordance_gy = port_start_y + oi * kLineH + kLineH * 0.5f;
        ++oi; // reserve the row so param sources appear below
    }

    // Param sources — visible only if connected as a source (and param wires shown)
    if (show_param_wires_) {
        std::vector<std::pair<uint32_t, std::string>> src_params;
        for (const auto& [name, idx] : ns.param_indices)
            if (!ns.output_port_indices.count(name)) src_params.push_back({idx, name});
        std::sort(src_params.begin(), src_params.end());
        for (const auto& [idx, name] : src_params) {
            if (!port_has_connection(snap_.connections, ns.node_id, name, true))
                continue;
            float py = port_start_y + oi * kLineH + kLineH * 0.5f;
            rect.outputs.push_back({name, rect.x + rect.w, py, true});
            ++oi;
        }
    }

    // Analysis ports (rms/peak/waveform) — visible only if connected
    std::vector<std::pair<uint32_t, std::string>> analysis_ports;
    for (const auto& [name, idx] : ns.analysis_output_port_indices)
        analysis_ports.push_back({idx, name});
    std::sort(analysis_ports.begin(), analysis_ports.end());
    for (const auto& [idx, name] : analysis_ports) {
        if (!port_has_connection(snap_.connections, ns.node_id, name, true))
            continue;
        float py = port_start_y + oi * kLineH + kLineH * 0.5f;
        rect.outputs.push_back({name, rect.x + rect.w, py, false});
        ++oi;
    }
}

// -----------------------------------------------------------------------
// Sugiyama-inspired auto-layout
// -----------------------------------------------------------------------
void NodeGraphUI::layout_nodes(bool force) {
    node_rects_.clear();
    const auto& nodes = snap_.nodes;
    const auto& conns = snap_.connections;
    if (nodes.empty()) return;

    // Build node_id -> index map
    std::unordered_map<std::string, uint32_t> id_to_idx;
    for (uint32_t i = 0; i < nodes.size(); ++i)
        id_to_idx[nodes[i].node_id] = i;

    // Build adjacency (predecessors list for longest-path layer assignment)
    std::vector<std::vector<uint32_t>> preds(nodes.size());
    std::vector<std::vector<uint32_t>> succs(nodes.size());
    for (const auto& c : conns) {
        auto fi = id_to_idx.find(c.from_node);
        auto ti = id_to_idx.find(c.to_node);
        if (fi != id_to_idx.end() && ti != id_to_idx.end()) {
            preds[ti->second].push_back(fi->second);
            succs[fi->second].push_back(ti->second);
        }
    }

    // Topological order
    uint32_t nn = static_cast<uint32_t>(nodes.size());
    std::vector<uint32_t> in_degree(nn);
    for (uint32_t i = 0; i < nn; ++i)
        in_degree[i] = static_cast<uint32_t>(preds[i].size());

    auto topo_order = kahn_sort(nn, succs, in_degree, /*soft_on_cycle=*/true);

    // Layer assignment: longest path from sources
    std::vector<int> layer(nodes.size(), 0);
    for (uint32_t idx : topo_order) {
        for (uint32_t p : preds[idx]) {
            layer[idx] = std::max(layer[idx], layer[p] + 1);
        }
    }

    // Group nodes by layer
    int max_layer = *std::max_element(layer.begin(), layer.end());
    std::vector<std::vector<uint32_t>> layers(max_layer + 1);
    for (uint32_t i = 0; i < nodes.size(); ++i)
        layers[layer[i]].push_back(i);

    // Barycenter crossing reduction: 4 passes (forward + backward)
    for (int pass = 0; pass < 4; ++pass) {
        if (pass % 2 == 0) {
            for (int l = 1; l <= max_layer; ++l) {
                std::vector<std::pair<float, uint32_t>> bary;
                for (uint32_t n : layers[l]) {
                    float sum = 0; int count = 0;
                    for (uint32_t p : preds[n]) {
                        auto& prev = layers[l - 1];
                        for (int j = 0; j < static_cast<int>(prev.size()); ++j) {
                            if (prev[j] == p) { sum += j; count++; break; }
                        }
                    }
                    float bc = (count > 0) ? sum / count : 0.0f;
                    bary.push_back({bc, n});
                }
                std::stable_sort(bary.begin(), bary.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
                for (size_t j = 0; j < bary.size(); ++j)
                    layers[l][j] = bary[j].second;
            }
        } else {
            for (int l = max_layer - 1; l >= 0; --l) {
                std::vector<std::pair<float, uint32_t>> bary;
                for (uint32_t n : layers[l]) {
                    float sum = 0; int count = 0;
                    for (uint32_t s : succs[n]) {
                        auto& next = layers[l + 1];
                        for (int j = 0; j < static_cast<int>(next.size()); ++j) {
                            if (next[j] == s) { sum += j; count++; break; }
                        }
                    }
                    float bc = (count > 0) ? sum / count : 0.0f;
                    bary.push_back({bc, n});
                }
                std::stable_sort(bary.begin(), bary.end(),
                    [](const auto& a, const auto& b) { return a.first < b.first; });
                for (size_t j = 0; j < bary.size(); ++j)
                    layers[l][j] = bary[j].second;
            }
        }
    }

    // Compute node rects
    node_rects_.resize(nodes.size());
    for (int l = 0; l <= max_layer; ++l) {
        float col_x = kLeftMargin + l * kColSpacing;
        float total_h = 0;

        // First pass: compute heights
        std::vector<float> heights(layers[l].size());
        for (size_t r = 0; r < layers[l].size(); ++r) {
            uint32_t ni = layers[l][r];
            const auto& ns = nodes[ni];

            bool has_ct = custom_thumb_nodes_.count(ns.node_id) > 0;
            float body_h = node_body_height(ns.is_gpu, ns.active_cadence, has_ct);

            uint32_t n_inputs = count_visible_input_ports(ns, show_param_wires_);
            uint32_t n_outputs = count_visible_output_ports(ns, show_param_wires_);
            uint32_t port_rows = std::max(n_inputs, n_outputs);
            float h = kAccentBarH + body_h + kNodePadY + kLineH * 2 + port_rows * kLineH + kNodePadY;
            heights[r] = h;
            total_h += h;
        }
        total_h += (layers[l].size() > 1 ? (layers[l].size() - 1) : 0) * kRowSpacing;

        // Center vertically in graph area
        float start_y = kTopMargin + (static_cast<float>(win_h_) - 2 * kTopMargin - total_h) * 0.5f;
        if (start_y < kTopMargin) start_y = kTopMargin;

        float cur_y = start_y;
        for (size_t r = 0; r < layers[l].size(); ++r) {
            uint32_t ni = layers[l][r];
            const auto& ns = nodes[ni];
            auto& rect = node_rects_[ni];
            rect.node_id = ns.node_id;
            rect.type_name = ns.type_name;
            rect.active_cadence = ns.active_cadence;
            rect.is_gpu = ns.is_gpu;
            rect.lane_behavior = ns.lane_behavior;
            rect.x = col_x;
            rect.y = cur_y;
            rect.w = kNodeW;
            rect.h = heights[r];
            rect.target_h = heights[r];

            // Override with saved layout position if present (unless forced)
            if (ns.has_layout && !force) {
                rect.x = ns.layout_x;
                rect.y = ns.layout_y;
            }

            recompute_ports(rect, ns);

            cur_y += heights[r] + kRowSpacing;
        }
    }

    last_node_count_ = nodes.size();
    last_conn_count_ = conns.size();
    first_layout_done_ = true;
}

// -----------------------------------------------------------------------
// Reposition unconnected output sinks to the right edge of the window.
// Called from draw() after win_w_/win_h_ are known.
// -----------------------------------------------------------------------
void NodeGraphUI::reposition_output_sinks() {
    const auto& nodes = snap_.nodes;
    const auto& conns = snap_.connections;

    // Build connectivity sets
    std::unordered_set<std::string> connected;
    for (const auto& c : conns) {
        connected.insert(c.from_node);
        connected.insert(c.to_node);
    }

    // Collect unconnected output sinks
    std::vector<size_t> sink_indices;
    float total_h = 0;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& ns = nodes[i];
        if (ns.has_layout) continue;
        if (connected.count(ns.node_id)) continue;
        if (ns.type_name != "video_out" && ns.type_name != "audio_out") continue;
        total_h += node_rects_[i].h;
        sink_indices.push_back(i);
    }
    if (sink_indices.empty()) return;

    total_h += (sink_indices.size() - 1) * kRowSpacing;
    float sx = static_cast<float>(win_w_) - kNodeW - 10.0f;
    float sy = (static_cast<float>(win_h_) - total_h) * 0.5f;
    for (size_t i : sink_indices) {
        node_rects_[i].x = sx;
        node_rects_[i].y = sy;
        sy += node_rects_[i].h + kRowSpacing;
    }
}

// -----------------------------------------------------------------------
// Incremental layout — only position newly added nodes
// -----------------------------------------------------------------------
void NodeGraphUI::place_new_nodes() {
    const auto& nodes = snap_.nodes;
    const auto& conns = snap_.connections;

    // Build old node_id -> NodeRect map
    std::unordered_map<std::string, NodeRect> old_rects;
    for (const auto& r : node_rects_)
        old_rects[r.node_id] = r;

    // Rebuild node_rects_ sized to current snapshot, copying existing rects
    node_rects_.resize(nodes.size());
    std::vector<size_t> new_indices;

    for (size_t i = 0; i < nodes.size(); ++i) {
        auto it = old_rects.find(nodes[i].node_id);
        if (it != old_rects.end()) {
            node_rects_[i] = it->second;
        } else {
            new_indices.push_back(i);
        }
    }

    // Build node_id -> index map for connection lookup
    std::unordered_map<std::string, size_t> id_to_idx;
    for (size_t i = 0; i < nodes.size(); ++i)
        id_to_idx[nodes[i].node_id] = i;

    // Compute node height helper
    auto compute_height = [&](size_t ni) -> float {
        const auto& ns = nodes[ni];
        bool has_ct = custom_thumb_nodes_.count(ns.node_id) > 0;
        float body_h = node_body_height(ns.is_gpu, ns.active_cadence, has_ct);
        uint32_t n_inputs = count_visible_input_ports(ns, show_param_wires_);
        uint32_t n_outputs = count_visible_output_ports(ns, show_param_wires_);
        uint32_t port_rows = std::max(n_inputs, n_outputs);
        return kAccentBarH + body_h + kNodePadY + kLineH * 2 + port_rows * kLineH + kNodePadY;
    };

    // Position each new node
    for (size_t ni : new_indices) {
        const auto& ns = nodes[ni];
        float h = compute_height(ni);

        auto& rect = node_rects_[ni];
        rect.node_id = ns.node_id;
        rect.type_name = ns.type_name;
        rect.active_cadence = ns.active_cadence;
        rect.is_gpu = ns.is_gpu;
        rect.lane_behavior = ns.lane_behavior;
        rect.w = kNodeW;
        rect.h = h;
        rect.target_h = h;

        // If the node already has a saved layout position (e.g. from chooser), use it
        if (ns.has_layout) {
            rect.x = ns.layout_x;
            rect.y = ns.layout_y;
            recompute_ports(rect, ns);
            continue;
        }

        // Find predecessors and successors of this new node
        std::vector<size_t> pred_indices, succ_indices;
        for (const auto& c : conns) {
            if (c.to_node == ns.node_id) {
                auto it = id_to_idx.find(c.from_node);
                if (it != id_to_idx.end()) pred_indices.push_back(it->second);
            }
            if (c.from_node == ns.node_id) {
                auto it = id_to_idx.find(c.to_node);
                if (it != id_to_idx.end()) succ_indices.push_back(it->second);
            }
        }

        if (!pred_indices.empty() && succ_indices.empty()) {
            // Has predecessors only — place to the right, centered vertically
            float rightmost_x = -1e9f;
            float sum_y = 0;
            for (size_t pi : pred_indices) {
                const auto& pr = node_rects_[pi];
                rightmost_x = std::max(rightmost_x, pr.x + pr.w);
                sum_y += pr.y + pr.h * 0.5f;
            }
            rect.x = rightmost_x + (kColSpacing - kNodeW);
            rect.y = sum_y / pred_indices.size() - h * 0.5f;
        } else if (pred_indices.empty() && !succ_indices.empty()) {
            // Has successors only — place to the left, centered vertically
            float leftmost_x = 1e9f;
            float sum_y = 0;
            for (size_t si : succ_indices) {
                const auto& sr = node_rects_[si];
                leftmost_x = std::min(leftmost_x, sr.x);
                sum_y += sr.y + sr.h * 0.5f;
            }
            rect.x = leftmost_x - kColSpacing + (kColSpacing - kNodeW);
            rect.y = sum_y / succ_indices.size() - h * 0.5f;
        } else if (!pred_indices.empty() && !succ_indices.empty()) {
            // Has both — place midway
            float rightmost_x = -1e9f;
            float leftmost_x = 1e9f;
            float sum_y = 0;
            size_t count = 0;
            for (size_t pi : pred_indices) {
                const auto& pr = node_rects_[pi];
                rightmost_x = std::max(rightmost_x, pr.x + pr.w);
                sum_y += pr.y + pr.h * 0.5f;
                count++;
            }
            for (size_t si : succ_indices) {
                const auto& sr = node_rects_[si];
                leftmost_x = std::min(leftmost_x, sr.x);
                sum_y += sr.y + sr.h * 0.5f;
                count++;
            }
            rect.x = (rightmost_x + leftmost_x) * 0.5f - kNodeW * 0.5f;
            rect.y = sum_y / count - h * 0.5f;
        } else {
            // No connections — place to the right of all existing nodes
            float rightmost_edge = kLeftMargin;
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i == ni) continue;
                rightmost_edge = std::max(rightmost_edge, node_rects_[i].x + node_rects_[i].w);
            }
            rect.x = rightmost_edge + (kColSpacing - kNodeW);
            rect.y = kTopMargin;
        }

        // Nudge to avoid overlap with existing and other new nodes
        for (int attempt = 0; attempt < 20; ++attempt) {
            bool overlap = false;
            for (size_t i = 0; i < nodes.size(); ++i) {
                if (i == ni) continue;
                const auto& other = node_rects_[i];
                if (other.node_id.empty()) continue;
                if (rect.x < other.x + other.w && rect.x + rect.w > other.x &&
                    rect.y < other.y + other.h && rect.y + rect.h > other.y) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) break;
            rect.y += kRowSpacing + h;
        }

        recompute_ports(rect, ns);
    }

    last_node_count_ = nodes.size();
    last_conn_count_ = conns.size();
}

// -----------------------------------------------------------------------
// Hit testing
// -----------------------------------------------------------------------
int NodeGraphUI::hit_test_node(float mx, float my) const {
    float gx = sx_to_gx(mx), gy = sy_to_gy(my);
    for (int i = static_cast<int>(node_rects_.size()) - 1; i >= 0; --i) {
        const auto& r = node_rects_[i];
        if (gx >= r.x && gx <= r.x + r.w && gy >= r.y && gy <= r.y + r.h)
            return i;
    }
    return -1;
}

NodeGraphUI::PortHit NodeGraphUI::hit_test_port(float mx, float my) const {
    float gx = sx_to_gx(mx), gy = sy_to_gy(my);
    constexpr float kPortHitRadius = 10.0f;
    float best_dist2 = kPortHitRadius * kPortHitRadius;
    PortHit best;
    // Check outputs first (drag source), then inputs
    for (int i = static_cast<int>(node_rects_.size()) - 1; i >= 0; --i) {
        const auto& r = node_rects_[i];
        for (const auto& p : r.outputs) {
            float dx = gx - p.x, dy = gy - p.y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best_dist2) {
                best_dist2 = d2;
                best = {i, p.name, true, p.x, p.y};
            }
        }
    }
    if (best.node_idx >= 0) return best;
    for (int i = static_cast<int>(node_rects_.size()) - 1; i >= 0; --i) {
        const auto& r = node_rects_[i];
        for (const auto& p : r.inputs) {
            float dx = gx - p.x, dy = gy - p.y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best_dist2) {
                best_dist2 = d2;
                best = {i, p.name, false, p.x, p.y};
            }
        }
    }
    return best;
}

int NodeGraphUI::hit_test_wire(float sx, float sy) const {
    const auto& conns = snap_.connections;

    // Build fast lookup: node_id -> index in node_rects_
    std::unordered_map<std::string, size_t> id_to_rect;
    for (size_t i = 0; i < node_rects_.size(); ++i)
        id_to_rect[node_rects_[i].node_id] = i;

    constexpr float kHitThresh = 8.0f;
    float thresh2 = kHitThresh * kHitThresh;

    for (int ci = 0; ci < static_cast<int>(conns.size()); ++ci) {
        const auto& c = conns[ci];
        if ((c.from_is_param || c.to_is_param) && !show_param_wires_) continue;
        auto fi = id_to_rect.find(c.from_node);
        auto ti = id_to_rect.find(c.to_node);
        if (fi == id_to_rect.end() || ti == id_to_rect.end()) continue;

        const auto& from_rect = node_rects_[fi->second];
        const auto& to_rect = node_rects_[ti->second];

        // Find port positions in graph space
        float gsx = from_rect.x + from_rect.w, gsy = from_rect.y + from_rect.h * 0.5f;
        for (const auto& p : from_rect.outputs)
            if (p.name == c.from_port) { gsx = p.x; gsy = p.y; break; }
        float gex = to_rect.x, gey = to_rect.y + to_rect.h * 0.5f;
        for (const auto& p : to_rect.inputs)
            if (p.name == c.to_port) { gex = p.x; gey = p.y; break; }

        float ssx = gx_to_sx(gsx), ssy = gy_to_sy(gsy);
        float sex = gx_to_sx(gex), sey = gy_to_sy(gey);

        int found = -1;
        traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
            [&](float x0, float y0, float x1, float y1) {
                if (found < 0 && point_seg_dist2(sx, sy, x0, y0, x1, y1) < thresh2)
                    found = ci;
            });
        if (found >= 0) return found;
    }
    return -1;
}

// -----------------------------------------------------------------------
// Text editing
// -----------------------------------------------------------------------
void NodeGraphUI::confirm_transport_bpm_edit() {
    if (!transport_bpm_editing_) return;
    try {
        float bpm = std::stof(transport_bpm_edit_buffer_);
        bpm = std::clamp(bpm, 1.0f, 300.0f);
        commands_.set_graph_metronome(bpm, std::max(1, snap_.metronome_beats_per_bar));
    } catch (...) {
        // Invalid input — silently discard and restore the prior display state.
    }
    transport_bpm_editing_ = false;
    transport_bpm_edit_buffer_.clear();
    text_edit_.reset(0);
}

void NodeGraphUI::cancel_transport_bpm_edit() {
    transport_bpm_editing_ = false;
    transport_bpm_edit_buffer_.clear();
    text_edit_.reset(0);
}

void NodeGraphUI::confirm_param_edit() {
    if (!inspector_.editing_param) return;
    const auto* ns = snap_.find_node(inspector_.edit_node_id);
    if (ns) {
        const ParamInfo* pd = ns->find_param(inspector_.edit_param_name);
        if (pd) {
            if (pd->type == VIVID_PARAM_TEXT) {
                commands_.set_string_param(inspector_.edit_node_id, inspector_.edit_param_name, inspector_.edit_buffer);
            } else {
                try {
                    float val = std::stof(inspector_.edit_buffer);
                val = std::max(pd->min_value, std::min(pd->max_value, val));
                if (pd->type == VIVID_PARAM_INT) val = std::round(val);
                commands_.set_param(inspector_.edit_node_id, inspector_.edit_param_name, val);
                } catch (...) {
                    // Invalid input — silently discard
                }
            }
        }
    }
    inspector_.editing_param = false;
    inspector_.edit_buffer.clear();
}

void NodeGraphUI::cancel_param_edit() {
    inspector_.editing_param = false;
    inspector_.edit_buffer.clear();
}

void NodeGraphUI::confirm_resolution_edit() {
    if (!inspector_.editing_resolution) return;
    try {
        int val = std::stoi(inspector_.edit_buffer);
        if (val < 1) val = 1;
        if (val > 8192) val = 8192;

        const auto* ns = snap_.find_node(inspector_.edit_res_node_id);
        if (ns) {
            uint32_t new_w = inspector_.edit_res_is_width ? static_cast<uint32_t>(val) : ns->gpu_tex_width;
            uint32_t new_h = inspector_.edit_res_is_width ? ns->gpu_tex_height : static_cast<uint32_t>(val);
            commands_.set_resolution(inspector_.edit_res_node_id, new_w, new_h);
        }
    } catch (...) {
        // Invalid input — silently discard
    }
    inspector_.editing_resolution = false;
    inspector_.edit_buffer.clear();
}

void NodeGraphUI::cancel_resolution_edit() {
    inspector_.editing_resolution = false;
    inspector_.edit_buffer.clear();
}

void NodeGraphUI::confirm_midi_range_edit() {
    if (!inspector_.editing_midi_range) return;
    try {
        float val = std::stof(inspector_.edit_buffer);
        const auto* mm = snap_.find_midi_mapping(inspector_.midi_range_node_id, inspector_.midi_range_param_name);
        if (mm) {
            float new_min = inspector_.midi_range_editing_min ? val : mm->range_min;
            float new_max = inspector_.midi_range_editing_min ? mm->range_max : val;
            commands_.update_midi_mapping(inspector_.midi_range_node_id, inspector_.midi_range_param_name, new_min, new_max);
        }
    } catch (...) {
        // Invalid input — silently discard
    }
    inspector_.editing_midi_range = false;
    inspector_.edit_buffer.clear();
}

void NodeGraphUI::cancel_midi_range_edit() {
    inspector_.editing_midi_range = false;
    inspector_.edit_buffer.clear();
}

// -----------------------------------------------------------------------
// Port type compatibility helpers (for insert-on-wire)
// -----------------------------------------------------------------------
// is_control_type() and port_type_compatible() are in node_graph_util.h

static bool can_insert_on_wire(const OperatorInfo& op, VividPortType src, VividPortType dst) {
    bool has_input = false, has_output = false;
    for (const auto& p : op.ports) {
        if (p.direction == VIVID_PORT_INPUT  && port_type_compatible(src, p.type)) has_input = true;
        if (p.direction == VIVID_PORT_OUTPUT && port_type_compatible(dst, p.type)) has_output = true;
        if (has_input && has_output) return true;
    }
    return false;
}

static bool has_compatible_port(const OperatorInfo& op, VividPortType wire_type,
                                VividPortDirection required_dir) {
    for (const auto& p : op.ports) {
        if (p.direction == required_dir && port_type_compatible(wire_type, p.type))
            return true;
    }
    return false;
}

static const ParamInfo* find_param_semantic_for_endpoint(const OperatorInfo& op,
                                                         const std::string& endpoint_name) {
    for (const auto& p : op.params) {
        if (p.name == endpoint_name) return &p;
    }
    // Single-param fallback for operators whose wire port name differs from param name.
    if (op.params.size() == 1) return &op.params[0];
    return nullptr;
}

static std::string semantic_tag_for_operator_endpoint(const OperatorInfo& op,
                                                      const std::string& endpoint_name) {
    const ParamInfo* p = find_param_semantic_for_endpoint(op, endpoint_name);
    return p ? p->semantic_tag : std::string{};
}

static std::string semantic_tag_for_snapshot_endpoint(const GraphSnapshot& snap,
                                                      const std::string& node_id,
                                                      const std::string& endpoint_name) {
    const auto* ns = snap.find_node(node_id);
    if (!ns || !ns->op_info) return {};
    return semantic_tag_for_operator_endpoint(*ns->op_info, endpoint_name);
}

static std::string find_compatible_port(const OperatorInfo& op, VividPortType wire_type,
                                        VividPortDirection dir,
                                        const std::string& preferred_semantic_tag = {}) {
    struct Candidate {
        size_t order = 0;
        int score = 0;
        std::string name;
    };
    std::vector<Candidate> candidates;
    size_t order = 0;
    for (const auto& p : op.ports) {
        if (p.direction != dir || !port_type_compatible(wire_type, p.type)) continue;
        int score = 0;
        if (!preferred_semantic_tag.empty()) {
            std::string candidate_tag = semantic_tag_for_operator_endpoint(op, p.name);
            if (!candidate_tag.empty() && candidate_tag == preferred_semantic_tag)
                score = 1;
        }
        candidates.push_back(Candidate{order++, score, p.name});
    }
    if (candidates.empty()) return {};
    auto best = std::max_element(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score) return a.score < b.score;
            return a.order > b.order;
        });
    return best->name;
}

// -----------------------------------------------------------------------
// Chooser filter
// -----------------------------------------------------------------------
void NodeGraphUI::rebuild_chooser_items() {
    if (!snap_valid_) return;
    chooser_mode_ = ChooserMode::Operators;
    chooser_subtitles_.clear();
    chooser_drop_actions_.clear();
    const auto& all = snap_.operator_types;
    chooser_items_.clear();

    // Case-insensitive substring match
    std::string lower_filter = chooser_filter_;
    for (auto& c : lower_filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto& name : all) {
        std::string lower_name = name;
        for (auto& c : lower_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_name.find(lower_filter) == std::string::npos)
            continue;
        // When inserting on a wire, filter to compatible operators
        if (chooser_insert_wire_) {
            auto cat_it = snap_.operator_catalog.find(name);
            if (cat_it == snap_.operator_catalog.end() || !cat_it->second) continue;
            if (!can_insert_on_wire(*cat_it->second, insert_wire_source_type_, insert_wire_dest_type_))
                continue;
        }
        // When connecting from a wire drag, filter to operators with a compatible port
        if (chooser_wire_connect_) {
            auto cat_it = snap_.operator_catalog.find(name);
            if (cat_it == snap_.operator_catalog.end() || !cat_it->second) continue;
            VividPortDirection need = wire_connect_from_output_ ? VIVID_PORT_INPUT : VIVID_PORT_OUTPUT;
            if (!has_compatible_port(*cat_it->second, wire_connect_type_, need))
                continue;
        }
        chooser_items_.push_back(name);
    }

    // Prepend "New Operator" sentinel when available
    if (commands_.can_create_operator() && !chooser_insert_wire_ && !chooser_wire_connect_) {
        std::string sentinel = "+ New Operator...";
        // Only show if it matches the current filter
        std::string lower_sentinel = sentinel;
        for (auto& c : lower_sentinel) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_filter.empty() || lower_sentinel.find(lower_filter) != std::string::npos) {
            chooser_items_.insert(chooser_items_.begin(), sentinel);
        }
    }

    chooser_sel_ = 0;
    chooser_scroll_ = 0;
}

// -----------------------------------------------------------------------
// Shared chooser confirm — creates node and optionally splices into wire
// -----------------------------------------------------------------------
void NodeGraphUI::reset_chooser_state() {
    chooser_insert_wire_ = false;
    chooser_wire_connect_ = false;
    chooser_open_ = false;
    chooser_filter_.clear();
    chooser_subtitles_.clear();
    chooser_drop_actions_.clear();
    chooser_mode_ = ChooserMode::Operators;
    chooser_error_.clear();
}

bool NodeGraphUI::graph_position_for_screen(float sx, float sy, float& gx, float& gy) const {
    if (sx < 0.0f || sy < 0.0f || sx >= graph_right() || sy >= graph_bottom())
        return false;
    gx = sx_to_gx(sx);
    gy = sy_to_gy(sy);
    return true;
}

void NodeGraphUI::graph_center_position(float& gx, float& gy) const {
    gx = sx_to_gx(graph_right() * 0.5f);
    gy = sy_to_gy(graph_bottom() * 0.5f);
}

void NodeGraphUI::open_file_drop_chooser(std::vector<FileDropChooserAction> actions,
                                         float graph_x, float graph_y) {
    if (async_add_active_ || async_graph_load_active_) return;
    chooser_mode_ = ChooserMode::FileDrop;
    chooser_items_.clear();
    chooser_subtitles_.clear();
    chooser_drop_actions_ = std::move(actions);
    for (const auto& action : chooser_drop_actions_) {
        chooser_items_.push_back(action.label);
        chooser_subtitles_.push_back(action.subtitle);
    }
    chooser_filter_.clear();
    chooser_sel_ = 0;
    chooser_scroll_ = 0;
    chooser_cursor_gx_ = graph_x;
    chooser_cursor_gy_ = graph_y;
    chooser_insert_wire_ = false;
    chooser_wire_connect_ = false;
    chooser_open_ = !chooser_items_.empty();
    chooser_error_.clear();
}

void NodeGraphUI::stash_chooser_restore_state() {
    async_add_restore_.valid = true;
    async_add_restore_.mode = chooser_mode_;
    async_add_restore_.filter = chooser_filter_;
    async_add_restore_.sel = chooser_sel_;
    async_add_restore_.scroll = chooser_scroll_;
    async_add_restore_.items = chooser_items_;
    async_add_restore_.subtitles = chooser_subtitles_;
    async_add_restore_.drop_actions = chooser_drop_actions_;
    async_add_restore_.cursor_gx = chooser_cursor_gx_;
    async_add_restore_.cursor_gy = chooser_cursor_gy_;
    async_add_restore_.insert_wire = chooser_insert_wire_;
    async_add_restore_.insert_conn = chooser_insert_conn_;
    async_add_restore_.insert_wire_source_type = insert_wire_source_type_;
    async_add_restore_.insert_wire_dest_type = insert_wire_dest_type_;
    async_add_restore_.wire_connect = chooser_wire_connect_;
    async_add_restore_.wire_connect_node_id = wire_connect_node_id_;
    async_add_restore_.wire_connect_port = wire_connect_port_;
    async_add_restore_.wire_connect_from_output = wire_connect_from_output_;
    async_add_restore_.wire_connect_type = wire_connect_type_;
}

void NodeGraphUI::restore_chooser_after_async_failure() {
    if (!async_add_restore_.valid) return;
    chooser_mode_ = async_add_restore_.mode;
    chooser_filter_ = async_add_restore_.filter;
    chooser_sel_ = async_add_restore_.sel;
    chooser_scroll_ = async_add_restore_.scroll;
    chooser_items_ = async_add_restore_.items;
    chooser_subtitles_ = async_add_restore_.subtitles;
    chooser_drop_actions_ = async_add_restore_.drop_actions;
    chooser_cursor_gx_ = async_add_restore_.cursor_gx;
    chooser_cursor_gy_ = async_add_restore_.cursor_gy;
    chooser_insert_wire_ = async_add_restore_.insert_wire;
    chooser_insert_conn_ = async_add_restore_.insert_conn;
    insert_wire_source_type_ = async_add_restore_.insert_wire_source_type;
    insert_wire_dest_type_ = async_add_restore_.insert_wire_dest_type;
    chooser_wire_connect_ = async_add_restore_.wire_connect;
    wire_connect_node_id_ = async_add_restore_.wire_connect_node_id;
    wire_connect_port_ = async_add_restore_.wire_connect_port;
    wire_connect_from_output_ = async_add_restore_.wire_connect_from_output;
    wire_connect_type_ = async_add_restore_.wire_connect_type;
    chooser_open_ = !chooser_items_.empty();
}

void NodeGraphUI::update_modal_only() {
    build_console_panel_.sync_from_model();
    clear_frame_flags();
}

void NodeGraphUI::begin_async_graph_load(const std::string& display_name) {
    async_graph_load_active_ = true;
    async_graph_load_stage_ = AsyncGraphLoadStage::Loading;
    async_graph_load_display_name_ = display_name;
    status_banner_error_.clear();
}

void NodeGraphUI::notify_async_graph_load_success() {
    async_graph_load_active_ = false;
    async_graph_load_display_name_.clear();
    status_banner_error_.clear();
}

void NodeGraphUI::notify_async_graph_load_failure(const std::string& summary) {
    async_graph_load_active_ = false;
    async_graph_load_display_name_.clear();
    status_banner_error_ = summary;
}

void NodeGraphUI::notify_async_add_success(const std::string& node_id) {
    async_add_active_ = false;
    async_add_display_name_.clear();
    async_add_restore_ = {};
    chooser_error_.clear();
    status_banner_error_.clear();
    selected_node_ids_.clear();
    if (!node_id.empty())
        selected_node_ids_.insert(node_id);
    selected_wire_idx_ = -1;
}

void NodeGraphUI::notify_async_add_failure(const std::string& summary) {
    async_add_active_ = false;
    async_add_display_name_.clear();
    restore_chooser_after_async_failure();
    chooser_error_ = summary;
}

void NodeGraphUI::confirm_chooser_selection(const std::string& type) {
    if (chooser_mode_ == ChooserMode::FileDrop) {
        for (size_t i = 0; i < chooser_drop_actions_.size(); ++i) {
            if (chooser_drop_actions_[i].type_name == type ||
                chooser_drop_actions_[i].label == type) {
                confirm_chooser_selection_idx(static_cast<int>(i));
                return;
            }
        }
        reset_chooser_state();
        return;
    }

    if (chooser_items_.empty()) {
        chooser_mode_ = ChooserMode::Operators;
        chooser_items_ = {type};
        chooser_sel_ = 0;
        chooser_scroll_ = 0;
        chooser_open_ = true;
        confirm_chooser_selection_idx(0);
        return;
    }

    auto it = std::find(chooser_items_.begin(), chooser_items_.end(), type);
    if (it == chooser_items_.end()) {
        reset_chooser_state();
        return;
    }
    confirm_chooser_selection_idx(static_cast<int>(std::distance(chooser_items_.begin(), it)));
}

bool NodeGraphUI::build_async_add_request_for_selection(int idx, AsyncAddOperatorRequest& request) {
    request = {};
    if (idx < 0 || idx >= static_cast<int>(chooser_items_.size()))
        return false;

    if (chooser_mode_ == ChooserMode::FileDrop) {
        if (idx >= static_cast<int>(chooser_drop_actions_.size()))
            return false;
        const auto& action = chooser_drop_actions_[idx];
        std::string id;
        for (int n = 1; ; ++n) {
            id = action.type_name + std::to_string(n);
            if (!snap_.has_node(id)) break;
        }
        request.type_name = action.type_name;
        request.node_id = id;
        request.display_name = action.label.empty() ? action.type_name : action.label;
        request.graph_x = chooser_cursor_gx_;
        request.graph_y = chooser_cursor_gy_;
        request.string_params[action.file_param] = action.dropped_path;
        return true;
    }

    const std::string& type = chooser_items_[idx];
    if (type == "+ New Operator...")
        return false;

    std::string id;
    for (int n = 1; ; ++n) {
        id = type + std::to_string(n);
        if (!snap_.has_node(id)) break;
    }

    request.type_name = type;
    request.node_id = id;
    request.display_name = type;
    request.graph_x = chooser_cursor_gx_;
    request.graph_y = chooser_cursor_gy_;

    if (chooser_insert_wire_) {
        auto cat_it = snap_.operator_catalog.find(type);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second) {
            const auto& op = *cat_it->second;
            std::string source_tag =
                semantic_tag_for_snapshot_endpoint(snap_, chooser_insert_conn_.from_node,
                                                   chooser_insert_conn_.from_port);
            std::string dest_tag =
                semantic_tag_for_snapshot_endpoint(snap_, chooser_insert_conn_.to_node,
                                                   chooser_insert_conn_.to_port);
            std::string src_addr =
                chooser_insert_conn_.from_node + "/" + chooser_insert_conn_.from_port;
            std::string dst_addr =
                chooser_insert_conn_.to_node + "/" + chooser_insert_conn_.to_port;
            std::string in_port =
                find_compatible_port(op, insert_wire_source_type_, VIVID_PORT_INPUT, source_tag);
            std::string out_port =
                find_compatible_port(op, insert_wire_dest_type_, VIVID_PORT_OUTPUT, dest_tag);
            if (!in_port.empty() && !out_port.empty()) {
                request.connection_mutations.push_back(
                    {AsyncAddConnectionMutation::Kind::Connect, src_addr, id + "/" + in_port});
                request.connection_mutations.push_back(
                    {AsyncAddConnectionMutation::Kind::Connect, id + "/" + out_port, dst_addr});
                request.connection_mutations.push_back(
                    {AsyncAddConnectionMutation::Kind::Disconnect, src_addr, dst_addr});
            }
        }
    }

    if (chooser_wire_connect_) {
        auto cat_it = snap_.operator_catalog.find(type);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second) {
            const auto& op = *cat_it->second;
            std::string sem_tag = semantic_tag_for_snapshot_endpoint(
                snap_, wire_connect_node_id_, wire_connect_port_);
            if (wire_connect_from_output_) {
                std::string in_port = find_compatible_port(
                    op, wire_connect_type_, VIVID_PORT_INPUT, sem_tag);
                if (!in_port.empty()) {
                    request.connection_mutations.push_back(
                        {AsyncAddConnectionMutation::Kind::Connect,
                         wire_connect_node_id_ + "/" + wire_connect_port_,
                         id + "/" + in_port});
                }
            } else {
                std::string out_port = find_compatible_port(
                    op, wire_connect_type_, VIVID_PORT_OUTPUT, sem_tag);
                if (!out_port.empty()) {
                    request.connection_mutations.push_back(
                        {AsyncAddConnectionMutation::Kind::Connect,
                         id + "/" + out_port,
                         wire_connect_node_id_ + "/" + wire_connect_port_});
                }
            }
        }
    }

    return true;
}

void NodeGraphUI::confirm_chooser_selection_idx(int idx) {
    if (idx < 0 || idx >= static_cast<int>(chooser_items_.size())) {
        reset_chooser_state();
        return;
    }

    if (async_add_callback_) {
        const std::string& selected = chooser_items_[idx];
        if (selected == "+ New Operator...") {
            dialogs_.open_create_popup();
            text_edit_.reset(0);
            reset_chooser_state();
            return;
        }

        AsyncAddOperatorRequest request;
        if (!build_async_add_request_for_selection(idx, request)) {
            reset_chooser_state();
            return;
        }

        std::string error;
        if (!async_add_callback_(request, error)) {
            chooser_error_ = error.empty() ? "Failed to start operator add" : error;
            return;
        }

        stash_chooser_restore_state();
        async_add_active_ = true;
        async_add_stage_ = AsyncAddStage::Preparing;
        async_add_display_name_ = request.display_name;
        status_banner_error_.clear();
        reset_chooser_state();
        return;
    }

    if (chooser_mode_ == ChooserMode::FileDrop) {
        if (idx >= static_cast<int>(chooser_drop_actions_.size())) {
            reset_chooser_state();
            return;
        }
        const auto& action = chooser_drop_actions_[idx];
        std::string id;
        for (int n = 1; ; ++n) {
            id = action.type_name + std::to_string(n);
            if (!snap_.has_node(id)) break;
        }
        std::string add_error;
        if (!commands_.try_add_node(action.type_name, id, &add_error)) {
            std::fprintf(stderr, "[vivid] Add node failed for dropped file '%s': %s\n",
                         action.type_name.c_str(), add_error.c_str());
            reset_chooser_state();
            return;
        }
        commands_.set_node_layout(id, chooser_cursor_gx_, chooser_cursor_gy_);
        commands_.set_string_param(id, action.file_param, action.dropped_path);
        selected_node_ids_ = {id};
        reset_chooser_state();
        return;
    }

    const std::string& type = chooser_items_[idx];
    auto rollback_connection = [&](const std::string& from,
                                   const std::string& to,
                                   const char* context) {
        std::string rollback_error;
        if (!commands_.try_disconnect(from, to, &rollback_error)) {
            std::fprintf(stderr, "[vivid] UI rollback failed while %s (%s -> %s): %s\n",
                         context, from.c_str(), to.c_str(), rollback_error.c_str());
        }
    };

    // Handle "New Operator" sentinel
    if (type == "+ New Operator...") {
        dialogs_.open_create_popup();
        text_edit_.reset(0);
        reset_chooser_state();
        return;
    }

    // Generate unique node ID
    std::string id;
    for (int n = 1; ; ++n) {
        id = type + std::to_string(n);
        if (!snap_.has_node(id)) break;
    }
    std::string add_error;
    if (!commands_.try_add_node(type, id, &add_error)) {
        std::fprintf(stderr, "[vivid] Add node failed for '%s': %s\n",
                     type.c_str(), add_error.c_str());
        reset_chooser_state();
        return;
    }
    commands_.set_node_layout(id, chooser_cursor_gx_, chooser_cursor_gy_);

    if (chooser_insert_wire_) {
        auto cat_it = snap_.operator_catalog.find(type);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second) {
            const auto& op = *cat_it->second;
            std::string source_tag =
                semantic_tag_for_snapshot_endpoint(snap_, chooser_insert_conn_.from_node,
                                                   chooser_insert_conn_.from_port);
            std::string dest_tag =
                semantic_tag_for_snapshot_endpoint(snap_, chooser_insert_conn_.to_node,
                                                   chooser_insert_conn_.to_port);
            std::string src_addr =
                chooser_insert_conn_.from_node + "/" + chooser_insert_conn_.from_port;
            std::string dst_addr =
                chooser_insert_conn_.to_node + "/" + chooser_insert_conn_.to_port;
            std::string in_port  = find_compatible_port(op, insert_wire_source_type_, VIVID_PORT_INPUT, source_tag);
            std::string out_port = find_compatible_port(op, insert_wire_dest_type_,   VIVID_PORT_OUTPUT, dest_tag);
            if (!in_port.empty() && !out_port.empty()) {
                std::string new_in_addr = id + "/" + in_port;
                std::string new_out_addr = id + "/" + out_port;
                std::string command_error;

                if (commands_.try_connect(src_addr, new_in_addr, &command_error)) {
                    if (commands_.try_connect(new_out_addr, dst_addr, &command_error)) {
                        if (!commands_.try_disconnect(src_addr, dst_addr, &command_error)) {
                            rollback_connection(new_out_addr, dst_addr,
                                                "restoring chooser splice after source disconnect failure");
                            rollback_connection(src_addr, new_in_addr,
                                                "restoring chooser splice after source disconnect failure");
                            std::fprintf(stderr,
                                         "[vivid] UI chooser splice kept original wire (%s -> %s): %s\n",
                                         src_addr.c_str(), dst_addr.c_str(), command_error.c_str());
                        }
                    } else {
                        rollback_connection(src_addr, new_in_addr,
                                            "restoring chooser splice after destination connect failure");
                        std::fprintf(stderr,
                                     "[vivid] UI chooser splice skipped (%s -> %s): %s\n",
                                     src_addr.c_str(), dst_addr.c_str(), command_error.c_str());
                    }
                } else {
                    std::fprintf(stderr,
                                 "[vivid] UI chooser splice skipped (%s -> %s): %s\n",
                                 src_addr.c_str(), dst_addr.c_str(), command_error.c_str());
                }
            } else {
                std::fprintf(stderr,
                             "[vivid] UI chooser splice kept original wire (%s -> %s): no compatible ports on %s\n",
                             src_addr.c_str(), dst_addr.c_str(), type.c_str());
            }
        }
    }

    if (chooser_wire_connect_) {
        auto cat_it = snap_.operator_catalog.find(type);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second) {
            const auto& op = *cat_it->second;
            std::string sem_tag = semantic_tag_for_snapshot_endpoint(
                snap_, wire_connect_node_id_, wire_connect_port_);
            if (wire_connect_from_output_) {
                // Dragged from an output — find compatible input on the new node
                std::string in_port = find_compatible_port(op, wire_connect_type_, VIVID_PORT_INPUT, sem_tag);
                if (!in_port.empty())
                    commands_.try_connect(wire_connect_node_id_ + "/" + wire_connect_port_,
                                          id + "/" + in_port);
            } else {
                // Dragged from an input — find compatible output on the new node
                std::string out_port = find_compatible_port(op, wire_connect_type_, VIVID_PORT_OUTPUT, sem_tag);
                if (!out_port.empty())
                    commands_.try_connect(id + "/" + out_port,
                                          wire_connect_node_id_ + "/" + wire_connect_port_);
            }
        }
    }

    selected_node_ids_ = { id };
    reset_chooser_state();
}

// -----------------------------------------------------------------------

} // namespace vivid::ui
