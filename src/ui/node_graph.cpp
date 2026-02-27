#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/ui_style.h"
#include "common/topo_sort.h"
#include "common/string_util.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <cctype>

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

float NodeGraphUI::graph_right() const {
    return has_selection() ? inspector_x() : static_cast<float>(win_w_);
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

    float port_start_y = rect.y + kAccentBarH + body_h + kNodePadY + kLineH * 2;
    size_t pi = 0;
    for (; pi < sorted_inputs.size(); ++pi) {
        float py = port_start_y + pi * kLineH + kLineH * 0.5f;
        rect.inputs.push_back({sorted_inputs[pi].second, rect.x, py});
    }

    // Add parameters as input ports (sorted by index for stable ordering)
    std::vector<std::pair<uint32_t, std::string>> sorted_params;
    for (const auto& [name, idx] : ns.param_indices)
        if (!ns.input_port_indices.count(name)) sorted_params.push_back({idx, name});
    std::sort(sorted_params.begin(), sorted_params.end());
    for (const auto& [idx, name] : sorted_params) {
        float py = port_start_y + pi * kLineH + kLineH * 0.5f;
        rect.inputs.push_back({name, rect.x, py});
        ++pi;
    }

    for (size_t oi = 0; oi < sorted_outputs_vec.size(); ++oi) {
        float py = port_start_y + oi * kLineH + kLineH * 0.5f;
        rect.outputs.push_back({sorted_outputs_vec[oi].second, rect.x + rect.w, py});
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

            uint32_t n_param_inputs = 0;
            for (const auto& [name, _] : ns.param_indices)
                if (!ns.input_port_indices.count(name)) n_param_inputs++;
            uint32_t n_inputs = static_cast<uint32_t>(ns.input_port_indices.size()) + n_param_inputs;
            uint32_t n_outputs = static_cast<uint32_t>(ns.output_port_indices.size());
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
        uint32_t n_param_inputs = 0;
        for (const auto& [name, _] : ns.param_indices)
            if (!ns.input_port_indices.count(name)) n_param_inputs++;
        uint32_t n_inputs = static_cast<uint32_t>(ns.input_port_indices.size()) + n_param_inputs;
        uint32_t n_outputs = static_cast<uint32_t>(ns.output_port_indices.size());
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
    try {
        float val = std::stof(edit_buffer_);
        const auto* ns = snap_.find_node(edit_node_id_);
        if (ns && ns->op_info) {
            for (const auto& pd : ns->op_info->params) {
                if (pd.name != edit_param_name_) continue;
                val = std::max(pd.min_value, std::min(pd.max_value, val));
                if (pd.type == VIVID_PARAM_INT) val = std::round(val);
                commands_.set_param(edit_node_id_, edit_param_name_, val);
                break;
            }
        }
    } catch (...) {
        // Invalid input — silently discard
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
static bool is_control_type(VividPortType t) {
    return t == VIVID_PORT_CONTROL_FLOAT || t == VIVID_PORT_CONTROL_INT ||
           t == VIVID_PORT_CONTROL_BOOL  || t == VIVID_PORT_CONTROL_SPREAD;
}

static bool port_type_compatible(VividPortType wire_type, VividPortType port_type) {
    if (wire_type == VIVID_PORT_GPU_TEXTURE)   return port_type == VIVID_PORT_GPU_TEXTURE;
    if (wire_type == VIVID_PORT_AUDIO_FLOAT)   return port_type == VIVID_PORT_AUDIO_FLOAT;
    // Any control type matches any control type
    return is_control_type(wire_type) && is_control_type(port_type);
}

static bool can_insert_on_wire(const OperatorInfo& op, VividPortType src, VividPortType dst) {
    bool has_input = false, has_output = false;
    for (const auto& p : op.ports) {
        if (p.direction == VIVID_PORT_INPUT  && port_type_compatible(src, p.type)) has_input = true;
        if (p.direction == VIVID_PORT_OUTPUT && port_type_compatible(dst, p.type)) has_output = true;
        if (has_input && has_output) return true;
    }
    return false;
}

static std::string find_compatible_port(const OperatorInfo& op, VividPortType wire_type,
                                        VividPortDirection dir) {
    for (const auto& p : op.ports) {
        if (p.direction == dir && port_type_compatible(wire_type, p.type))
            return p.name;
    }
    return {};
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
        chooser_items_.push_back(name);
    }

    chooser_sel_ = 0;
    chooser_scroll_ = 0;
}

// -----------------------------------------------------------------------
// Shared chooser confirm — creates node and optionally splices into wire
// -----------------------------------------------------------------------
void NodeGraphUI::confirm_chooser_selection(const std::string& type) {
    // Generate unique node ID
    std::string id;
    for (int n = 1; ; ++n) {
        id = type + std::to_string(n);
        if (!snap_.has_node(id)) break;
    }
    commands_.add_node(type, id);
    commands_.set_node_layout(id, chooser_cursor_gx_, chooser_cursor_gy_);

    if (chooser_insert_wire_) {
        auto cat_it = snap_.operator_catalog.find(type);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second) {
            const auto& op = *cat_it->second;
            // Remove original wire
            commands_.disconnect(
                chooser_insert_conn_.from_node + "/" + chooser_insert_conn_.from_port,
                chooser_insert_conn_.to_node   + "/" + chooser_insert_conn_.to_port);
            // Find compatible ports on the new node
            std::string in_port  = find_compatible_port(op, insert_wire_source_type_, VIVID_PORT_INPUT);
            std::string out_port = find_compatible_port(op, insert_wire_dest_type_,   VIVID_PORT_OUTPUT);
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

    selected_node_ids_ = { id };
    chooser_insert_wire_ = false;
    chooser_open_ = false;
}

// -----------------------------------------------------------------------
// Update — thin dispatcher calling decomposed sub-methods
// -----------------------------------------------------------------------
void NodeGraphUI::update(const GraphSnapshot& snapshot) {
    snap_ = snapshot;
    snap_valid_ = true;

    // MIDI map mode: capture CC event when waiting for knob wiggle
    if (midi_map_waiting_ && !snap_.pending_cc_events.empty()) {
        const auto& ev = snap_.pending_cc_events[0];
        // Look up param descriptor for default range
        float range_min = 0.0f, range_max = 1.0f;
        const auto* ns = snap_.find_node(midi_map_node_id_);
        if (ns && ns->op_info) {
            for (const auto& pd : ns->op_info->params) {
                if (pd.name == midi_map_param_name_) {
                    range_min = pd.min_value;
                    range_max = pd.max_value;
                    break;
                }
            }
        }
        commands_.add_midi_mapping(midi_map_node_id_, midi_map_param_name_,
                                   ev.cc_number, 0, range_min, range_max);
        midi_map_waiting_ = false;
    }

    check_relayout();
    update_pan();
    update_node_drag();
    update_box_select();
    update_wire_drag();
    update_scrollbar_drag();
    update_slider_drag();
    update_drum_mod_drag();
    update_chooser_hover();
    update_preferences();    // may consume left_clicked
    update_clone_confirm();  // may consume left_clicked
    update_context_menu();   // may consume left_clicked
    handle_right_click();
    handle_left_click();     // dispatches to sub-handlers
    update_pan_release();
    clear_frame_flags();
    update_wire_hover();
    update_sparklines();
}

void NodeGraphUI::check_relayout() {
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
            std::string to_node = node_rects_[ph.node_idx].node_id;
            commands_.connect(wire_from_node_id_ + "/" + wire_from_port_,
                         to_node + "/" + ph.port_name);
        }
        dragging_wire_ = false;
    }
}

void NodeGraphUI::update_slider_drag() {
    if (active_slider_idx_ < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down) {
        const auto& s = slider_rects_[active_slider_idx_];
        const auto* ns = snap_.find_node(active_slider_node_id_);
        if (ns && ns->op_info) {
            for (const auto& pd : ns->op_info->params) {
                if (pd.name != active_slider_param_name_) continue;
                float t = (mouse_.x - s.x) / s.w;
                t = std::max(0.0f, std::min(1.0f, t));
                float val = pd.min_value + t * (pd.max_value - pd.min_value);
                if (pd.type == VIVID_PARAM_INT) {
                    val = std::round(val);
                }
                commands_.set_param(active_slider_node_id_, active_slider_param_name_, val);
                break;
            }
        }
    }
    if (mouse_.left_released) {
        active_slider_idx_ = -1;
    }
}

void NodeGraphUI::update_drum_mod_drag() {
    if (active_drum_mod_idx_ < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down) {
        // Find the rect in whichever mod vector was clicked
        const auto& rects_a = drum_mod_a_rects_;
        const auto& rects_b = drum_mod_b_rects_;
        const InspectorRect* rect = nullptr;
        if (active_drum_mod_idx_ < static_cast<int>(rects_a.size()) &&
            rects_a[active_drum_mod_idx_].param_name == active_drum_mod_param_name_) {
            rect = &rects_a[active_drum_mod_idx_];
        } else if (active_drum_mod_idx_ < static_cast<int>(rects_b.size()) &&
                   rects_b[active_drum_mod_idx_].param_name == active_drum_mod_param_name_) {
            rect = &rects_b[active_drum_mod_idx_];
        }
        if (rect) {
            float cell_pad = 2.0f;
            float inner_y = rect->y + cell_pad;
            float inner_h = rect->h - 2 * cell_pad;
            float t = 1.0f - (mouse_.y - inner_y) / inner_h;
            t = std::max(0.0f, std::min(1.0f, t));
            commands_.set_param(active_drum_mod_node_id_, active_drum_mod_param_name_, t);
        }
    }
    if (mouse_.left_released) {
        active_drum_mod_idx_ = -1;
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

void NodeGraphUI::set_style_options(std::vector<UIStyle> styles, int current_idx) {
    prefs_styles_ = std::move(styles);
    prefs_style_sel_ = current_idx;
    prefs_saved_style_sel_ = current_idx;
    if (current_idx >= 0 && current_idx < static_cast<int>(prefs_styles_.size())) {
        style_ = prefs_styles_[current_idx];
    }
}

} // namespace vivid::ui
