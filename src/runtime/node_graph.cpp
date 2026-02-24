#include "runtime/node_graph.h"
#include "runtime/runtime_api.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/text_renderer.h"
#include "runtime/operator_loader.h"
#include "operator_api/types.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cstdio>
#include <cmath>

namespace vivid {

// Layout constants
static constexpr float kGraphX = 0.0f;
static constexpr float kGraphY = 0.0f;
static constexpr float kGraphW = 960.0f;
static constexpr float kGraphH = 640.0f;
static constexpr float kInspectorX = 960.0f;
static constexpr float kInspectorW = 320.0f;
static constexpr float kNodeW = 140.0f;
static constexpr float kColSpacing = 180.0f;
static constexpr float kRowSpacing = 16.0f;
static constexpr float kPortDotSize = 4.0f;
static constexpr float kLeftMargin = 30.0f;
static constexpr float kTopMargin = 30.0f;
static constexpr float kLineH = 18.0f;  // line height for node internals
static constexpr float kNodePadY = 8.0f;

// Colors
static constexpr float kNodeBg[] = { 0.12f, 0.13f, 0.15f };
static constexpr float kNodeSelBg[] = { 0.18f, 0.22f, 0.30f };
static constexpr float kConnColor[] = { 0.4f, 0.5f, 0.55f, 0.5f };
static constexpr float kConnSelColor[] = { 0.5f, 0.65f, 0.75f, 0.8f };
static constexpr float kInspBg[] = { 0.10f, 0.11f, 0.13f };
static constexpr float kAccent[] = { 0.35f, 0.55f, 0.85f };
static constexpr float kDimText[] = { 0.55f, 0.58f, 0.62f };
static constexpr float kSliderTrack[] = { 0.18f, 0.19f, 0.22f };
static constexpr float kSliderFill[] = { 0.25f, 0.42f, 0.68f };

NodeGraphUI::NodeGraphUI(RuntimeAPI& api, const Graph& graph, const Scheduler& scheduler)
    : api_(api), graph_(graph), scheduler_(scheduler) {}

void NodeGraphUI::on_mouse_move(float x, float y) {
    mouse_.x = x;
    mouse_.y = y;
}

void NodeGraphUI::on_mouse_button(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            mouse_.left_down = true;
            mouse_.left_clicked = true;
        } else if (action == GLFW_RELEASE) {
            mouse_.left_down = false;
            mouse_.left_released = true;
        }
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

    // Build node_id → index map
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
            // Forward pass
            for (int l = 1; l <= max_layer; ++l) {
                std::vector<std::pair<float, uint32_t>> bary;
                for (uint32_t n : layers[l]) {
                    float sum = 0; int count = 0;
                    for (uint32_t p : preds[n]) {
                        // Find position of p in its layer
                        auto& prev = layers[l - 1];
                        for (int j = 0; j < (int)prev.size(); ++j) {
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
            // Backward pass
            for (int l = max_layer - 1; l >= 0; --l) {
                std::vector<std::pair<float, uint32_t>> bary;
                for (uint32_t n : layers[l]) {
                    float sum = 0; int count = 0;
                    for (uint32_t s : succs[n]) {
                        auto& next = layers[l + 1];
                        for (int j = 0; j < (int)next.size(); ++j) {
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
            uint32_t n_inputs = static_cast<uint32_t>(ns.input_port_indices.size());
            uint32_t n_outputs = static_cast<uint32_t>(ns.output_port_indices.size());
            uint32_t port_rows = std::max(n_inputs, n_outputs);
            float h = kNodePadY + kLineH * 2 + port_rows * kLineH + kNodePadY;
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
            rect.x = col_x;
            rect.y = cur_y;
            rect.w = kNodeW;
            rect.h = heights[r];

            // Compute port positions — collect sorted port names for deterministic layout
            rect.inputs.clear();
            rect.outputs.clear();

            // Sort input ports by their index for stable ordering
            std::vector<std::pair<uint32_t, std::string>> sorted_inputs;
            for (const auto& [name, idx] : ns.input_port_indices)
                sorted_inputs.push_back({idx, name});
            std::sort(sorted_inputs.begin(), sorted_inputs.end());

            std::vector<std::pair<uint32_t, std::string>> sorted_outputs;
            for (const auto& [name, idx] : ns.output_port_indices)
                sorted_outputs.push_back({idx, name});
            std::sort(sorted_outputs.begin(), sorted_outputs.end());

            float port_start_y = rect.y + kNodePadY + kLineH * 2;
            for (size_t pi = 0; pi < sorted_inputs.size(); ++pi) {
                float py = port_start_y + pi * kLineH + kLineH * 0.5f;
                rect.inputs.push_back({sorted_inputs[pi].second, rect.x, py});
            }
            for (size_t pi = 0; pi < sorted_outputs.size(); ++pi) {
                float py = port_start_y + pi * kLineH + kLineH * 0.5f;
                rect.outputs.push_back({sorted_outputs[pi].second, rect.x + rect.w, py});
            }

            cur_y += heights[r] + kRowSpacing;
        }
    }

    last_node_count_ = nodes.size();
    last_conn_count_ = conns.size();
}

// -----------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------
void NodeGraphUI::draw_graph(TextRenderer& tr) {
    for (size_t i = 0; i < node_rects_.size(); ++i) {
        const auto& r = node_rects_[i];
        bool selected = (r.node_id == selected_node_id_);
        const float* bg = selected ? kNodeSelBg : kNodeBg;

        // Node background
        tr.draw_rect(r.x, r.y, r.w, r.h, bg[0], bg[1], bg[2], 0.92f);

        // Type name (centered at top)
        float tw = tr.text_width(r.type_name.c_str());
        float tx = r.x + (r.w - tw) * 0.5f;
        tr.draw_text(tx, r.y + kNodePadY, r.type_name.c_str(), 1.0f, 1.0f, 1.0f);

        // Node ID below type
        float iw = tr.text_width(r.node_id.c_str());
        float ix = r.x + (r.w - iw) * 0.5f;
        tr.draw_text(ix, r.y + kNodePadY + kLineH, r.node_id.c_str(),
                     kDimText[0], kDimText[1], kDimText[2]);

        // Input port dots and labels
        for (const auto& p : r.inputs) {
            tr.draw_rect(p.x - kPortDotSize, p.y - kPortDotSize * 0.5f,
                         kPortDotSize, kPortDotSize,
                         kAccent[0], kAccent[1], kAccent[2]);
            tr.draw_text(p.x + 4, p.y - tr.line_height() * 0.5f, p.name.c_str(),
                         kDimText[0], kDimText[1], kDimText[2]);
        }
        // Output port dots and labels
        for (const auto& p : r.outputs) {
            tr.draw_rect(p.x, p.y - kPortDotSize * 0.5f,
                         kPortDotSize, kPortDotSize,
                         kAccent[0], kAccent[1], kAccent[2]);
            float lw = tr.text_width(p.name.c_str());
            tr.draw_text(p.x - lw - 4, p.y - tr.line_height() * 0.5f, p.name.c_str(),
                         kDimText[0], kDimText[1], kDimText[2]);
        }
    }
}

void NodeGraphUI::draw_connections(TextRenderer& tr) {
    const auto& conns = graph_.connections();

    // Build fast lookup: node_id → index in node_rects_
    std::unordered_map<std::string, size_t> id_to_rect;
    for (size_t i = 0; i < node_rects_.size(); ++i)
        id_to_rect[node_rects_[i].node_id] = i;

    for (const auto& c : conns) {
        auto fi = id_to_rect.find(c.from_node);
        auto ti = id_to_rect.find(c.to_node);
        if (fi == id_to_rect.end() || ti == id_to_rect.end()) continue;

        const auto& from_rect = node_rects_[fi->second];
        const auto& to_rect = node_rects_[ti->second];

        // Find output port position
        float sx = from_rect.x + from_rect.w, sy = from_rect.y + from_rect.h * 0.5f;
        for (const auto& p : from_rect.outputs) {
            if (p.name == c.from_port) { sx = p.x; sy = p.y; break; }
        }
        // Find input port position
        float ex = to_rect.x, ey = to_rect.y + to_rect.h * 0.5f;
        for (const auto& p : to_rect.inputs) {
            if (p.name == c.to_port) { ex = p.x; ey = p.y; break; }
        }

        bool sel = (c.from_node == selected_node_id_ || c.to_node == selected_node_id_);
        const float* col = sel ? kConnSelColor : kConnColor;
        float a = sel ? kConnSelColor[3] : kConnColor[3];

        // Z-route: horizontal from source → vertical → horizontal to dest
        float mid_x = (sx + ex) * 0.5f;
        // Horizontal segment from source
        tr.draw_rect(sx, sy - 1, mid_x - sx, 2, col[0], col[1], col[2], a);
        // Vertical segment
        float vy = std::min(sy, ey);
        float vh = std::fabs(ey - sy);
        tr.draw_rect(mid_x - 1, vy, 2, vh + 2, col[0], col[1], col[2], a);
        // Horizontal segment to dest
        tr.draw_rect(mid_x, ey - 1, ex - mid_x, 2, col[0], col[1], col[2], a);
    }
}

void NodeGraphUI::draw_inspector(TextRenderer& tr, uint32_t w) {
    slider_rects_.clear();
    bool_rects_.clear();

    // Inspector background
    tr.draw_rect(kInspectorX, 0, kInspectorW, kGraphH, kInspBg[0], kInspBg[1], kInspBg[2], 0.95f);
    // Separator line
    tr.draw_rect(kInspectorX, 0, 2, kGraphH, 0.25f, 0.27f, 0.30f);

    if (selected_node_id_.empty()) {
        tr.draw_text(kInspectorX + 16, 20, "Select a node", kDimText[0], kDimText[1], kDimText[2]);
        return;
    }

    // Find the selected node in scheduler
    const auto& sched_nodes = scheduler_.nodes();
    const NodeState* sel_node = nullptr;
    for (const auto& ns : sched_nodes) {
        if (ns.node_id == selected_node_id_) { sel_node = &ns; break; }
    }
    if (!sel_node) {
        tr.draw_text(kInspectorX + 16, 20, "Node not found", kDimText[0], kDimText[1], kDimText[2]);
        return;
    }

    const auto* desc = sel_node->loader->descriptor();
    float px = kInspectorX + 16;
    float py = 16;
    float panel_w = kInspectorW - 32;

    // Header: type name
    tr.draw_text(px, py, desc->name, 1.0f, 1.0f, 1.0f);
    py += kLineH;
    // Node ID
    tr.draw_text(px, py, selected_node_id_.c_str(), kDimText[0], kDimText[1], kDimText[2]);
    py += kLineH + 8;

    // Separator
    tr.draw_rect(px, py, panel_w, 1, 0.25f, 0.27f, 0.30f);
    py += 8;

    // Parameters
    for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
        const auto& pd = desc->params[pi];
        float val = sel_node->param_values[pi];

        // Label
        tr.draw_text(px, py, pd.name, 0.8f, 0.82f, 0.85f);

        // Value text
        char val_buf[32];
        if (pd.type == VIVID_PARAM_BOOL) {
            std::snprintf(val_buf, sizeof(val_buf), "%s", val > 0.5f ? "true" : "false");
        } else if (pd.type == VIVID_PARAM_INT) {
            std::snprintf(val_buf, sizeof(val_buf), "%d", static_cast<int>(val));
        } else {
            std::snprintf(val_buf, sizeof(val_buf), "%.2f", val);
        }
        float vw = tr.text_width(val_buf);
        tr.draw_text(px + panel_w - vw, py, val_buf, 0.8f, 0.82f, 0.85f);
        py += kLineH;

        if (pd.type == VIVID_PARAM_BOOL) {
            // Toggle square
            float bx = px, by = py;
            float bsz = 14.0f;
            tr.draw_rect(bx, by, bsz, bsz, kSliderTrack[0], kSliderTrack[1], kSliderTrack[2]);
            if (val > 0.5f) {
                tr.draw_rect(bx + 2, by + 2, bsz - 4, bsz - 4,
                             kAccent[0], kAccent[1], kAccent[2]);
            }
            bool_rects_.push_back({bx, by, bsz, bsz, selected_node_id_, pd.name});
            py += bsz + 6;
        } else {
            // Slider track
            float sx = px, sy = py;
            float sw = panel_w, sh = 10.0f;
            tr.draw_rect(sx, sy, sw, sh, kSliderTrack[0], kSliderTrack[1], kSliderTrack[2]);

            // Fill
            float range = pd.max_value - pd.min_value;
            float t = (range > 0) ? (val - pd.min_value) / range : 0.0f;
            t = std::max(0.0f, std::min(1.0f, t));
            tr.draw_rect(sx, sy, sw * t, sh, kSliderFill[0], kSliderFill[1], kSliderFill[2]);

            // Thumb
            float thumb_x = sx + sw * t - 3;
            tr.draw_rect(thumb_x, sy - 2, 6, sh + 4, kAccent[0], kAccent[1], kAccent[2]);

            slider_rects_.push_back({sx, sy, sw, sh, selected_node_id_, pd.name});
            py += sh + 10;
        }
    }

    // Separator before outputs
    py += 4;
    tr.draw_rect(px, py, panel_w, 1, 0.25f, 0.27f, 0.30f);
    py += 8;

    // Output values
    tr.draw_text(px, py, "Outputs", kDimText[0], kDimText[1], kDimText[2]);
    py += kLineH;

    // Sort outputs by index for stable display
    std::vector<std::pair<uint32_t, std::string>> sorted_outputs;
    for (const auto& [name, idx] : sel_node->output_port_indices)
        sorted_outputs.push_back({idx, name});
    std::sort(sorted_outputs.begin(), sorted_outputs.end());

    for (const auto& [idx, name] : sorted_outputs) {
        char line[64];
        std::snprintf(line, sizeof(line), "%s = %.4f", name.c_str(), sel_node->output_values[idx]);
        tr.draw_text(px, py, line, kDimText[0], kDimText[1], kDimText[2]);
        py += kLineH;
    }
}

// -----------------------------------------------------------------------
// Hit testing
// -----------------------------------------------------------------------
int NodeGraphUI::hit_test_node(float mx, float my) const {
    for (int i = static_cast<int>(node_rects_.size()) - 1; i >= 0; --i) {
        const auto& r = node_rects_[i];
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h)
            return i;
    }
    return -1;
}

int NodeGraphUI::hit_test_slider(float mx, float my) const {
    for (int i = 0; i < static_cast<int>(slider_rects_.size()); ++i) {
        const auto& s = slider_rects_[i];
        // Expand hit area vertically for easier dragging
        if (mx >= s.x && mx <= s.x + s.w && my >= s.y - 4 && my <= s.y + s.h + 4)
            return i;
    }
    return -1;
}

int NodeGraphUI::hit_test_bool(float mx, float my) const {
    for (int i = 0; i < static_cast<int>(bool_rects_.size()); ++i) {
        const auto& b = bool_rects_[i];
        if (mx >= b.x && mx <= b.x + b.w && my >= b.y && my <= b.y + b.h)
            return i;
    }
    return -1;
}

// -----------------------------------------------------------------------
// Update (process mouse input)
// -----------------------------------------------------------------------
void NodeGraphUI::update() {
    // Re-layout if topology changed
    size_t cur_nodes = scheduler_.nodes().size();
    size_t cur_conns = graph_.connections().size();
    if (cur_nodes != last_node_count_ || cur_conns != last_conn_count_) {
        layout_nodes();
    }

    // Active slider drag
    if (active_slider_idx_ >= 0) {
        if (mouse_.left_down) {
            // Map mouse.x to slider value
            const auto& s = slider_rects_[active_slider_idx_];

            // Find param descriptor for min/max
            const auto& sched_nodes = scheduler_.nodes();
            for (const auto& ns : sched_nodes) {
                if (ns.node_id != active_slider_node_id_) continue;
                const auto* desc = ns.loader->descriptor();
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
                break;
            }
        }
        if (mouse_.left_released) {
            active_slider_idx_ = -1;
        }
    }

    // Click handling
    if (mouse_.left_clicked) {
        // Priority: inspector sliders/bools > graph nodes
        if (mouse_.x >= kInspectorX && mouse_.y < kGraphH) {
            int si = hit_test_slider(mouse_.x, mouse_.y);
            if (si >= 0) {
                active_slider_idx_ = si;
                active_slider_node_id_ = slider_rects_[si].node_id;
                active_slider_param_name_ = slider_rects_[si].param_name;
            } else {
                int bi = hit_test_bool(mouse_.x, mouse_.y);
                if (bi >= 0) {
                    // Toggle bool
                    const auto& br = bool_rects_[bi];
                    const auto& sched_nodes = scheduler_.nodes();
                    for (const auto& ns : sched_nodes) {
                        if (ns.node_id != br.node_id) continue;
                        auto it = ns.param_indices.find(br.param_name);
                        if (it != ns.param_indices.end()) {
                            float cur = ns.param_values[it->second];
                            api_.set_param(br.node_id, br.param_name, cur > 0.5f ? 0.0f : 1.0f);
                        }
                        break;
                    }
                }
            }
        } else if (mouse_.x < kGraphW && mouse_.y < kGraphH) {
            int ni = hit_test_node(mouse_.x, mouse_.y);
            if (ni >= 0) {
                selected_node_id_ = node_rects_[ni].node_id;
            } else {
                selected_node_id_.clear();
            }
        }
    }

    // Clear one-frame flags
    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

// -----------------------------------------------------------------------
// Draw
// -----------------------------------------------------------------------
void NodeGraphUI::draw(TextRenderer& tr, uint32_t w, uint32_t h) {
    if (node_rects_.empty() && !scheduler_.nodes().empty()) {
        layout_nodes();
    }

    draw_connections(tr);
    draw_graph(tr);
    draw_inspector(tr, w);
}

} // namespace vivid
