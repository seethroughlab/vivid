#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/ui_style.h"
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
    : commands_(commands) {
    // Initialize with default Dark Steel style
    auto styles = builtin_styles();
    if (!styles.empty()) style_ = styles[0];
}

void NodeGraphUI::open_clone_confirm_dialog(const std::string& type_name) {
    clone_confirm_type_ = type_name;
    clone_confirm_project_available_ = commands_.has_project_clone_destination();
    clone_confirm_destination_ = clone_confirm_project_available_ ? 0 : 1;
    clone_confirm_open_ = true;
}

void NodeGraphUI::open_save_confirm_dialog(SaveConfirmAction action) {
    save_confirm_action_ = action;
    save_confirm_open_ = true;
}

float NodeGraphUI::graph_right() const {
    return has_selection() ? inspector_x() : static_cast<float>(win_w_);
}

float NodeGraphUI::graph_bottom() const {
    float h = static_cast<float>(win_h_);
    if (session_grid_open_) h -= kSessionStripH;
    return h;
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
template int NodeGraphUI::hit_test_rect(const std::vector<InspectorRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<ResolutionRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<MidiRemoveRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<MidiRangeRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<XYPadRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<ColorSwatchRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<StatePresetRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<StateHeaderRect>& rects, float mx, float my);

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

uint32_t NodeGraphUI::count_visible_input_ports(const NodeSnapshot& ns) const {
    // Signal input ports are always visible
    uint32_t count = static_cast<uint32_t>(ns.input_port_indices.size());
    // Param inputs only visible if connected
    for (const auto& [name, idx] : ns.param_indices) {
        if (ns.input_port_indices.count(name)) continue; // already counted as signal port
        if (port_has_connection(snap_.connections, ns.node_id, name, false))
            count++;
    }
    return count;
}

uint32_t NodeGraphUI::count_visible_output_ports(const NodeSnapshot& ns) const {
    bool few_outputs = ns.output_port_indices.size() <= 3;
    bool expanded    = outputs_expanded_.count(ns.node_id) > 0;
    bool show_all    = few_outputs || expanded;

    uint32_t count = 0;
    for (const auto& [name, idx] : ns.output_port_indices) {
        if (show_all || port_has_connection(snap_.connections, ns.node_id, name, true))
            count++;
    }
    // Affordance row always reserves one row for nodes with >3 outputs
    if (!few_outputs)
        count++;
    // Param sources — visible only if connected as source
    for (const auto& [name, idx] : ns.param_indices) {
        if (ns.output_port_indices.count(name)) continue;
        if (port_has_connection(snap_.connections, ns.node_id, name, true))
            count++;
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
    float body_h = domain_body_height(rect.domain, has_ct);

    rect.inputs.clear();
    rect.outputs.clear();

    auto sorted_inputs = sorted_ports(ns.input_port_indices);
    auto sorted_outputs_vec = sorted_ports(ns.output_port_indices);
    bool few_outputs = ns.output_port_indices.size() <= 3;
    bool expanded    = outputs_expanded_.count(ns.node_id) > 0;
    bool show_all    = few_outputs || expanded;

    float port_start_y = rect.y + kAccentBarH + body_h + kNodePadY + kLineH * 2;

    // Input signal ports — always visible
    size_t pi = 0;
    for (; pi < sorted_inputs.size(); ++pi) {
        float py = port_start_y + pi * kLineH + kLineH * 0.5f;
        rect.inputs.push_back({sorted_inputs[pi].second, rect.x, py, false});
    }

    // Parameter inputs — only visible if connected
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

    // Output ports — show all when few or expanded, otherwise only connected
    size_t oi = 0;
    for (const auto& [idx, name] : sorted_outputs_vec) {
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
        uint32_t total = static_cast<uint32_t>(ns.output_port_indices.size());
        rect.hidden_output_count = expanded ? 0 : total - static_cast<uint32_t>(oi);
        rect.affordance_gy = port_start_y + oi * kLineH + kLineH * 0.5f;
        ++oi; // reserve the row so param sources appear below
    }

    // Param sources — visible only if connected as a source
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
            float body_h = domain_body_height(ns.domain, has_ct);

            uint32_t n_inputs = count_visible_input_ports(ns);
            uint32_t n_outputs = count_visible_output_ports(ns);
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
            rect.domain = ns.domain;
            rect.x = col_x;
            rect.y = cur_y;
            rect.w = kNodeW;
            rect.h = heights[r];

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
        float body_h = domain_body_height(ns.domain, has_ct);
        uint32_t n_inputs = count_visible_input_ports(ns);
        uint32_t n_outputs = count_visible_output_ports(ns);
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
        rect.domain = ns.domain;
        rect.w = kNodeW;
        rect.h = h;

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
void NodeGraphUI::confirm_param_edit() {
    if (!editing_param_) return;
    const auto* ns = snap_.find_node(edit_node_id_);
    if (ns) {
        const ParamInfo* pd = ns->find_param(edit_param_name_);
        if (pd) {
            if (pd->type == VIVID_PARAM_TEXT) {
                commands_.set_string_param(edit_node_id_, edit_param_name_, edit_buffer_);
            } else {
                try {
                    float val = std::stof(edit_buffer_);
                val = std::max(pd->min_value, std::min(pd->max_value, val));
                if (pd->type == VIVID_PARAM_INT) val = std::round(val);
                commands_.set_param(edit_node_id_, edit_param_name_, val);
                } catch (...) {
                    // Invalid input — silently discard
                }
            }
        }
    }
    editing_param_ = false;
    edit_buffer_.clear();
}

void NodeGraphUI::cancel_param_edit() {
    editing_param_ = false;
    edit_buffer_.clear();
}

void NodeGraphUI::confirm_resolution_edit() {
    if (!editing_resolution_) return;
    try {
        int val = std::stoi(edit_buffer_);
        if (val < 1) val = 1;
        if (val > 8192) val = 8192;

        const auto* ns = snap_.find_node(edit_res_node_id_);
        if (ns) {
            uint32_t new_w = edit_res_is_width_ ? static_cast<uint32_t>(val) : ns->gpu_tex_width;
            uint32_t new_h = edit_res_is_width_ ? ns->gpu_tex_height : static_cast<uint32_t>(val);
            commands_.set_resolution(edit_res_node_id_, new_w, new_h);
        }
    } catch (...) {
        // Invalid input — silently discard
    }
    editing_resolution_ = false;
    edit_buffer_.clear();
}

void NodeGraphUI::cancel_resolution_edit() {
    editing_resolution_ = false;
    edit_buffer_.clear();
}

void NodeGraphUI::confirm_midi_range_edit() {
    if (!editing_midi_range_) return;
    try {
        float val = std::stof(edit_buffer_);
        const auto* mm = snap_.find_midi_mapping(midi_range_node_id_, midi_range_param_name_);
        if (mm) {
            float new_min = midi_range_editing_min_ ? val : mm->range_min;
            float new_max = midi_range_editing_min_ ? mm->range_max : val;
            commands_.update_midi_mapping(midi_range_node_id_, midi_range_param_name_, new_min, new_max);
        }
    } catch (...) {
        // Invalid input — silently discard
    }
    editing_midi_range_ = false;
    edit_buffer_.clear();
}

void NodeGraphUI::cancel_midi_range_edit() {
    editing_midi_range_ = false;
    edit_buffer_.clear();
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
void NodeGraphUI::confirm_chooser_selection(const std::string& type) {
    // Handle "New Operator" sentinel
    if (type == "+ New Operator...") {
        create_popup_open_ = true;
        create_domain_sel_ = 0;
        create_name_buf_.clear();
        text_edit_.reset(0);
        create_error_.clear();
        create_composite_ = false;
        create_destination_ = 0;
        create_active_field_ = 0;
        create_inputs_  = {{"input", 0}};   // domain-default: float
        create_outputs_ = {{"output", 0}};  // domain-default: float
        create_params_.clear();
        chooser_open_ = false;
        chooser_insert_wire_ = false;
        chooser_wire_connect_ = false;
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
        chooser_insert_wire_ = false;
        chooser_wire_connect_ = false;
        chooser_open_ = false;
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
            // Remove original wire
            commands_.disconnect(
                chooser_insert_conn_.from_node + "/" + chooser_insert_conn_.from_port,
                chooser_insert_conn_.to_node   + "/" + chooser_insert_conn_.to_port);
            // Find compatible ports on the new node
            std::string in_port  = find_compatible_port(op, insert_wire_source_type_, VIVID_PORT_INPUT, source_tag);
            std::string out_port = find_compatible_port(op, insert_wire_dest_type_,   VIVID_PORT_OUTPUT, dest_tag);
            // Wire source → new node
            if (!in_port.empty())
                commands_.connect(chooser_insert_conn_.from_node + "/" + chooser_insert_conn_.from_port,
                                  id + "/" + in_port);
            // Wire new node → dest
            if (!out_port.empty())
                commands_.connect(id + "/" + out_port,
                                  chooser_insert_conn_.to_node + "/" + chooser_insert_conn_.to_port);
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
                    commands_.connect(wire_connect_node_id_ + "/" + wire_connect_port_,
                                      id + "/" + in_port);
            } else {
                // Dragged from an input — find compatible output on the new node
                std::string out_port = find_compatible_port(op, wire_connect_type_, VIVID_PORT_OUTPUT, sem_tag);
                if (!out_port.empty())
                    commands_.connect(id + "/" + out_port,
                                      wire_connect_node_id_ + "/" + wire_connect_port_);
            }
        }
    }

    selected_node_ids_ = { id };
    chooser_insert_wire_ = false;
    chooser_wire_connect_ = false;
    chooser_open_ = false;
}

// -----------------------------------------------------------------------
// Update — thin dispatcher calling decomposed sub-methods
// -----------------------------------------------------------------------
void NodeGraphUI::update(const GraphSnapshot& snapshot) {
    snap_ = snapshot;
    snap_valid_ = true;

    // Deselect a param wire that becomes hidden
    if (!show_param_wires_ && selected_wire_idx_ >= 0 &&
        selected_wire_idx_ < (int)snap_.connections.size() &&
        (snap_.connections[selected_wire_idx_].from_is_param ||
         snap_.connections[selected_wire_idx_].to_is_param)) {
        selected_wire_idx_ = -1;
    }

    // MIDI map mode: capture CC event when waiting for knob wiggle
    if (midi_map_waiting_ && !snap_.pending_cc_events.empty()) {
        const auto& ev = snap_.pending_cc_events[0];
        // Look up param descriptor for default range
        float range_min = 0.0f, range_max = 1.0f;
        const auto* ns = snap_.find_node(midi_map_node_id_);
        if (ns) {
            const ParamInfo* pd = ns->find_param(midi_map_param_name_);
            if (pd) {
                range_min = pd->min_value;
                range_max = pd->max_value;
            }
        }
        commands_.add_midi_mapping(midi_map_node_id_, midi_map_param_name_,
                                   ev.cc_number, ev.channel, range_min, range_max);
        midi_map_waiting_ = false;
    }

    check_relayout();
    update_pan();
    update_node_drag();
    update_box_select();
    update_wire_drag();
    update_scrollbar_drag();
    update_slider_drag();
    update_xy_pad_drag();
    update_color_drag();
    update_patch_drag();
    update_chooser_hover();
    update_param_picker();   // may consume left_clicked
    update_package_browser(); // may consume left_clicked
    update_example_browser(); // may consume left_clicked
    update_graph_meta_editor(); // may consume left_clicked
    update_about();             // may consume left_clicked
    update_preferences();    // may consume left_clicked
    update_save_confirm();   // may consume left_clicked
    update_clone_confirm();  // may consume left_clicked
    update_create_popup();   // may consume left_clicked
    update_context_menu();   // may consume left_clicked
    handle_right_click();
    handle_left_click();     // dispatches to sub-handlers
    update_pan_release();
    // Preserve click events for custom inspector draw phase
    insp_mouse_left_clicked_ = mouse_.left_clicked;
    insp_mouse_left_released_ = mouse_.left_released;
    insp_mouse_right_clicked_ = mouse_.right_clicked;
    clear_frame_flags();
    update_wire_hover();
    update_node_hover();

    // Port hover — find nearest port when not in a drag/popup state
    hovered_port_ = {};
    if (hovered_node_id_.empty() && !dragging_wire_ && !panning_ && !box_selecting_ &&
        dragging_node_idx_ < 0 && !context_menu_open_ && !chooser_open_ && !dropdown_open_) {
        PortHit ph = hit_test_port(mouse_.x, mouse_.y);
        if (ph.node_idx >= 0 && ph.node_idx < static_cast<int>(node_rects_.size())) {
            hovered_port_.node_id = node_rects_[ph.node_idx].node_id;
            hovered_port_.port_name = ph.port_name;
            hovered_port_.is_output = ph.is_output;
        }
    }

    // Inspector widget hover
    hovered_slider_idx_ = -1;
    hovered_bool_idx_ = -1;
    hovered_dropdown_idx_ = -1;
    if (mouse_.x >= graph_right() && has_selection() && !editing_param_) {
        for (int i = 0; i < static_cast<int>(slider_rects_.size()); ++i) {
            const auto& r = slider_rects_[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                hovered_slider_idx_ = i;
                break;
            }
        }
        for (int i = 0; i < static_cast<int>(bool_rects_.size()); ++i) {
            const auto& r = bool_rects_[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                hovered_bool_idx_ = i;
                break;
            }
        }
        for (int i = 0; i < static_cast<int>(dropdown_rects_.size()); ++i) {
            const auto& r = dropdown_rects_[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                hovered_dropdown_idx_ = i;
                break;
            }
        }
    }

    update_sparklines();

    // --- Animation updates ---
    float dt = dt_;
    if (dt <= 0.0f) dt = 1.0f / 60.0f; // fallback

    // Smooth zoom/pan (only when not directly panning with middle mouse)
    if (!panning_) {
        zoom_ = lerp_toward(zoom_, zoom_target_, kZoomLerpSpeed, dt);
        pan_x_ = lerp_toward(pan_x_, pan_target_x_, kPanLerpSpeed, dt);
        pan_y_ = lerp_toward(pan_y_, pan_target_y_, kPanLerpSpeed, dt);
        // Snap when close enough to avoid perpetual sub-pixel drift
        if (std::fabs(zoom_ - zoom_target_) < 0.001f) zoom_ = zoom_target_;
        if (std::fabs(pan_x_ - pan_target_x_) < 0.5f) pan_x_ = pan_target_x_;
        if (std::fabs(pan_y_ - pan_target_y_) < 0.5f) pan_y_ = pan_target_y_;
    }

    // Popup fade
    bool any_popup = chooser_open_ || create_popup_open_ || prefs_open_ ||
                     pkg_browser_open_ || example_browser_open_ || graph_meta_editor_open_ ||
                     clone_confirm_open_ || save_confirm_open_ || preset_name_popup_open_ ||
                     about_open_ || mcp_setup_open_ || color_popup_open_;
    float popup_target = any_popup ? 1.0f : 0.0f;
    popup_opacity_ = lerp_toward(popup_opacity_, popup_target, kPopupFadeSpeed, dt);
    if (popup_opacity_ < 0.01f) popup_opacity_ = 0.0f;
    if (popup_opacity_ > 0.99f) popup_opacity_ = 1.0f;

    // Node hover alpha
    float hover_target = hovered_node_id_.empty() ? 0.0f : 1.0f;
    if (!hovered_node_id_.empty() && hovered_node_id_ != node_hover_anim_id_) {
        node_hover_anim_id_ = hovered_node_id_;
        node_hover_alpha_ = 0.0f;
    } else if (hovered_node_id_.empty() && node_hover_alpha_ < 0.01f) {
        node_hover_anim_id_.clear();
    }
    node_hover_alpha_ = lerp_toward(node_hover_alpha_, hover_target, kHoverFadeSpeed, dt);

    // Selection glow pulse
    if (!selected_node_ids_.empty()) {
        float glow_target = selection_glow_rising_ ? 1.0f : 0.0f;
        selection_glow_ = lerp_toward(selection_glow_, glow_target, kSelectionGlowSpeed, dt);
        if (selection_glow_ > 0.95f) selection_glow_rising_ = false;
        if (selection_glow_ < 0.05f) selection_glow_rising_ = true;
    } else {
        selection_glow_ = 0.0f;
        selection_glow_rising_ = true;
    }
}

void NodeGraphUI::check_relayout() {
    // Detect full graph replacement (e.g. drag-and-drop new file).
    // Normal edits reuse existing node IDs; a new graph introduces unknown IDs.
    if (first_layout_done_ && !node_rects_.empty()) {
        std::unordered_set<std::string> rect_ids;
        rect_ids.reserve(node_rects_.size());
        for (const auto& r : node_rects_) rect_ids.insert(r.node_id);
        for (const auto& n : snap_.nodes) {
            if (!rect_ids.count(n.node_id)) {
                layout_nodes();
                return;
            }
        }
    }

    size_t cur_nodes = snap_.nodes.size();
    size_t cur_conns = snap_.connections.size();
    if (cur_nodes > last_node_count_) {
        if (dragging_node_idx_ < 0 && !dragging_wire_) {
            if (!first_layout_done_)
                layout_nodes();       // first time: full Sugiyama
            else
                place_new_nodes();    // subsequent: incremental
        }
    } else if (cur_nodes < last_node_count_) {
        prune_node_rects();
    } else if (cur_conns != last_conn_count_) {
        // Connection changed — recompute ports and heights for all nodes
        // (connected params/outputs may have appeared or disappeared)
        std::unordered_map<std::string, size_t> rect_by_id;
        for (size_t i = 0; i < node_rects_.size(); ++i)
            rect_by_id[node_rects_[i].node_id] = i;

        for (const auto& ns : snap_.nodes) {
            auto it = rect_by_id.find(ns.node_id);
            if (it == rect_by_id.end()) continue;
            auto& rect = node_rects_[it->second];
            rect.domain = ns.domain;
            rect.type_name = ns.type_name;
            bool has_ct = custom_thumb_nodes_.count(ns.node_id) > 0;
            float body_h = domain_body_height(ns.domain, has_ct);
            uint32_t n_inputs = count_visible_input_ports(ns);
            uint32_t n_outputs = count_visible_output_ports(ns);
            uint32_t port_rows = std::max(n_inputs, n_outputs);
            rect.h = kAccentBarH + body_h + kNodePadY + kLineH * 2 + port_rows * kLineH + kNodePadY;
            recompute_ports(rect, ns);
        }
        last_node_count_ = cur_nodes;
        last_conn_count_ = cur_conns;
    }
}

void NodeGraphUI::prune_node_rects() {
    std::unordered_map<std::string, NodeRect> old_rects;
    for (const auto& r : node_rects_)
        old_rects[r.node_id] = r;

    const auto& nodes = snap_.nodes;
    node_rects_.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto it = old_rects.find(nodes[i].node_id);
        if (it != old_rects.end())
            node_rects_[i] = it->second;
    }

    if (nodes.empty())
        first_layout_done_ = false;

    // Prune stale selection IDs
    std::unordered_set<std::string> live_ids;
    for (const auto& n : nodes) live_ids.insert(n.node_id);
    for (auto it = selected_node_ids_.begin(); it != selected_node_ids_.end(); ) {
        if (!live_ids.count(*it))
            it = selected_node_ids_.erase(it);
        else
            ++it;
    }

    last_node_count_ = nodes.size();
    last_conn_count_ = snap_.connections.size();
}

void NodeGraphUI::update_pan() {
    if (panning_) {
        pan_x_ = pan_start_px_ + (mouse_.x - pan_start_mx_);
        pan_y_ = pan_start_py_ + (mouse_.y - pan_start_my_);
    }
}

void NodeGraphUI::update_node_drag() {
    if (dragging_node_idx_ < 0) return;
    if (mouse_.left_down) {
        // Detect real drag vs jittery click (2px threshold)
        if (!did_drag_ && !pending_select_node_id_.empty()) {
            float dx = mouse_.x - drag_start_sx_;
            float dy = mouse_.y - drag_start_sy_;
            if (dx * dx + dy * dy > 4.0f)
                did_drag_ = true;
        }

        float mgx = sx_to_gx(mouse_.x);
        float mgy = sy_to_gy(mouse_.y);

        if (group_drag_offsets_.size() > 1) {
            // Group drag: move all selected nodes
            for (auto& nr : node_rects_) {
                auto it = group_drag_offsets_.find(nr.node_id);
                if (it == group_drag_offsets_.end()) continue;
                nr.x = mgx - it->second.dx;
                nr.y = mgy - it->second.dy;
                const auto* ns = snap_.find_node(nr.node_id);
                if (ns) recompute_ports(nr, *ns);
            }
        } else {
            // Single drag
            auto& rect = node_rects_[dragging_node_idx_];
            rect.x = mgx - drag_offset_x_;
            rect.y = mgy - drag_offset_y_;
            const auto* ns = snap_.find_node(rect.node_id);
            if (ns) recompute_ports(rect, *ns);
        }
    }
    if (mouse_.left_released) {
        if (group_drag_offsets_.size() > 1) {
            for (const auto& nr : node_rects_) {
                if (group_drag_offsets_.count(nr.node_id))
                    commands_.set_node_layout(nr.node_id, nr.x, nr.y);
            }
            group_drag_offsets_.clear();
        } else {
            auto& rect = node_rects_[dragging_node_idx_];
            commands_.set_node_layout(rect.node_id, rect.x, rect.y);
        }
        dragging_node_idx_ = -1;

        // Deferred deselection: narrow to clicked node if no drag occurred
        if (!pending_select_node_id_.empty() && !did_drag_) {
            selected_node_ids_ = { pending_select_node_id_ };
        }
        pending_select_node_id_.clear();
    }
}

void NodeGraphUI::update_box_select() {
    if (!box_selecting_) return;
    if (mouse_.left_released) {
        // Compute graph-space AABB from anchor + current mouse
        float cur_gx = sx_to_gx(mouse_.x);
        float cur_gy = sy_to_gy(mouse_.y);
        float min_gx = std::min(box_start_gx_, cur_gx);
        float max_gx = std::max(box_start_gx_, cur_gx);
        float min_gy = std::min(box_start_gy_, cur_gy);
        float max_gy = std::max(box_start_gy_, cur_gy);

        // If not shift, start fresh
        if (!box_shift_held_)
            selected_node_ids_.clear();

        // Test intersection against all node_rects_
        for (const auto& r : node_rects_) {
            if (r.x + r.w >= min_gx && r.x <= max_gx &&
                r.y + r.h >= min_gy && r.y <= max_gy) {
                selected_node_ids_.insert(r.node_id);
            }
        }
        box_selecting_ = false;
    }
}

void NodeGraphUI::update_wire_drag() {
    if (mouse_.left_released && dragging_wire_) {
        PortHit ph = hit_test_port(mouse_.x, mouse_.y);
        if (ph.node_idx >= 0 && !ph.is_output) {
            // Dropped on an input port — connect directly
            std::string to_node = node_rects_[ph.node_idx].node_id;
            commands_.connect(wire_from_node_id_ + "/" + wire_from_port_,
                         to_node + "/" + ph.port_name);
        } else {
            // Check if dropped on a node body (not a port, not the source node)
            int ni = hit_test_node(mouse_.x, mouse_.y);
            if (ni >= 0 && node_rects_[ni].node_id != wire_from_node_id_) {
                // Open parameter picker for the target node's input params
                param_picker_node_id_ = node_rects_[ni].node_id;
                param_picker_wire_from_node_ = wire_from_node_id_;
                param_picker_wire_from_port_ = wire_from_port_;
                param_picker_is_output_ = false; // picking a destination param
                param_picker_x_ = mouse_.x;
                param_picker_y_ = mouse_.y;
                param_picker_sel_ = 0;
                param_picker_scroll_ = 0;
                rebuild_param_picker_items();
                if (!param_picker_items_.empty())
                    param_picker_open_ = true;
            }
        }
        dragging_wire_ = false;
    }
}

void NodeGraphUI::update_slider_drag() {
    if (active_slider_idx_ < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down) {
        const auto& s = slider_rects_[active_slider_idx_];
        const auto* ns = snap_.find_node(active_slider_node_id_);
        if (ns) {
            const ParamInfo* pd = ns->find_param(active_slider_param_name_);
            if (pd) {
                float val;
                if (pd->display_hint == VIVID_DISPLAY_KNOB) {
                    // Vertical drag: up = increase
                    float dy = mouse_.prev_y - mouse_.y;
                    float range = pd->max_value - pd->min_value;
                    float sensitivity = range / 200.0f;
                    auto pi_it = ns->param_indices.find(pd->name);
                    float cur = (pi_it != ns->param_indices.end())
                        ? ns->param_values[pi_it->second] : pd->min_value;
                    val = cur + dy * sensitivity;
                    val = std::max(pd->min_value, std::min(pd->max_value, val));
                } else {
                    float t = (mouse_.x - s.x) / s.w;
                    t = std::max(0.0f, std::min(1.0f, t));
                    val = pd->min_value + t * (pd->max_value - pd->min_value);
                }
                if (pd->type == VIVID_PARAM_INT) {
                    val = std::round(val);
                }
                commands_.set_param(active_slider_node_id_, active_slider_param_name_, val);
            }
        }
    }
    if (mouse_.left_released) {
        active_slider_idx_ = -1;
    }
}

void NodeGraphUI::update_xy_pad_drag() {
    if (active_xy_pad_idx_ < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down && active_xy_pad_idx_ < static_cast<int>(xy_pad_rects_.size())) {
        const auto& pad = xy_pad_rects_[active_xy_pad_idx_];
        const auto* ns = snap_.find_node(active_xy_node_id_);
        if (ns) {
            const ParamInfo* pdx = ns->find_param(active_xy_param_x_);
            const ParamInfo* pdy = ns->find_param(active_xy_param_y_);
            if (pdx && pdy) {
                float tx = (mouse_.x - pad.x) / pad.w;
                float ty = (mouse_.y - pad.y) / pad.h;
                tx = std::max(0.0f, std::min(1.0f, tx));
                ty = std::max(0.0f, std::min(1.0f, ty));
                float val_x = pdx->min_value + tx * (pdx->max_value - pdx->min_value);
                float val_y = pdy->min_value + (1.0f - ty) * (pdy->max_value - pdy->min_value);
                commands_.set_param(active_xy_node_id_, active_xy_param_x_, val_x);
                commands_.set_param(active_xy_node_id_, active_xy_param_y_, val_y);
            }
        }
    }
    if (mouse_.left_released) {
        active_xy_pad_idx_ = -1;
    }
}

void NodeGraphUI::update_color_drag() {
    if (!color_popup_open_) return;
    if (!color_dragging_sv_ && !color_dragging_hue_) return;
    if (mouse_.left_down) {
        float pad = kColorPopupPad;
        float sv_size = kColorPopupSVSize;
        float hue_bar_w = kColorHueBarW;
        float gap = kColorPopupGap;
        float sv_x = color_popup_x_ + pad;
        float sv_y = color_popup_y_ + pad;
        float hue_x = sv_x + sv_size + gap;
        float hue_y = sv_y;

        if (color_dragging_sv_) {
            color_popup_s_ = std::max(0.0f, std::min(1.0f, (mouse_.x - sv_x) / sv_size));
            color_popup_v_ = std::max(0.0f, std::min(1.0f, 1.0f - (mouse_.y - sv_y) / sv_size));
        }
        if (color_dragging_hue_) {
            color_popup_h_ = std::max(0.0f, std::min(360.0f,
                (mouse_.y - hue_y) / sv_size * 360.0f));
        }
        float r, g, b;
        hsv_to_rgb(color_popup_h_, color_popup_s_, color_popup_v_, r, g, b);
        commands_.set_param(color_popup_node_id_, color_popup_param_r_, r);
        commands_.set_param(color_popup_node_id_, color_popup_param_g_, g);
        commands_.set_param(color_popup_node_id_, color_popup_param_b_, b);
    }
    if (mouse_.left_released) {
        color_dragging_sv_ = false;
        color_dragging_hue_ = false;
    }
}

void NodeGraphUI::update_chooser_hover() {
    if (!chooser_open_) return;
    float items_y = kChooserY + kChooserHeaderH;
    int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
    if (mouse_.x >= chooser_x() && mouse_.x <= chooser_x() + kChooserW &&
        mouse_.y >= items_y && mouse_.y < items_y + visible * kChooserItemH &&
        !chooser_items_.empty()) {
        int idx = chooser_scroll_ + static_cast<int>((mouse_.y - items_y) / kChooserItemH);
        if (idx >= 0 && idx < static_cast<int>(chooser_items_.size()))
            chooser_sel_ = idx;
    }
}

void NodeGraphUI::update_pan_release() {
    if (mouse_.left_released && panning_ && dragging_node_idx_ < 0) {
        panning_ = false;
    }
}

void NodeGraphUI::clear_frame_flags() {
    mouse_.left_clicked = false;
    mouse_.left_released = false;
    mouse_.right_clicked = false;
}

void NodeGraphUI::update_node_hover() {
    hovered_node_id_.clear();
    if (dragging_wire_ || panning_ || box_selecting_ || dragging_node_idx_ >= 0 ||
        context_menu_open_ || chooser_open_ || dropdown_open_) return;
    float gx = sx_to_gx(mouse_.x);
    float gy = sy_to_gy(mouse_.y);
    for (const auto& nr : node_rects_) {
        if (gx >= nr.x && gx <= nr.x + nr.w && gy >= nr.y && gy <= nr.y + nr.h) {
            hovered_node_id_ = nr.node_id;
            break;
        }
    }
}

void NodeGraphUI::update_wire_hover() {
    if (!dragging_wire_ && !panning_ && !box_selecting_ && dragging_node_idx_ < 0 &&
        !context_menu_open_ && !chooser_open_ && !dropdown_open_) {
        hovered_wire_idx_ = hit_test_wire(mouse_.x, mouse_.y);
    } else {
        hovered_wire_idx_ = -1;
    }
}

void NodeGraphUI::update_sparklines() {
    for (const auto& ns : snap_.nodes) {
        if (ns.is_gpu || ns.is_audio) continue;

        auto sorted_outs = sorted_ports(ns.output_port_indices);
        if (sorted_outs.empty()) continue;
        if (sorted_outs[0].first >= ns.output_values.size()) continue;

        std::string key = ns.node_id + "/" + sorted_outs[0].second;
        float val = ns.output_values[sorted_outs[0].first];

        auto& sd = sparklines_[key];
        sd.values[sd.write_idx] = val;
        sd.write_idx = (sd.write_idx + 1) % kSparklineLen;
        if (sd.write_idx == 0) sd.filled = true;
    }
}

void NodeGraphUI::toggle_preferences() {
    if (prefs_open_) {
        // Cancel: revert style
        if (prefs_saved_style_sel_ >= 0 &&
            prefs_saved_style_sel_ < static_cast<int>(prefs_styles_.size())) {
            style_ = prefs_styles_[prefs_saved_style_sel_];
        }
        prefs_open_ = false;
        prefs_editing_custom_ = false;
    } else {
        prefs_open_ = true;
        prefs_editing_custom_ = false;
        prefs_saved_style_sel_ = prefs_style_sel_;
    }
}

void NodeGraphUI::set_editor_options(std::vector<std::string> names,
                                     std::vector<std::string> ids,
                                     int current_idx,
                                     const std::string& custom_command) {
    prefs_editor_names_ = std::move(names);
    prefs_editor_ids_ = std::move(ids);
    prefs_editor_sel_ = current_idx;
    prefs_custom_command_ = custom_command;
}

void NodeGraphUI::set_style_options(std::vector<UIStyle> styles, int current_idx,
                                     std::vector<ThemeInfo> themes) {
    prefs_styles_ = std::move(styles);
    prefs_themes_ = std::move(themes);
    prefs_style_sel_ = current_idx;
    prefs_saved_style_sel_ = current_idx;
    if (current_idx >= 0 && current_idx < static_cast<int>(prefs_styles_.size())) {
        style_ = prefs_styles_[current_idx];
    }
}

void NodeGraphUI::show_core_update_notice(const std::string& latest_version,
                                          const std::string& summary) {
    core_update_notice_open_ = true;
    core_update_notice_version_ = latest_version;
    core_update_notice_summary_ = summary;
}

void NodeGraphUI::clear_core_update_notice() {
    core_update_notice_open_ = false;
    core_update_notice_version_.clear();
    core_update_notice_summary_.clear();
    core_update_button_rects_.clear();
}

void NodeGraphUI::set_core_update_notice_callbacks(std::function<void()> install_cb,
                                                   std::function<void()> skip_cb,
                                                   std::function<void()> later_cb) {
    on_core_update_install_ = std::move(install_cb);
    on_core_update_skip_ = std::move(skip_cb);
    on_core_update_later_ = std::move(later_cb);
}

// -----------------------------------------------------------------------
// resolve_port_type — shared utility (was file-local static in input.cpp)
// -----------------------------------------------------------------------
VividPortType NodeGraphUI::resolve_port_type(const GraphSnapshot& snap,
                                              const std::string& node_id,
                                              const std::string& port_name,
                                              bool is_output) {
    const auto* ns = snap.find_node(node_id);
    if (!ns || !ns->op_info) return VIVID_PORT_FLOAT;
    for (const auto& p : ns->op_info->ports) {
        if (p.name == port_name &&
            ((is_output && p.direction == VIVID_PORT_OUTPUT) ||
             (!is_output && p.direction == VIVID_PORT_INPUT)))
            return p.type;
    }
    return VIVID_PORT_FLOAT;
}

// -----------------------------------------------------------------------
// wire_inspector_visible — true when a wire with a numeric dest type is selected
// -----------------------------------------------------------------------
bool NodeGraphUI::wire_inspector_visible() const {
    if (selected_wire_idx_ < 0 ||
        selected_wire_idx_ >= static_cast<int>(snap_.connections.size()))
        return false;
    const auto& c = snap_.connections[selected_wire_idx_];
    VividPortType t = resolve_port_type(snap_, c.to_node, c.to_port, false);
    return is_numeric_type(t);
}

// -----------------------------------------------------------------------
// Parameter picker popup
// -----------------------------------------------------------------------
void NodeGraphUI::rebuild_param_picker_items() {
    param_picker_items_.clear();
    param_picker_item_is_param_.clear();
    const auto* ns = snap_.find_node(param_picker_node_id_);
    if (!ns || !ns->op_info) return;

    if (param_picker_is_output_) {
        // Picking an output port on this node (source side)
        // Output ports first
        auto sorted_outs = sorted_ports(ns->output_port_indices);
        for (const auto& [idx, name] : sorted_outs) {
            param_picker_items_.push_back(name);
            param_picker_item_is_param_.push_back(false);
        }
        // Params (non-FILE, not already an output port name)
        std::vector<std::pair<uint32_t, std::string>> sorted_params;
        for (const auto& [name, idx] : ns->param_indices)
            if (!ns->output_port_indices.count(name)) sorted_params.push_back({idx, name});
        std::sort(sorted_params.begin(), sorted_params.end());
        for (const auto& [idx, name] : sorted_params) {
            const ParamInfo* pd = ns->find_param(name);
            if (pd && (pd->type == VIVID_PARAM_FILE || pd->type == VIVID_PARAM_TEXT)) continue;
            param_picker_items_.push_back(name);
            param_picker_item_is_param_.push_back(true);
        }
    } else {
        // Picking an input param on this node (destination side)
        // Determine source port type for compatibility filtering
        VividPortType src_type;
        std::string src_semantic_tag;
        if (!wire_from_is_output_)
            src_type = VIVID_PORT_FLOAT;  // param sources are always float
        else
            src_type = resolve_port_type(snap_, param_picker_wire_from_node_,
                                          param_picker_wire_from_port_, true);
        src_semantic_tag = semantic_tag_for_snapshot_endpoint(
            snap_, param_picker_wire_from_node_, param_picker_wire_from_port_);

        struct PickerCandidate {
            std::string name;
            bool is_param = false;
            int semantic_score = 0;
            size_t order = 0;
        };
        std::vector<PickerCandidate> candidates;
        size_t candidate_order = 0;

        // Add signal input ports first
        auto sorted_ins = sorted_ports(ns->input_port_indices);
        for (const auto& [idx, name] : sorted_ins) {
            // Check not already connected from this source
            bool already_connected = false;
            for (const auto& c : snap_.connections) {
                if (c.to_node == param_picker_node_id_ && c.to_port == name) {
                    already_connected = true;
                    break;
                }
            }
            if (already_connected) continue;

            // Check type compatibility
            VividPortType dest_type = VIVID_PORT_FLOAT;
            for (const auto& p : ns->op_info->ports) {
                if (p.name == name && p.direction == VIVID_PORT_INPUT) {
                    dest_type = p.type;
                    break;
                }
            }
            if (!port_type_compatible(src_type, dest_type)) continue;
            int semantic_score = 0;
            if (!src_semantic_tag.empty()) {
                std::string candidate_tag =
                    semantic_tag_for_snapshot_endpoint(snap_, param_picker_node_id_, name);
                if (!candidate_tag.empty() && candidate_tag == src_semantic_tag)
                    semantic_score = 1;
            }
            candidates.push_back(PickerCandidate{name, false, semantic_score, candidate_order++});
        }

        // Add params (not already signal ports, not FILE type, not already connected)
        std::vector<std::pair<uint32_t, std::string>> sorted_params;
        for (const auto& [name, idx] : ns->param_indices)
            if (!ns->input_port_indices.count(name)) sorted_params.push_back({idx, name});
        std::sort(sorted_params.begin(), sorted_params.end());

        for (const auto& [idx, name] : sorted_params) {
            // Skip FILE params (can't wire to them)
            const ParamInfo* pd = ns->find_param(name);
            if (pd && (pd->type == VIVID_PARAM_FILE || pd->type == VIVID_PARAM_TEXT)) continue;

            // Skip if already connected
            bool already_connected = false;
            for (const auto& c : snap_.connections) {
                if (c.to_node == param_picker_node_id_ && c.to_port == name) {
                    already_connected = true;
                    break;
                }
            }
            if (already_connected) continue;

            int semantic_score = 0;
            if (!src_semantic_tag.empty() && pd &&
                !pd->semantic_tag.empty() && pd->semantic_tag == src_semantic_tag) {
                semantic_score = 1;
            }
            candidates.push_back(PickerCandidate{name, true, semantic_score, candidate_order++});
        }

        std::stable_sort(candidates.begin(), candidates.end(),
            [](const PickerCandidate& a, const PickerCandidate& b) {
                if (a.semantic_score != b.semantic_score)
                    return a.semantic_score > b.semantic_score;
                return a.order < b.order;
            });
        for (const auto& c : candidates) {
            param_picker_items_.push_back(c.name);
            param_picker_item_is_param_.push_back(c.is_param);
        }
    }
}

void NodeGraphUI::update_param_picker() {
    if (!param_picker_open_) return;

    // Hover tracking
    static constexpr float kPickerItemH = 22.0f;
    static constexpr float kPickerW = 220.0f;
    int visible = std::min(static_cast<int>(param_picker_items_.size()), 12);
    float items_y = param_picker_y_;
    float items_h = visible * kPickerItemH;

    if (mouse_.x >= param_picker_x_ && mouse_.x <= param_picker_x_ + kPickerW &&
        mouse_.y >= items_y && mouse_.y < items_y + items_h) {
        int idx = param_picker_scroll_ + static_cast<int>((mouse_.y - items_y) / kPickerItemH);
        if (idx >= 0 && idx < static_cast<int>(param_picker_items_.size()))
            param_picker_sel_ = idx;
    }

    // Click handling
    if (mouse_.left_clicked) {
        if (mouse_.x >= param_picker_x_ && mouse_.x <= param_picker_x_ + kPickerW &&
            mouse_.y >= items_y && mouse_.y < items_y + items_h &&
            !param_picker_items_.empty()) {
            int idx = param_picker_scroll_ + static_cast<int>((mouse_.y - items_y) / kPickerItemH);
            if (idx >= 0 && idx < static_cast<int>(param_picker_items_.size())) {
                const std::string& selected = param_picker_items_[idx];
                if (param_picker_is_output_) {
                    // Selected an output port or param — now start a wire drag from it
                    const auto* ns = snap_.find_node(param_picker_node_id_);
                    if (ns) {
                        bool is_param = (!param_picker_item_is_param_.empty() &&
                                         idx < static_cast<int>(param_picker_item_is_param_.size()) &&
                                         param_picker_item_is_param_[idx]);
                        dragging_wire_ = true;
                        wire_from_node_id_ = param_picker_node_id_;
                        wire_from_port_ = selected;
                        wire_from_is_output_ = !is_param;
                        // Find port position or use node center
                        for (const auto& r : node_rects_) {
                            if (r.node_id == param_picker_node_id_) {
                                wire_from_gx_ = r.x + r.w;
                                wire_from_gy_ = r.y + r.h * 0.5f;
                                if (!is_param) {
                                    for (const auto& p : r.outputs) {
                                        if (p.name == selected) {
                                            wire_from_gx_ = p.x;
                                            wire_from_gy_ = p.y;
                                            break;
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                } else {
                    // Selected a destination param — create connection
                    commands_.connect(param_picker_wire_from_node_ + "/" + param_picker_wire_from_port_,
                                 param_picker_node_id_ + "/" + selected);
                }
                param_picker_open_ = false;
            }
        } else {
            // Clicked outside — close
            param_picker_open_ = false;
        }
        mouse_.left_clicked = false;
        mouse_.left_released = false;
    }
}

} // namespace vivid::ui
