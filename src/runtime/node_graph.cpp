#include "runtime/node_graph.h"
#include "runtime/node_graph_constants.h"
#include "runtime/string_util.h"
#include "runtime/runtime_api.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/operator_loader.h"
#include "runtime/operator_registry.h"
#include "operator_api/types.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cmath>
#include <cctype>

namespace vivid {

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
NodeGraphUI::NodeGraphUI(RuntimeAPI& api, const Graph& graph, const Scheduler& scheduler,
                         AudioEngine* audio_engine)
    : api_(api), graph_(graph), scheduler_(scheduler), audio_engine_(audio_engine) {}

float NodeGraphUI::graph_right() const {
    return has_selection() ? kInspectorX : static_cast<float>(win_w_);
}

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
const NodeState* NodeGraphUI::find_sched_node(const std::string& id) const {
    for (const auto& ns : scheduler_.nodes()) {
        if (ns.node_id == id) return &ns;
    }
    return nullptr;
}

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

// Explicit instantiations for all rect types used across translation units
template int NodeGraphUI::hit_test_rect(const std::vector<SliderRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<BoolRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<ValueTextRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<DropdownRect>& rects, float mx, float my);
template int NodeGraphUI::hit_test_rect(const std::vector<ResolutionRect>& rects, float mx, float my);

// -----------------------------------------------------------------------
// Port position helper
// -----------------------------------------------------------------------
void NodeGraphUI::recompute_ports(NodeRect& rect, const NodeState& ns) {
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
void NodeGraphUI::layout_nodes() {
    node_rects_.clear();
    const auto& nodes = scheduler_.nodes();
    const auto& conns = graph_.connections();
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

    // Topological order (Kahn's algorithm)
    std::vector<uint32_t> in_degree(nodes.size(), 0);
    for (uint32_t i = 0; i < nodes.size(); ++i)
        in_degree[i] = static_cast<uint32_t>(preds[i].size());

    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < nodes.size(); ++i)
        if (in_degree[i] == 0) q.push(i);

    std::vector<uint32_t> topo_order;
    topo_order.reserve(nodes.size());
    while (!q.empty()) {
        uint32_t n = q.front(); q.pop();
        topo_order.push_back(n);
        for (uint32_t s : succs[n]) {
            if (--in_degree[s] == 0) q.push(s);
        }
    }
    // If cycle, append remaining
    if (topo_order.size() < nodes.size()) {
        std::unordered_set<uint32_t> visited(topo_order.begin(), topo_order.end());
        for (uint32_t i = 0; i < nodes.size(); ++i)
            if (visited.find(i) == visited.end()) topo_order.push_back(i);
    }

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

            VividDomain dom = ns.loader->descriptor()->domain;
            bool has_ct = custom_thumb_nodes_.count(ns.node_id) > 0;
            float body_h = domain_body_height(dom, has_ct);

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
        float start_y = kTopMargin + (kGraphH - 2 * kTopMargin - total_h) * 0.5f;
        if (start_y < kTopMargin) start_y = kTopMargin;

        float cur_y = start_y;
        for (size_t r = 0; r < layers[l].size(); ++r) {
            uint32_t ni = layers[l][r];
            const auto& ns = nodes[ni];
            auto& rect = node_rects_[ni];
            rect.node_id = ns.node_id;
            rect.type_name = scheduler_.type_name(ni);
            rect.domain = ns.loader->descriptor()->domain;
            rect.x = col_x;
            rect.y = cur_y;
            rect.w = kNodeW;
            rect.h = heights[r];

            // Override with saved layout position if present
            const NodeDef* ndef = graph_.find_node(ns.node_id);
            if (ndef && ndef->has_layout()) {
                rect.x = ndef->layout_x;
                rect.y = ndef->layout_y;
            }

            recompute_ports(rect, ns);

            cur_y += heights[r] + kRowSpacing;
        }
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
    const auto& conns = graph_.connections();

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

        if (bezier_wires_) {
            auto pts = wire_bezier_points(ssx, ssy, sex, sey);
            float px = ssx, py = ssy;
            for (int seg = 1; seg <= kBezierSegments; ++seg) {
                float t = static_cast<float>(seg) / kBezierSegments;
                float nx, ny;
                eval_bezier(t, pts[0].first, pts[0].second, pts[1].first, pts[1].second,
                            pts[2].first, pts[2].second, pts[3].first, pts[3].second, nx, ny);
                if (point_seg_dist2(sx, sy, px, py, nx, ny) < thresh2)
                    return ci;
                px = nx; py = ny;
            }
        } else {
            auto segs = wire_zroute_segments(ssx, ssy, sex, sey);
            if (point_seg_dist2(sx, sy, segs[0].first.first, segs[0].first.second,
                                segs[0].second.first, segs[0].second.second) < thresh2)
                return ci;
            if (point_seg_dist2(sx, sy, segs[1].first.first, segs[1].first.second,
                                segs[1].second.first, segs[1].second.second) < thresh2)
                return ci;
            if (point_seg_dist2(sx, sy, segs[2].first.first, segs[2].first.second,
                                segs[2].second.first, segs[2].second.second) < thresh2)
                return ci;
        }
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
        const NodeState* ns = find_sched_node(edit_node_id_);
        if (ns) {
            const auto* desc = ns->loader ? ns->loader->descriptor() : nullptr;
            if (desc) {
                for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                    if (std::string(desc->params[pi].name) != edit_param_name_) continue;
                    const auto& pd = desc->params[pi];
                    val = std::max(pd.min_value, std::min(pd.max_value, val));
                    if (pd.type == VIVID_PARAM_INT) val = std::round(val);
                    api_.set_param(edit_node_id_, edit_param_name_, val);
                    break;
                }
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

        const NodeState* ns = find_sched_node(edit_res_node_id_);
        if (ns) {
            uint32_t new_w = edit_res_is_width_ ? static_cast<uint32_t>(val) : ns->gpu_tex_width;
            uint32_t new_h = edit_res_is_width_ ? ns->gpu_tex_height : static_cast<uint32_t>(val);
            api_.set_resolution(edit_res_node_id_, new_w, new_h);
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

// -----------------------------------------------------------------------
// Chooser filter
// -----------------------------------------------------------------------
void NodeGraphUI::rebuild_chooser_items() {
    if (!registry_) return;
    auto all = registry_->type_names();
    std::sort(all.begin(), all.end());
    chooser_items_.clear();

    // Case-insensitive substring match
    std::string lower_filter = chooser_filter_;
    for (auto& c : lower_filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const auto& name : all) {
        std::string lower_name = name;
        for (auto& c : lower_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower_name.find(lower_filter) != std::string::npos)
            chooser_items_.push_back(name);
    }

    chooser_sel_ = 0;
    chooser_scroll_ = 0;
}

// -----------------------------------------------------------------------
// Update — thin dispatcher calling decomposed sub-methods
// -----------------------------------------------------------------------
void NodeGraphUI::update() {
    check_relayout();
    update_pan();
    update_node_drag();
    update_wire_drag();
    update_slider_drag();
    update_chooser_hover();
    update_context_menu();   // may consume left_clicked
    handle_right_click();
    handle_left_click();     // dispatches to sub-handlers
    update_pan_release();
    clear_frame_flags();
    update_wire_hover();
    update_sparklines();
}

void NodeGraphUI::check_relayout() {
    size_t cur_nodes = scheduler_.nodes().size();
    size_t cur_conns = graph_.connections().size();
    if (cur_nodes > last_node_count_) {
        if (dragging_node_idx_ < 0 && !dragging_wire_) {
            layout_nodes();
        }
    } else if (cur_nodes != last_node_count_ || cur_conns != last_conn_count_) {
        last_node_count_ = cur_nodes;
        last_conn_count_ = cur_conns;
    }
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
        auto& rect = node_rects_[dragging_node_idx_];
        rect.x = sx_to_gx(mouse_.x) - drag_offset_x_;
        rect.y = sy_to_gy(mouse_.y) - drag_offset_y_;
        const NodeState* ns = find_sched_node(rect.node_id);
        if (ns) recompute_ports(rect, *ns);
    }
    if (mouse_.left_released) {
        auto& rect = node_rects_[dragging_node_idx_];
        api_.set_node_layout(rect.node_id, rect.x, rect.y);
        dragging_node_idx_ = -1;
    }
}

void NodeGraphUI::update_wire_drag() {
    if (mouse_.left_released && dragging_wire_) {
        PortHit ph = hit_test_port(mouse_.x, mouse_.y);
        if (ph.node_idx >= 0 && !ph.is_output) {
            std::string to_node = node_rects_[ph.node_idx].node_id;
            api_.connect(wire_from_node_id_ + "/" + wire_from_port_,
                         to_node + "/" + ph.port_name);
        }
        dragging_wire_ = false;
    }
}

void NodeGraphUI::update_slider_drag() {
    if (active_slider_idx_ < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down) {
        const auto& s = slider_rects_[active_slider_idx_];
        const NodeState* ns = find_sched_node(active_slider_node_id_);
        if (ns) {
            const auto* desc = ns->loader->descriptor();
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                if (std::string(desc->params[pi].name) != active_slider_param_name_) continue;
                float t = (mouse_.x - s.x) / s.w;
                t = std::max(0.0f, std::min(1.0f, t));
                float val = desc->params[pi].min_value + t * (desc->params[pi].max_value - desc->params[pi].min_value);
                if (desc->params[pi].type == VIVID_PARAM_INT) {
                    val = std::round(val);
                }
                api_.set_param(active_slider_node_id_, active_slider_param_name_, val);
                break;
            }
        }
    }
    if (mouse_.left_released) {
        active_slider_idx_ = -1;
    }
}

void NodeGraphUI::update_chooser_hover() {
    if (!chooser_open_) return;
    float items_y = kChooserY + kChooserHeaderH;
    int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
    if (mouse_.x >= kChooserX && mouse_.x <= kChooserX + kChooserW &&
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
    if (!dragging_wire_ && !panning_ && dragging_node_idx_ < 0 &&
        !context_menu_open_ && !chooser_open_ && !dropdown_open_) {
        hovered_wire_idx_ = hit_test_wire(mouse_.x, mouse_.y);
    } else {
        hovered_wire_idx_ = -1;
    }
}

void NodeGraphUI::update_sparklines() {
    const auto& sched_nodes = scheduler_.nodes();
    for (const auto& ns : sched_nodes) {
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

} // namespace vivid
