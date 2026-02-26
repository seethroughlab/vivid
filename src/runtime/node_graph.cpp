#include "runtime/node_graph.h"
#include "runtime/runtime_api.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/text_renderer.h"
#include "runtime/audio_engine.h"
#include "runtime/thumbnail_cache.h"
#include "runtime/thumbnail_renderer.h"
#include "runtime/operator_loader.h"
#include "runtime/operator_registry.h"
#include "operator_api/types.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cstdio>
#include <cmath>
#include <cctype>

namespace vivid {

// Layout constants
static constexpr float kGraphX = 0.0f;
static constexpr float kGraphY = 0.0f;
static constexpr float kGraphW = 960.0f;
static constexpr float kGraphH = 640.0f;
static constexpr float kInspectorX = 960.0f;
static constexpr float kInspectorW = 320.0f;
static constexpr float kNodeW = 140.0f;
static constexpr float kColSpacing = 200.0f;
static constexpr float kRowSpacing = 16.0f;
static constexpr float kPortDotSize = 6.0f;
static constexpr float kLeftMargin = 30.0f;
static constexpr float kTopMargin = 30.0f;
static constexpr float kLineH = 18.0f;
static constexpr float kNodePadY = 8.0f;

// Domain body heights (step 2)
static constexpr float kAccentBarH = 3.0f;
static constexpr float kGpuThumbH = 88.0f;    // 140 * 10/16 ≈ 87.5
static constexpr float kAudioWaveH = 40.0f;
static constexpr float kControlSparkH = 30.0f;

// Colors
static constexpr float kNodeBg[] = { 0.12f, 0.13f, 0.15f };
static constexpr float kNodeSelBg[] = { 0.18f, 0.22f, 0.30f };
static constexpr float kConnColor[] = { 0.5f, 0.6f, 0.65f, 0.7f };
static constexpr float kConnSelColor[] = { 0.6f, 0.75f, 0.85f, 0.9f };
static constexpr float kInspBg[] = { 0.10f, 0.11f, 0.13f };
static constexpr float kAccent[] = { 0.35f, 0.55f, 0.85f };
static constexpr float kDimText[] = { 0.55f, 0.58f, 0.62f };
static constexpr float kSliderTrack[] = { 0.18f, 0.19f, 0.22f };
static constexpr float kSliderFill[] = { 0.25f, 0.42f, 0.68f };
static constexpr float kDarkBg[] = { 0.07f, 0.08f, 0.09f };

// Domain accent colors (step 1)
static constexpr float kGpuAccent[] = { 0.306f, 0.804f, 0.769f };     // #4ECDC4 cyan
static constexpr float kAudioAccent[] = { 0.941f, 0.627f, 0.188f };   // #F0A030 amber
static constexpr float kControlAccent[] = { 0.753f, 0.784f, 0.816f }; // #C0C8D0 gray

static const float* domain_color(VividDomain domain) {
    switch (domain) {
        case VIVID_DOMAIN_GPU:     return kGpuAccent;
        case VIVID_DOMAIN_AUDIO:   return kAudioAccent;
        case VIVID_DOMAIN_CONTROL: return kControlAccent;
        default:                   return kControlAccent;
    }
}

static float domain_body_height(VividDomain domain, bool has_custom_thumb = false) {
    if (has_custom_thumb && domain != VIVID_DOMAIN_GPU) return kGpuThumbH;
    switch (domain) {
        case VIVID_DOMAIN_GPU:     return kGpuThumbH;
        case VIVID_DOMAIN_AUDIO:   return kAudioWaveH;
        case VIVID_DOMAIN_CONTROL: return kControlSparkH;
        default:                   return kControlSparkH;
    }
}

// Bezier wire rendering
static constexpr int kBezierSegments = 30;

static void eval_bezier(float t, float x0, float y0, float x1, float y1,
                         float x2, float y2, float x3, float y3,
                         float& ox, float& oy) {
    float u = 1.0f - t;
    float uu = u * u, uuu = uu * u;
    float tt = t * t, ttt = tt * t;
    ox = uuu * x0 + 3 * uu * t * x1 + 3 * u * tt * x2 + ttt * x3;
    oy = uuu * y0 + 3 * uu * t * y1 + 3 * u * tt * y2 + ttt * y3;
}

NodeGraphUI::NodeGraphUI(RuntimeAPI& api, const Graph& graph, const Scheduler& scheduler,
                         AudioEngine* audio_engine)
    : api_(api), graph_(graph), scheduler_(scheduler), audio_engine_(audio_engine) {}

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
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            mouse_.right_clicked = true;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            panning_ = true;
            pan_start_mx_ = mouse_.x;
            pan_start_my_ = mouse_.y;
            pan_start_px_ = pan_x_;
            pan_start_py_ = pan_y_;
        } else if (action == GLFW_RELEASE) {
            panning_ = false;
        }
    }
}

void NodeGraphUI::on_scroll(float /*x_offset*/, float y_offset) {
    // Only zoom when cursor is in graph area
    if (mouse_.x >= graph_right()) return;

    float factor = std::pow(1.12f, y_offset);
    float new_zoom = zoom_ * factor;
    new_zoom = std::max(0.4f, std::min(2.5f, new_zoom));

    // Pivot around mouse cursor
    float gx = sx_to_gx(mouse_.x);
    float gy = sy_to_gy(mouse_.y);
    zoom_ = new_zoom;
    pan_x_ = mouse_.x - gx * zoom_;
    pan_y_ = mouse_.y - gy * zoom_;
}

float NodeGraphUI::graph_right() const {
    return has_selection() ? kInspectorX : static_cast<float>(win_w_);
}

// -----------------------------------------------------------------------
// Port position helper
// -----------------------------------------------------------------------
void NodeGraphUI::recompute_ports(NodeRect& rect, const NodeState& ns) {
    bool has_ct = custom_thumb_nodes_.count(rect.node_id) > 0;
    float body_h = domain_body_height(rect.domain, has_ct);

    rect.inputs.clear();
    rect.outputs.clear();

    std::vector<std::pair<uint32_t, std::string>> sorted_inputs;
    for (const auto& [name, idx] : ns.input_port_indices)
        sorted_inputs.push_back({idx, name});
    std::sort(sorted_inputs.begin(), sorted_inputs.end());

    std::vector<std::pair<uint32_t, std::string>> sorted_outputs;
    for (const auto& [name, idx] : ns.output_port_indices)
        sorted_outputs.push_back({idx, name});
    std::sort(sorted_outputs.begin(), sorted_outputs.end());

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

    for (size_t pi = 0; pi < sorted_outputs.size(); ++pi) {
        float py = port_start_y + pi * kLineH + kLineH * 0.5f;
        rect.outputs.push_back({sorted_outputs[pi].second, rect.x + rect.w, py});
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
            for (int l = 1; l <= max_layer; ++l) {
                std::vector<std::pair<float, uint32_t>> bary;
                for (uint32_t n : layers[l]) {
                    float sum = 0; int count = 0;
                    for (uint32_t p : preds[n]) {
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
// Drawing
// -----------------------------------------------------------------------
void NodeGraphUI::draw_graph(TextRenderer& tr) {
    const auto& sched_nodes = scheduler_.nodes();

    for (size_t i = 0; i < node_rects_.size(); ++i) {
        const auto& r = node_rects_[i];
        bool selected = (r.node_id == selected_node_id_);
        const float* bg = selected ? kNodeSelBg : kNodeBg;
        const float* dcol = domain_color(r.domain);

        // Transform graph-space rect to screen space
        float sx = gx_to_sx(r.x), sy = gy_to_sy(r.y);
        float sw = g_to_s(r.w), sh = g_to_s(r.h);

        // Node background
        tr.draw_rect(sx, sy, sw, sh, bg[0], bg[1], bg[2]);

        // Accent bar at top (step 1)
        float s_accent_h = g_to_s(kAccentBarH);
        tr.draw_rect(sx, sy, sw, s_accent_h, dcol[0], dcol[1], dcol[2]);

        // --- Domain body region ---
        float s_body_y = sy + s_accent_h;
        bool has_ct = custom_thumb_nodes_.count(r.node_id) > 0;
        float body_h = domain_body_height(r.domain, has_ct);
        float s_body_h = g_to_s(body_h);

        if (r.domain == VIVID_DOMAIN_CONTROL && !has_ct) {
            // Sparkline (step 3)
            tr.draw_rect(sx + g_to_s(2), s_body_y + g_to_s(2),
                         sw - g_to_s(4), s_body_h - g_to_s(4),
                         kDarkBg[0], kDarkBg[1], kDarkBg[2], 0.9f);

            // Find sparkline data for this node's first output
            std::string spark_key;
            if (i < sched_nodes.size()) {
                const auto& ns = sched_nodes[i];
                // Find first output port
                std::vector<std::pair<uint32_t, std::string>> sorted_outs;
                for (const auto& [name, idx] : ns.output_port_indices)
                    sorted_outs.push_back({idx, name});
                std::sort(sorted_outs.begin(), sorted_outs.end());
                if (!sorted_outs.empty())
                    spark_key = ns.node_id + "/" + sorted_outs[0].second;
            }

            auto it = sparklines_.find(spark_key);
            if (it != sparklines_.end() && !spark_key.empty()) {
                const auto& sd = it->second;
                uint32_t count = sd.filled ? kSparklineLen : sd.write_idx;
                if (count > 0) {
                    // Current value text (left side)
                    uint32_t last_idx = (sd.write_idx == 0 ? kSparklineLen - 1 : sd.write_idx - 1);
                    float cur_val = sd.values[last_idx];
                    char val_buf[16];
                    std::snprintf(val_buf, sizeof(val_buf), "%.2f", cur_val);
                    tr.draw_text(sx + g_to_s(5), s_body_y + g_to_s(4), val_buf,
                                 dcol[0], dcol[1], dcol[2], 1.0f, zoom_);

                    // Sparkline plot (right side)
                    float spark_x = sx + g_to_s(52);
                    float spark_w = sw - g_to_s(56);
                    float spark_y = s_body_y + g_to_s(4);
                    float spark_h = s_body_h - g_to_s(8);

                    // Find min/max
                    float vmin = sd.values[0], vmax = sd.values[0];
                    for (uint32_t si = 0; si < count; ++si) {
                        uint32_t idx = sd.filled ? (sd.write_idx + si) % kSparklineLen : si;
                        float v = sd.values[idx];
                        if (v < vmin) vmin = v;
                        if (v > vmax) vmax = v;
                    }
                    float range = vmax - vmin;
                    if (range < 0.001f) range = 1.0f;

                    float bar_w = spark_w / kSparklineLen;
                    for (uint32_t si = 0; si < count; ++si) {
                        uint32_t idx = sd.filled ? (sd.write_idx + si) % kSparklineLen : si;
                        float v = sd.values[idx];
                        float t = (v - vmin) / range;
                        float bh = std::max(1.0f, t * spark_h);
                        float bx = spark_x + si * bar_w;
                        float by = spark_y + spark_h - bh;
                        tr.draw_rect(bx, by, std::max(1.0f, bar_w - 0.5f), bh,
                                     dcol[0], dcol[1], dcol[2], 0.7f);
                    }
                }
            }
        } else if (r.domain == VIVID_DOMAIN_AUDIO && !has_ct) {
            // Waveform (step 4)
            tr.draw_rect(sx + g_to_s(2), s_body_y + g_to_s(2),
                         sw - g_to_s(4), s_body_h - g_to_s(4),
                         kDarkBg[0], kDarkBg[1], kDarkBg[2], 0.9f);

            if (audio_engine_) {
                int ae_idx = audio_engine_->audio_node_index(r.node_id);
                if (ae_idx >= 0) {
                    const auto& snap = audio_engine_->analysis_read();
                    if (ae_idx < static_cast<int>(snap.waveform.size())) {
                        const auto& wave = snap.waveform[ae_idx];
                        float wave_x = sx + g_to_s(4);
                        float wave_w = sw - g_to_s(8);
                        float wave_y = s_body_y + g_to_s(4);
                        float wave_h = s_body_h - g_to_s(10);
                        float center_y = wave_y + wave_h * 0.5f;

                        // Center line
                        tr.draw_rect(wave_x, center_y, wave_w, 1,
                                     dcol[0], dcol[1], dcol[2], 0.2f);

                        // Waveform bars
                        constexpr uint32_t kWaveN = AnalysisSnapshot::kWaveformSamples;
                        float bar_w = wave_w / kWaveN;
                        for (uint32_t si = 0; si < kWaveN; ++si) {
                            float amp = wave[si];
                            float bh = std::fabs(amp) * wave_h * 0.5f;
                            bh = std::max(0.5f, bh);
                            float bx = wave_x + si * bar_w;
                            float by = (amp >= 0) ? center_y - bh : center_y;
                            tr.draw_rect(bx, by, std::max(0.5f, bar_w - 0.3f), bh,
                                         dcol[0], dcol[1], dcol[2], 0.8f);
                        }

                        // Peak meter strip at bottom
                        float peak_y = s_body_y + s_body_h - g_to_s(4);
                        if (ae_idx < static_cast<int>(snap.peak.size())) {
                            float pk = std::min(1.0f, snap.peak[ae_idx]);
                            tr.draw_rect(wave_x, peak_y, wave_w * pk, g_to_s(2),
                                         dcol[0], dcol[1], dcol[2], 0.9f);
                        }
                    }
                }
            }
        }
        // GPU domain: body region left blank (thumbnails drawn in separate pass)

        // Type name (centered, below accent bar + body)
        float text_y = sy + s_accent_h + s_body_h + g_to_s(kNodePadY);
        float tw = tr.text_width(r.type_name.c_str(), zoom_);
        float tx = sx + (sw - tw) * 0.5f;
        tr.draw_text(tx, text_y, r.type_name.c_str(), 1.0f, 1.0f, 1.0f, 1.0f, zoom_);

        // Node ID below type
        float iw = tr.text_width(r.node_id.c_str(), zoom_);
        float ix = sx + (sw - iw) * 0.5f;
        tr.draw_text(ix, text_y + g_to_s(kLineH), r.node_id.c_str(),
                     kDimText[0], kDimText[1], kDimText[2], 1.0f, zoom_);

        // Input port dots and labels (use domain color)
        float s_dot = kPortDotSize * zoom_;
        float s_line_h = tr.line_height() * zoom_;
        for (const auto& p : r.inputs) {
            float spx = gx_to_sx(p.x), spy = gy_to_sy(p.y);
            tr.draw_rect(spx - s_dot, spy - s_dot * 0.5f,
                         s_dot, s_dot,
                         dcol[0], dcol[1], dcol[2]);
            tr.draw_text(spx + g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         kDimText[0], kDimText[1], kDimText[2], 1.0f, zoom_);
        }
        // Output port dots and labels (use domain color)
        for (const auto& p : r.outputs) {
            float spx = gx_to_sx(p.x), spy = gy_to_sy(p.y);
            tr.draw_rect(spx, spy - s_dot * 0.5f,
                         s_dot, s_dot,
                         dcol[0], dcol[1], dcol[2]);
            float lw = tr.text_width(p.name.c_str(), zoom_);
            tr.draw_text(spx - lw - g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         kDimText[0], kDimText[1], kDimText[2], 1.0f, zoom_);
        }
    }
}

void NodeGraphUI::draw_connections(TextRenderer& tr) {
    const auto& conns = graph_.connections();

    // Build fast lookup: node_id → index in node_rects_
    std::unordered_map<std::string, size_t> id_to_rect;
    for (size_t i = 0; i < node_rects_.size(); ++i)
        id_to_rect[node_rects_[i].node_id] = i;

    for (int ci = 0; ci < static_cast<int>(conns.size()); ++ci) {
        const auto& c = conns[ci];
        auto fi = id_to_rect.find(c.from_node);
        auto ti = id_to_rect.find(c.to_node);
        if (fi == id_to_rect.end() || ti == id_to_rect.end()) continue;

        const auto& from_rect = node_rects_[fi->second];
        const auto& to_rect = node_rects_[ti->second];

        // Find output port position in graph space
        float gsx = from_rect.x + from_rect.w;
        float gsy = from_rect.y + from_rect.h * 0.5f;
        for (const auto& p : from_rect.outputs) {
            if (p.name == c.from_port) { gsx = p.x; gsy = p.y; break; }
        }
        // Find input port position in graph space
        float gex = to_rect.x;
        float gey = to_rect.y + to_rect.h * 0.5f;
        for (const auto& p : to_rect.inputs) {
            if (p.name == c.to_port) { gex = p.x; gey = p.y; break; }
        }

        // Transform to screen space
        float ssx = gx_to_sx(gsx), ssy = gy_to_sy(gsy);
        float sex = gx_to_sx(gex), sey = gy_to_sy(gey);

        // Domain-colored wires (source node's accent color)
        const float* dcol = domain_color(from_rect.domain);
        bool sel = (c.from_node == selected_node_id_ || c.to_node == selected_node_id_);
        bool hov = (ci == hovered_wire_idx_);
        float brightness = (hov || sel) ? 1.3f : 1.0f;
        float cr = std::min(1.0f, dcol[0] * brightness);
        float cg = std::min(1.0f, dcol[1] * brightness);
        float cb = std::min(1.0f, dcol[2] * brightness);
        float a = (hov || sel) ? 0.95f : 0.8f;

        float wire_th = std::max(1.0f, (hov ? 5.0f : 3.0f) * zoom_);

        if (bezier_wires_) {
            // Cubic bezier with horizontal tangents
            float cp_off = std::fabs(sex - ssx) * 0.5f;
            float px = ssx, py = ssy;
            for (int seg = 1; seg <= kBezierSegments; ++seg) {
                float t = static_cast<float>(seg) / kBezierSegments;
                float nx, ny;
                eval_bezier(t, ssx, ssy, ssx + cp_off, ssy,
                            sex - cp_off, sey, sex, sey, nx, ny);
                tr.draw_line(px, py, nx, ny, wire_th, cr, cg, cb, a);
                px = nx; py = ny;
            }
        } else {
            // Z-route: horizontal from source → vertical → horizontal to dest
            float mid_x = (ssx + sex) * 0.5f;
            tr.draw_rect(ssx, ssy - 1, mid_x - ssx, wire_th, cr, cg, cb, a);
            float vy = std::min(ssy, sey);
            float vh = std::fabs(sey - ssy);
            tr.draw_rect(mid_x - 1, vy, wire_th, vh + wire_th, cr, cg, cb, a);
            tr.draw_rect(mid_x, sey - 1, sex - mid_x, wire_th, cr, cg, cb, a);
        }
    }
}

void NodeGraphUI::draw_wire_tooltip(TextRenderer& tr) {
    if (hovered_wire_idx_ < 0) return;

    const auto& conns = graph_.connections();
    if (hovered_wire_idx_ >= static_cast<int>(conns.size())) return;
    const auto& c = conns[hovered_wire_idx_];

    // Find source node in scheduler to read current value
    const auto& sched_nodes = scheduler_.nodes();
    const NodeState* src_ns = nullptr;
    for (const auto& ns : sched_nodes) {
        if (ns.node_id == c.from_node) { src_ns = &ns; break; }
    }

    // Build label line: "from_node/port -> to_node/port"
    char label[256];
    std::snprintf(label, sizeof(label), "%s/%s -> %s/%s",
                  c.from_node.c_str(), c.from_port.c_str(),
                  c.to_node.c_str(), c.to_port.c_str());

    // Build value line
    char value_str[128] = "";
    if (src_ns) {
        auto it = src_ns->output_port_indices.find(c.from_port);
        if (it != src_ns->output_port_indices.end()) {
            uint32_t pidx = it->second;
            if (pidx < src_ns->output_values.size()) {
                float val = src_ns->output_values[pidx];
                // Check for spread data
                if (pidx < src_ns->output_spreads.size() &&
                    !src_ns->output_spreads[pidx].empty()) {
                    std::snprintf(value_str, sizeof(value_str), "%.4f [spread: %zu]",
                                  val, src_ns->output_spreads[pidx].size());
                } else {
                    std::snprintf(value_str, sizeof(value_str), "%.4f", val);
                }
            }
        }
    }

    // Measure text and compute popup dimensions
    float label_w = tr.text_width(label);
    float value_w = value_str[0] ? tr.text_width(value_str) : 0.0f;
    float pad = 8.0f;
    float popup_w = std::max(label_w, value_w) + pad * 2;
    float line_h = 16.0f;
    float popup_h = (value_str[0] ? line_h * 2 : line_h) + pad * 2;

    // Position near cursor, offset down-right, clamped to graph bounds
    float px = mouse_.x + 14;
    float py = mouse_.y + 14;
    if (px + popup_w > graph_right()) px = mouse_.x - popup_w - 6;
    if (py + popup_h > static_cast<float>(win_h_)) py = mouse_.y - popup_h - 6;

    // Find domain color for accent from source node rect
    const float* dcol = nullptr;
    for (const auto& r : node_rects_) {
        if (r.node_id == c.from_node) { dcol = domain_color(r.domain); break; }
    }

    // Background
    tr.draw_rect(px, py, popup_w, popup_h, 0.10f, 0.11f, 0.13f, 0.95f);
    // Accent line at top
    if (dcol) {
        tr.draw_rect(px, py, popup_w, 2.0f, dcol[0], dcol[1], dcol[2], 0.9f);
    }
    // Label text
    tr.draw_text(px + pad, py + pad, label, kDimText[0], kDimText[1], kDimText[2]);
    // Value text
    if (value_str[0]) {
        tr.draw_text(px + pad, py + pad + line_h, value_str, 0.9f, 0.92f, 0.95f);
    }
}

void NodeGraphUI::draw_inspector(TextRenderer& tr, uint32_t w, uint32_t h) {
    slider_rects_.clear();
    bool_rects_.clear();
    value_text_rects_.clear();
    dropdown_rects_.clear();
    resolution_rects_.clear();

    if (selected_node_id_.empty()) return;

    // Inspector background
    tr.draw_rect(kInspectorX, 0, kInspectorW, static_cast<float>(h), kInspBg[0], kInspBg[1], kInspBg[2], 0.95f);
    // Separator line
    tr.draw_rect(kInspectorX, 0, 2, static_cast<float>(h), 0.25f, 0.27f, 0.30f);

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

        bool is_editing_this = editing_param_ &&
                               edit_node_id_ == selected_node_id_ &&
                               edit_param_name_ == pd.name;

        // Label
        tr.draw_text(px, py, pd.name, 0.8f, 0.82f, 0.85f);

        // Value text (right-aligned on the label line)
        char val_buf[32];
        if (pd.type == VIVID_PARAM_BOOL) {
            std::snprintf(val_buf, sizeof(val_buf), "%s", val > 0.5f ? "true" : "false");
        } else if (pd.choice_count > 0) {
            int idx = static_cast<int>(val);
            if (idx >= 0 && idx < static_cast<int>(pd.choice_count))
                std::snprintf(val_buf, sizeof(val_buf), "%s", pd.choice_labels[idx]);
            else
                std::snprintf(val_buf, sizeof(val_buf), "%d", idx);
        } else if (pd.type == VIVID_PARAM_INT) {
            std::snprintf(val_buf, sizeof(val_buf), "%d", static_cast<int>(val));
        } else {
            std::snprintf(val_buf, sizeof(val_buf), "%.2f", val);
        }

        float vw = tr.text_width(val_buf);
        float val_x = px + panel_w - vw;
        float val_y = py;

        if (is_editing_this) {
            // Draw text-edit field in place of value text
            float edit_w = panel_w * 0.4f;
            float edit_x = px + panel_w - edit_w;
            float edit_h = kLineH;
            tr.draw_rect(edit_x - 1, val_y - 1, edit_w + 2, edit_h + 2,
                         kAccent[0], kAccent[1], kAccent[2]);
            tr.draw_rect(edit_x, val_y, edit_w, edit_h, 0.08f, 0.09f, 0.11f);
            std::string display = edit_buffer_ + "_";
            tr.draw_text(edit_x + 2, val_y, display.c_str(), 0.95f, 0.95f, 0.95f);
        } else {
            tr.draw_text(val_x, py, val_buf, 0.8f, 0.82f, 0.85f);
            // Track value text rect for click-to-edit (not for bools or enums)
            if (pd.type != VIVID_PARAM_BOOL && pd.choice_count == 0) {
                value_text_rects_.push_back({val_x, val_y, vw, kLineH,
                                             selected_node_id_, pd.name});
            }
        }
        py += kLineH;

        if (pd.type == VIVID_PARAM_BOOL) {
            float bx = px, by = py;
            float bsz = 14.0f;
            tr.draw_rect(bx, by, bsz, bsz, kSliderTrack[0], kSliderTrack[1], kSliderTrack[2]);
            if (val > 0.5f) {
                tr.draw_rect(bx + 2, by + 2, bsz - 4, bsz - 4,
                             kAccent[0], kAccent[1], kAccent[2]);
            }
            bool_rects_.push_back({bx, by, bsz, bsz, selected_node_id_, pd.name});
            py += bsz + 6;
        } else if (pd.choice_count > 0) {
            // Dropdown row for enum params
            float dx = px, dy = py;
            float dw = panel_w, dh = 18.0f;
            tr.draw_rect(dx, dy, dw, dh, kSliderTrack[0], kSliderTrack[1], kSliderTrack[2]);
            // Show current label
            int idx = static_cast<int>(val);
            const char* label = (idx >= 0 && idx < static_cast<int>(pd.choice_count))
                                ? pd.choice_labels[idx] : "?";
            tr.draw_text(dx + 6, dy + 1, label, 0.9f, 0.92f, 0.95f);
            // Down-arrow indicator
            float arrow_x = dx + dw - 16;
            tr.draw_text(arrow_x, dy + 1, "\xE2\x96\xBE", kDimText[0], kDimText[1], kDimText[2]);
            dropdown_rects_.push_back({dx, dy, dw, dh, selected_node_id_, pd.name});
            py += dh + 6;
        } else {
            // Normal slider
            float sx = px, sy = py;
            float sw = panel_w, sh = 10.0f;

            tr.draw_rect(sx, sy, sw, sh, kSliderTrack[0], kSliderTrack[1], kSliderTrack[2]);

            float range = pd.max_value - pd.min_value;
            float t = (range > 0) ? (val - pd.min_value) / range : 0.0f;
            t = std::max(0.0f, std::min(1.0f, t));
            tr.draw_rect(sx, sy, sw * t, sh, kSliderFill[0], kSliderFill[1], kSliderFill[2]);

            float thumb_x = sx + sw * t - 3;
            tr.draw_rect(thumb_x, sy - 2, 6, sh + 4, kAccent[0], kAccent[1], kAccent[2]);

            slider_rects_.push_back({sx, sy, sw, sh, selected_node_id_, pd.name});
            py += sh + 10;
        }
    }

    // GPU texture resolution (editable for generators, read-only for filters)
    if (sel_node->is_gpu && sel_node->gpu_tex_width > 0 && sel_node->gpu_tex_height > 0) {
        bool is_generator = sel_node->texture_input_port_indices.empty() && !sel_node->is_gpu_sink;

        py += 4;
        tr.draw_rect(px, py, panel_w, 1, 0.25f, 0.27f, 0.30f);
        py += 8;

        tr.draw_text(px, py, "Resolution", kDimText[0], kDimText[1], kDimText[2]);
        py += kLineH;

        // Width
        bool editing_w = editing_resolution_ &&
                         edit_res_node_id_ == selected_node_id_ && edit_res_is_width_;
        bool editing_h = editing_resolution_ &&
                         edit_res_node_id_ == selected_node_id_ && !edit_res_is_width_;

        char w_buf[16], h_buf[16];
        if (editing_w) {
            std::snprintf(w_buf, sizeof(w_buf), "%s_", edit_buffer_.c_str());
        } else {
            std::snprintf(w_buf, sizeof(w_buf), "%u", sel_node->gpu_tex_width);
        }
        if (editing_h) {
            std::snprintf(h_buf, sizeof(h_buf), "%s_", edit_buffer_.c_str());
        } else {
            std::snprintf(h_buf, sizeof(h_buf), "%u", sel_node->gpu_tex_height);
        }

        float val_x = px + 4;
        float w_text_w = 40.0f;

        if (is_generator) {
            // Clickable width value
            if (editing_w) {
                tr.draw_rect(val_x, py, w_text_w, kLineH, 0.12f, 0.14f, 0.18f);
            }
            tr.draw_text(val_x, py, w_buf,
                         editing_w ? 1.0f : 0.8f,
                         editing_w ? 1.0f : 0.82f,
                         editing_w ? 1.0f : 0.85f);
            resolution_rects_.push_back({val_x, py, w_text_w, kLineH,
                                         selected_node_id_, true});
        } else {
            tr.draw_text(val_x, py, w_buf, 0.5f, 0.52f, 0.55f);
        }

        tr.draw_text(val_x + w_text_w, py, " x ", kDimText[0], kDimText[1], kDimText[2]);

        float h_val_x = val_x + w_text_w + 24.0f;

        if (is_generator) {
            // Clickable height value
            if (editing_h) {
                tr.draw_rect(h_val_x, py, w_text_w, kLineH, 0.12f, 0.14f, 0.18f);
            }
            tr.draw_text(h_val_x, py, h_buf,
                         editing_h ? 1.0f : 0.8f,
                         editing_h ? 1.0f : 0.82f,
                         editing_h ? 1.0f : 0.85f);
            resolution_rects_.push_back({h_val_x, py, w_text_w, kLineH,
                                         selected_node_id_, false});
        } else {
            tr.draw_text(h_val_x, py, h_buf, 0.5f, 0.52f, 0.55f);
        }

        py += kLineH;
    }

    // Separator before outputs
    py += 4;
    tr.draw_rect(px, py, panel_w, 1, 0.25f, 0.27f, 0.30f);
    py += 8;

    // Output values
    tr.draw_text(px, py, "Outputs", kDimText[0], kDimText[1], kDimText[2]);
    py += kLineH;

    std::vector<std::pair<uint32_t, std::string>> sorted_outputs;
    for (const auto& [name, idx] : sel_node->output_port_indices)
        sorted_outputs.push_back({idx, name});
    std::sort(sorted_outputs.begin(), sorted_outputs.end());

    for (const auto& [idx, name] : sorted_outputs) {
        char line[64];
        if (idx < sel_node->output_spreads.size() && !sel_node->output_spreads[idx].empty()) {
            std::snprintf(line, sizeof(line), "%s = [%u bins]", name.c_str(),
                          static_cast<unsigned>(sel_node->output_spreads[idx].size()));
        } else {
            std::snprintf(line, sizeof(line), "%s = %.4f", name.c_str(), sel_node->output_values[idx]);
        }
        tr.draw_text(px, py, line, kDimText[0], kDimText[1], kDimText[2]);
        py += kLineH;
    }
}

// -----------------------------------------------------------------------
// Operator chooser
// -----------------------------------------------------------------------
static constexpr int kChooserMaxVisible = 12;
static constexpr float kChooserW = 300.0f;
static constexpr float kChooserHeaderH = 28.0f;
static constexpr float kChooserItemH = 22.0f;
static constexpr float kChooserX = (kGraphW - kChooserW) * 0.5f;
static constexpr float kChooserY = 80.0f;

// Context menu constants
static constexpr float kCtxMenuW = 120.0f;
static constexpr float kCtxMenuItemH = 22.0f;
static constexpr float kCtxMenuPadTop = 3.0f;  // accent bar + padding

void NodeGraphUI::on_key(int key, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (editing_param_) {
        if (key == GLFW_KEY_ENTER)       confirm_param_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_param_edit();
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
        return;  // consume all keys while editing
    }

    if (editing_resolution_) {
        if (key == GLFW_KEY_ENTER)       confirm_resolution_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_resolution_edit();
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
        return;
    }

    if (context_menu_open_) {
        if (key == GLFW_KEY_ESCAPE) context_menu_open_ = false;
        return;
    }

    if (dropdown_open_) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                dropdown_open_ = false;
                break;
            case GLFW_KEY_UP:
                if (dropdown_sel_ > 0) dropdown_sel_--;
                break;
            case GLFW_KEY_DOWN:
                if (dropdown_sel_ < static_cast<int>(dropdown_labels_.size()) - 1)
                    dropdown_sel_++;
                break;
            case GLFW_KEY_ENTER:
                api_.set_param(dropdown_node_id_, dropdown_param_name_,
                               static_cast<float>(dropdown_sel_));
                dropdown_open_ = false;
                break;
        }
        return;
    }

    if (!chooser_open_) {
        // Tab opens the chooser (only if cursor is in graph area)
        if (key == GLFW_KEY_TAB && action == GLFW_PRESS && registry_) {
            if (mouse_.x < graph_right()) {
                chooser_cursor_gx_ = sx_to_gx(mouse_.x);
                chooser_cursor_gy_ = sy_to_gy(mouse_.y);
                chooser_filter_.clear();
                chooser_sel_ = 0;
                chooser_scroll_ = 0;
                chooser_items_ = registry_->type_names();
                std::sort(chooser_items_.begin(), chooser_items_.end());
                chooser_open_ = true;
            }
        }
        // B toggles bezier wire rendering
        if (key == GLFW_KEY_B && action == GLFW_PRESS) {
            bezier_wires_ = !bezier_wires_;
        }
        // Delete selected node (Delete or Backspace)
        if ((key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) && action == GLFW_PRESS) {
            if (!selected_node_id_.empty()) {
                api_.remove_node(selected_node_id_);
                selected_node_id_.clear();
            }
        }
        return;
    }

    // Chooser is open
    switch (key) {
        case GLFW_KEY_ESCAPE:
            chooser_open_ = false;
            break;

        case GLFW_KEY_ENTER: {
            if (!chooser_items_.empty() && chooser_sel_ >= 0 &&
                chooser_sel_ < static_cast<int>(chooser_items_.size())) {
                const std::string& type = chooser_items_[chooser_sel_];
                // Generate unique ID: type1, type2, ...
                std::string id;
                for (int n = 1; ; ++n) {
                    id = type + std::to_string(n);
                    if (!graph_.find_node(id)) break;
                }
                api_.add_node(type, id);
                api_.set_node_layout(id, chooser_cursor_gx_, chooser_cursor_gy_);
                selected_node_id_ = id;
            }
            chooser_open_ = false;
            break;
        }

        case GLFW_KEY_UP:
            if (chooser_sel_ > 0) {
                chooser_sel_--;
                if (chooser_sel_ < chooser_scroll_)
                    chooser_scroll_ = chooser_sel_;
            }
            break;

        case GLFW_KEY_DOWN:
            if (chooser_sel_ < static_cast<int>(chooser_items_.size()) - 1) {
                chooser_sel_++;
                if (chooser_sel_ >= chooser_scroll_ + kChooserMaxVisible)
                    chooser_scroll_ = chooser_sel_ - kChooserMaxVisible + 1;
            }
            break;

        case GLFW_KEY_BACKSPACE:
            if (!chooser_filter_.empty()) {
                chooser_filter_.pop_back();
                rebuild_chooser_items();
            }
            break;

        default:
            break;
    }
}

void NodeGraphUI::on_char(unsigned int codepoint) {
    if (editing_param_) {
        char ch = static_cast<char>(codepoint);
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-')
            edit_buffer_ += ch;
        return;
    }
    if (editing_resolution_) {
        char ch = static_cast<char>(codepoint);
        if (std::isdigit(static_cast<unsigned char>(ch)))
            edit_buffer_ += ch;
        return;
    }
    if (!chooser_open_) return;
    if (codepoint >= 32 && codepoint < 127) {
        chooser_filter_ += static_cast<char>(codepoint);
        rebuild_chooser_items();
    }
}

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

void NodeGraphUI::draw_preview_wire(TextRenderer& tr) {
    if (!dragging_wire_) return;
    float ssx = gx_to_sx(wire_from_gx_), ssy = gy_to_sy(wire_from_gy_);
    float sex = mouse_.x, sey = mouse_.y;
    float wire_th = std::max(1.0f, 3.0f * zoom_);

    if (bezier_wires_) {
        float cp_off = std::fabs(sex - ssx) * 0.5f;
        float px = ssx, py = ssy;
        for (int seg = 1; seg <= kBezierSegments; ++seg) {
            float t = static_cast<float>(seg) / kBezierSegments;
            float nx, ny;
            eval_bezier(t, ssx, ssy, ssx + cp_off, ssy,
                        sex - cp_off, sey, sex, sey, nx, ny);
            tr.draw_line(px, py, nx, ny, wire_th, 1.0f, 1.0f, 1.0f, 0.5f);
            px = nx; py = ny;
        }
    } else {
        float mid_x = (ssx + sex) * 0.5f;
        // Z-route: horizontal → vertical → horizontal (white, alpha 0.5)
        tr.draw_rect(ssx, ssy - 1, mid_x - ssx, wire_th, 1.0f, 1.0f, 1.0f, 0.5f);
        float vy = std::min(ssy, sey);
        float vh = std::fabs(sey - ssy);
        tr.draw_rect(mid_x - 1, vy, wire_th, vh + wire_th, 1.0f, 1.0f, 1.0f, 0.5f);
        tr.draw_rect(mid_x, sey - 1, sex - mid_x, wire_th, 1.0f, 1.0f, 1.0f, 0.5f);
    }
}

void NodeGraphUI::draw_chooser(TextRenderer& tr) {
    if (!chooser_open_) return;

    int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
    if (visible == 0) visible = 1; // show at least the header area
    float panel_h = kChooserHeaderH + visible * kChooserItemH + 4;

    float px = kChooserX;
    float py = kChooserY;

    // Background
    tr.draw_rect(px, py, kChooserW, panel_h, kInspBg[0], kInspBg[1], kInspBg[2], 0.97f);
    // Top accent bar
    tr.draw_rect(px, py, kChooserW, 2, kAccent[0], kAccent[1], kAccent[2]);

    // Filter text
    float tx = px + 8;
    float ty = py + 6;
    std::string display_filter = chooser_filter_ + "_";
    tr.draw_text(tx, ty, display_filter.c_str(), 1.0f, 1.0f, 1.0f);

    // Items
    float iy = py + kChooserHeaderH;
    for (int i = 0; i < visible; ++i) {
        int idx = chooser_scroll_ + i;
        if (idx >= static_cast<int>(chooser_items_.size())) break;

        float item_y = iy + i * kChooserItemH;

        // Highlight selected
        if (idx == chooser_sel_) {
            tr.draw_rect(px + 2, item_y, kChooserW - 4, kChooserItemH,
                         kNodeSelBg[0], kNodeSelBg[1], kNodeSelBg[2], 0.9f);
        }

        // Domain color dot
        const std::string& name = chooser_items_[idx];
        const float* dcol = kControlAccent; // default
        if (registry_) {
            auto* loader = registry_->find(name);
            if (loader && loader->descriptor()) {
                dcol = domain_color(loader->descriptor()->domain);
            }
        }
        float dot_x = px + 10;
        float dot_y = item_y + (kChooserItemH - 6) * 0.5f;
        tr.draw_rect(dot_x, dot_y, 6, 6, dcol[0], dcol[1], dcol[2]);

        // Type name
        tr.draw_text(px + 22, item_y + 3, name.c_str(), 0.85f, 0.87f, 0.90f);
    }

    // Show "no matches" if empty
    if (chooser_items_.empty()) {
        tr.draw_text(px + 8, iy + 3, "no matches", kDimText[0], kDimText[1], kDimText[2]);
    }
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

int NodeGraphUI::hit_test_slider(float mx, float my) const {
    for (int i = 0; i < static_cast<int>(slider_rects_.size()); ++i) {
        const auto& s = slider_rects_[i];
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

int NodeGraphUI::hit_test_value_text(float mx, float my) const {
    for (int i = 0; i < static_cast<int>(value_text_rects_.size()); ++i) {
        const auto& v = value_text_rects_[i];
        if (mx >= v.x && mx <= v.x + v.w && my >= v.y && my <= v.y + v.h)
            return i;
    }
    return -1;
}

int NodeGraphUI::hit_test_dropdown(float mx, float my) const {
    for (int i = 0; i < static_cast<int>(dropdown_rects_.size()); ++i) {
        const auto& d = dropdown_rects_[i];
        if (mx >= d.x && mx <= d.x + d.w && my >= d.y && my <= d.y + d.h)
            return i;
    }
    return -1;
}

int NodeGraphUI::hit_test_resolution(float mx, float my) const {
    for (int i = 0; i < static_cast<int>(resolution_rects_.size()); ++i) {
        const auto& r = resolution_rects_[i];
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h)
            return i;
    }
    return -1;
}

// Point-to-segment squared distance helper
static float point_seg_dist2(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float len2 = dx * dx + dy * dy;
    float t = (len2 > 0) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    float cx = ax + t * dx, cy = ay + t * dy;
    float ex = px - cx, ey = py - cy;
    return ex * ex + ey * ey;
}

int NodeGraphUI::hit_test_wire(float sx, float sy) const {
    const auto& conns = graph_.connections();

    // Build fast lookup: node_id → index in node_rects_
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

        // Find port positions in graph space (same as draw_connections)
        float gsx = from_rect.x + from_rect.w, gsy = from_rect.y + from_rect.h * 0.5f;
        for (const auto& p : from_rect.outputs)
            if (p.name == c.from_port) { gsx = p.x; gsy = p.y; break; }
        float gex = to_rect.x, gey = to_rect.y + to_rect.h * 0.5f;
        for (const auto& p : to_rect.inputs)
            if (p.name == c.to_port) { gex = p.x; gey = p.y; break; }

        float ssx = gx_to_sx(gsx), ssy = gy_to_sy(gsy);
        float sex = gx_to_sx(gex), sey = gy_to_sy(gey);

        if (bezier_wires_) {
            float cp_off = std::fabs(sex - ssx) * 0.5f;
            float px = ssx, py = ssy;
            for (int seg = 1; seg <= kBezierSegments; ++seg) {
                float t = static_cast<float>(seg) / kBezierSegments;
                float nx, ny;
                eval_bezier(t, ssx, ssy, ssx + cp_off, ssy,
                            sex - cp_off, sey, sex, sey, nx, ny);
                if (point_seg_dist2(sx, sy, px, py, nx, ny) < thresh2)
                    return ci;
                px = nx; py = ny;
            }
        } else {
            float mid_x = (ssx + sex) * 0.5f;
            // Horizontal from source to mid
            if (point_seg_dist2(sx, sy, ssx, ssy, mid_x, ssy) < thresh2) return ci;
            // Vertical at mid
            if (point_seg_dist2(sx, sy, mid_x, ssy, mid_x, sey) < thresh2) return ci;
            // Horizontal from mid to dest
            if (point_seg_dist2(sx, sy, mid_x, sey, sex, sey) < thresh2) return ci;
        }
    }
    return -1;
}

void NodeGraphUI::confirm_param_edit() {
    if (!editing_param_) return;
    try {
        float val = std::stof(edit_buffer_);
        const auto& sched_nodes = scheduler_.nodes();
        for (const auto& ns : sched_nodes) {
            if (ns.node_id != edit_node_id_) continue;
            const auto* desc = ns.loader ? ns.loader->descriptor() : nullptr;
            if (!desc) break;
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                if (std::string(desc->params[pi].name) != edit_param_name_) continue;
                const auto& pd = desc->params[pi];
                val = std::max(pd.min_value, std::min(pd.max_value, val));
                if (pd.type == VIVID_PARAM_INT) val = std::round(val);
                api_.set_param(edit_node_id_, edit_param_name_, val);
                break;
            }
            break;
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

        // Find current resolution to preserve the other dimension
        const auto& sched_nodes = scheduler_.nodes();
        for (const auto& ns : sched_nodes) {
            if (ns.node_id != edit_res_node_id_) continue;
            uint32_t new_w = edit_res_is_width_ ? static_cast<uint32_t>(val) : ns.gpu_tex_width;
            uint32_t new_h = edit_res_is_width_ ? ns.gpu_tex_height : static_cast<uint32_t>(val);
            api_.set_resolution(edit_res_node_id_, new_w, new_h);
            break;
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
// Update (process mouse input + sparkline recording)
// -----------------------------------------------------------------------
void NodeGraphUI::update() {
    // Re-layout if topology changed (but not mid-drag)
    size_t cur_nodes = scheduler_.nodes().size();
    size_t cur_conns = graph_.connections().size();
    if (cur_nodes > last_node_count_) {
        if (dragging_node_idx_ < 0 && !dragging_wire_) {
            layout_nodes();
        }
    } else if (cur_nodes != last_node_count_ || cur_conns != last_conn_count_) {
        // Topology changed but no new nodes — just update counts without re-layout
        last_node_count_ = cur_nodes;
        last_conn_count_ = cur_conns;
    }

    // Active pan (middle-mouse drag)
    if (panning_) {
        pan_x_ = pan_start_px_ + (mouse_.x - pan_start_mx_);
        pan_y_ = pan_start_py_ + (mouse_.y - pan_start_my_);
    }

    // Active node drag
    if (dragging_node_idx_ >= 0) {
        if (mouse_.left_down) {
            auto& rect = node_rects_[dragging_node_idx_];
            rect.x = sx_to_gx(mouse_.x) - drag_offset_x_;
            rect.y = sy_to_gy(mouse_.y) - drag_offset_y_;
            // Find the corresponding NodeState for port recomputation
            const auto& sched_nodes = scheduler_.nodes();
            for (const auto& ns : sched_nodes) {
                if (ns.node_id == rect.node_id) {
                    recompute_ports(rect, ns);
                    break;
                }
            }
        }
        if (mouse_.left_released) {
            auto& rect = node_rects_[dragging_node_idx_];
            api_.set_node_layout(rect.node_id, rect.x, rect.y);
            dragging_node_idx_ = -1;
        }
    }

    // Wire drag release
    if (mouse_.left_released && dragging_wire_) {
        PortHit ph = hit_test_port(mouse_.x, mouse_.y);
        if (ph.node_idx >= 0 && !ph.is_output) {
            std::string to_node = node_rects_[ph.node_idx].node_id;
            api_.connect(wire_from_node_id_ + "/" + wire_from_port_,
                         to_node + "/" + ph.port_name);
        }
        dragging_wire_ = false;
    }

    // Active slider drag (skip if node drag is active)
    if (active_slider_idx_ >= 0 && dragging_node_idx_ < 0) {
        if (mouse_.left_down) {
            const auto& s = slider_rects_[active_slider_idx_];
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

    // Chooser hover: update selection to follow cursor
    if (chooser_open_) {
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

    // Close context menu on topology change
    if (context_menu_open_ &&
        (cur_nodes != last_node_count_ || cur_conns != last_conn_count_)) {
        context_menu_open_ = false;
    }

    // Context menu interaction
    if (context_menu_open_ && mouse_.left_clicked) {
        float menu_h = kCtxMenuPadTop + kCtxMenuItemH + 2.0f;
        if (mouse_.x >= context_menu_x_ && mouse_.x <= context_menu_x_ + kCtxMenuW &&
            mouse_.y >= context_menu_y_ && mouse_.y <= context_menu_y_ + menu_h) {
            // Clicked inside menu → execute action
            if (!context_node_id_.empty()) {
                api_.remove_node(context_node_id_);
                if (selected_node_id_ == context_node_id_) selected_node_id_.clear();
            } else if (context_wire_idx_ >= 0) {
                const auto& conns = graph_.connections();
                if (context_wire_idx_ < static_cast<int>(conns.size())) {
                    const auto& c = conns[context_wire_idx_];
                    api_.disconnect(c.from_node + "/" + c.from_port,
                                    c.to_node + "/" + c.to_port);
                }
            }
            context_menu_open_ = false;
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            goto click_done;
        } else {
            // Clicked outside → close
            context_menu_open_ = false;
        }
    }

    // Right-click → open context menu
    if (mouse_.right_clicked) {
        context_menu_open_ = false;
        int ni = hit_test_node(mouse_.x, mouse_.y);
        if (ni >= 0) {
            context_menu_open_ = true;
            context_node_id_ = node_rects_[ni].node_id;
            context_wire_idx_ = -1;
            context_menu_x_ = mouse_.x;
            context_menu_y_ = mouse_.y;
        } else {
            int wi = hit_test_wire(mouse_.x, mouse_.y);
            if (wi >= 0) {
                context_menu_open_ = true;
                context_node_id_.clear();
                context_wire_idx_ = wi;
                context_menu_x_ = mouse_.x;
                context_menu_y_ = mouse_.y;
            }
        }
    }

    // Click handling
    if (mouse_.left_clicked) {
        // Handle chooser click first (it floats over everything)
        if (chooser_open_) {
            int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
            if (visible == 0) visible = 1;
            float panel_h = kChooserHeaderH + visible * kChooserItemH + 4;
            float items_y = kChooserY + kChooserHeaderH;

            if (mouse_.x >= kChooserX && mouse_.x <= kChooserX + kChooserW &&
                mouse_.y >= items_y && mouse_.y <= items_y + visible * kChooserItemH &&
                !chooser_items_.empty()) {
                // Clicked on an item
                int idx = chooser_scroll_ + static_cast<int>((mouse_.y - items_y) / kChooserItemH);
                if (idx >= 0 && idx < static_cast<int>(chooser_items_.size())) {
                    const std::string& type = chooser_items_[idx];
                    std::string id;
                    for (int n = 1; ; ++n) {
                        id = type + std::to_string(n);
                        if (!graph_.find_node(id)) break;
                    }
                    api_.add_node(type, id);
                    api_.set_node_layout(id, chooser_cursor_gx_, chooser_cursor_gy_);
                    selected_node_id_ = id;
                }
            }
            chooser_open_ = false;
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            goto click_done;
        }

        // Handle dropdown popup click first (it floats over everything)
        if (dropdown_open_ && !dropdown_labels_.empty()) {
            float item_h = 20.0f;
            float popup_h = dropdown_labels_.size() * item_h + 4;
            if (mouse_.x >= dropdown_x_ && mouse_.x <= dropdown_x_ + dropdown_w_ &&
                mouse_.y >= dropdown_y_ && mouse_.y <= dropdown_y_ + popup_h) {
                int idx = static_cast<int>((mouse_.y - dropdown_y_ - 2) / item_h);
                if (idx >= 0 && idx < static_cast<int>(dropdown_labels_.size())) {
                    api_.set_param(dropdown_node_id_, dropdown_param_name_,
                                   static_cast<float>(idx));
                }
                dropdown_open_ = false;
                // Consume click
                mouse_.left_clicked = false;
                mouse_.left_released = false;
                goto click_done;
            } else {
                dropdown_open_ = false;
            }
        }

        if (mouse_.x >= graph_right() && mouse_.y < static_cast<float>(win_h_)) {

            // Confirm any active text edit when clicking in inspector
            if (editing_param_) confirm_param_edit();
            if (editing_resolution_) confirm_resolution_edit();

            // Check resolution rect click-to-edit
            int ri = hit_test_resolution(mouse_.x, mouse_.y);
            if (ri >= 0) {
                const auto& rr = resolution_rects_[ri];
                editing_resolution_ = true;
                edit_res_node_id_ = rr.node_id;
                edit_res_is_width_ = rr.is_width;
                // Pre-fill with current value
                const auto& sched_nodes = scheduler_.nodes();
                for (const auto& ns : sched_nodes) {
                    if (ns.node_id != rr.node_id) continue;
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%u",
                                  rr.is_width ? ns.gpu_tex_width : ns.gpu_tex_height);
                    edit_buffer_ = buf;
                    break;
                }
                goto click_done;
            }

            // Check value text click-to-edit first
            int vt = hit_test_value_text(mouse_.x, mouse_.y);
            if (vt >= 0) {
                editing_param_ = true;
                edit_node_id_ = value_text_rects_[vt].node_id;
                edit_param_name_ = value_text_rects_[vt].param_name;
                // Pre-fill with current value
                const auto& sched_nodes = scheduler_.nodes();
                for (const auto& ns : sched_nodes) {
                    if (ns.node_id != edit_node_id_) continue;
                    auto it = ns.param_indices.find(edit_param_name_);
                    if (it != ns.param_indices.end()) {
                        const auto* d = ns.loader->descriptor();
                        for (uint32_t pi = 0; pi < d->param_count; ++pi) {
                            if (std::string(d->params[pi].name) != edit_param_name_) continue;
                            if (d->params[pi].type == VIVID_PARAM_INT) {
                                char buf[32];
                                std::snprintf(buf, sizeof(buf), "%d",
                                              static_cast<int>(ns.param_values[it->second]));
                                edit_buffer_ = buf;
                            } else {
                                char buf[32];
                                std::snprintf(buf, sizeof(buf), "%.2f",
                                              ns.param_values[it->second]);
                                edit_buffer_ = buf;
                            }
                            break;
                        }
                    }
                    break;
                }
            } else {
                // Check dropdown click
                int di = hit_test_dropdown(mouse_.x, mouse_.y);
                if (di >= 0) {
                    const auto& dr = dropdown_rects_[di];
                    dropdown_node_id_ = dr.node_id;
                    dropdown_param_name_ = dr.param_name;
                    dropdown_x_ = dr.x;
                    dropdown_y_ = dr.y + dr.h;
                    dropdown_w_ = dr.w;
                    // Find choice labels from descriptor
                    dropdown_labels_.clear();
                    const auto& sched_nodes = scheduler_.nodes();
                    for (const auto& ns : sched_nodes) {
                        if (ns.node_id != dr.node_id) continue;
                        const auto* d = ns.loader->descriptor();
                        for (uint32_t pi = 0; pi < d->param_count; ++pi) {
                            if (std::string(d->params[pi].name) != dr.param_name) continue;
                            for (uint32_t ci = 0; ci < d->params[pi].choice_count; ++ci)
                                dropdown_labels_.push_back(d->params[pi].choice_labels[ci]);
                            auto it = ns.param_indices.find(dr.param_name);
                            if (it != ns.param_indices.end())
                                dropdown_sel_ = static_cast<int>(ns.param_values[it->second]);
                            break;
                        }
                        break;
                    }
                    dropdown_open_ = !dropdown_labels_.empty();
                } else {
                    // Check slider
                    int si = hit_test_slider(mouse_.x, mouse_.y);
                    if (si >= 0) {
                        active_slider_idx_ = si;
                        active_slider_node_id_ = slider_rects_[si].node_id;
                        active_slider_param_name_ = slider_rects_[si].param_name;
                    } else {
                        int bi = hit_test_bool(mouse_.x, mouse_.y);
                        if (bi >= 0) {
                            const auto& br = bool_rects_[bi];
                            const auto& sched_nodes = scheduler_.nodes();
                            for (const auto& ns : sched_nodes) {
                                if (ns.node_id != br.node_id) continue;
                                auto it = ns.param_indices.find(br.param_name);
                                if (it != ns.param_indices.end()) {
                                    float cur = ns.param_values[it->second];
                                    api_.set_param(br.node_id, br.param_name,
                                                   cur > 0.5f ? 0.0f : 1.0f);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        } else if (mouse_.x < graph_right() && mouse_.y < static_cast<float>(win_h_)) {
            // Clicking in graph area confirms any active text edit
            if (editing_param_) confirm_param_edit();
            if (editing_resolution_) confirm_resolution_edit();

            // Port hit test first (ports are on node edges, inside node AABB)
            PortHit ph = hit_test_port(mouse_.x, mouse_.y);
            if (ph.node_idx >= 0) {
                if (ph.is_output) {
                    // Start wire drag from output port
                    dragging_wire_ = true;
                    wire_from_node_id_ = node_rects_[ph.node_idx].node_id;
                    wire_from_port_ = ph.port_name;
                    wire_from_gx_ = ph.gx;
                    wire_from_gy_ = ph.gy;
                } else {
                    // Click on input port → disconnect existing wires to this input
                    std::string to_node = node_rects_[ph.node_idx].node_id;
                    const auto& conns = graph_.connections();
                    for (const auto& c : conns) {
                        if (c.to_node == to_node && c.to_port == ph.port_name) {
                            api_.disconnect(c.from_node + "/" + c.from_port,
                                            to_node + "/" + ph.port_name);
                        }
                    }
                }
            } else {
                int ni = hit_test_node(mouse_.x, mouse_.y);
                if (ni >= 0) {
                    selected_node_id_ = node_rects_[ni].node_id;
                    dragging_node_idx_ = ni;
                    drag_offset_x_ = sx_to_gx(mouse_.x) - node_rects_[ni].x;
                    drag_offset_y_ = sy_to_gy(mouse_.y) - node_rects_[ni].y;
                } else {
                    selected_node_id_.clear();
                    // Start left-drag pan on empty canvas
                    panning_ = true;
                    pan_start_mx_ = mouse_.x;
                    pan_start_my_ = mouse_.y;
                    pan_start_px_ = pan_x_;
                    pan_start_py_ = pan_y_;
                }
            }
        }
    }

    click_done:

    if (mouse_.left_released && panning_ && dragging_node_idx_ < 0) {
        panning_ = false;
    }

    // Clear one-frame flags
    mouse_.left_clicked = false;
    mouse_.left_released = false;
    mouse_.right_clicked = false;

    // Wire hover detection
    if (!dragging_wire_ && !panning_ && dragging_node_idx_ < 0 &&
        !context_menu_open_ && !chooser_open_ && !dropdown_open_) {
        hovered_wire_idx_ = hit_test_wire(mouse_.x, mouse_.y);
    } else {
        hovered_wire_idx_ = -1;
    }

    // Record sparkline data for control nodes (step 3)
    const auto& sched_nodes = scheduler_.nodes();
    for (const auto& ns : sched_nodes) {
        if (ns.is_gpu || ns.is_audio) continue;

        // Find first output port
        std::vector<std::pair<uint32_t, std::string>> sorted_outs;
        for (const auto& [name, idx] : ns.output_port_indices)
            sorted_outs.push_back({idx, name});
        std::sort(sorted_outs.begin(), sorted_outs.end());

        if (sorted_outs.empty()) continue;

        std::string key = ns.node_id + "/" + sorted_outs[0].second;
        float val = ns.output_values[sorted_outs[0].first];

        auto& sd = sparklines_[key];
        sd.values[sd.write_idx] = val;
        sd.write_idx = (sd.write_idx + 1) % kSparklineLen;
        if (sd.write_idx == 0) sd.filled = true;
    }
}

// -----------------------------------------------------------------------
// Draw
// -----------------------------------------------------------------------
void NodeGraphUI::draw(TextRenderer& tr, uint32_t w, uint32_t h) {
    if (!visible_) return;
    win_w_ = w;
    win_h_ = h;

    if (node_rects_.empty() && !scheduler_.nodes().empty()) {
        layout_nodes();
    }

    // Semi-transparent scrim so wires are visible over the visualization
    tr.draw_rect(0, 0, static_cast<float>(w), static_cast<float>(h), 0.05f, 0.06f, 0.07f, 0.55f);

    draw_graph(tr);
    draw_connections(tr);
    draw_preview_wire(tr);
    draw_wire_tooltip(tr);

    draw_inspector(tr, w, h);
    draw_chooser(tr);

    // Dropdown popup (drawn last, on top of everything)
    if (dropdown_open_ && !dropdown_labels_.empty()) {
        float item_h = 20.0f;
        float popup_h = dropdown_labels_.size() * item_h + 4;
        // Background
        tr.draw_rect(dropdown_x_, dropdown_y_, dropdown_w_, popup_h,
                     0.14f, 0.15f, 0.18f, 0.97f);
        // Border
        tr.draw_rect(dropdown_x_, dropdown_y_, dropdown_w_, 1,
                     kAccent[0], kAccent[1], kAccent[2]);
        for (int i = 0; i < static_cast<int>(dropdown_labels_.size()); ++i) {
            float iy = dropdown_y_ + 2 + i * item_h;
            if (i == dropdown_sel_) {
                tr.draw_rect(dropdown_x_ + 2, iy, dropdown_w_ - 4, item_h,
                             kNodeSelBg[0], kNodeSelBg[1], kNodeSelBg[2], 0.9f);
            }
            tr.draw_text(dropdown_x_ + 8, iy + 2, dropdown_labels_[i].c_str(),
                         0.9f, 0.92f, 0.95f);
        }
    }

    // Right-click context menu (drawn last, on top of everything)
    if (context_menu_open_) {
        float menu_h = kCtxMenuPadTop + kCtxMenuItemH + 2.0f;
        float mx = context_menu_x_, my = context_menu_y_;

        // Background
        tr.draw_rect(mx, my, kCtxMenuW, menu_h, 0.14f, 0.15f, 0.18f, 0.97f);
        // Accent bar
        tr.draw_rect(mx, my, kCtxMenuW, 1, kAccent[0], kAccent[1], kAccent[2]);

        // Hover highlight
        float item_y = my + kCtxMenuPadTop;
        if (mouse_.x >= mx && mouse_.x <= mx + kCtxMenuW &&
            mouse_.y >= item_y && mouse_.y <= item_y + kCtxMenuItemH) {
            tr.draw_rect(mx + 2, item_y, kCtxMenuW - 4, kCtxMenuItemH,
                         kNodeSelBg[0], kNodeSelBg[1], kNodeSelBg[2], 0.9f);
        }

        const char* label = !context_node_id_.empty() ? "Delete Node" : "Delete Wire";
        tr.draw_text(mx + 8, item_y + 3, label, 0.9f, 0.92f, 0.95f);
    }
}

// -----------------------------------------------------------------------
// GPU thumbnail overlay (step 6)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_thumbnails(ThumbnailRenderer& renderer, const ThumbnailCache& cache,
                                  WGPUCommandEncoder encoder, WGPUTextureView surface,
                                  uint32_t w, uint32_t h) {
    renderer.begin(encoder, surface, w, h);
    for (const auto& r : node_rects_) {
        WGPUTextureView thumb_view = cache.get_view(r.node_id);
        if (!thumb_view) continue;
        // Viewport units are physical pixels — apply zoom/pan then dpi_scale
        float tx = gx_to_sx(r.x) * dpi_scale_;
        float ty = gy_to_sy(r.y + kAccentBarH) * dpi_scale_;
        float tw = g_to_s(r.w) * dpi_scale_;
        float th = g_to_s(kGpuThumbH) * dpi_scale_;
        if (tw <= 0 || th <= 0) continue;
        // Compute visible intersection with render target
        float fw = static_cast<float>(w), fh = static_cast<float>(h);
        float vis_x0 = std::max(tx, 0.0f);
        float vis_y0 = std::max(ty, 0.0f);
        float vis_x1 = std::min(tx + tw, fw);
        float vis_y1 = std::min(ty + th, fh);
        if (vis_x0 >= vis_x1 || vis_y0 >= vis_y1) continue;  // fully off-screen
        uint32_t sc_x = static_cast<uint32_t>(vis_x0);
        uint32_t sc_y = static_cast<uint32_t>(vis_y0);
        uint32_t sc_w = static_cast<uint32_t>(vis_x1 - vis_x0);
        uint32_t sc_h = static_cast<uint32_t>(vis_y1 - vis_y0);
        if (sc_w == 0 || sc_h == 0) continue;
        renderer.draw(thumb_view, tx, ty, tw, th, sc_x, sc_y, sc_w, sc_h);
    }
    renderer.end();
}

} // namespace vivid
