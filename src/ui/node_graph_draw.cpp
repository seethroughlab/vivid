#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
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
            tr.draw_rect(spx - s_dot, spy - s_dot * 0.5f,
                         s_dot, s_dot,
                         dcol[0], dcol[1], dcol[2]);
            tr.draw_text(spx + g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, zoom_);
        }
        // Output port dots and labels (use domain color)
        for (const auto& p : r.outputs) {
            float spx = gx_to_sx(p.x), spy = gy_to_sy(p.y);
            tr.draw_rect(spx, spy - s_dot * 0.5f,
                         s_dot, s_dot,
                         dcol[0], dcol[1], dcol[2]);
            float lw = tr.text_width(p.name.c_str(), zoom_);
            tr.draw_text(spx - lw - g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, zoom_);
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

        float wire_th = std::max(1.0f, (hov ? kWireHoverThickness : kWireThickness) * zoom_);

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
    bool_rects_.clear();
    value_text_rects_.clear();
    dropdown_rects_.clear();
    file_button_rects_.clear();
    drum_grid_rects_.clear();
    resolution_rects_.clear();
    midi_remove_rects_.clear();
    midi_range_rects_.clear();

    if (selected_node_ids_.empty()) return;

    // Inspector background + separator (drawn outside clip rect)
    float insp_x = inspector_x();
    tr.draw_rect(insp_x, 0, kInspectorW, static_cast<float>(h), style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.95f);
    tr.draw_rect(insp_x, 0, 2, static_cast<float>(h), style_.separator[0], style_.separator[1], style_.separator[2]);

    // Multi-selection summary panel
    if (selected_node_ids_.size() > 1) {
        float px = insp_x + kInspPadX;
        float py = kPerfBarH + 8;
        std::string label = std::to_string(selected_node_ids_.size()) + " nodes selected";
        tr.draw_text(px, py, label.c_str(), 1.0f, 1.0f, 1.0f);
        py += kLineH + 4;
        tr.draw_text(px, py, "Delete / Backspace to remove", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        insp_content_h_ = 0;
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

void NodeGraphUI::draw_one_inspector_param(Renderer2D& tr, const NodeSnapshot& node,
                                           float px, float& py, uint32_t pi) {
    const auto& op = *node.op_info;
    float panel_w = kInspContentW;
    const auto& pd = op.params[pi];
    float val = node.param_values[pi];

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

    // Label (dimmed if driven by connection)
    if (is_connected)
        tr.draw_text(px, py, pd.name.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    else
        tr.draw_text(px, py, pd.name.c_str(), 0.8f, 0.82f, 0.85f);

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
            draw_one_inspector_param(tr, node, px, py, ds_steps_it->second);
        if (ds_swing_it != node.param_indices.end())
            draw_one_inspector_param(tr, node, px, py, ds_swing_it->second);

        // Build skip set for the 96 grid params
        std::unordered_set<uint32_t> grid_params;
        static const char* kDrumPrefixes[] = {"kick_", "snare_", "hat_", "oh_", "clap_", "tom_"};
        for (const char* prefix : kDrumPrefixes) {
            for (int s = 0; s < 16; ++s) {
                std::string name = std::string(prefix) + std::to_string(s);
                auto it = node.param_indices.find(name);
                if (it != node.param_indices.end())
                    grid_params.insert(it->second);
            }
        }
        if (ds_steps_it != node.param_indices.end())
            grid_params.insert(ds_steps_it->second);
        if (ds_swing_it != node.param_indices.end())
            grid_params.insert(ds_swing_it->second);

        // Draw remaining params, skipping grid params
        for (uint32_t pi = 0; pi < static_cast<uint32_t>(op.params.size()); ++pi) {
            if (grid_params.count(pi)) continue;
            draw_one_inspector_param(tr, node, px, py, pi);
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
        draw_one_inspector_param(tr, node, px, py, steps_it->second);

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
            draw_one_inspector_param(tr, node, px, py, pi);
        }
    } else {
        // Default: sequential rendering
        for (uint32_t pi = 0; pi < static_cast<uint32_t>(op.params.size()); ++pi) {
            draw_one_inspector_param(tr, node, px, py, pi);
        }
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
    float total_h = grid_h + 8.0f;  // padding

    py += 4;

    // Dark background
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

    for (int drum = 0; drum < 6; ++drum) {
        float row_y = grid_y + drum * cell_h;

        // Row label
        tr.draw_text(px + 2, row_y + 1, kDrumLabel[drum],
                     kDrumColors[drum][0], kDrumColors[drum][1], kDrumColors[drum][2], 0.8f);

        for (int s = 0; s < 16; ++s) {
            float cx = grid_x + s * cell_w;
            std::string param_name = std::string(kDrumPrefix[drum]) + std::to_string(s);
            auto pit = node.param_indices.find(param_name);
            bool active = false;
            if (pit != node.param_indices.end())
                active = node.param_values[pit->second] > 0.5f;

            bool beyond_steps = (s >= num_steps);

            if (active) {
                float alpha = beyond_steps ? 0.25f : 0.9f;
                tr.draw_rect(cx + cell_pad, row_y + cell_pad,
                             cell_w - 2 * cell_pad, cell_h - 2 * cell_pad,
                             kDrumColors[drum][0], kDrumColors[drum][1], kDrumColors[drum][2], alpha);
            }

            // Push hit rect for click handling
            drum_grid_rects_.push_back({cx, row_y, cell_w, cell_h,
                                        single_selected_id(), param_name});
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

void NodeGraphUI::draw_preview_wire(Renderer2D& tr) {
    if (!dragging_wire_) return;
    float ssx = gx_to_sx(wire_from_gx_), ssy = gy_to_sy(wire_from_gy_);
    float sex = mouse_.x, sey = mouse_.y;
    float wire_th = std::max(1.0f, kWireThickness * zoom_);

    traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
        [&](float x0, float y0, float x1, float y1) {
            tr.draw_line(x0, y0, x1, y1, wire_th, 1.0f, 1.0f, 1.0f, 0.5f);
        });
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

        // Domain color dot
        const std::string& name = chooser_items_[idx];
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

    // Dropdown popup
    if (dropdown_open_ && !dropdown_labels_.empty()) {
        float item_h = kDropdownItemH;
        float popup_h = dropdown_labels_.size() * item_h + 4;
        // Background
        tr.draw_rect(dropdown_x_, dropdown_y_, dropdown_w_, popup_h,
                     style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
        // Border
        tr.draw_rect(dropdown_x_, dropdown_y_, dropdown_w_, 1,
                     style_.accent[0], style_.accent[1], style_.accent[2]);
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
        tr.draw_rect(mx, my, kCtxMenuW, menu_h, style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
        // Accent bar
        tr.draw_rect(mx, my, kCtxMenuW, 1, style_.accent[0], style_.accent[1], style_.accent[2]);

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

    draw_clone_confirm(tr);
    draw_preferences(tr);
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

} // namespace vivid::ui
