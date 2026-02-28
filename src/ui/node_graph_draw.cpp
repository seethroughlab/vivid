#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/renderer_2d.h"
#include "ui/thumbnail_cache.h"
#include "ui/thumbnail_renderer.h"
#include "common/string_util.h"
#include "common/system_info.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>

namespace vivid::ui {

using vivid::format_float;
using vivid::format_int;
using vivid::format_uint;

// -----------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------
void NodeGraphUI::draw_graph(Renderer2D& tr) {
    for (size_t i = 0; i < node_rects_.size(); ++i) {
        const auto& r = node_rects_[i];
        bool selected = selected_node_ids_.count(r.node_id) > 0;
        const float* bg = selected ? style_.node_sel_bg.data() : style_.node_bg.data();
        bool node_errored = (i < snap_.nodes.size() && snap_.nodes[i].errored);
        const float* dcol = node_errored ? kErrorAccent.data() : domain_color(r.domain);

        // Transform graph-space rect to screen space
        float sx = gx_to_sx(r.x), sy = gy_to_sy(r.y);
        float sw = g_to_s(r.w), sh = g_to_s(r.h);

        // Node background
        float sr = g_to_s(style_.corner_radius);
        tr.draw_rounded_rect(sx, sy, sw, sh, sr, bg[0], bg[1], bg[2]);

        // Red border on errored nodes (2px)
        if (node_errored) {
            float bw = g_to_s(2.0f);
            tr.draw_rect(sx, sy, sw, bw, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]);           // top
            tr.draw_rect(sx, sy + sh - bw, sw, bw, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]); // bottom
            tr.draw_rect(sx, sy, bw, sh, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]);           // left
            tr.draw_rect(sx + sw - bw, sy, bw, sh, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]); // right
        }

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
                         style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.9f);

            // Find sparkline data for this node's first output
            std::string spark_key;
            if (i < snap_.nodes.size()) {
                const auto& ns = snap_.nodes[i];
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
                         style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.9f);

            auto ae_it = snap_.audio_index.find(r.node_id);
            if (ae_it != snap_.audio_index.end() && ae_it->second >= 0) {
                int ae_idx = ae_it->second;
                if (ae_idx < static_cast<int>(snap_.audio_analysis.size())) {
                    const auto& analysis = snap_.audio_analysis[ae_idx];
                    float wave_x = sx + g_to_s(4);
                    float wave_w = sw - g_to_s(8);
                    float wave_y = s_body_y + g_to_s(4);
                    float wave_h = s_body_h - g_to_s(10);
                    float center_y = wave_y + wave_h * 0.5f;

                    // Center line
                    tr.draw_rect(wave_x, center_y, wave_w, 1,
                                 dcol[0], dcol[1], dcol[2], 0.2f);

                    // Waveform bars — downsample 1024 source samples to
                    // ~256 visual bars (max-amplitude per bucket) to cut
                    // vertex cost from 6144 to ~1536 with no visible loss.
                    constexpr uint32_t kWaveN = AudioNodeAnalysis::kWaveformSamples;
                    constexpr uint32_t kVisualBars = 256;
                    constexpr uint32_t kStride = kWaveN / kVisualBars; // 4
                    float bar_w = wave_w / kVisualBars;
                    for (uint32_t bi = 0; bi < kVisualBars; ++bi) {
                        // Find the sample with the largest absolute amplitude in this bucket
                        float amp = analysis.waveform[bi * kStride];
                        for (uint32_t j = 1; j < kStride; ++j) {
                            float s = analysis.waveform[bi * kStride + j];
                            if (std::fabs(s) > std::fabs(amp)) amp = s;
                        }
                        float bh = std::fabs(amp) * wave_h * 0.5f;
                        bh = std::max(0.5f, bh);
                        float bx = wave_x + bi * bar_w;
                        float by = (amp >= 0) ? center_y - bh : center_y;
                        tr.draw_rect(bx, by, std::max(0.5f, bar_w - 0.3f), bh,
                                     dcol[0], dcol[1], dcol[2], 0.8f);
                    }

                    // Peak meter strip at bottom
                    float peak_y = s_body_y + s_body_h - g_to_s(4);
                    float pk = std::min(1.0f, analysis.peak);
                    tr.draw_rect(wave_x, peak_y, wave_w * pk, g_to_s(2),
                                 dcol[0], dcol[1], dcol[2], 0.9f);
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
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, zoom_);

        // Input port dots and labels (use domain color)
        float s_dot = kPortDotSize * zoom_;
        float s_line_h = tr.line_height() * zoom_;
        for (const auto& p : r.inputs) {
            float spx = gx_to_sx(p.x), spy = gy_to_sy(p.y);
            float dot_scale = p.is_param ? 0.7f : 1.0f;
            float dot_alpha = p.is_param ? 0.6f : 1.0f;
            float sd = s_dot * dot_scale;
            tr.draw_rect(spx - sd, spy - sd * 0.5f,
                         sd, sd,
                         dcol[0], dcol[1], dcol[2], dot_alpha);
            tr.draw_text(spx + g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], dot_alpha, zoom_);
        }
        // Output port dots and labels (use domain color)
        for (const auto& p : r.outputs) {
            float spx = gx_to_sx(p.x), spy = gy_to_sy(p.y);
            float dot_scale = p.is_param ? 0.7f : 1.0f;
            float dot_alpha = p.is_param ? 0.6f : 1.0f;
            float sd = s_dot * dot_scale;
            tr.draw_rect(spx, spy - sd * 0.5f,
                         sd, sd,
                         dcol[0], dcol[1], dcol[2], dot_alpha);
            float lw = tr.text_width(p.name.c_str(), zoom_);
            tr.draw_text(spx - lw - g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], dot_alpha, zoom_);
        }
    }
}

void NodeGraphUI::draw_connections(Renderer2D& tr) {
    const auto& conns = snap_.connections;

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
        bool sel = selected_node_ids_.count(c.from_node) > 0 || selected_node_ids_.count(c.to_node) > 0;
        bool hov = (ci == hovered_wire_idx_);
        float brightness = (hov || sel) ? kWireHoverBright : 1.0f;
        float cr = std::min(1.0f, dcol[0] * brightness);
        float cg = std::min(1.0f, dcol[1] * brightness);
        float cb = std::min(1.0f, dcol[2] * brightness);
        float a = (hov || sel) ? 0.95f : 0.8f;

        bool is_param_wire = c.from_is_param;
        float wire_th;
        if (is_param_wire)
            wire_th = std::max(1.0f, 1.5f * zoom_);
        else
            wire_th = std::max(1.0f, (hov ? kWireHoverThickness : kWireThickness) * zoom_);

        if (is_param_wire) {
            // Thin dashed dimmed wire for param-to-param connections
            float a_param = (hov || sel) ? 0.6f : 0.35f;
            float cumulative = 0.0f;
            float dash_cycle = kDashOn + kDashOff;
            traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
                [&](float x0, float y0, float x1, float y1) {
                    float dx = x1 - x0, dy = y1 - y0;
                    float seg_len = std::sqrt(dx * dx + dy * dy);
                    if (seg_len < 0.001f) { cumulative += seg_len; return; }
                    float nx = dx / seg_len, ny = dy / seg_len;
                    float consumed = 0.0f;
                    while (consumed < seg_len) {
                        float phase = std::fmod(cumulative + consumed, dash_cycle);
                        bool on = (phase < kDashOn);
                        float remain_in_state = on ? (kDashOn - phase) : (dash_cycle - phase);
                        float chunk = std::min(remain_in_state, seg_len - consumed);
                        if (on) {
                            float cx0 = x0 + nx * consumed;
                            float cy0 = y0 + ny * consumed;
                            float cx1 = x0 + nx * (consumed + chunk);
                            float cy1 = y0 + ny * (consumed + chunk);
                            tr.draw_line(cx0, cy0, cx1, cy1, wire_th, cr, cg, cb, a_param);
                        }
                        consumed += chunk;
                    }
                    cumulative += seg_len;
                });
        } else {
        bool cross_domain = from_rect.domain != to_rect.domain;
        if (cross_domain) {
            // Dashed wire: traverse and subdivide segments at dash boundaries
            float cumulative = 0.0f;
            float dash_cycle = kDashOn + kDashOff;
            traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
                [&](float x0, float y0, float x1, float y1) {
                    float dx = x1 - x0, dy = y1 - y0;
                    float seg_len = std::sqrt(dx * dx + dy * dy);
                    if (seg_len < 0.001f) { cumulative += seg_len; return; }
                    float nx = dx / seg_len, ny = dy / seg_len;
                    float consumed = 0.0f;
                    while (consumed < seg_len) {
                        float phase = std::fmod(cumulative + consumed, dash_cycle);
                        bool on = (phase < kDashOn);
                        float remain_in_state = on ? (kDashOn - phase) : (dash_cycle - phase);
                        float chunk = std::min(remain_in_state, seg_len - consumed);
                        if (on) {
                            float cx0 = x0 + nx * consumed;
                            float cy0 = y0 + ny * consumed;
                            float cx1 = x0 + nx * (consumed + chunk);
                            float cy1 = y0 + ny * (consumed + chunk);
                            tr.draw_line(cx0, cy0, cx1, cy1, wire_th, cr, cg, cb, a);
                        }
                        consumed += chunk;
                    }
                    cumulative += seg_len;
                });
        } else {
            traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
                [&](float x0, float y0, float x1, float y1) {
                    tr.draw_line(x0, y0, x1, y1, wire_th, cr, cg, cb, a);
                });
        }
        }
    }
}

void NodeGraphUI::draw_wire_tooltip(Renderer2D& tr) {
    if (hovered_wire_idx_ < 0) return;

    const auto& conns = snap_.connections;
    if (hovered_wire_idx_ >= static_cast<int>(conns.size())) return;
    const auto& c = conns[hovered_wire_idx_];

    // Find source node in snapshot to read current value
    const auto* src_ns = snap_.find_node(c.from_node);

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
        } else {
            // Fallback: param source
            auto pit = src_ns->param_indices.find(c.from_port);
            if (pit != src_ns->param_indices.end() && pit->second < src_ns->param_values.size())
                value_str = format_float(src_ns->param_values[pit->second]);
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
    tr.draw_rect(px, py, popup_w, popup_h, style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.95f);
    // Accent line at top
    if (dcol) {
        tr.draw_rect(px, py, popup_w, 2.0f, dcol[0], dcol[1], dcol[2], 0.9f);
    }
    // Label text
    tr.draw_text(px + pad, py + pad, label.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    // Value text
    if (!value_str.empty()) {
        tr.draw_text(px + pad, py + pad + line_h, value_str.c_str(), style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
}

void NodeGraphUI::draw_inspector(Renderer2D& tr, uint32_t w, uint32_t h) {
    slider_rects_.clear();
    xy_pad_rects_.clear();
    color_swatch_rects_.clear();
    bool_rects_.clear();
    value_text_rects_.clear();
    dropdown_rects_.clear();
    file_button_rects_.clear();
    drum_grid_rects_.clear();
    drum_mod_a_rects_.clear();
    drum_mod_b_rects_.clear();
    drum_tab_rects_.clear();
    resolution_rects_.clear();
    midi_remove_rects_.clear();
    midi_range_rects_.clear();
    matrix_cell_rects_.clear();
    group_header_rects_.clear();

    if (selected_node_ids_.empty()) return;

    // Inspector background + separator (drawn outside clip rect)
    float insp_x = inspector_x();
    tr.draw_rect(insp_x, 0, kInspectorW, static_cast<float>(h), style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.95f);
    tr.draw_rect(insp_x, 0, 2, static_cast<float>(h), style_.separator[0], style_.separator[1], style_.separator[2]);

    // Multi-selection panel
    if (selected_node_ids_.size() > 1) {
        if (selected_node_ids_.size() == 2) {
            // 2-node selection: show connection matrix between the pair
            auto it = selected_node_ids_.begin();
            const std::string& id_a = *it++;
            const std::string& id_b = *it;
            const auto* node_a = snap_.find_node(id_a);
            const auto* node_b = snap_.find_node(id_b);
            if (!node_a || !node_b || !node_a->op_info || !node_b->op_info) {
                float px = insp_x + kInspPadX;
                float py = kPerfBarH + 8;
                tr.draw_text(px, py, "Node not found", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
                insp_content_h_ = 0;
                return;
            }

            // Reset scroll when the selected pair changes
            std::string scroll_key = id_a + "+" + id_b;
            if (scroll_key != insp_scroll_node_id_) {
                insp_scroll_y_ = 0.0f;
                insp_scroll_node_id_ = scroll_key;
            }

            float viewport_top = kPerfBarH;
            float viewport_h = static_cast<float>(h) - viewport_top;
            float max_scroll = std::max(0.0f, insp_content_h_ - viewport_h);
            insp_scroll_y_ = std::max(0.0f, std::min(insp_scroll_y_, max_scroll));

            tr.push_clip_rect(insp_x, viewport_top, kInspectorW, viewport_h);

            float px = insp_x + kInspPadX;
            float py = viewport_top + 8 - insp_scroll_y_;

            // Header: both node names with domain colors
            const float* clr_a = domain_color(node_a->domain);
            const float* clr_b = domain_color(node_b->domain);
            tr.draw_text(px, py, node_a->op_info->name.c_str(), clr_a[0], clr_a[1], clr_a[2]);
            float name_w = tr.text_width(node_a->op_info->name.c_str());
            tr.draw_text(px + name_w + 4, py, " + ", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            float plus_w = tr.text_width(" + ");
            tr.draw_text(px + name_w + 4 + plus_w, py, node_b->op_info->name.c_str(), clr_b[0], clr_b[1], clr_b[2]);
            py += kLineH;

            tr.draw_text(px, py, "Delete / Backspace to remove", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            py += kLineH + 8;

            tr.draw_rect(px, py, kInspContentW, 1, style_.separator[0], style_.separator[1], style_.separator[2]);
            py += 8;

            // A → B matrix section
            draw_matrix_section(tr, *node_a, *node_b, px, py);
            // B → A matrix section
            draw_matrix_section(tr, *node_b, *node_a, px, py);

            tr.pop_clip_rect();

            insp_content_h_ = (py + insp_scroll_y_) - viewport_top;
            draw_inspector_scrollbar(tr);
        } else {
            // 3+ nodes: summary
            float px = insp_x + kInspPadX;
            float py = kPerfBarH + 8;
            std::string label = std::to_string(selected_node_ids_.size()) + " nodes selected";
            tr.draw_text(px, py, label.c_str(), 1.0f, 1.0f, 1.0f);
            py += kLineH + 4;
            tr.draw_text(px, py, "Delete / Backspace to remove", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            py += kLineH + 8;

            // "Connection Matrix (M)" button
            float btn_w = kInspContentW;
            float btn_h = 22.0f;
            bool btn_hover = mouse_.x >= px && mouse_.x <= px + btn_w &&
                             mouse_.y >= py && mouse_.y <= py + btn_h;
            if (btn_hover)
                tr.draw_rect(px, py, btn_w, btn_h,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
            else
                tr.draw_rect(px, py, btn_w, btn_h,
                             style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.6f);
            tr.draw_text(px + 8, py + 3, "Connection Matrix (M)",
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            patchbay_button_rect_ = {px, py, btn_w, btn_h};

            insp_content_h_ = 0;
        }
        return;
    }

    const auto& sel_id = single_selected_id();

    // Reset scroll when selection changes
    if (sel_id != insp_scroll_node_id_) {
        insp_scroll_y_ = 0.0f;
        insp_scroll_node_id_ = sel_id;
    }

    // Find the selected node in snapshot
    const auto* sel_node = snap_.find_node(sel_id);
    if (!sel_node || !sel_node->op_info) {
        tr.draw_text(insp_x + kInspPadX, 20, "Node not found", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        return;
    }

    float viewport_top = kPerfBarH;
    float viewport_h = static_cast<float>(h) - viewport_top;

    // Clamp scroll before drawing
    float max_scroll = std::max(0.0f, insp_content_h_ - viewport_h);
    insp_scroll_y_ = std::max(0.0f, std::min(insp_scroll_y_, max_scroll));

    // Clip rect for scrollable content
    tr.push_clip_rect(insp_x, viewport_top, kInspectorW, viewport_h);

    float px = insp_x + kInspPadX;
    float py = viewport_top + 8 - insp_scroll_y_;

    draw_inspector_header(tr, *sel_node, px, py);

    // Error banner for errored nodes
    if (sel_node->errored) {
        tr.draw_text(px, py, ("ERROR: " + sel_node->error_message).c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]);
        py += kLineH + 4;
    }

    draw_inspector_params(tr, *sel_node, px, py);
    draw_inspector_adsr_preview(tr, *sel_node, px, py);
    draw_inspector_note_pattern(tr, *sel_node, px, py);
    draw_inspector_drum_grid(tr, *sel_node, px, py);
    draw_inspector_resolution(tr, *sel_node, px, py);
    draw_inspector_outputs(tr, *sel_node, px, py);

    tr.pop_clip_rect();

    // Compute content height from final py (relative to viewport top)
    insp_content_h_ = (py + insp_scroll_y_) - viewport_top;

    // Draw scrollbar outside clip rect
    draw_inspector_scrollbar(tr);
}

void NodeGraphUI::draw_inspector_scrollbar(Renderer2D& tr) {
    float viewport_top = kPerfBarH;
    float viewport_h = static_cast<float>(win_h_) - viewport_top;

    // Only show scrollbar when content overflows
    if (insp_content_h_ <= viewport_h) return;

    float insp_x = inspector_x();
    float track_x = insp_x + kInspectorW - kInspScrollbarW - 2.0f;
    float track_y = viewport_top + 2.0f;
    float track_h = viewport_h - 4.0f;

    // Track background
    tr.draw_rect(track_x, track_y, kInspScrollbarW, track_h,
                 style_.scrollbar_track[0], style_.scrollbar_track[1], style_.scrollbar_track[2], 0.5f);

    // Thumb size proportional to viewport/content ratio
    float ratio = viewport_h / insp_content_h_;
    float thumb_h = std::max(kInspScrollbarMinThumb, track_h * ratio);

    // Thumb position based on scroll ratio
    float max_scroll = insp_content_h_ - viewport_h;
    float scroll_ratio = (max_scroll > 0) ? insp_scroll_y_ / max_scroll : 0.0f;
    float thumb_y = track_y + scroll_ratio * (track_h - thumb_h);

    // Thumb
    bool hovered = mouse_.x >= track_x && mouse_.x <= track_x + kInspScrollbarW &&
                   mouse_.y >= thumb_y && mouse_.y <= thumb_y + thumb_h;
    float thumb_alpha = (hovered || insp_scrollbar_dragging_) ? 0.8f : 0.5f;
    tr.draw_rect(track_x, thumb_y, kInspScrollbarW, thumb_h,
                 style_.scrollbar_thumb[0], style_.scrollbar_thumb[1], style_.scrollbar_thumb[2], thumb_alpha);
}

void NodeGraphUI::draw_midi_map_banner(Renderer2D& tr) {
    if (!midi_map_mode_) return;

    float banner_y = kPerfBarH;
    float banner_w = static_cast<float>(win_w_);
    if (has_selection()) banner_w = graph_right();

    tr.draw_rect(0, banner_y, banner_w, kMidiMapBannerH,
                 kMidiMapBanner[0], kMidiMapBanner[1], kMidiMapBanner[2], kMidiMapBanner[3]);

    const char* status = midi_map_waiting_
        ? "MIDI MAP: Wiggle a knob..."
        : "MIDI MAP: Click a parameter...";
    tr.draw_text(10, banner_y + 4, status, 0.9f, 0.95f, 1.0f);
}

void NodeGraphUI::draw_inspector_header(Renderer2D& tr, const NodeSnapshot& node,
                                        float px, float& py) {
    const auto& op = *node.op_info;
    tr.draw_text(px, py, op.name.c_str(), 1.0f, 1.0f, 1.0f);
    py += kLineH;
    tr.draw_text(px, py, single_selected_id().c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    py += kLineH + 8;

    // Separator
    tr.draw_rect(px, py, kInspContentW, 1, style_.separator[0], style_.separator[1], style_.separator[2]);
    py += 8;
}

void NodeGraphUI::draw_inspector_knob(Renderer2D& tr, const NodeSnapshot& node,
                                       InspectorLayout& layout, uint32_t pi) {
    const auto& pd = node.op_info->params[pi];
    float val = node.param_values[pi];
    float px = layout.x;
    float py = layout.y;
    float panel_w = layout.col_w;

    // Knob center
    float cx = px + panel_w * 0.5f;
    float cy = py + kKnobRadius;

    // Normalized value
    float range = pd.max_value - pd.min_value;
    float t = (range > 0) ? (val - pd.min_value) / range : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));

    // Track arc (full 270° sweep)
    tr.draw_arc(cx, cy, kKnobRadius, kKnobArcAngleStart, kKnobArcAngleEnd,
                kKnobArcThickness, kKnobArcSegments,
                style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);

    // Fill arc (partial sweep based on value)
    float fill_end = kKnobArcAngleStart + t * (kKnobArcAngleEnd - kKnobArcAngleStart);
    if (t > 0.001f) {
        int fill_segs = std::max(1, static_cast<int>(kKnobArcSegments * t));
        tr.draw_arc(cx, cy, kKnobRadius, kKnobArcAngleStart, fill_end,
                    kKnobArcThickness, fill_segs,
                    style_.slider_fill[0], style_.slider_fill[1], style_.slider_fill[2]);
    }

    // Indicator dot at current value angle
    float dot_angle = fill_end;
    float dot_x = cx + std::cos(dot_angle) * kKnobRadius;
    float dot_y = cy + std::sin(dot_angle) * kKnobRadius;
    float dot_sz = 4.0f;
    tr.draw_rect(dot_x - dot_sz * 0.5f, dot_y - dot_sz * 0.5f, dot_sz, dot_sz,
                 1.0f, 1.0f, 1.0f);

    // Param name label centered below knob
    float label_y = cy + kKnobRadius + kKnobLabelGap;
    float label_w = tr.text_width(pd.name.c_str(), 0.85f);
    float label_x = cx - label_w * 0.5f;
    tr.draw_text(label_x, label_y, pd.name.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);

    // Value text centered below label
    std::string val_str = (pd.type == VIVID_PARAM_INT)
        ? format_int(static_cast<int>(val))
        : format_float(val, 2);
    float val_w = tr.text_width(val_str.c_str(), 0.8f);
    float val_text_y = label_y + tr.line_height() * 0.85f + kKnobValueGap;
    float val_text_x = cx - val_w * 0.5f;
    tr.draw_text(val_text_x, val_text_y, val_str.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f, 0.8f);

    // Hit-test rect for slider drag (covers the knob area)
    float knob_rect_x = cx - kKnobRadius - 2;
    float knob_rect_y = py - 2;
    float knob_rect_w = kKnobDiameter + 4;
    float knob_rect_h = kKnobDiameter + 4;
    slider_rects_.push_back({knob_rect_x, knob_rect_y, knob_rect_w, knob_rect_h,
                             single_selected_id(), pd.name});

    // Value text rect for click-to-edit
    value_text_rects_.push_back({val_text_x, val_text_y, val_w, tr.line_height() * 0.8f,
                                 single_selected_id(), pd.name});

    // Total height: knob diameter + label gap + label line + value gap + value line
    float total_h = kKnobDiameter + kKnobLabelGap + tr.line_height() * 0.85f +
                    kKnobValueGap + tr.line_height() * 0.8f + 4.0f;
    layout.end_param(total_h);
}

// -----------------------------------------------------------------------
// XY Pad widget
// -----------------------------------------------------------------------
void NodeGraphUI::draw_inspector_xy_pad(Renderer2D& tr, const NodeSnapshot& node,
                                         InspectorLayout& layout,
                                         uint32_t pi_x, uint32_t pi_y) {
    const auto& op = *node.op_info;
    const auto& pd_x = op.params[pi_x];
    const auto& pd_y = op.params[pi_y];
    float val_x = node.param_values[pi_x];
    float val_y = node.param_values[pi_y];
    float py = layout.y;

    float pad_size = kXYPadSize;
    float content_w = layout.full_w;
    float pad_x = layout.base_x + (content_w - pad_size) * 0.5f;
    float pad_y = py;

    // Dark background rect
    tr.draw_rect(pad_x, pad_y, pad_size, pad_size,
                 style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);

    // Faint center crosshair lines
    float center_x = pad_x + pad_size * 0.5f;
    float center_y = pad_y + pad_size * 0.5f;
    tr.draw_rect(center_x, pad_y, 1, pad_size,
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.15f);
    tr.draw_rect(pad_x, center_y, pad_size, 1,
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.15f);

    // Map param values to pad coords
    float range_x = pd_x.max_value - pd_x.min_value;
    float range_y = pd_y.max_value - pd_y.min_value;
    float tx = (range_x > 0) ? (val_x - pd_x.min_value) / range_x : 0.5f;
    float ty = (range_y > 0) ? 1.0f - (val_y - pd_y.min_value) / range_y : 0.5f;
    tx = std::max(0.0f, std::min(1.0f, tx));
    ty = std::max(0.0f, std::min(1.0f, ty));

    float dot_sx = pad_x + tx * pad_size;
    float dot_sy = pad_y + ty * pad_size;

    // Bright crosshair lines at current position
    tr.draw_rect(dot_sx, pad_y, 1, pad_size,
                 style_.accent[0], style_.accent[1], style_.accent[2], 0.5f);
    tr.draw_rect(pad_x, dot_sy, pad_size, 1,
                 style_.accent[0], style_.accent[1], style_.accent[2], 0.5f);

    // Filled dot at intersection
    float dot_r = kXYPadDotSize * 0.5f;
    tr.draw_rect(dot_sx - dot_r, dot_sy - dot_r, kXYPadDotSize, kXYPadDotSize,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    // Labels below pad
    float label_y = pad_y + pad_size + kXYPadLabelGap;
    std::string label = std::string(pd_x.name) + ": " + format_float(val_x, 2) +
                        "  " + pd_y.name + ": " + format_float(val_y, 2);
    float label_w = tr.text_width(label.c_str(), 0.85f);
    float label_lx = layout.base_x + (content_w - label_w) * 0.5f;
    tr.draw_text(label_lx, label_y, label.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);

    // Hit-test rect for XY pad drag
    xy_pad_rects_.push_back({pad_x, pad_y, pad_size, pad_size,
                             single_selected_id(), pd_x.name, pd_y.name});

    // Value text rects for click-to-edit on each param
    float val_x_str_w = tr.text_width(format_float(val_x, 2).c_str(), 0.85f);
    float val_y_str_w = tr.text_width(format_float(val_y, 2).c_str(), 0.85f);
    float name_x_w = tr.text_width((std::string(pd_x.name) + ": ").c_str(), 0.85f);
    float name_y_w = tr.text_width(("  " + std::string(pd_y.name) + ": ").c_str(), 0.85f);
    float lh = tr.line_height() * 0.85f;
    value_text_rects_.push_back({label_lx + name_x_w, label_y, val_x_str_w, lh,
                                 single_selected_id(), pd_x.name});
    value_text_rects_.push_back({label_lx + name_x_w + val_x_str_w + name_y_w, label_y,
                                 val_y_str_w, lh, single_selected_id(), pd_y.name});

    float total_h = pad_size + kXYPadLabelGap + lh + 8.0f;
    layout.end_param(total_h);
}

// -----------------------------------------------------------------------
// Color swatch widget (inline in inspector)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_inspector_color_swatch(Renderer2D& tr, const NodeSnapshot& node,
                                               InspectorLayout& layout,
                                               uint32_t pi_r, uint32_t pi_g, uint32_t pi_b) {
    const auto& op = *node.op_info;
    float r = node.param_values[pi_r];
    float g = node.param_values[pi_g];
    float b = node.param_values[pi_b];
    float py = layout.y;
    float px = layout.base_x;
    float sw = layout.full_w;

    // Color label
    tr.draw_text(px, py, "color", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    py += kLineH;

    // Filled swatch rect
    tr.draw_rect(px, py, sw, kColorSwatchH, r, g, b);

    // 1px border
    draw_rect_border(tr, px, py, sw, kColorSwatchH,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.6f);

    // Hex text overlay (right-aligned)
    char hex[8];
    rgb_to_hex(r, g, b, hex, sizeof(hex));
    float hex_w = tr.text_width(hex);
    // Choose text color based on luminance for readability
    float lum = 0.299f * r + 0.587f * g + 0.114f * b;
    float tc = lum > 0.5f ? 0.0f : 1.0f;
    tr.draw_text(px + sw - hex_w - 4, py + 3, hex, tc, tc, tc, 0.9f);

    color_swatch_rects_.push_back({px, py, sw, kColorSwatchH,
                                   single_selected_id(),
                                   op.params[pi_r].name, op.params[pi_g].name, op.params[pi_b].name});

    float total_h = kLineH + kColorSwatchH + 8.0f;
    layout.end_param(total_h);
}

// -----------------------------------------------------------------------
// Color picker popup (drawn over everything in overlay pass)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_color_popup(Renderer2D& tr) {
    if (!color_popup_open_) return;

    float pad = kColorPopupPad;
    float sv_size = kColorPopupSVSize;
    float hue_w = kColorHueBarW;
    float gap = kColorPopupGap;
    float hex_h = kColorHexFieldH;

    float rgb_gap = kColorRGBGap;
    float rgb_h = kColorRGBFieldH;
    float popup_w = pad + sv_size + gap + hue_w + pad;
    float popup_h = pad + sv_size + gap + hex_h + rgb_gap + rgb_h + pad;
    float px = color_popup_x_;
    float py = color_popup_y_;

    // Clamp to window bounds
    if (px + popup_w > static_cast<float>(win_w_)) px = static_cast<float>(win_w_) - popup_w;
    if (py + popup_h > static_cast<float>(win_h_)) py = static_cast<float>(win_h_) - popup_h;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    color_popup_x_ = px;
    color_popup_y_ = py;

    // Background
    draw_popup_bg(tr, style_, px, py, popup_w, popup_h);

    float sv_x = px + pad;
    float sv_y = py + pad;

    // SV square: render as grid of colored rects
    constexpr int kGridN = 32;
    float cell_w = sv_size / kGridN;
    float cell_h = sv_size / kGridN;
    for (int yi = 0; yi < kGridN; ++yi) {
        for (int xi = 0; xi < kGridN; ++xi) {
            float s = (xi + 0.5f) / kGridN;
            float v = 1.0f - (yi + 0.5f) / kGridN;
            float cr, cg, cb;
            hsv_to_rgb(color_popup_h_, s, v, cr, cg, cb);
            tr.draw_rect(sv_x + xi * cell_w, sv_y + yi * cell_h,
                         cell_w + 0.5f, cell_h + 0.5f, cr, cg, cb);
        }
    }

    // SV crosshair indicator
    float sv_ix = sv_x + color_popup_s_ * sv_size;
    float sv_iy = sv_y + (1.0f - color_popup_v_) * sv_size;
    tr.draw_rect(sv_ix - 5, sv_iy, 11, 1, 1.0f, 1.0f, 1.0f, 0.8f);
    tr.draw_rect(sv_ix, sv_iy - 5, 1, 11, 1.0f, 1.0f, 1.0f, 0.8f);
    // Dark outline for visibility on bright backgrounds
    tr.draw_rect(sv_ix - 6, sv_iy - 1, 13, 1, 0.0f, 0.0f, 0.0f, 0.4f);
    tr.draw_rect(sv_ix - 6, sv_iy + 1, 13, 1, 0.0f, 0.0f, 0.0f, 0.4f);
    tr.draw_rect(sv_ix - 1, sv_iy - 6, 1, 13, 0.0f, 0.0f, 0.0f, 0.4f);
    tr.draw_rect(sv_ix + 1, sv_iy - 6, 1, 13, 0.0f, 0.0f, 0.0f, 0.4f);

    // Hue bar
    float hue_x = sv_x + sv_size + gap;
    float hue_y = sv_y;
    float hue_cell_h = sv_size / kGridN;
    for (int i = 0; i < kGridN; ++i) {
        float h = (i + 0.5f) / kGridN * 360.0f;
        float cr, cg, cb;
        hsv_to_rgb(h, 1.0f, 1.0f, cr, cg, cb);
        tr.draw_rect(hue_x, hue_y + i * hue_cell_h,
                     hue_w, hue_cell_h + 0.5f, cr, cg, cb);
    }

    // Hue bar indicator
    float hue_iy = hue_y + (color_popup_h_ / 360.0f) * sv_size;
    tr.draw_rect(hue_x - 1, hue_iy - 1, hue_w + 2, 3, 1.0f, 1.0f, 1.0f, 0.9f);
    tr.draw_rect(hue_x - 2, hue_iy - 2, hue_w + 4, 1, 0.0f, 0.0f, 0.0f, 0.5f);
    tr.draw_rect(hue_x - 2, hue_iy + 2, hue_w + 4, 1, 0.0f, 0.0f, 0.0f, 0.5f);

    // Hex input field
    float hex_y = sv_y + sv_size + gap;
    float hex_w_full = sv_size + gap + hue_w;
    tr.draw_rect(sv_x, hex_y, hex_w_full, hex_h,
                 style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
    // Border
    if (color_editing_hex_) {
        tr.draw_rect(sv_x - 1, hex_y - 1, hex_w_full + 2, hex_h + 2,
                     style_.accent[0], style_.accent[1], style_.accent[2]);
    }

    // Hex text
    float cr, cg, cb;
    hsv_to_rgb(color_popup_h_, color_popup_s_, color_popup_v_, cr, cg, cb);
    char hex[8];
    rgb_to_hex(cr, cg, cb, hex, sizeof(hex));

    if (color_editing_hex_) {
        std::string display = color_hex_buffer_ + "_";
        tr.draw_text(sv_x + 4, hex_y + 2, display.c_str(), 0.95f, 0.95f, 0.95f);
    } else {
        tr.draw_text(sv_x + 4, hex_y + 2, hex,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }

    // RGB channel fields
    float rgb_y = hex_y + hex_h + rgb_gap;
    float field_gap = 4.0f;
    float total_gap = field_gap * 2.0f;
    float field_w = (hex_w_full - total_gap) / 3.0f;
    int rgb_values[3] = {
        static_cast<int>(cr * 255.0f + 0.5f),
        static_cast<int>(cg * 255.0f + 0.5f),
        static_cast<int>(cb * 255.0f + 0.5f)
    };
    const char* rgb_labels[3] = { "R", "G", "B" };
    for (int ch = 0; ch < 3; ++ch) {
        float fx = sv_x + ch * (field_w + field_gap);
        // Background
        tr.draw_rect(fx, rgb_y, field_w, rgb_h,
                     style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
        // Accent border if editing this channel
        if (color_editing_rgb_ == ch) {
            tr.draw_rect(fx - 1, rgb_y - 1, field_w + 2, rgb_h + 2,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
            std::string display = std::string(rgb_labels[ch]) + " " + color_rgb_buffer_ + "_";
            tr.draw_text(fx + 3, rgb_y + 2, display.c_str(), 0.95f, 0.95f, 0.95f);
        } else {
            char label[16];
            snprintf(label, sizeof(label), "%s %d", rgb_labels[ch], rgb_values[ch]);
            tr.draw_text(fx + 3, rgb_y + 2, label,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }
}

void NodeGraphUI::draw_one_inspector_param(Renderer2D& tr, const NodeSnapshot& node,
                                           InspectorLayout& layout, uint32_t pi) {
    const auto& op = *node.op_info;
    float start_y = layout.y;
    float py = layout.y;
    float px = layout.x;
    float panel_w = layout.col_w;
    const auto& pd = op.params[pi];
    float val = node.param_values[pi];

    if (pd.display_hint == VIVID_DISPLAY_KNOB && pd.type == VIVID_PARAM_FLOAT) {
        draw_inspector_knob(tr, node, layout, pi);
        return;
    }

    // Check if this param is driven by a wire connection
    std::string conn_source_label;
    std::string conn_from_node, conn_from_port;
    bool is_connected = false;
    for (const auto& c : snap_.connections) {
        if (c.to_node == single_selected_id() && c.to_port == pd.name) {
            is_connected = true;
            conn_from_node = c.from_node;
            conn_from_port = c.from_port;
            conn_source_label = "\xE2\x86\x90 " + c.from_node + "/" + c.from_port;  // "← node/port"
            break;
        }
    }

    bool is_editing_this = editing_param_ &&
                           edit_node_id_ == single_selected_id() &&
                           edit_param_name_ == pd.name;

    // CC badge (if this param has a MIDI mapping)
    const auto* midi_mm = snap_.find_midi_mapping(single_selected_id(), pd.name);

    // Domain-colored dot indicating this param is driven by a connection
    float label_x = px;
    if (is_connected) {
        const auto* src_ns = snap_.find_node(conn_from_node);
        const float* dot_clr = src_ns ? domain_color(src_ns->domain) : style_.accent.data();
        float dot_sz = 5.0f;
        float dot_x = px - dot_sz - 2.0f;
        float dot_y = py + (kLineH - dot_sz) * 0.5f;
        tr.draw_rect(dot_x, dot_y, dot_sz, dot_sz, dot_clr[0], dot_clr[1], dot_clr[2], 0.9f);
    }

    // Label (dimmed if driven by connection)
    if (is_connected)
        tr.draw_text(label_x, py, pd.name.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    else
        tr.draw_text(label_x, py, pd.name.c_str(), 0.8f, 0.82f, 0.85f);

    // CC badge inline with label
    if (midi_mm) {
        std::string badge = "CC " + std::to_string(midi_mm->cc_number);
        float label_w = tr.text_width(pd.name.c_str());
        float badge_x = px + label_w + 6;
        float badge_w = tr.text_width(badge.c_str()) + 8;
        tr.draw_rect(badge_x, py, badge_w, kMidiBadgeH,
                     kMidiMapBadge[0], kMidiMapBadge[1], kMidiMapBadge[2], kMidiMapBadge[3]);
        tr.draw_text(badge_x + 4, py, badge.c_str(), 0.85f, 0.90f, 1.0f);
    }

    // "Waiting" highlight (pulsing blue outline)
    if (midi_map_waiting_ && midi_map_node_id_ == single_selected_id() &&
        midi_map_param_name_ == pd.name) {
        float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(perf_frame_counter_) * 0.15f);
        tr.draw_rect(px - 2, py - 2, panel_w + 4, kLineH + 4,
                     0.3f, 0.5f, 0.9f, pulse * 0.6f);
    }

    // File params: special rendering (filename + browse button), then return
    if (pd.type == VIVID_PARAM_FILE) {
        py += kLineH;
        // Look up current file path
        std::string file_path;
        auto fp_it = node.file_param_values.find(pd.name);
        if (fp_it != node.file_param_values.end())
            file_path = fp_it->second;

        // Extract just the filename for display
        std::string display_name = "Browse\xe2\x80\xa6";  // "Browse…"
        if (!file_path.empty()) {
            auto slash = file_path.rfind('/');
            display_name = (slash != std::string::npos) ? file_path.substr(slash + 1) : file_path;
        }

        // Draw button background
        float btn_h = kDropdownH;
        tr.draw_rect(px, py, panel_w, btn_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        // Truncate display name if it's too wide
        float max_text_w = panel_w - 12;
        std::string truncated = display_name;
        while (tr.text_width(truncated.c_str()) > max_text_w && truncated.size() > 4) {
            truncated = "\xe2\x80\xa6" + truncated.substr(truncated.size() - (truncated.size() - 4));
        }
        tr.draw_text(px + 6, py + 1, truncated.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        file_button_rects_.push_back({px, py, panel_w, btn_h,
                                      single_selected_id(), pd.name});
        py += btn_h + 6;
        return;
    }

    // Value text (right-aligned on the label line)
    std::string val_str;
    if (pd.type == VIVID_PARAM_BOOL) {
        val_str = val > 0.5f ? "true" : "false";
    } else if (pd.choice_count > 0) {
        int idx = static_cast<int>(val);
        if (idx >= 0 && idx < static_cast<int>(pd.choice_labels.size()))
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
        float edit_w = panel_w * 0.4f;
        float edit_x = px + panel_w - edit_w;
        float edit_h = kLineH;
        tr.draw_rect(edit_x - 1, val_y - 1, edit_w + 2, edit_h + 2,
                     style_.accent[0], style_.accent[1], style_.accent[2]);
        tr.draw_rect(edit_x, val_y, edit_w, edit_h, style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
        std::string display = edit_buffer_ + "_";
        tr.draw_text(edit_x + 2, val_y, display.c_str(), 0.95f, 0.95f, 0.95f);
    } else {
        if (is_connected)
            tr.draw_text(val_x, py, val_str.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
        else
            tr.draw_text(val_x, py, val_str.c_str(), 0.8f, 0.82f, 0.85f);
        if (pd.type != VIVID_PARAM_BOOL && pd.choice_count == 0) {
            value_text_rects_.push_back({val_x, val_y, vw, kLineH,
                                         single_selected_id(), pd.name});
        }
    }
    py += kLineH;

    if (pd.type == VIVID_PARAM_BOOL) {
        float bx = px, by = py;
        tr.draw_rect(bx, by, kCheckboxSize, kCheckboxSize, style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        if (val > 0.5f) {
            float check_a = is_connected ? 0.3f : 1.0f;
            tr.draw_rect(bx + 2, by + 2, kCheckboxSize - 4, kCheckboxSize - 4,
                         style_.accent[0], style_.accent[1], style_.accent[2], check_a);
        }
        bool_rects_.push_back({bx, by, kCheckboxSize, kCheckboxSize, single_selected_id(), pd.name});
        py += kCheckboxSize + 6;
    } else if (pd.choice_count > 0) {
        float dx = px, dy = py;
        float dw = panel_w, dh = kDropdownH;
        tr.draw_rect(dx, dy, dw, dh, style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        int idx = static_cast<int>(val);
        const char* label = (idx >= 0 && idx < static_cast<int>(pd.choice_labels.size()))
                            ? pd.choice_labels[idx].c_str() : "?";
        if (is_connected)
            tr.draw_text(dx + 6, dy + 1, label, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
        else
            tr.draw_text(dx + 6, dy + 1, label, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        float arrow_x = dx + dw - 16;
        tr.draw_text(arrow_x, dy + 1, "\xE2\x96\xBE", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        dropdown_rects_.push_back({dx, dy, dw, dh, single_selected_id(), pd.name});
        py += dh + 6;
    } else {
        float sx = px, sy = py;
        float sw = panel_w, sh = kSliderH;
        tr.draw_rect(sx, sy, sw, sh, style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        float range = pd.max_value - pd.min_value;
        float t = (range > 0) ? (val - pd.min_value) / range : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));
        const float* sc = domain_color(node.domain);
        if (is_connected) {
            tr.draw_rect(sx, sy, sw * t, sh, sc[0], sc[1], sc[2], 0.3f);
            // Modulation range overlay: show range between static value and modulated value
            if (!conn_from_node.empty()) {
                auto ni = snap_.node_index.find(conn_from_node);
                if (ni != snap_.node_index.end()) {
                    const auto& src = snap_.nodes[ni->second];
                    auto pi_it = src.output_port_indices.find(conn_from_port);
                    if (pi_it != src.output_port_indices.end() && pi_it->second < src.output_values.size()) {
                        float mod_val = src.output_values[pi_it->second];
                        float mod_t = (range > 0) ? std::clamp((mod_val - pd.min_value) / range, 0.0f, 1.0f) : 0.0f;
                        float t_min = std::min(t, mod_t);
                        float t_max = std::max(t, mod_t);
                        tr.draw_rect(sx + sw * t_min, sy, sw * (t_max - t_min), sh, sc[0], sc[1], sc[2], 0.20f);
                    }
                }
            }
        } else {
            tr.draw_rect(sx, sy, sw * t, sh, sc[0], sc[1], sc[2]);
            float thumb_x = sx + sw * t - 3;
            tr.draw_rect(thumb_x, sy - 2, 6, sh + 4, style_.accent[0], style_.accent[1], style_.accent[2]);
        }
        slider_rects_.push_back({sx, sy - 4, sw, sh + 8, single_selected_id(), pd.name});
        py += sh + 10;
    }

    // Source label for connected params (e.g. "← lfo_1/value")
    if (is_connected) {
        tr.draw_text(px, py - 4, conn_source_label.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.6f);
        py += kLineH - 2;
    }

    // Inline MIDI min/max controls (only in MIDI map mode, only for mapped params)
    if (midi_map_mode_ && midi_mm) {
        float row_y = py;
        float field_w = 50.0f;

        bool is_editing_min = editing_midi_range_ &&
                              midi_range_node_id_ == single_selected_id() &&
                              midi_range_param_name_ == pd.name &&
                              midi_range_editing_min_;
        bool is_editing_max = editing_midi_range_ &&
                              midi_range_node_id_ == single_selected_id() &&
                              midi_range_param_name_ == pd.name &&
                              !midi_range_editing_min_;

        // "min" label
        tr.draw_text(px, row_y, "min", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float min_x = px + 28;
        if (is_editing_min) {
            tr.draw_rect(min_x - 1, row_y - 1, field_w + 2, kMidiRangeH,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
            tr.draw_rect(min_x, row_y, field_w, kMidiRangeH - 2, style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
            std::string display = edit_buffer_ + "_";
            tr.draw_text(min_x + 2, row_y, display.c_str(), 0.95f, 0.95f, 0.95f);
        } else {
            tr.draw_rect(min_x, row_y, field_w, kMidiRangeH - 2,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            std::string min_str = format_float(midi_mm->range_min, 2);
            tr.draw_text(min_x + 2, row_y, min_str.c_str(), 0.8f, 0.82f, 0.85f);
        }
        midi_range_rects_.push_back({min_x, row_y, field_w, kMidiRangeH,
                                     single_selected_id(), pd.name, true});

        // "max" label
        float max_label_x = min_x + field_w + 10;
        tr.draw_text(max_label_x, row_y, "max", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float max_x = max_label_x + 30;
        if (is_editing_max) {
            tr.draw_rect(max_x - 1, row_y - 1, field_w + 2, kMidiRangeH,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
            tr.draw_rect(max_x, row_y, field_w, kMidiRangeH - 2, style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
            std::string display = edit_buffer_ + "_";
            tr.draw_text(max_x + 2, row_y, display.c_str(), 0.95f, 0.95f, 0.95f);
        } else {
            tr.draw_rect(max_x, row_y, field_w, kMidiRangeH - 2,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            std::string max_str = format_float(midi_mm->range_max, 2);
            tr.draw_text(max_x + 2, row_y, max_str.c_str(), 0.8f, 0.82f, 0.85f);
        }
        midi_range_rects_.push_back({max_x, row_y, field_w, kMidiRangeH,
                                     single_selected_id(), pd.name, false});

        // "x" remove button
        float remove_x = max_x + field_w + 8;
        tr.draw_rect(remove_x, row_y, 16, kMidiRangeH - 2, 0.5f, 0.2f, 0.2f, 0.8f);
        tr.draw_text(remove_x + 3, row_y, "x", 0.9f, 0.6f, 0.6f);
        midi_remove_rects_.push_back({remove_x, row_y, 16, kMidiRangeH,
                                      single_selected_id(), pd.name});

        py += kMidiRangeH + 4;
    }

    layout.end_param(py - start_y);
}

void NodeGraphUI::draw_one_inspector_param_simple(Renderer2D& tr, const NodeSnapshot& node,
                                                  float px, float& py, uint32_t pi) {
    InspectorLayout layout;
    layout.x = px; layout.y = py;
    layout.col_w = kInspContentW; layout.full_w = kInspContentW; layout.base_x = px;
    layout.begin_param(0, 0);
    draw_one_inspector_param(tr, node, layout, pi);
    py = layout.y;
}

void NodeGraphUI::draw_inspector_group_header(Renderer2D& tr, InspectorLayout& layout,
                                               const std::string& type_name,
                                               const std::string& group_name,
                                               bool collapsed) {
    float hx = layout.base_x;
    float hy = layout.y + kGroupHeaderPadTop;
    float hw = layout.full_w;

    tr.draw_rect(hx, hy, hw, kGroupHeaderH,
                 style_.group_header_bg[0], style_.group_header_bg[1], style_.group_header_bg[2]);

    float cx = hx + 8.0f;
    float cy = hy + kGroupHeaderH * 0.5f;
    float cs = kGroupChevronSize * 0.5f;

    if (collapsed) {
        // Right-pointing triangle
        tr.draw_tri(cx, cy - cs, cx, cy + cs, cx + cs, cy,
                    style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.8f);
    } else {
        // Down-pointing triangle
        tr.draw_tri(cx - cs, cy - cs * 0.5f, cx + cs, cy - cs * 0.5f, cx, cy + cs * 0.5f,
                    style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.8f);
    }

    float tx = cx + cs + 6.0f;
    tr.draw_text(tx, hy + 3.0f, group_name.c_str(),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.9f);

    group_header_rects_.push_back({hx, hy, hw, kGroupHeaderH, type_name, group_name});
    layout.y = hy + kGroupHeaderH;
}

void NodeGraphUI::draw_inspector_params(Renderer2D& tr, const NodeSnapshot& node,
                                        float px, float& py) {
    const auto& op = *node.op_info;

    // Detect DrumSequencer: has "kick_0" + "snare_0" + "hat_0"
    auto kick0_it = node.param_indices.find("kick_0");
    auto snare0_it = node.param_indices.find("snare_0");
    auto hat0_it = node.param_indices.find("hat_0");
    bool is_drum_seq = (kick0_it != node.param_indices.end() &&
                        snare0_it != node.param_indices.end() &&
                        hat0_it != node.param_indices.end());

    if (is_drum_seq) {
        // Draw steps and swing normally, skip all 96 grid params
        auto ds_steps_it = node.param_indices.find("steps");
        auto ds_swing_it = node.param_indices.find("swing");
        if (ds_steps_it != node.param_indices.end())
            draw_one_inspector_param_simple(tr, node, px, py, ds_steps_it->second);
        if (ds_swing_it != node.param_indices.end())
            draw_one_inspector_param_simple(tr, node, px, py, ds_swing_it->second);

        // Build skip set for the 96 grid params + 192 mod params
        std::unordered_set<uint32_t> grid_params;
        static const char* kDrumPrefixes[] = {"kick_", "snare_", "hat_", "oh_", "clap_", "tom_"};
        static const char* kModSuffixes[] = {"", "ma_", "mb_"};
        for (const char* prefix : kDrumPrefixes) {
            for (const char* mod : kModSuffixes) {
                for (int s = 0; s < 16; ++s) {
                    std::string name = std::string(prefix) + mod + std::to_string(s);
                    auto it = node.param_indices.find(name);
                    if (it != node.param_indices.end())
                        grid_params.insert(it->second);
                }
            }
        }
        if (ds_steps_it != node.param_indices.end())
            grid_params.insert(ds_steps_it->second);
        if (ds_swing_it != node.param_indices.end())
            grid_params.insert(ds_swing_it->second);

        // Draw remaining params, skipping grid params
        for (uint32_t pi = 0; pi < static_cast<uint32_t>(op.params.size()); ++pi) {
            if (grid_params.count(pi)) continue;
            draw_one_inspector_param_simple(tr, node, px, py, pi);
        }
        return;
    }

    // Detect NotePattern: has "steps" + "root_0".."root_7" + "type_0".."type_7"
    auto steps_it = node.param_indices.find("steps");
    auto root0_it = node.param_indices.find("root_0");
    auto type0_it = node.param_indices.find("type_0");
    bool is_note_pattern = (steps_it != node.param_indices.end() &&
                            root0_it != node.param_indices.end() &&
                            type0_it != node.param_indices.end());

    if (is_note_pattern) {
        int num_steps = static_cast<int>(node.param_values[steps_it->second]);
        num_steps = std::max(1, std::min(8, num_steps));

        uint32_t root_base = root0_it->second;
        uint32_t type_base = type0_it->second;

        // Build set of step-indexed param indices to skip in the tail
        std::unordered_set<uint32_t> step_params;
        for (int s = 0; s < 8; ++s) {
            step_params.insert(root_base + s);
            step_params.insert(type_base + s);
        }

        // Draw "steps" slider first
        draw_one_inspector_param_simple(tr, node, px, py, steps_it->second);

        // Draw grouped steps: "Step N" header + root_N dropdown + type_N dropdown
        for (int s = 0; s < num_steps; ++s) {
            // Step header
            char header[16];
            std::snprintf(header, sizeof(header), "Step %d", s + 1);
            py += 4;
            tr.draw_text(px, py, header, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            py += kLineH;

            // Draw root (key) and type (mode) side by side
            {
                float gap = 8.0f;
                float half_w = (kInspContentW - gap) / 2.0f;

                auto draw_half_dropdown = [&](uint32_t pi, float dx, float dw) {
                    const auto& pd = op.params[pi];
                    float val = node.param_values[pi];
                    int idx = static_cast<int>(val);
                    const char* label = (idx >= 0 && idx < static_cast<int>(pd.choice_labels.size()))
                                        ? pd.choice_labels[idx].c_str() : "?";
                    tr.draw_rect(dx, py, dw, kDropdownH,
                                 style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
                    tr.draw_text(dx + 6, py + 1, label, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                    float arrow_x = dx + dw - 16;
                    tr.draw_text(arrow_x, py + 1, "\xE2\x96\xBE",
                                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
                    dropdown_rects_.push_back({dx, py, dw, kDropdownH,
                                               single_selected_id(), pd.name});
                };

                draw_half_dropdown(root_base + s, px, half_w);
                draw_half_dropdown(type_base + s, px + half_w + gap, half_w);
                py += kDropdownH + 6;
            }
        }

        // Draw remaining params (octave, beats_per_step, gate_length, velocity)
        // skipping steps (already drawn) and all root_*/type_* params
        step_params.insert(steps_it->second);
        for (uint32_t pi = 0; pi < static_cast<uint32_t>(op.params.size()); ++pi) {
            if (step_params.count(pi)) continue;
            draw_one_inspector_param_simple(tr, node, px, py, pi);
        }
    } else {
        // Default: layout-aware rendering with group headers
        InspectorLayout layout;
        layout.base_x = px; layout.x = px; layout.y = py;
        layout.full_w = kInspContentW; layout.col_w = kInspContentW;

        std::string current_group;

        uint32_t param_count = static_cast<uint32_t>(op.params.size());
        for (uint32_t pi = 0; pi < param_count; ) {
            const auto& pd = op.params[pi];

            if (pd.group != current_group) {
                layout.flush_row();
                current_group = pd.group;

                if (!current_group.empty()) {
                    bool collapsed = is_group_collapsed(node.type_name, current_group);
                    draw_inspector_group_header(tr, layout, node.type_name, current_group, collapsed);
                    if (collapsed) {
                        while (pi + 1 < param_count &&
                               op.params[pi + 1].group == current_group)
                            ++pi;
                        ++pi;
                        continue;
                    }
                }
            }

            // Compound widget: XY pad (two consecutive XY_PAD params)
            if (pd.display_hint == VIVID_DISPLAY_XY_PAD &&
                pi + 1 < param_count &&
                op.params[pi + 1].display_hint == VIVID_DISPLAY_XY_PAD) {
                layout.flush_row();
                layout.begin_param(0, 0);
                draw_inspector_xy_pad(tr, node, layout, pi, pi + 1);
                pi += 2;
                continue;
            }

            // Compound widget: Color swatch (three consecutive COLOR params)
            if (pd.display_hint == VIVID_DISPLAY_COLOR &&
                pi + 2 < param_count &&
                op.params[pi + 1].display_hint == VIVID_DISPLAY_COLOR &&
                op.params[pi + 2].display_hint == VIVID_DISPLAY_COLOR) {
                layout.flush_row();
                layout.begin_param(0, 0);
                draw_inspector_color_swatch(tr, node, layout, pi, pi + 1, pi + 2);
                pi += 3;
                continue;
            }

            layout.begin_param(pd.layout_columns, pd.layout_column_index);
            draw_one_inspector_param(tr, node, layout, pi);
            ++pi;
        }
        layout.flush_row();
        py = layout.y;
    }
}

void NodeGraphUI::draw_inspector_adsr_preview(Renderer2D& tr, const NodeSnapshot& node,
                                               float px, float& py) {
    // Only draw if all 4 ADSR params exist
    auto a_it = node.param_indices.find("attack");
    auto d_it = node.param_indices.find("decay");
    auto s_it = node.param_indices.find("sustain");
    auto r_it = node.param_indices.find("release");
    if (a_it == node.param_indices.end() || d_it == node.param_indices.end() ||
        s_it == node.param_indices.end() || r_it == node.param_indices.end())
        return;

    float atk = node.param_values[a_it->second];
    float dec = node.param_values[d_it->second];
    float sus = node.param_values[s_it->second];
    float rel = node.param_values[r_it->second];

    // Clamp to sane minimums
    if (atk < 0.0001f) atk = 0.0001f;
    if (dec < 0.001f)  dec = 0.001f;
    if (rel < 0.001f)  rel = 0.001f;
    sus = std::max(0.0f, std::min(1.0f, sus));

    // Check env_bypass state
    bool bypassed = false;
    auto bp_it = node.param_indices.find("env_bypass");
    if (bp_it != node.param_indices.end())
        bypassed = node.param_values[bp_it->second] > 0.5f;

    float alpha_mult = bypassed ? 0.35f : 1.0f;

    // Layout
    float w = kInspContentW;
    float h = kAdsrPreviewH;
    float pad = 6.0f;

    py += 4;

    // Dark background
    tr.draw_rect(px, py, w, h, style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.9f);

    // Curve geometry (same math as Envelope::draw_thumbnail)
    float sustain_width = 0.3f * (atk + dec + rel);
    float total_time = atk + dec + sustain_width + rel;

    auto env_at = [&](float t) -> float {
        if (t <= atk)
            return t / atk;
        t -= atk;
        if (t <= dec)
            return 1.0f - (1.0f - sus) * (t / dec);
        t -= dec;
        if (t <= sustain_width)
            return sus;
        t -= sustain_width;
        if (t <= rel)
            return sus * (1.0f - t / rel);
        return 0.0f;
    };

    auto time_to_x = [&](float t) -> float {
        return px + pad + (t / total_time) * (w - 2.0f * pad);
    };
    auto env_to_y = [&](float e) -> float {
        return py + pad + (1.0f - e) * (h - 2.0f * pad);
    };

    // Filled region below curve (3px-wide translucent rects)
    float plot_w = w - 2.0f * pad;
    int cols = static_cast<int>(plot_w / 3.0f);
    float col_w = plot_w / static_cast<float>(cols);
    float bottom_y = env_to_y(0.0f);

    for (int i = 0; i < cols; ++i) {
        float fx = px + pad + static_cast<float>(i) * col_w;
        float t = (static_cast<float>(i) / static_cast<float>(cols)) * total_time;
        float e = env_at(t);
        float ey = env_to_y(e);
        float fill_h = bottom_y - ey;
        if (fill_h > 0.0f) {
            tr.draw_rect(fx, ey, col_w, fill_h,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.15f * alpha_mult);
        }
    }

    // Curve line segments (~1 point per 2px)
    int segments = std::max(4, cols / 2);
    float prev_x = time_to_x(0.0f);
    float prev_y = env_to_y(env_at(0.0f));
    for (int i = 1; i <= segments; ++i) {
        float t = (static_cast<float>(i) / static_cast<float>(segments)) * total_time;
        float cx = time_to_x(t);
        float cy = env_to_y(env_at(t));
        tr.draw_line(prev_x, prev_y, cx, cy, 1.5f,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.9f * alpha_mult);
        prev_x = cx;
        prev_y = cy;
    }

    // Vertical dashed markers at attack/decay/release boundaries
    float marker_times[3] = { atk, atk + dec, atk + dec + sustain_width };
    for (float mt : marker_times) {
        float mx = time_to_x(mt);
        float top_y = py + pad;
        // Draw dashes (4px on, 4px off)
        for (float dy = top_y; dy < bottom_y; dy += 8.0f) {
            float dash_end = std::min(dy + 4.0f, bottom_y);
            tr.draw_line(mx, dy, mx, dash_end, 1.0f,
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.3f * alpha_mult);
        }
    }

    // "bypassed" label when env_bypass is on
    if (bypassed) {
        tr.draw_text(px + w - tr.text_width("bypassed") - 4, py + 2, "bypassed",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
    }

    py += h + 4;
}

void NodeGraphUI::draw_inspector_note_pattern(Renderer2D& tr, const NodeSnapshot& node,
                                               float px, float& py) {
    // Only draw if this is a NotePattern (has steps, root_0, type_0)
    auto steps_it = node.param_indices.find("steps");
    auto root0_it = node.param_indices.find("root_0");
    auto type0_it = node.param_indices.find("type_0");
    if (steps_it == node.param_indices.end() ||
        root0_it == node.param_indices.end() ||
        type0_it == node.param_indices.end())
        return;

    int num_steps = static_cast<int>(node.param_values[steps_it->second]);
    num_steps = std::max(1, std::min(8, num_steps));

    uint32_t root_base = root0_it->second;
    uint32_t type_base = type0_it->second;

    // Note names and chord abbreviations
    static const char* kNoteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    static const char* kChordAbbr[] = {"M","m","dim","aug","7","m7","M7"};

    // 7-color palette for chord types (matches thumbnail)
    static constexpr std::array<float, 3> kTypeColors[7] = {
        {0.39f, 0.63f, 0.86f},   // Major  — blue
        {0.63f, 0.39f, 0.78f},   // Minor  — purple
        {0.78f, 0.39f, 0.39f},   // Dim    — red
        {0.86f, 0.71f, 0.31f},   // Aug    — gold
        {0.31f, 0.71f, 0.63f},   // Dom7   — teal
        {0.55f, 0.47f, 0.78f},   // Min7   — lavender
        {0.31f, 0.55f, 0.86f},   // Maj7   — sky blue
    };

    // Detect current step from first output note
    int current_step = -1;
    auto notes_it = node.output_port_indices.find("notes");
    if (notes_it != node.output_port_indices.end()) {
        uint32_t pidx = notes_it->second;
        float out_note = 0.0f;
        if (pidx < node.output_spreads.size() && !node.output_spreads[pidx].empty())
            out_note = node.output_spreads[pidx][0];
        else if (pidx < node.output_values.size())
            out_note = node.output_values[pidx];

        auto oct_it = node.param_indices.find("octave");
        int oct = (oct_it != node.param_indices.end()) ? static_cast<int>(node.param_values[oct_it->second]) : 4;

        for (int s = 0; s < num_steps; ++s) {
            int root = static_cast<int>(node.param_values[root_base + s]);
            float expected = static_cast<float>(root + oct * 12);
            if (std::fabs(out_note - expected) < 0.5f) {
                current_step = s;
                break;
            }
        }
    }

    float w = kInspContentW;
    float h = kNotePatternPreviewH;
    float cell_w = w / static_cast<float>(num_steps);

    py += 4;

    // Dark background
    tr.draw_rect(px, py, w, h, style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.9f);

    for (int s = 0; s < num_steps; ++s) {
        int root = static_cast<int>(node.param_values[root_base + s]);
        int chord_type = static_cast<int>(node.param_values[type_base + s]);
        root = std::max(0, std::min(11, root));
        chord_type = std::max(0, std::min(6, chord_type));

        float cx = px + s * cell_w;
        bool is_current = (s == current_step);

        // Current step highlight
        if (is_current) {
            tr.draw_rect(cx, py, cell_w, h, style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.6f);
        }

        // Note name centered
        const char* note = kNoteNames[root];
        float nw = tr.text_width(note);
        float text_x = cx + (cell_w - nw) * 0.5f;
        float text_y = py + 6;
        float bright = is_current ? 1.0f : 0.85f;
        tr.draw_text(text_x, text_y, note, bright, bright, bright);

        // Chord abbreviation below in dim color
        const char* chord = kChordAbbr[chord_type];
        float cw = tr.text_width(chord);
        float chord_x = cx + (cell_w - cw) * 0.5f;
        float chord_y = text_y + kLineH;
        const auto& tc = kTypeColors[chord_type];
        tr.draw_text(chord_x, chord_y, chord, tc[0], tc[1], tc[2], is_current ? 1.0f : 0.7f);

        // Colored bar at bottom
        float bar_h = 4.0f;
        float bar_y = py + h - bar_h - 2.0f;
        tr.draw_rect(cx + 2, bar_y, cell_w - 4, bar_h, tc[0], tc[1], tc[2], is_current ? 0.9f : 0.6f);

        // Cell divider
        if (s > 0) {
            tr.draw_rect(cx, py, 1, h, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
        }
    }

    py += h + 4;
}

void NodeGraphUI::draw_inspector_drum_grid(Renderer2D& tr, const NodeSnapshot& node,
                                           float px, float& py) {
    // Only draw if this is a DrumSequencer (has kick_0, snare_0, hat_0)
    auto kick0_it = node.param_indices.find("kick_0");
    auto snare0_it = node.param_indices.find("snare_0");
    auto hat0_it = node.param_indices.find("hat_0");
    if (kick0_it == node.param_indices.end() ||
        snare0_it == node.param_indices.end() ||
        hat0_it == node.param_indices.end())
        return;

    // Read steps param
    auto steps_it = node.param_indices.find("steps");
    int num_steps = 16;
    if (steps_it != node.param_indices.end())
        num_steps = std::max(1, std::min(16, static_cast<int>(node.param_values[steps_it->second])));

    // Detect current step from "step" output port
    int current_step = -1;
    auto step_out_it = node.output_port_indices.find("step");
    if (step_out_it != node.output_port_indices.end()) {
        uint32_t pidx = step_out_it->second;
        if (pidx < node.output_values.size())
            current_step = static_cast<int>(node.output_values[pidx]);
    }

    // Drum row config: prefix, label, color
    static const char* kDrumPrefix[] = {"kick_", "snare_", "hat_", "oh_", "clap_", "tom_"};
    static const char* kDrumLabel[]  = {"KK", "SN", "CH", "OH", "CP", "TM"};
    static constexpr std::array<float, 3> kDrumColors[6] = {
        {0.86f, 0.31f, 0.31f},  // kick — red
        {0.86f, 0.75f, 0.24f},  // snare — gold
        {0.24f, 0.78f, 0.71f},  // hat — teal
        {0.31f, 0.51f, 0.86f},  // oh — blue
        {0.63f, 0.35f, 0.78f},  // clap — purple
        {0.31f, 0.78f, 0.39f},  // tom — green
    };

    // Layout
    float panel_w = kInspContentW;
    float label_w = 28.0f;
    float grid_w = panel_w - label_w;
    float cell_w = grid_w / 16.0f;
    float cell_h = 14.0f;
    float cell_pad = 2.0f;
    float grid_h = 6.0f * cell_h;

    py += 4;

    // --- Tab bar ---
    static const char* kTabLabels[] = {"Pattern", "Mod A", "Mod B"};
    float tab_w = 80.0f;
    float tab_h = 18.0f;
    float tab_y = py;

    for (int t = 0; t < 3; ++t) {
        float tx = px + t * tab_w;
        bool active = (drum_grid_tab_ == t);

        // Tab background
        if (active) {
            tr.draw_rect(tx, tab_y, tab_w, tab_h,
                         style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.9f);
            // Accent underline
            tr.draw_rect(tx, tab_y + tab_h - 2, tab_w, 2,
                         style_.accent[0], style_.accent[1], style_.accent[2], 1.0f);
        }

        float text_alpha = active ? 1.0f : 0.5f;
        tr.draw_text(tx + 8, tab_y + 3, kTabLabels[t],
                     style_.dim_text[0] * (active ? 1.5f : 1.0f),
                     style_.dim_text[1] * (active ? 1.5f : 1.0f),
                     style_.dim_text[2] * (active ? 1.5f : 1.0f),
                     text_alpha);

        drum_tab_rects_.push_back({tx, tab_y, tab_w, tab_h,
                                   single_selected_id(), std::to_string(t)});
    }

    py += tab_h + 2;

    float total_h = grid_h + 8.0f;

    // Dark background for grid area
    tr.draw_rect(px, py, panel_w, total_h, style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.9f);

    float grid_x = px + label_w;
    float grid_y = py + 4.0f;

    // Current step column highlight (full height)
    if (current_step >= 0 && current_step < num_steps) {
        float hx = grid_x + current_step * cell_w;
        tr.draw_rect(hx, grid_y, cell_w, grid_h,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.15f);
    }

    // Beat group separators (every 4 steps)
    for (int b = 1; b < 4; ++b) {
        float sx = grid_x + b * 4 * cell_w;
        tr.draw_rect(sx - 0.5f, grid_y, 1.0f, grid_h,
                     style_.separator[0], style_.separator[1], style_.separator[2], 0.6f);
    }

    // Mod param prefixes for Mod A / Mod B
    static const char* kModAPrefix[] = {"kick_ma_", "snare_ma_", "hat_ma_", "oh_ma_", "clap_ma_", "tom_ma_"};
    static const char* kModBPrefix[] = {"kick_mb_", "snare_mb_", "hat_mb_", "oh_mb_", "clap_mb_", "tom_mb_"};

    for (int drum = 0; drum < 6; ++drum) {
        float row_y = grid_y + drum * cell_h;

        // Row label
        tr.draw_text(px + 2, row_y + 1, kDrumLabel[drum],
                     kDrumColors[drum][0], kDrumColors[drum][1], kDrumColors[drum][2], 0.8f);

        for (int s = 0; s < 16; ++s) {
            float cx = grid_x + s * cell_w;
            bool beyond_steps = (s >= num_steps);

            // Check trigger state (used by all tabs)
            std::string trig_name = std::string(kDrumPrefix[drum]) + std::to_string(s);
            auto trig_it = node.param_indices.find(trig_name);
            bool trigger_active = false;
            if (trig_it != node.param_indices.end())
                trigger_active = node.param_values[trig_it->second] > 0.5f;

            if (drum_grid_tab_ == 0) {
                // --- Pattern tab: boolean toggle grid ---
                if (trigger_active) {
                    float alpha = beyond_steps ? 0.25f : 0.9f;
                    tr.draw_rect(cx + cell_pad, row_y + cell_pad,
                                 cell_w - 2 * cell_pad, cell_h - 2 * cell_pad,
                                 kDrumColors[drum][0], kDrumColors[drum][1], kDrumColors[drum][2], alpha);
                }

                drum_grid_rects_.push_back({cx, row_y, cell_w, cell_h,
                                            single_selected_id(), trig_name});
            } else {
                // --- Mod A or Mod B tab: vertical fill bar grid ---
                const char** mod_prefix = (drum_grid_tab_ == 1) ? kModAPrefix : kModBPrefix;
                auto& mod_rects = (drum_grid_tab_ == 1) ? drum_mod_a_rects_ : drum_mod_b_rects_;

                std::string mod_name = std::string(mod_prefix[drum]) + std::to_string(s);
                auto mod_it = node.param_indices.find(mod_name);
                float mod_val = 0.5f;
                if (mod_it != node.param_indices.end())
                    mod_val = node.param_values[mod_it->second];

                float base_alpha = beyond_steps ? 0.25f : (trigger_active ? 0.8f : 0.3f);

                // Dark track background
                tr.draw_rect(cx + cell_pad, row_y + cell_pad,
                             cell_w - 2 * cell_pad, cell_h - 2 * cell_pad,
                             0.1f, 0.1f, 0.12f, base_alpha);

                // Fill bar from bottom
                float inner_h = cell_h - 2 * cell_pad;
                float fill_h = mod_val * inner_h;
                tr.draw_rect(cx + cell_pad, row_y + cell_pad + inner_h - fill_h,
                             cell_w - 2 * cell_pad, fill_h,
                             kDrumColors[drum][0], kDrumColors[drum][1], kDrumColors[drum][2], base_alpha);

                mod_rects.push_back({cx, row_y, cell_w, cell_h,
                                     single_selected_id(), mod_name});
            }
        }
    }

    py += total_h + 4;
}

void NodeGraphUI::draw_inspector_resolution(Renderer2D& tr, const NodeSnapshot& node,
                                            float px, float& py) {
    if (!node.is_gpu || node.gpu_tex_width == 0 || node.gpu_tex_height == 0)
        return;

    bool is_generator = node.is_generator;
    float panel_w = kInspContentW;

    py += 4;
    tr.draw_rect(px, py, panel_w, 1, style_.separator[0], style_.separator[1], style_.separator[2]);
    py += 8;

    tr.draw_text(px, py, "Resolution", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    py += kLineH;

    bool editing_w = editing_resolution_ &&
                     edit_res_node_id_ == single_selected_id() && edit_res_is_width_;
    bool editing_h = editing_resolution_ &&
                     edit_res_node_id_ == single_selected_id() && !edit_res_is_width_;

    std::string w_str = editing_w ? (edit_buffer_ + "_") : format_uint(node.gpu_tex_width);
    std::string h_str = editing_h ? (edit_buffer_ + "_") : format_uint(node.gpu_tex_height);

    float val_x = px + 4;

    if (is_generator) {
        if (editing_w) {
            tr.draw_rect(val_x, py, kResInputW, kLineH, 0.12f, 0.14f, 0.18f);
        }
        tr.draw_text(val_x, py, w_str.c_str(),
                     editing_w ? 1.0f : 0.8f,
                     editing_w ? 1.0f : 0.82f,
                     editing_w ? 1.0f : 0.85f);
        resolution_rects_.push_back({val_x, py, kResInputW, kLineH,
                                     single_selected_id(), true});
    } else {
        tr.draw_text(val_x, py, w_str.c_str(), 0.5f, 0.52f, 0.55f);
    }

    tr.draw_text(val_x + kResInputW, py, " x ", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

    float h_val_x = val_x + kResInputW + 24.0f;

    if (is_generator) {
        if (editing_h) {
            tr.draw_rect(h_val_x, py, kResInputW, kLineH, 0.12f, 0.14f, 0.18f);
        }
        tr.draw_text(h_val_x, py, h_str.c_str(),
                     editing_h ? 1.0f : 0.8f,
                     editing_h ? 1.0f : 0.82f,
                     editing_h ? 1.0f : 0.85f);
        resolution_rects_.push_back({h_val_x, py, kResInputW, kLineH,
                                     single_selected_id(), false});
    } else {
        tr.draw_text(h_val_x, py, h_str.c_str(), 0.5f, 0.52f, 0.55f);
    }

    py += kLineH;
}

void NodeGraphUI::draw_inspector_outputs(Renderer2D& tr, const NodeSnapshot& node,
                                         float px, float& py) {
    float panel_w = kInspContentW;

    // Separator before outputs
    py += 4;
    tr.draw_rect(px, py, panel_w, 1, style_.separator[0], style_.separator[1], style_.separator[2]);
    py += 8;

    tr.draw_text(px, py, "Outputs", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    py += kLineH;

    auto sorted_outs = sorted_ports(node.output_port_indices);

    for (const auto& [idx, name] : sorted_outs) {
        std::string line;
        if (idx < node.output_spreads.size() && !node.output_spreads[idx].empty()) {
            line = name + " = [" + std::to_string(node.output_spreads[idx].size()) + " bins]";
        } else {
            line = name + " = " + format_float(node.output_values[idx]);
        }
        tr.draw_text(px, py, line.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        py += kLineH;
    }
}

// -----------------------------------------------------------------------
// Connection Matrix
// -----------------------------------------------------------------------
static constexpr float kMatrixCellSize = 18.0f;
static constexpr float kMatrixCellPad = 2.0f;
static constexpr float kMatrixHeaderH = 60.0f;  // space for rotated column labels
static constexpr float kMatrixRowLabelW = 100.0f;

void NodeGraphUI::draw_matrix_section(Renderer2D& tr, const NodeSnapshot& src_node,
                                       const NodeSnapshot& dst_node, float px, float& py) {
    if (!src_node.op_info || !dst_node.op_info) return;

    // Rows: source node's output ports + params
    struct RowInfo { std::string port_name; bool is_param = false; };
    std::vector<RowInfo> rows;
    auto sorted_outs = sorted_ports(src_node.output_port_indices);
    for (const auto& [idx, name] : sorted_outs)
        rows.push_back({name, false});
    // Param rows
    std::vector<std::pair<uint32_t, std::string>> sorted_params;
    for (const auto& [name, idx] : src_node.param_indices)
        if (!src_node.output_port_indices.count(name)) sorted_params.push_back({idx, name});
    std::sort(sorted_params.begin(), sorted_params.end());
    for (const auto& [idx, name] : sorted_params) {
        const ParamInfo* pd = src_node.find_param(name);
        if (pd && pd->type == VIVID_PARAM_FILE) continue;
        rows.push_back({name, true});
    }
    if (rows.empty()) return;

    // Columns: destination node's params (non-FILE) + signal input ports
    struct ColInfo { std::string name; };
    std::vector<ColInfo> columns;
    for (const auto& pd : dst_node.op_info->params) {
        if (pd.type == VIVID_PARAM_FILE) continue;
        columns.push_back({pd.name});
    }
    for (const auto& pi : dst_node.op_info->ports) {
        if (pi.direction != VIVID_PORT_INPUT) continue;
        bool is_param = false;
        for (const auto& pd : dst_node.op_info->params) {
            if (pd.name == pi.name) { is_param = true; break; }
        }
        if (!is_param) columns.push_back({pi.name});
    }
    if (columns.empty()) return;

    // Build lookup for existing connections from src -> dst
    struct ConnKey { std::string from_port, to_port; float scale; };
    std::vector<ConnKey> active_conns;
    for (const auto& c : snap_.connections) {
        if (c.from_node == src_node.node_id && c.to_node == dst_node.node_id)
            active_conns.push_back({c.from_port, c.to_port, c.scale});
    }
    auto find_conn = [&](const std::string& fp, const std::string& tp) -> const ConnKey* {
        for (const auto& c : active_conns) {
            if (c.from_port == fp && c.to_port == tp) return &c;
        }
        return nullptr;
    };

    // --- Section header: "src_name → dst_name" with domain colors ---
    float panel_w = kInspContentW;
    const float* src_clr = domain_color(src_node.domain);
    const float* dst_clr = domain_color(dst_node.domain);
    tr.draw_text(px, py, src_node.op_info->name.c_str(), src_clr[0], src_clr[1], src_clr[2]);
    float src_w = tr.text_width(src_node.op_info->name.c_str());
    tr.draw_text(px + src_w + 2, py, " \xE2\x86\x92 ", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);  // " → "
    float arrow_w = tr.text_width(" \xE2\x86\x92 ");
    tr.draw_text(px + src_w + 2 + arrow_w, py, dst_node.op_info->name.c_str(), dst_clr[0], dst_clr[1], dst_clr[2]);
    py += kLineH + 4;

    // Compute layout geometry
    float matrix_x = px + kMatrixRowLabelW;
    float matrix_y = py + kMatrixHeaderH;
    float cell_step = kMatrixCellSize + kMatrixCellPad;

    // Limit visible columns/rows to fit inspector width
    float avail_w = panel_w - kMatrixRowLabelW;
    int max_cols = static_cast<int>(avail_w / cell_step);
    if (max_cols < 1) max_cols = 1;
    int vis_cols = std::min(static_cast<int>(columns.size()), max_cols);

    // --- Draw column headers (abbreviated, drawn vertically) ---
    for (int ci = 0; ci < vis_cols; ++ci) {
        float cx = matrix_x + ci * cell_step + cell_step * 0.5f;
        const auto& name = columns[ci].name;
        std::string abbr = name.length() > 5 ? name.substr(0, 5) : name;
        for (size_t chi = 0; chi < abbr.size(); ++chi) {
            char buf[2] = { abbr[chi], '\0' };
            float cy = py + chi * 10.0f;
            tr.draw_text(cx - 3, cy, buf, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f, 0.8f);
        }
    }

    // --- Draw rows ---
    int vis_rows = std::min(static_cast<int>(rows.size()), 30);

    for (int ri = 0; ri < vis_rows; ++ri) {
        const auto& row = rows[ri];
        float row_y = matrix_y + ri * cell_step;

        // Row label: output port or param name (truncated)
        std::string label = row.is_param ? ("\xC2\xB7 " + row.port_name) : row.port_name;
        if (label.size() > 14) label = label.substr(0, 13) + "~";
        float row_alpha = row.is_param ? 0.5f : 0.7f;
        tr.draw_text(px, row_y + 2, label.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], row_alpha, 0.8f);

        for (int ci = 0; ci < vis_cols; ++ci) {
            float cx = matrix_x + ci * cell_step;
            float cy = row_y;

            // Cell background
            tr.draw_rect(cx, cy, kMatrixCellSize, kMatrixCellSize,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.5f);

            const auto* conn = find_conn(row.port_name, columns[ci].name);
            bool is_connected = (conn != nullptr);
            float scale = is_connected ? conn->scale : 0.0f;

            if (is_connected) {
                float fill_h = kMatrixCellSize * scale;
                float fill_y = cy + kMatrixCellSize - fill_h;
                tr.draw_rect(cx, fill_y, kMatrixCellSize, fill_h,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
            }

            // Store cell rect for hit testing (with both from_node and to_node)
            matrix_cell_rects_.push_back({
                cx, cy, kMatrixCellSize, kMatrixCellSize,
                src_node.node_id, row.port_name,
                dst_node.node_id, columns[ci].name,
                is_connected, scale
            });
        }
    }

    // Draw hover tooltip
    if (!matrix_scale_dragging_) {
        int hi = hit_test_rect(matrix_cell_rects_, mouse_.x, mouse_.y);
        if (hi >= 0) {
            const auto& cell = matrix_cell_rects_[hi];
            std::string tip = cell.from_node + "/" + cell.from_port + " -> " + cell.to_node + "/" + cell.to_port;
            if (cell.connected) {
                tip += " (scale: " + format_float(cell.scale, 2) + ")";
            }
            float tw = tr.text_width(tip.c_str(), 0.8f);
            float tx = mouse_.x + 12;
            float ty = mouse_.y - 6;
            tr.draw_rect(tx - 3, ty - 2, tw + 6, kLineH, 0.0f, 0.0f, 0.0f, 0.85f);
            tr.draw_text(tx, ty, tip.c_str(), 1.0f, 1.0f, 1.0f, 0.9f, 0.8f);
        }
    }

    py = matrix_y + vis_rows * cell_step + 8;
}

void NodeGraphUI::draw_preview_wire(Renderer2D& tr) {
    if (!dragging_wire_) return;
    float ssx = gx_to_sx(wire_from_gx_), ssy = gy_to_sy(wire_from_gy_);
    float sex = mouse_.x, sey = mouse_.y;

    if (!wire_from_is_output_) {
        // Param source: thin dashed preview
        float wire_th = std::max(1.0f, 1.5f * zoom_);
        float cumulative = 0.0f;
        float dash_cycle = kDashOn + kDashOff;
        traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
            [&](float x0, float y0, float x1, float y1) {
                float dx = x1 - x0, dy = y1 - y0;
                float seg_len = std::sqrt(dx * dx + dy * dy);
                if (seg_len < 0.001f) { cumulative += seg_len; return; }
                float nx = dx / seg_len, ny = dy / seg_len;
                float consumed = 0.0f;
                while (consumed < seg_len) {
                    float phase = std::fmod(cumulative + consumed, dash_cycle);
                    bool on = (phase < kDashOn);
                    float remain_in_state = on ? (kDashOn - phase) : (dash_cycle - phase);
                    float chunk = std::min(remain_in_state, seg_len - consumed);
                    if (on) {
                        float cx0 = x0 + nx * consumed;
                        float cy0 = y0 + ny * consumed;
                        float cx1 = x0 + nx * (consumed + chunk);
                        float cy1 = y0 + ny * (consumed + chunk);
                        tr.draw_line(cx0, cy0, cx1, cy1, wire_th, 1.0f, 1.0f, 1.0f, 0.3f);
                    }
                    consumed += chunk;
                }
                cumulative += seg_len;
            });
    } else {
        float wire_th = std::max(1.0f, kWireThickness * zoom_);
        traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
            [&](float x0, float y0, float x1, float y1) {
                tr.draw_line(x0, y0, x1, y1, wire_th, 1.0f, 1.0f, 1.0f, 0.5f);
            });
    }

    // Highlight target node body when cursor is over a different node (drop target)
    int ni = hit_test_node(mouse_.x, mouse_.y);
    if (ni >= 0 && node_rects_[ni].node_id != wire_from_node_id_) {
        const auto& r = node_rects_[ni];
        float sx = gx_to_sx(r.x), sy = gy_to_sy(r.y);
        float sw = g_to_s(r.w), sh = g_to_s(r.h);
        float bw = 2.0f;
        tr.draw_rect(sx, sy, sw, bw, style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
        tr.draw_rect(sx, sy + sh - bw, sw, bw, style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
        tr.draw_rect(sx, sy, bw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
        tr.draw_rect(sx + sw - bw, sy, bw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
    }
}

void NodeGraphUI::draw_box_select(Renderer2D& tr) {
    if (!box_selecting_) return;
    // Convert graph-space rect to screen-space
    float cur_gx = sx_to_gx(mouse_.x);
    float cur_gy = sy_to_gy(mouse_.y);
    float sx0 = gx_to_sx(std::min(box_start_gx_, cur_gx));
    float sy0 = gy_to_sy(std::min(box_start_gy_, cur_gy));
    float sx1 = gx_to_sx(std::max(box_start_gx_, cur_gx));
    float sy1 = gy_to_sy(std::max(box_start_gy_, cur_gy));
    float sw = sx1 - sx0;
    float sh = sy1 - sy0;
    // Semi-transparent fill
    tr.draw_rect(sx0, sy0, sw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.12f);
    // Border
    float bw = 1.0f;
    tr.draw_rect(sx0, sy0, sw, bw, style_.accent[0], style_.accent[1], style_.accent[2], 0.6f); // top
    tr.draw_rect(sx0, sy1 - bw, sw, bw, style_.accent[0], style_.accent[1], style_.accent[2], 0.6f); // bottom
    tr.draw_rect(sx0, sy0, bw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.6f); // left
    tr.draw_rect(sx1 - bw, sy0, bw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.6f); // right
}

void NodeGraphUI::draw_chooser(Renderer2D& tr) {
    if (!chooser_open_) return;

    int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
    if (visible == 0) visible = 1; // show at least the header area
    float panel_h = kChooserHeaderH + visible * kChooserItemH + 4;

    float px = chooser_x();
    float py = kChooserY;

    // Background
    tr.draw_rect(px, py, kChooserW, panel_h, style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.97f);
    // Top accent bar
    tr.draw_rect(px, py, kChooserW, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

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
                         style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
        }

        const std::string& name = chooser_items_[idx];

        if (name == "+ New Operator...") {
            // Sentinel: accent-colored text, no domain dot
            tr.draw_text(px + 10, item_y + 3, name.c_str(),
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        } else {
            // Domain color dot
            const float* dcol = kControlAccent.data(); // default
            auto cat_it = snap_.operator_catalog.find(name);
            if (cat_it != snap_.operator_catalog.end()) {
                dcol = domain_color(cat_it->second->domain);
            }
            float dot_x = px + 10;
            float dot_y = item_y + (kChooserItemH - 6) * 0.5f;
            tr.draw_rect(dot_x, dot_y, 6, 6, dcol[0], dcol[1], dcol[2]);

            // Type name
            tr.draw_text(px + 22, item_y + 3, name.c_str(), 0.85f, 0.87f, 0.90f);
        }
    }

    // Show "no matches" if empty
    if (chooser_items_.empty()) {
        tr.draw_text(px + 8, iy + 3, "no matches", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    }

    // Scrollbar when items overflow
    int total_items = static_cast<int>(chooser_items_.size());
    if (total_items > kChooserMaxVisible) {
        float track_x = px + kChooserW - kInspScrollbarW - 2.0f;
        float track_y = iy;
        float track_h = visible * kChooserItemH;

        // Track background
        tr.draw_rect(track_x, track_y, kInspScrollbarW, track_h,
                     style_.scrollbar_track[0], style_.scrollbar_track[1], style_.scrollbar_track[2], 0.5f);

        // Thumb
        float ratio = static_cast<float>(kChooserMaxVisible) / static_cast<float>(total_items);
        float thumb_h = std::max(kInspScrollbarMinThumb, track_h * ratio);
        int max_scroll = total_items - kChooserMaxVisible;
        float scroll_ratio = (max_scroll > 0) ? static_cast<float>(chooser_scroll_) / static_cast<float>(max_scroll) : 0.0f;
        float thumb_y = track_y + scroll_ratio * (track_h - thumb_h);
        tr.draw_rect(track_x, thumb_y, kInspScrollbarW, thumb_h,
                     style_.scrollbar_thumb[0], style_.scrollbar_thumb[1], style_.scrollbar_thumb[2], 0.6f);
    }
}

// -----------------------------------------------------------------------
// Workspace grid
// -----------------------------------------------------------------------
void NodeGraphUI::draw_grid(Renderer2D& tr) {
    // Skip when zoomed out so far that grid lines would be < 8px apart
    float screen_spacing = kGridSpacing * zoom_;
    if (screen_spacing < 8.0f) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    // Find the range of graph-space coordinates visible on screen
    float g_left   = sx_to_gx(0.0f);
    float g_right  = sx_to_gx(wf);
    float g_top    = sy_to_gy(0.0f);
    float g_bottom = sy_to_gy(hf);

    // Snap to grid boundaries
    float gx_start = std::floor(g_left / kGridSpacing) * kGridSpacing;
    float gy_start = std::floor(g_top / kGridSpacing) * kGridSpacing;

    // Vertical lines
    for (float gx = gx_start; gx <= g_right; gx += kGridSpacing) {
        float sx = gx_to_sx(gx);
        tr.draw_rect(sx, 0.0f, 1.0f, hf,
                     kGpuAccent[0], kGpuAccent[1], kGpuAccent[2], kGridLineAlpha);
    }

    // Horizontal lines
    for (float gy = gy_start; gy <= g_bottom; gy += kGridSpacing) {
        float sy = gy_to_sy(gy);
        tr.draw_rect(0.0f, sy, wf, 1.0f,
                     kGpuAccent[0], kGpuAccent[1], kGpuAccent[2], kGridLineAlpha);
    }
}

// -----------------------------------------------------------------------
// Draw (top-level)
// -----------------------------------------------------------------------
void NodeGraphUI::draw(Renderer2D& tr, uint32_t w, uint32_t h) {
    if (!visible_) return;
    win_w_ = w;
    win_h_ = h;

    if (node_rects_.empty() && !snap_.nodes.empty()) {
        layout_nodes();
    }

    // Semi-transparent scrim so wires are visible over the visualization
    tr.draw_rect(0, 0, static_cast<float>(w), static_cast<float>(h), 0.05f, 0.06f, 0.07f, 0.55f);

    draw_grid(tr);
    draw_perf_bar(tr);
    draw_midi_map_banner(tr);

    draw_graph(tr);
    draw_connections(tr);
    draw_preview_wire(tr);
    draw_box_select(tr);
    draw_wire_tooltip(tr);
}

// -----------------------------------------------------------------------
// Overlays — rendered in a separate pass after GPU thumbnails so that
// popups (context menu, dropdown) appear on top of everything.
// -----------------------------------------------------------------------
void NodeGraphUI::draw_overlays(Renderer2D& tr) {
    // Inspector — drawn in overlay pass so it paints over GPU thumbnails
    draw_inspector(tr, win_w_, win_h_);

    // Operator chooser — drawn here (overlay pass) so it appears above GPU thumbnails
    draw_chooser(tr);

    // Parameter picker popup
    draw_param_picker(tr);

    // Dropdown popup
    if (dropdown_open_ && !dropdown_labels_.empty()) {
        float item_h = kDropdownItemH;
        float popup_h = dropdown_labels_.size() * item_h + 4;
        // Background
        draw_popup_bg(tr, style_, dropdown_x_, dropdown_y_, dropdown_w_, popup_h);
        for (int i = 0; i < static_cast<int>(dropdown_labels_.size()); ++i) {
            float iy = dropdown_y_ + 2 + i * item_h;
            if (i == dropdown_sel_) {
                tr.draw_rect(dropdown_x_ + 2, iy, dropdown_w_ - 4, item_h,
                             style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
            }
            tr.draw_text(dropdown_x_ + 8, iy + 2, dropdown_labels_[i].c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }

    // Right-click context menu
    if (context_menu_open_) {
        int item_count = 1;
        if (!context_node_id_.empty() && context_node_has_shader_)
            item_count = 2;
        if (context_wire_idx_ >= 0)
            item_count = 2;

        float menu_h = kCtxMenuPadTop + item_count * kCtxMenuItemH + 2.0f;
        float mx = context_menu_x_, my = context_menu_y_;

        // Background
        draw_popup_bg(tr, style_, mx, my, kCtxMenuW, menu_h);

        // Item labels
        std::string delete_label;
        const char* labels[3];
        if (context_bg_menu_) {
            labels[0] = "Re-layout All";
        } else if (!context_node_id_.empty()) {
            if (selected_node_ids_.count(context_node_id_) && selected_node_ids_.size() > 1) {
                delete_label = "Delete " + std::to_string(selected_node_ids_.size()) + " Nodes";
                labels[0] = delete_label.c_str();
            } else {
                labels[0] = "Delete Node";
            }
            labels[1] = "Clone & Edit";
        } else {
            labels[0] = "Delete Wire";
            labels[1] = "Insert Node";
        }

        for (int i = 0; i < item_count; ++i) {
            float item_y = my + kCtxMenuPadTop + i * kCtxMenuItemH;
            // Per-item hover highlight
            if (mouse_.x >= mx && mouse_.x <= mx + kCtxMenuW &&
                mouse_.y >= item_y && mouse_.y <= item_y + kCtxMenuItemH) {
                tr.draw_rect(mx + 2, item_y, kCtxMenuW - 4, kCtxMenuItemH,
                             style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
            }
            tr.draw_text(mx + 8, item_y + 3, labels[i], style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }

    draw_color_popup(tr);
    draw_clone_confirm(tr);
    draw_create_popup(tr);
    draw_preferences(tr);
    draw_patchbay(tr);
}

// -----------------------------------------------------------------------
// Clone confirmation dialog
// -----------------------------------------------------------------------
void NodeGraphUI::draw_clone_confirm(Renderer2D& tr) {
    if (!clone_confirm_open_) return;

    // Scrim over entire window
    tr.draw_rect(0, 0, static_cast<float>(win_w_), static_cast<float>(win_h_),
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3]);

    // Dialog panel (centered)
    float dw = 280.0f, dh = 70.0f;
    float dx = (static_cast<float>(win_w_) - dw) * 0.5f;
    float dy = (static_cast<float>(win_h_) - dh) * 0.5f;

    // Background
    tr.draw_rounded_rect(dx, dy, dw, dh, style_.corner_radius, style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    // Accent bar at top
    tr.draw_rect(dx, dy, dw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    // Label text
    std::string label = "Clone " + clone_confirm_type_ + " for editing?";
    tr.draw_text(dx + 12, dy + 10, label.c_str(), style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    // Buttons
    float btn_w = 70.0f, btn_h = 22.0f;
    float btn_y = dy + dh - btn_h - 8.0f;
    float clone_x = dx + dw * 0.5f - btn_w - 6.0f;
    float cancel_x = dx + dw * 0.5f + 6.0f;

    // Clone button
    bool clone_hover = mouse_.x >= clone_x && mouse_.x <= clone_x + btn_w &&
                       mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h;
    if (clone_hover)
        tr.draw_rect(clone_x, btn_y, btn_w, btn_h, style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    else
        tr.draw_rect(clone_x, btn_y, btn_w, btn_h, style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    tr.draw_text(clone_x + 16, btn_y + 3, "Clone", style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    // Cancel button
    bool cancel_hover = mouse_.x >= cancel_x && mouse_.x <= cancel_x + btn_w &&
                        mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h;
    if (cancel_hover)
        tr.draw_rect(cancel_x, btn_y, btn_w, btn_h, style_.button_hover[0], style_.button_hover[1], style_.button_hover[2], 0.9f);
    else
        tr.draw_rect(cancel_x, btn_y, btn_w, btn_h, style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    tr.draw_text(cancel_x + 13, btn_y + 3, "Cancel", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
}

// -----------------------------------------------------------------------
// Create operator popup
// -----------------------------------------------------------------------
void NodeGraphUI::draw_create_popup(Renderer2D& tr) {
    if (!create_popup_open_) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    // Scrim overlay
    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3]);

    // Centered panel
    float pw = kCreatePopupW, ph = kCreatePopupH;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    // Accent bar at top
    tr.draw_rect(px, py, pw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = px + 16.0f;
    float cy = py + 12.0f;

    // Title
    tr.draw_text(cx, cy, "New Operator",
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    cy += 24.0f;

    // Domain selector buttons
    const char* domain_labels[] = { "control", "audio", "gpu" };
    const std::array<float, 3>* domain_colors[] = { &kControlAccent, &kAudioAccent, &kGpuAccent };
    float btn_gap = 8.0f;
    float total_btn_w = 3 * kCreateDomainBtnW + 2 * btn_gap;
    float bx = px + (pw - total_btn_w) * 0.5f;

    for (int i = 0; i < 3; ++i) {
        float btn_x = bx + i * (kCreateDomainBtnW + btn_gap);
        const auto& dc = *domain_colors[i];
        if (i == create_domain_sel_) {
            // Selected: filled with domain color
            tr.draw_rect(btn_x, cy, kCreateDomainBtnW, kCreateDomainBtnH,
                         dc[0], dc[1], dc[2], 0.9f);
            tr.draw_text(btn_x + 8, cy + 3, domain_labels[i], 0.0f, 0.0f, 0.0f);
        } else {
            // Unselected: outline style
            tr.draw_rect(btn_x, cy, kCreateDomainBtnW, kCreateDomainBtnH,
                         style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
            tr.draw_text(btn_x + 8, cy + 3, domain_labels[i],
                         dc[0], dc[1], dc[2]);
        }
    }
    cy += kCreateDomainBtnH + 10.0f;

    // Name text field
    float field_w = pw - 32.0f;
    tr.draw_rect(cx, cy, field_w, 22.0f,
                 style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
    // Active indicator
    tr.draw_rect(cx, cy, field_w, 1,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    // Blinking cursor
    std::string display = create_name_buf_;
    if (static_cast<int>(perf_frame_counter_ / 30) % 2 == 0)
        display += "_";
    else
        display += " ";

    if (display.size() <= 1 && create_name_buf_.empty()) {
        // Placeholder
        tr.draw_text(cx + 4, cy + 3, "operator_name",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
    } else {
        tr.draw_text(cx + 4, cy + 3, display.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
    cy += 24.0f;

    // Error text
    if (!create_error_.empty()) {
        tr.draw_text(cx, cy, create_error_.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.9f);
    }
}

// -----------------------------------------------------------------------
// Preferences panel
// -----------------------------------------------------------------------
void NodeGraphUI::draw_preferences(Renderer2D& tr) {
    if (!prefs_open_) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3]);

    // Compute panel height dynamically
    int editor_count = static_cast<int>(prefs_editor_names_.size());
    int style_count = static_cast<int>(prefs_styles_.size());
    bool show_custom = (prefs_editor_sel_ >= 0 &&
                        prefs_editor_sel_ < static_cast<int>(prefs_editor_ids_.size()) &&
                        prefs_editor_ids_[prefs_editor_sel_] == "custom");

    float content_h = kPrefsPadY
        + kPrefsRowH                              // "Preferences" title
        + kPrefsSectionGap
        + kPrefsRowH                              // "TEXT EDITOR" section header
        + editor_count * kPrefsRowH               // radio items
        + (show_custom ? kPrefsRowH + 4 : 0)      // custom command field
        + kPrefsSectionGap
        + kPrefsRowH                              // "STYLE" section header
        + style_count * kPrefsRowH                // radio items
        + kPrefsRowH + 4                          // "Open Themes Folder" button
        + kPrefsSectionGap
        + kPrefsBtnH                              // buttons
        + kPrefsPadY;

    float pw = kPrefsW;
    float ph = content_h;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    // Panel background
    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    // Accent bar
    tr.draw_rect(px, py, pw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = px + kPrefsPadX;
    float cy = py + kPrefsPadY;
    float inner_w = pw - 2 * kPrefsPadX;

    // Title
    tr.draw_text(cx, cy, "Preferences",
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    cy += kPrefsRowH + kPrefsSectionGap;

    // --- TEXT EDITOR section ---
    tr.draw_text(cx, cy, "TEXT EDITOR",
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    cy += kPrefsRowH;

    for (int i = 0; i < editor_count; ++i) {
        bool sel = (i == prefs_editor_sel_);
        // Radio button circle
        float radio_x = cx + 2;
        float radio_y = cy + kPrefsRowH * 0.5f - 5;
        tr.draw_rect(radio_x, radio_y, 10, 10,
                     style_.separator[0], style_.separator[1], style_.separator[2]);
        if (sel) {
            tr.draw_rect(radio_x + 2, radio_y + 2, 6, 6,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        }
        // Label
        tr.draw_text(cx + 18, cy + 1, prefs_editor_names_[i].c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        cy += kPrefsRowH;
    }

    // Custom command text field
    if (show_custom) {
        cy += 2;
        tr.draw_rect(cx + 18, cy, inner_w - 18, kPrefsRowH - 2,
                     style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
        if (prefs_editing_custom_) {
            tr.draw_rect(cx + 18, cy, inner_w - 18, 1,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        }
        std::string display = prefs_custom_command_;
        if (prefs_editing_custom_) display += "_";
        if (display.empty()) display = "/usr/local/bin/code {file}";
        float text_alpha = prefs_custom_command_.empty() && !prefs_editing_custom_ ? 0.4f : 1.0f;
        tr.draw_text(cx + 22, cy + 2, display.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], text_alpha);
        cy += kPrefsRowH + 2;
    }

    cy += kPrefsSectionGap;

    // --- STYLE section ---
    tr.draw_text(cx, cy, "STYLE",
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    cy += kPrefsRowH;

    for (int i = 0; i < style_count; ++i) {
        bool sel = (i == prefs_style_sel_);
        float radio_x = cx + 2;
        float radio_y = cy + kPrefsRowH * 0.5f - 5;
        tr.draw_rect(radio_x, radio_y, 10, 10,
                     style_.separator[0], style_.separator[1], style_.separator[2]);
        if (sel) {
            tr.draw_rect(radio_x + 2, radio_y + 2, 6, 6,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        }
        tr.draw_text(cx + 18, cy + 1, prefs_styles_[i].name.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        cy += kPrefsRowH;
    }

    // "Open Themes Folder" link
    cy += 4;
    {
        const char* label = "Open Themes Folder...";
        bool link_hover = mouse_.x >= cx + 18 && mouse_.x <= cx + 18 + tr.text_width(label) &&
                          mouse_.y >= cy && mouse_.y <= cy + kPrefsRowH;
        float alpha = link_hover ? 1.0f : 0.7f;
        tr.draw_text(cx + 18, cy + 1, label,
                     style_.accent[0], style_.accent[1], style_.accent[2], alpha);
    }
    cy += kPrefsRowH;

    cy += kPrefsSectionGap;

    // --- Buttons ---
    float btn_total = 2 * kPrefsBtnW + 12;
    float save_x = px + (pw - btn_total) * 0.5f;
    float cancel_x = save_x + kPrefsBtnW + 12;

    bool save_hover = mouse_.x >= save_x && mouse_.x <= save_x + kPrefsBtnW &&
                      mouse_.y >= cy && mouse_.y <= cy + kPrefsBtnH;
    bool cancel_hover = mouse_.x >= cancel_x && mouse_.x <= cancel_x + kPrefsBtnW &&
                        mouse_.y >= cy && mouse_.y <= cy + kPrefsBtnH;

    if (save_hover)
        tr.draw_rounded_rect(save_x, cy, kPrefsBtnW, kPrefsBtnH, style_.corner_radius,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    else
        tr.draw_rounded_rect(save_x, cy, kPrefsBtnW, kPrefsBtnH, style_.corner_radius,
                             style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    float save_tw = tr.text_width("Save");
    tr.draw_text(save_x + (kPrefsBtnW - save_tw) * 0.5f, cy + 4, "Save",
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    if (cancel_hover)
        tr.draw_rounded_rect(cancel_x, cy, kPrefsBtnW, kPrefsBtnH, style_.corner_radius,
                             style_.button_hover[0], style_.button_hover[1], style_.button_hover[2], 0.9f);
    else
        tr.draw_rounded_rect(cancel_x, cy, kPrefsBtnW, kPrefsBtnH, style_.corner_radius,
                             style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    float cancel_tw = tr.text_width("Cancel");
    tr.draw_text(cancel_x + (kPrefsBtnW - cancel_tw) * 0.5f, cy + 4, "Cancel",
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
}

// -----------------------------------------------------------------------
// Performance bar
// -----------------------------------------------------------------------
void NodeGraphUI::draw_perf_bar(Renderer2D& tr) {
    // Update smoothed values (EMA)
    constexpr float kSmooth = 0.05f;
    float raw_fps = (dt_ > 0.0f) ? 1.0f / dt_ : 0.0f;
    float raw_ms = dt_ * 1000.0f;

    if (perf_frame_counter_ == 0) {
        smoothed_fps_ = raw_fps;
        smoothed_ms_ = raw_ms;
    } else {
        smoothed_fps_ += kSmooth * (raw_fps - smoothed_fps_);
        smoothed_ms_ += kSmooth * (raw_ms - smoothed_ms_);
    }

    fps_history_.push(smoothed_fps_);
    frame_time_history_.push(smoothed_ms_);

    // Sample memory at lower cadence
    if (perf_frame_counter_ % kPerfMemSampleInterval == 0) {
        uint64_t mem_bytes = vivid::get_process_memory_bytes();
        float mem_mb = static_cast<float>(mem_bytes) / (1024.0f * 1024.0f);
        smoothed_mem_mb_ = mem_mb;
        memory_history_.push(mem_mb);
    }
    perf_frame_counter_++;

    float fw = static_cast<float>(win_w_);

    // Bar background
    tr.draw_rect(0, 0, fw, kPerfBarH,
                 kPerfBarBg[0], kPerfBarBg[1], kPerfBarBg[2], kPerfBarBg[3]);

    // Bottom separator line
    tr.draw_rect(0, kPerfBarH - 1, fw, 1, 0.20f, 0.22f, 0.25f, 0.6f);

    float x = kPerfBarPadX;
    float text_y = (kPerfBarH - tr.line_height()) * 0.5f;

    // --- FPS ---
    // Color-code: green >= 55, yellow >= 30, red < 30
    float fr, fg, fb;
    if (smoothed_fps_ >= 55.0f) {
        fr = kPerfFpsColor[0]; fg = kPerfFpsColor[1]; fb = kPerfFpsColor[2];
    } else if (smoothed_fps_ >= 30.0f) {
        fr = 0.95f; fg = 0.85f; fb = 0.30f; // yellow
    } else {
        fr = 0.95f; fg = 0.35f; fb = 0.30f; // red
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f FPS", smoothed_fps_);
    tr.draw_text(x, text_y, buf, fr, fg, fb);
    x += tr.text_width(buf) + kPerfSepMargin;

    // Separator
    tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
    x += kPerfSepW + kPerfSepMargin;

    // --- Frame time ---
    std::snprintf(buf, sizeof(buf), "%.1f ms", smoothed_ms_);
    tr.draw_text(x, text_y, buf, kPerfMsColor[0], kPerfMsColor[1], kPerfMsColor[2]);
    x += tr.text_width(buf) + kPerfSepMargin;

    // Separator
    tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
    x += kPerfSepW + kPerfSepMargin;

    // --- Memory ---
    char mem_buf[64];
    vivid::format_memory(mem_buf, sizeof(mem_buf),
                         static_cast<uint64_t>(smoothed_mem_mb_ * 1024.0f * 1024.0f));
    std::snprintf(buf, sizeof(buf), "MEM %s", mem_buf);
    tr.draw_text(x, text_y, buf, kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2]);
    x += tr.text_width(buf) + kPerfSepMargin;

    // --- Mini memory sparkline ---
    float graph_x = x;
    float graph_y = (kPerfBarH - kPerfMiniGraphH) * 0.5f;
    perf_mem_graph_x_ = graph_x;
    perf_mem_graph_y_ = graph_y;

    // Dark background for sparkline
    tr.draw_rect(graph_x, graph_y, kPerfMiniGraphW, kPerfMiniGraphH,
                 0.04f, 0.05f, 0.06f, 0.8f);

    draw_perf_sparkline(tr, memory_history_.values, kPerfHistoryLen,
                        memory_history_.write_idx, memory_history_.filled,
                        graph_x, graph_y, kPerfMiniGraphW, kPerfMiniGraphH,
                        kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2], 0.7f);

    x = graph_x + kPerfMiniGraphW + kPerfSepMargin;

    // --- XRUN counter ---
    if (snap_.audio_underrun_count > 0) {
        tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
        x += kPerfSepW + kPerfSepMargin;
        std::snprintf(buf, sizeof(buf), "XRUN %u", snap_.audio_underrun_count);
        float xr = kErrorAccent[0], xg = kErrorAccent[1], xb = kErrorAccent[2];
        if (snap_.audio_underrun_active) { xr = 1.0f; xg = 0.4f; xb = 0.4f; }
        tr.draw_text(x, text_y, buf, xr, xg, xb);
        x += tr.text_width(buf) + kPerfSepMargin;
    }

    // Check hover over mini graph
    bool in_mini = mouse_.x >= graph_x && mouse_.x <= graph_x + kPerfMiniGraphW &&
                   mouse_.y >= graph_y && mouse_.y <= graph_y + kPerfMiniGraphH;

    // Also check if mouse is in the expanded popup region (prevents flicker)
    float exp_x = graph_x;
    float exp_right = exp_x + kPerfExpandedW;
    if (exp_right > fw - 10.0f) exp_x = fw - 10.0f - kPerfExpandedW;
    bool in_expanded = perf_mem_hovered_ &&
                       mouse_.x >= exp_x && mouse_.x <= exp_x + kPerfExpandedW &&
                       mouse_.y >= kPerfBarH && mouse_.y <= kPerfBarH + kPerfExpandedH + 30.0f;

    perf_mem_hovered_ = in_mini || in_expanded;

    if (perf_mem_hovered_) {
        draw_perf_expanded(tr);
    }
}

void NodeGraphUI::draw_perf_sparkline(Renderer2D& tr, const float* buf, uint32_t buf_len,
                                      uint32_t write_idx, bool filled,
                                      float x, float y, float w, float h,
                                      float r, float g, float b, float a) {
    uint32_t count = filled ? buf_len : write_idx;
    if (count == 0) return;

    // Find min/max for auto-scaling
    float vmin = buf[0], vmax = buf[0];
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = filled ? (write_idx + i) % buf_len : i;
        float v = buf[idx];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    float range = vmax - vmin;
    if (range < 0.001f) range = 1.0f;

    float bar_w = w / static_cast<float>(buf_len);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = filled ? (write_idx + i) % buf_len : i;
        float v = buf[idx];
        float t = (v - vmin) / range;
        float bh = std::max(1.0f, t * h);
        float bx = x + static_cast<float>(i) * bar_w;
        float by = y + h - bh;
        tr.draw_rect(bx, by, std::max(1.0f, bar_w - 0.3f), bh, r, g, b, a);
    }
}

void NodeGraphUI::draw_perf_expanded(Renderer2D& tr) {
    float fw = static_cast<float>(win_w_);

    // Position below the perf bar, aligned to the mini graph X
    float ex = perf_mem_graph_x_;
    if (ex + kPerfExpandedW > fw - 10.0f) {
        ex = fw - 10.0f - kPerfExpandedW;
    }
    float ey = kPerfBarH;

    float pad = 8.0f;
    float total_h = kPerfExpandedH + 30.0f; // extra for title/labels

    // Panel background
    tr.draw_rect(ex, ey, kPerfExpandedW, total_h, 0.08f, 0.09f, 0.10f, 0.95f);
    // Top accent
    tr.draw_rect(ex, ey, kPerfExpandedW, 2, kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2], 0.8f);

    // Title
    float tx = ex + pad;
    float ty = ey + pad;
    tr.draw_text(tx, ty, "Memory", 0.85f, 0.87f, 0.90f);

    // Current value
    char mem_buf[64];
    vivid::format_memory(mem_buf, sizeof(mem_buf),
                         static_cast<uint64_t>(smoothed_mem_mb_ * 1024.0f * 1024.0f));
    float val_w = tr.text_width(mem_buf);
    tr.draw_text(ex + kPerfExpandedW - pad - val_w, ty, mem_buf,
                 kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2]);

    // Sparkline area
    float graph_y = ty + tr.line_height() + 4.0f;
    float graph_h = total_h - (graph_y - ey) - pad;
    float graph_x = tx;
    float graph_w = kPerfExpandedW - pad * 2;

    // Dark background
    tr.draw_rect(graph_x, graph_y, graph_w, graph_h, 0.04f, 0.05f, 0.06f, 0.8f);

    draw_perf_sparkline(tr, memory_history_.values, kPerfHistoryLen,
                        memory_history_.write_idx, memory_history_.filled,
                        graph_x, graph_y, graph_w, graph_h,
                        kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2], 0.7f);

    // Min/max labels
    uint32_t count = memory_history_.filled ? kPerfHistoryLen : memory_history_.write_idx;
    if (count > 0) {
        float vmin = memory_history_.values[0], vmax = memory_history_.values[0];
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t idx = memory_history_.filled
                ? (memory_history_.write_idx + i) % kPerfHistoryLen : i;
            float v = memory_history_.values[idx];
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        char label[32];
        std::snprintf(label, sizeof(label), "%.0f", vmax);
        tr.draw_text(graph_x + 2, graph_y, label, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
        std::snprintf(label, sizeof(label), "%.0f", vmin);
        tr.draw_text(graph_x + 2, graph_y + graph_h - tr.line_height(), label,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
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

// -----------------------------------------------------------------------
// Parameter picker popup
// -----------------------------------------------------------------------
void NodeGraphUI::draw_param_picker(Renderer2D& tr) {
    if (!param_picker_open_ || param_picker_items_.empty()) return;

    static constexpr float kPickerItemH = 22.0f;
    static constexpr float kPickerW = 220.0f;
    static constexpr int kPickerMaxVisible = 12;

    int visible = std::min(static_cast<int>(param_picker_items_.size()), kPickerMaxVisible);
    float popup_h = visible * kPickerItemH + 4;

    float px = param_picker_x_;
    float py = param_picker_y_;

    // Clamp to window bounds
    if (px + kPickerW > static_cast<float>(win_w_)) px = static_cast<float>(win_w_) - kPickerW;
    if (py + popup_h > static_cast<float>(win_h_)) py = static_cast<float>(win_h_) - popup_h;

    // Background
    draw_popup_bg(tr, style_, px, py, kPickerW, popup_h);

    // Items
    for (int i = 0; i < visible; ++i) {
        int idx = param_picker_scroll_ + i;
        if (idx < 0 || idx >= static_cast<int>(param_picker_items_.size())) break;

        float iy = py + 2 + i * kPickerItemH;
        if (idx == param_picker_sel_) {
            tr.draw_rect(px + 2, iy, kPickerW - 4, kPickerItemH,
                         style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
        }

        bool is_param = (!param_picker_item_is_param_.empty() &&
                         idx < static_cast<int>(param_picker_item_is_param_.size()) &&
                         param_picker_item_is_param_[idx]);
        if (is_param) {
            std::string display = "\xC2\xB7 " + param_picker_items_[idx];  // "· name"
            tr.draw_text(px + 8, iy + 3, display.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.6f);
        } else {
            tr.draw_text(px + 8, iy + 3, param_picker_items_[idx].c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }
}

// -----------------------------------------------------------------------
// Patchbay (multi-node connection matrix overlay)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_patchbay(Renderer2D& tr) {
    if (!patchbay_open_) return;
    if (patchbay_rows_.empty() || patchbay_cols_.empty()) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3]);

    // Compute content size
    float cell_step = kPatchbayCellSize + kPatchbayCellPad;
    int nr = static_cast<int>(patchbay_rows_.size());
    int nc = static_cast<int>(patchbay_cols_.size());

    // Group headers add height/width between groups
    int n_row_groups = 0, n_col_groups = 0;
    for (const auto& g : patchbay_groups_) {
        if (g.row_count > 0) n_row_groups++;
        if (g.col_count > 0) n_col_groups++;
    }

    float content_w = kPatchbayRowLabelW + nc * cell_step + std::max(0, n_col_groups - 1) * kPatchbayGroupHeaderH;
    float content_h = kPatchbayColHeaderH + nr * cell_step + std::max(0, n_row_groups - 1) * kPatchbayGroupHeaderH;

    // Panel size (capped)
    float panel_w = std::min(kPatchbayMaxW, content_w + 2 * kPatchbayPad);
    float panel_h = std::min(kPatchbayMaxH, content_h + 2 * kPatchbayPad);

    // Center panel
    float px = (wf - panel_w) * 0.5f;
    float py = (hf - panel_h) * 0.5f;
    patchbay_panel_x_ = px;
    patchbay_panel_y_ = py;
    patchbay_panel_w_ = panel_w;
    patchbay_panel_h_ = panel_h;

    // Panel background
    tr.draw_rounded_rect(px, py, panel_w, panel_h, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(px, py, panel_w, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    // Build connection lookup: "from_node/from_port->to_node/to_port" -> scale
    std::unordered_map<std::string, float> conn_map;
    for (const auto& c : snap_.connections) {
        // Only index connections involving selected nodes
        if (patchbay_node_ids_.count(c.from_node) && patchbay_node_ids_.count(c.to_node)) {
            std::string key = c.from_node + "/" + c.from_port + "->" + c.to_node + "/" + c.to_port;
            conn_map[key] = c.scale;
        }
    }

    // Clear matrix cells (mutually exclusive with 2-node matrix)
    matrix_cell_rects_.clear();

    // Coordinate system within panel
    float inner_x = px + kPatchbayPad;
    float inner_y = py + kPatchbayPad;

    // Clamp scroll
    float viewport_w = panel_w - 2 * kPatchbayPad - kPatchbayRowLabelW;
    float viewport_h = panel_h - 2 * kPatchbayPad - kPatchbayColHeaderH;
    float total_cell_w = content_w - kPatchbayRowLabelW;
    float total_cell_h = content_h - kPatchbayColHeaderH;
    float max_scroll_x = std::max(0.0f, total_cell_w - viewport_w);
    float max_scroll_y = std::max(0.0f, total_cell_h - viewport_h);
    patchbay_scroll_x_ = std::max(0.0f, std::min(patchbay_scroll_x_, max_scroll_x));
    patchbay_scroll_y_ = std::max(0.0f, std::min(patchbay_scroll_y_, max_scroll_y));

    // Pre-compute row Y positions (accounting for group headers)
    std::vector<float> row_y_offsets(nr);  // offset from cells origin
    {
        float y_off = 0;
        for (const auto& g : patchbay_groups_) {
            if (g.row_count <= 0) continue;
            if (g.row_start > 0) y_off += kPatchbayGroupHeaderH;
            for (int r = g.row_start; r < g.row_start + g.row_count; ++r) {
                row_y_offsets[r] = y_off;
                y_off += cell_step;
            }
        }
    }

    // Pre-compute column X positions
    std::vector<float> col_x_offsets(nc);
    {
        float x_off = 0;
        for (const auto& g : patchbay_groups_) {
            if (g.col_count <= 0) continue;
            if (g.col_start > 0) x_off += kPatchbayGroupHeaderH;
            for (int c = g.col_start; c < g.col_start + g.col_count; ++c) {
                col_x_offsets[c] = x_off;
                x_off += cell_step;
            }
        }
    }

    // Region origins
    float labels_x = inner_x;
    float labels_y = inner_y + kPatchbayColHeaderH;
    float headers_x = inner_x + kPatchbayRowLabelW;
    float headers_y = inner_y;
    float cells_x = inner_x + kPatchbayRowLabelW;
    float cells_y = inner_y + kPatchbayColHeaderH;

    // Viewport culling: determine visible row/col range
    int first_vis_row = 0, last_vis_row = nr - 1;
    int first_vis_col = 0, last_vis_col = nc - 1;
    for (int r = 0; r < nr; ++r) {
        float ry = row_y_offsets[r] - patchbay_scroll_y_;
        if (ry + cell_step >= 0) { first_vis_row = r; break; }
    }
    for (int r = nr - 1; r >= 0; --r) {
        float ry = row_y_offsets[r] - patchbay_scroll_y_;
        if (ry <= viewport_h) { last_vis_row = r; break; }
    }
    for (int c = 0; c < nc; ++c) {
        float cx = col_x_offsets[c] - patchbay_scroll_x_;
        if (cx + cell_step >= 0) { first_vis_col = c; break; }
    }
    for (int c = nc - 1; c >= 0; --c) {
        float cx = col_x_offsets[c] - patchbay_scroll_x_;
        if (cx <= viewport_w) { last_vis_col = c; break; }
    }

    // --- Region A: Corner label (static) ---
    tr.push_clip_rect(labels_x, headers_y, kPatchbayRowLabelW, kPatchbayColHeaderH);
    tr.draw_text(labels_x + 4, headers_y + kPatchbayColHeaderH - kLineH,
                 "src \\ dest", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f, 0.8f);
    tr.pop_clip_rect();

    // --- Region B: Column headers (scroll X only) ---
    tr.push_clip_rect(headers_x, headers_y, viewport_w, kPatchbayColHeaderH);
    for (const auto& g : patchbay_groups_) {
        if (g.col_count <= 0) continue;
        // Group header bar
        float gx = headers_x + col_x_offsets[g.col_start] - patchbay_scroll_x_;
        float gw = g.col_count * cell_step;
        const float* dc = domain_color(g.domain);
        tr.draw_rect(gx, headers_y, gw, 3, dc[0], dc[1], dc[2], 0.7f);

        // Node name
        std::string glabel = g.display_name;
        if (glabel.size() > 12) glabel = glabel.substr(0, 11) + "~";
        tr.draw_text(gx + 2, headers_y + 4, glabel.c_str(), dc[0], dc[1], dc[2], 0.8f, 0.8f);

        // Column labels (vertical abbreviated text)
        for (int ci = g.col_start; ci < g.col_start + g.col_count; ++ci) {
            if (ci < first_vis_col || ci > last_vis_col) continue;
            float cx = headers_x + col_x_offsets[ci] - patchbay_scroll_x_ + cell_step * 0.5f;
            const auto& name = patchbay_cols_[ci].port_name;
            std::string abbr = name.length() > 5 ? name.substr(0, 5) : name;
            for (size_t chi = 0; chi < abbr.size(); ++chi) {
                char buf[2] = { abbr[chi], '\0' };
                float cy = headers_y + 18 + chi * 10.0f;
                tr.draw_text(cx - 3, cy, buf,
                             style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f, 0.8f);
            }
        }
    }
    tr.pop_clip_rect();

    // --- Region C: Row labels (scroll Y only) ---
    tr.push_clip_rect(labels_x, labels_y, kPatchbayRowLabelW, viewport_h);
    for (const auto& g : patchbay_groups_) {
        if (g.row_count <= 0) continue;
        // Group header bar
        float gy = labels_y + row_y_offsets[g.row_start] - patchbay_scroll_y_;
        if (g.row_start > 0) gy -= kPatchbayGroupHeaderH;
        if (g.row_start > 0) {
            const float* dc = domain_color(g.domain);
            tr.draw_rect(labels_x, gy, kPatchbayRowLabelW, kPatchbayGroupHeaderH,
                         dc[0], dc[1], dc[2], 0.15f);
            std::string glabel = g.display_name;
            if (glabel.size() > 14) glabel = glabel.substr(0, 13) + "~";
            tr.draw_text(labels_x + 4, gy + 3, glabel.c_str(), dc[0], dc[1], dc[2], 0.8f, 0.8f);
        } else {
            // First group: draw name at top
            const float* dc = domain_color(g.domain);
            float gy0 = labels_y - kPatchbayGroupHeaderH - patchbay_scroll_y_;
            // Only draw if it would be above the first row
            if (gy0 + kPatchbayGroupHeaderH > labels_y - kPatchbayGroupHeaderH) {
                std::string glabel = g.display_name;
                if (glabel.size() > 14) glabel = glabel.substr(0, 13) + "~";
                tr.draw_text(labels_x + 4, labels_y + row_y_offsets[g.row_start] - patchbay_scroll_y_ - kLineH,
                             glabel.c_str(), dc[0], dc[1], dc[2], 0.8f, 0.8f);
            }
        }

        // Row labels
        for (int ri = g.row_start; ri < g.row_start + g.row_count; ++ri) {
            if (ri < first_vis_row || ri > last_vis_row) continue;
            float ry = labels_y + row_y_offsets[ri] - patchbay_scroll_y_;
            const auto& row = patchbay_rows_[ri];
            std::string label = row.is_param ? ("\xC2\xB7 " + row.port_name) : row.port_name;
            if (label.size() > 14) label = label.substr(0, 13) + "~";
            float row_alpha = row.is_param ? 0.5f : 0.7f;
            tr.draw_text(labels_x + 4, ry + 2, label.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], row_alpha, 0.8f);
        }
    }
    tr.pop_clip_rect();

    // --- Region D: Cells (scroll X+Y) ---
    tr.push_clip_rect(cells_x, cells_y, viewport_w, viewport_h);
    for (int ri = first_vis_row; ri <= last_vis_row && ri < nr; ++ri) {
        float ry = cells_y + row_y_offsets[ri] - patchbay_scroll_y_;
        if (ry > cells_y + viewport_h) break;
        if (ry + cell_step < cells_y) continue;

        for (int ci = first_vis_col; ci <= last_vis_col && ci < nc; ++ci) {
            float cx = cells_x + col_x_offsets[ci] - patchbay_scroll_x_;
            if (cx > cells_x + viewport_w) break;
            if (cx + cell_step < cells_x) continue;

            uint8_t compat = patchbay_compat_[ri][ci];
            if (compat == 0) continue;  // same node — blank

            if (compat == 1) {
                // Incompatible: very dim rect
                tr.draw_rect(cx, ry, kPatchbayCellSize, kPatchbayCellSize,
                             style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.15f);
                continue;
            }

            // Compatible cell — check if connected
            std::string key = patchbay_rows_[ri].node_id + "/" + patchbay_rows_[ri].port_name +
                              "->" + patchbay_cols_[ci].node_id + "/" + patchbay_cols_[ci].port_name;
            auto conn_it = conn_map.find(key);
            bool connected = (conn_it != conn_map.end());
            float scale = connected ? conn_it->second : 0.0f;

            // Background
            tr.draw_rect(cx, ry, kPatchbayCellSize, kPatchbayCellSize,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.5f);

            if (connected) {
                float fill_h = kPatchbayCellSize * scale;
                float fill_y = ry + kPatchbayCellSize - fill_h;
                tr.draw_rect(cx, fill_y, kPatchbayCellSize, fill_h,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
            }

            // Store cell rect for hit testing (reuse MatrixCell)
            matrix_cell_rects_.push_back({
                cx, ry, kPatchbayCellSize, kPatchbayCellSize,
                patchbay_rows_[ri].node_id, patchbay_rows_[ri].port_name,
                patchbay_cols_[ci].node_id, patchbay_cols_[ci].port_name,
                connected, scale
            });
        }
    }
    tr.pop_clip_rect();

    // Hover tooltip (drawn outside clip rects)
    if (!matrix_scale_dragging_) {
        int hi = hit_test_rect(matrix_cell_rects_, mouse_.x, mouse_.y);
        if (hi >= 0) {
            const auto& cell = matrix_cell_rects_[hi];
            std::string tip = cell.from_node + "/" + cell.from_port + " -> " + cell.to_node + "/" + cell.to_port;
            if (cell.connected) {
                tip += " (scale: " + format_float(cell.scale, 2) + ")";
            }
            float tw = tr.text_width(tip.c_str(), 0.8f);
            float tx = mouse_.x + 12;
            float ty = mouse_.y - 6;
            // Clamp tooltip to screen
            if (tx + tw + 6 > wf) tx = wf - tw - 6;
            if (ty < 0) ty = mouse_.y + 16;
            tr.draw_rect(tx - 3, ty - 2, tw + 6, kLineH, 0.0f, 0.0f, 0.0f, 0.85f);
            tr.draw_text(tx, ty, tip.c_str(), 1.0f, 1.0f, 1.0f, 0.9f, 0.8f);
        }
    }
}

} // namespace vivid::ui
