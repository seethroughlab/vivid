#include "runtime/node_graph.h"
#include "runtime/node_graph_constants.h"
#include "runtime/string_util.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/text_renderer.h"
#include "runtime/audio_engine.h"
#include "runtime/thumbnail_cache.h"
#include "runtime/thumbnail_renderer.h"
#include "runtime/operator_loader.h"
#include "runtime/operator_registry.h"
#include "operator_api/types.h"
#include <algorithm>
#include <cmath>

namespace vivid {

// -----------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------
void NodeGraphUI::draw_graph(TextRenderer& tr) {
    const auto& sched_nodes = scheduler_.nodes();

    for (size_t i = 0; i < node_rects_.size(); ++i) {
        const auto& r = node_rects_[i];
        bool selected = (r.node_id == selected_node_id_);
        const float* bg = selected ? kNodeSelBg.data() : kNodeBg.data();
        const float* dcol = domain_color(r.domain);

        // Transform graph-space rect to screen space
        float sx = gx_to_sx(r.x), sy = gy_to_sy(r.y);
        float sw = g_to_s(r.w), sh = g_to_s(r.h);

        // Node background
        tr.draw_rect(sx, sy, sw, sh, bg[0], bg[1], bg[2]);

        // Accent bar at top
        float s_accent_h = g_to_s(kAccentBarH);
        tr.draw_rect(sx, sy, sw, s_accent_h, dcol[0], dcol[1], dcol[2]);

        // --- Domain body region ---
        float s_body_y = sy + s_accent_h;
        bool has_ct = custom_thumb_nodes_.count(r.node_id) > 0;
        float body_h = domain_body_height(r.domain, has_ct);
        float s_body_h = g_to_s(body_h);

        if (r.domain == VIVID_DOMAIN_CONTROL && !has_ct) {
            // Sparkline
            tr.draw_rect(sx + g_to_s(2), s_body_y + g_to_s(2),
                         sw - g_to_s(4), s_body_h - g_to_s(4),
                         kDarkBg[0], kDarkBg[1], kDarkBg[2], 0.9f);

            // Find sparkline data for this node's first output
            std::string spark_key;
            if (i < sched_nodes.size()) {
                const auto& ns = sched_nodes[i];
                auto sorted_outs = sorted_ports(ns.output_port_indices);
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
                    std::string val_str = format_float(cur_val, 2);
                    tr.draw_text(sx + g_to_s(5), s_body_y + g_to_s(4), val_str.c_str(),
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
            // Waveform
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

    // Build fast lookup: node_id -> index in node_rects_
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

        traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
            [&](float x0, float y0, float x1, float y1) {
                tr.draw_line(x0, y0, x1, y1, wire_th, cr, cg, cb, a);
            });
    }
}

void NodeGraphUI::draw_wire_tooltip(TextRenderer& tr) {
    if (hovered_wire_idx_ < 0) return;

    const auto& conns = graph_.connections();
    if (hovered_wire_idx_ >= static_cast<int>(conns.size())) return;
    const auto& c = conns[hovered_wire_idx_];

    // Find source node in scheduler to read current value
    const NodeState* src_ns = find_sched_node(c.from_node);

    // Build label line
    std::string label = c.from_node + "/" + c.from_port + " -> " + c.to_node + "/" + c.to_port;

    // Build value line
    std::string value_str;
    if (src_ns) {
        auto it = src_ns->output_port_indices.find(c.from_port);
        if (it != src_ns->output_port_indices.end()) {
            uint32_t pidx = it->second;
            if (pidx < src_ns->output_values.size()) {
                float val = src_ns->output_values[pidx];
                if (pidx < src_ns->output_spreads.size() &&
                    !src_ns->output_spreads[pidx].empty()) {
                    value_str = format_float(val) + " [spread: " +
                                std::to_string(src_ns->output_spreads[pidx].size()) + "]";
                } else {
                    value_str = format_float(val);
                }
            }
        }
    }

    // Measure text and compute popup dimensions
    float label_w = tr.text_width(label.c_str());
    float value_w = value_str.empty() ? 0.0f : tr.text_width(value_str.c_str());
    float pad = 8.0f;
    float popup_w = std::max(label_w, value_w) + pad * 2;
    float line_h = 16.0f;
    float popup_h = (value_str.empty() ? line_h : line_h * 2) + pad * 2;

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
    tr.draw_text(px + pad, py + pad, label.c_str(), kDimText[0], kDimText[1], kDimText[2]);
    // Value text
    if (!value_str.empty()) {
        tr.draw_text(px + pad, py + pad + line_h, value_str.c_str(), 0.9f, 0.92f, 0.95f);
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
    const NodeState* sel_node = find_sched_node(selected_node_id_);
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
        std::string val_str;
        if (pd.type == VIVID_PARAM_BOOL) {
            val_str = val > 0.5f ? "true" : "false";
        } else if (pd.choice_count > 0) {
            int idx = static_cast<int>(val);
            if (idx >= 0 && idx < static_cast<int>(pd.choice_count))
                val_str = pd.choice_labels[idx];
            else
                val_str = format_int(idx);
        } else if (pd.type == VIVID_PARAM_INT) {
            val_str = format_int(static_cast<int>(val));
        } else {
            val_str = format_float(val, 2);
        }

        float vw = tr.text_width(val_str.c_str());
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
            tr.draw_text(val_x, py, val_str.c_str(), 0.8f, 0.82f, 0.85f);
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

            slider_rects_.push_back({sx, sy - 4, sw, sh + 8, selected_node_id_, pd.name});
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

        std::string w_str = editing_w ? (edit_buffer_ + "_") : format_uint(sel_node->gpu_tex_width);
        std::string h_str = editing_h ? (edit_buffer_ + "_") : format_uint(sel_node->gpu_tex_height);

        float val_x = px + 4;
        float w_text_w = 40.0f;

        if (is_generator) {
            // Clickable width value
            if (editing_w) {
                tr.draw_rect(val_x, py, w_text_w, kLineH, 0.12f, 0.14f, 0.18f);
            }
            tr.draw_text(val_x, py, w_str.c_str(),
                         editing_w ? 1.0f : 0.8f,
                         editing_w ? 1.0f : 0.82f,
                         editing_w ? 1.0f : 0.85f);
            resolution_rects_.push_back({val_x, py, w_text_w, kLineH,
                                         selected_node_id_, true});
        } else {
            tr.draw_text(val_x, py, w_str.c_str(), 0.5f, 0.52f, 0.55f);
        }

        tr.draw_text(val_x + w_text_w, py, " x ", kDimText[0], kDimText[1], kDimText[2]);

        float h_val_x = val_x + w_text_w + 24.0f;

        if (is_generator) {
            // Clickable height value
            if (editing_h) {
                tr.draw_rect(h_val_x, py, w_text_w, kLineH, 0.12f, 0.14f, 0.18f);
            }
            tr.draw_text(h_val_x, py, h_str.c_str(),
                         editing_h ? 1.0f : 0.8f,
                         editing_h ? 1.0f : 0.82f,
                         editing_h ? 1.0f : 0.85f);
            resolution_rects_.push_back({h_val_x, py, w_text_w, kLineH,
                                         selected_node_id_, false});
        } else {
            tr.draw_text(h_val_x, py, h_str.c_str(), 0.5f, 0.52f, 0.55f);
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

    auto sorted_outs = sorted_ports(sel_node->output_port_indices);

    for (const auto& [idx, name] : sorted_outs) {
        std::string line;
        if (idx < sel_node->output_spreads.size() && !sel_node->output_spreads[idx].empty()) {
            line = name + " = [" + std::to_string(sel_node->output_spreads[idx].size()) + " bins]";
        } else {
            line = name + " = " + format_float(sel_node->output_values[idx]);
        }
        tr.draw_text(px, py, line.c_str(), kDimText[0], kDimText[1], kDimText[2]);
        py += kLineH;
    }
}

void NodeGraphUI::draw_preview_wire(TextRenderer& tr) {
    if (!dragging_wire_) return;
    float ssx = gx_to_sx(wire_from_gx_), ssy = gy_to_sy(wire_from_gy_);
    float sex = mouse_.x, sey = mouse_.y;
    float wire_th = std::max(1.0f, 3.0f * zoom_);

    traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
        [&](float x0, float y0, float x1, float y1) {
            tr.draw_line(x0, y0, x1, y1, wire_th, 1.0f, 1.0f, 1.0f, 0.5f);
        });
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
        const float* dcol = kControlAccent.data(); // default
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
// Draw (top-level)
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
// GPU thumbnail overlay
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
