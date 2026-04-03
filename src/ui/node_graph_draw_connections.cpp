#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/renderer_2d.h"
#include "ui/i18n.h"
#include "common/string_util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

using vivid::format_float;

// Shared dashed-wire drawing: traverse wire segments and draw dash-on/off pattern.
// dash_on/dash_off control dash geometry; flow_offset scrolls the pattern (animation).
static void draw_dashed_wire_ex(Renderer2D& tr,
                                float ssx, float ssy, float sex, float sey,
                                bool bezier, float thickness,
                                float dash_on, float dash_off, float flow_offset,
                                float r, float g, float b, float a) {
    float cumulative = 0.0f;
    float dash_cycle = dash_on + dash_off;
    traverse_wire(ssx, ssy, sex, sey, bezier,
        [&](float x0, float y0, float x1, float y1) {
            float dx = x1 - x0, dy = y1 - y0;
            float seg_len = std::sqrt(dx * dx + dy * dy);
            if (seg_len < 0.001f) { cumulative += seg_len; return; }
            float nx = dx / seg_len, ny = dy / seg_len;
            float consumed = 0.0f;
            while (consumed < seg_len) {
                float phase = std::fmod(cumulative + consumed + flow_offset, dash_cycle);
                if (phase < 0.0f) phase += dash_cycle;
                bool on = (phase < dash_on);
                float remain_in_state = on ? (dash_on - phase) : (dash_cycle - phase);
                float chunk = std::min(remain_in_state, seg_len - consumed);
                if (on) {
                    float cx0 = x0 + nx * consumed;
                    float cy0 = y0 + ny * consumed;
                    float cx1 = x0 + nx * (consumed + chunk);
                    float cy1 = y0 + ny * (consumed + chunk);
                    tr.draw_line(cx0, cy0, cx1, cy1, thickness, r, g, b, a);
                }
                consumed += chunk;
            }
            cumulative += seg_len;
        });
}

// Convenience: static dashed wire (no animation) using the original constants
static void draw_dashed_wire(Renderer2D& tr,
                             float ssx, float ssy, float sex, float sey,
                             bool bezier, float thickness,
                             float r, float g, float b, float a) {
    draw_dashed_wire_ex(tr, ssx, ssy, sex, sey, bezier, thickness,
                        kDashOn, kDashOff, 0.0f, r, g, b, a);
}


void NodeGraphUI::draw_connections(Renderer2D& tr) {
    const auto& conns = snap_.connections;

    // Build fast lookup: node_id -> index in node_rects_
    std::unordered_map<std::string, size_t> id_to_rect;
    for (size_t i = 0; i < node_rects_.size(); ++i)
        id_to_rect[node_rects_[i].node_id] = i;

    for (int ci = 0; ci < static_cast<int>(conns.size()); ++ci) {
        const auto& c = conns[ci];
        if ((c.from_is_param || c.to_is_param) && !show_param_wires_) continue;
        auto fi = id_to_rect.find(c.from_node);
        auto ti = id_to_rect.find(c.to_node);
        if (fi == id_to_rect.end() || ti == id_to_rect.end()) continue;

        const auto& from_rect = node_rects_[fi->second];
        const auto& to_rect = node_rects_[ti->second];

        // Find output port position in graph space
        float gsx = from_rect.x + from_rect.w;
        float gsy = from_rect.y + from_rect.h * 0.5f;
        bool from_port_found = false;
        for (const auto& p : from_rect.outputs) {
            if (p.name == c.from_port) { gsx = p.x; gsy = p.y; from_port_found = true; break; }
        }
        // Find input port position in graph space
        float gex = to_rect.x;
        float gey = to_rect.y + to_rect.h * 0.5f;
        bool to_port_found = false;
        for (const auto& p : to_rect.inputs) {
            if (p.name == c.to_port) { gex = p.x; gey = p.y; to_port_found = true; break; }
        }

        // Skip wires with unresolvable port endpoints (unless invalid — keep those visible)
        if ((!from_port_found || !to_port_found) && !c.invalid) continue;

        // Transform to screen space
        float ssx = gx_to_sx(gsx), ssy = gy_to_sy(gsy);
        float sex = gx_to_sx(gex), sey = gy_to_sy(gey);

        // Env-colored wires (source node's accent color)
        const float* dcol = node_accent_color(from_rect.is_gpu, from_rect.active_cadence);
        bool sel = selected_node_ids_.count(c.from_node) > 0 || selected_node_ids_.count(c.to_node) > 0;
        bool wire_sel = (ci == selected_wire_idx_);
        bool hov = (ci == hovered_wire_idx_) || wire_sel;
        float brightness = (hov || sel) ? kWireHoverBright : 1.0f;
        float cr = std::min(1.0f, dcol[0] * brightness);
        float cg = std::min(1.0f, dcol[1] * brightness);
        float cb = std::min(1.0f, dcol[2] * brightness);
        float a = (hov || sel) ? 0.95f : 0.8f;
        if (c.invalid) {
            cr = 1.0f;
            cg = 0.38f;
            cb = 0.32f;
            a = (hov || sel) ? 1.0f : 0.9f;
        }

        bool is_param_wire = c.from_is_param || c.to_is_param;
        float wire_th;
        if (is_param_wire)
            wire_th = std::max(1.0f, style_.wire_param_thickness * zoom_);
        else
            wire_th = std::max(1.0f, (hov ? style_.wire_hover_thickness : style_.wire_thickness) * zoom_);

        if (is_param_wire || c.invalid) {
            float a_param = (hov || sel) ? 0.6f : 0.35f;
            if (c.invalid) a_param = a;
            draw_dashed_wire(tr, ssx, ssy, sex, sey, bezier_wires_, wire_th, cr, cg, cb, a_param);
        } else if (from_rect.is_gpu) {
            // GPU wires: solid (no flow animation)
            traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
                [&](float x0, float y0, float x1, float y1) {
                    tr.draw_line(x0, y0, x1, y1, wire_th, cr, cg, cb, a);
                });
        } else if (from_rect.active_cadence == Cadence::Audio) {
            // Audio wires: fast dense marching dashes
            float offset = wire_flow_time_ * kAudioFlowSpeed;
            draw_dashed_wire_ex(tr, ssx, ssy, sex, sey, bezier_wires_, wire_th,
                                kAudioFlowDashOn, kAudioFlowDashOff, offset,
                                cr, cg, cb, a);
        } else {
            // Frame wires: slow sparse marching dashes
            float offset = wire_flow_time_ * kFrameFlowSpeed;
            draw_dashed_wire_ex(tr, ssx, ssy, sex, sey, bezier_wires_, wire_th,
                                kFrameFlowDashOn, kFrameFlowDashOff, offset,
                                cr, cg, cb, a);
        }

        // Lane cardinality badge: show "×N" at wire midpoint for multi-lane wires.
        // Use semantic lane_count from compiled edge (primary), falling back to
        // materialized lane length when semantic count is unavailable.
        float badge_offset = 0.0f;
        {
            size_t lane_n = 0;
            if (c.lane_count > 1) {
                // Semantic lane count from compiled graph
                lane_n = c.lane_count;
            } else if (!c.from_is_param) {
                // Fallback: materialized runtime lane count
                auto src_it = snap_.node_index.find(c.from_node);
                if (src_it != snap_.node_index.end()) {
                    const auto& src_node = snap_.nodes[src_it->second];
                    auto port_it = src_node.output_port_indices.find(c.from_port);
                    if (port_it != src_node.output_port_indices.end()) {
                        uint32_t pidx = port_it->second;
                        if (pidx < src_node.output_lanes.size() &&
                            !src_node.output_lanes[pidx].empty()) {
                            lane_n = src_node.output_lanes[pidx].size();
                        } else if (pidx < src_node.output_string_lanes.size() &&
                                   !src_node.output_string_lanes[pidx].empty()) {
                            lane_n = src_node.output_string_lanes[pidx].size();
                        }
                    }
                }
            }
            if (lane_n > 1) {
                float mx = (ssx + sex) * 0.5f;
                float my = (ssy + sey) * 0.5f - kWireBadgeYOff * zoom_;
                char badge[16];
                std::snprintf(badge, sizeof(badge), "\xc3\x97%zu", lane_n);
                tr.draw_text(mx, my, badge, cr, cg, cb, 0.6f, zoom_);
                badge_offset = kWireBadgeSpacing * zoom_;
            }
        }

        // Remap "R" badge at wire midpoint when remap is non-default
        if (c.has_remap()) {
            float mx = (ssx + sex) * 0.5f;
            float my = (ssy + sey) * 0.5f - kWireBadgeYOff * zoom_ - badge_offset;
            tr.draw_text(mx, my, "R", cr, cg, cb, 0.7f, zoom_);
        }
        if (c.invalid) {
            float mx = (ssx + sex) * 0.5f;
            float my = (ssy + sey) * 0.5f + kWireBadgeYOff * zoom_;
            tr.draw_text(mx, my, "!", cr, cg, cb, 0.8f, zoom_);
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
    if (c.invalid) {
        value_str = c.invalid_reason.empty() ? "Broken connection" : c.invalid_reason;
    } else if (src_ns) {
        auto it = src_ns->output_port_indices.find(c.from_port);
        if (it != src_ns->output_port_indices.end()) {
            uint32_t pidx = it->second;
            if (pidx < src_ns->output_values.size()) {
                float val = src_ns->output_values[pidx];
                if (pidx < src_ns->output_lanes.size() &&
                    !src_ns->output_lanes[pidx].empty()) {
                    value_str = format_float(val) + " [lane_array: " +
                                std::to_string(src_ns->output_lanes[pidx].size()) + "]";
                } else if (pidx < src_ns->output_string_lanes.size() &&
                           !src_ns->output_string_lanes[pidx].empty()) {
                    value_str = "\"" + src_ns->output_string_lanes[pidx][0] + "\" [string_lanes: " +
                                std::to_string(src_ns->output_string_lanes[pidx].size()) + "]";
                } else {
                    if (pidx < src_ns->output_string_values.size() &&
                        !src_ns->output_string_values[pidx].empty()) {
                        value_str = "\"" + src_ns->output_string_values[pidx] + "\"";
                    } else {
                        value_str = format_float(val);
                    }
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
    float pad = kTooltipPad;
    float popup_w = std::max(label_w, value_w) + pad * 2;
    float line_h = kTooltipLineH;
    float popup_h = (value_str.empty() ? line_h : line_h * 2) + pad * 2;

    // Position near cursor, offset down-right, clamped to graph bounds
    float px = mouse_.x + kTooltipCursorOff;
    float py = mouse_.y + kTooltipCursorOff;
    if (px + popup_w > graph_right()) px = mouse_.x - popup_w - kTooltipClampMargin;
    if (py + popup_h > static_cast<float>(win_h_)) py = mouse_.y - popup_h - kTooltipClampMargin;

    // Find accent color from source node rect
    const float* dcol = nullptr;
    for (const auto& r : node_rects_) {
        if (r.node_id == c.from_node) { dcol = node_accent_color(r.is_gpu, r.active_cadence); break; }
    }

    // Shadow + Background
    draw_shadow(tr, px, py, popup_w, popup_h);
    tr.draw_rect(px, py, popup_w, popup_h, style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], kTooltipBgAlpha);
    // Accent line at top
    if (c.invalid) {
        tr.draw_rect(px, py, popup_w, kTooltipAccentH, 1.0f, 0.38f, 0.32f, kTooltipBgAlpha);
    } else if (dcol) {
        tr.draw_rect(px, py, popup_w, kTooltipAccentH, dcol[0], dcol[1], dcol[2], 0.9f);
    }
    // Label text
    tr.draw_text(px + pad, py + pad, label.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    // Value text
    if (!value_str.empty()) {
        tr.draw_text(px + pad, py + pad + line_h, value_str.c_str(), style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
}

// -----------------------------------------------------------------------
// Patch Panel (2-node connection view)
// -----------------------------------------------------------------------
static constexpr float kPatchJackRadius = 4.0f;
static constexpr float kPatchRowH = 20.0f;
static constexpr float kPatchColW = 100.0f;
static constexpr float kPatchWireThickness = 2.0f;
static constexpr float kPatchHeaderH = 20.0f;

void NodeGraphUI::draw_patch_panel(Renderer2D& tr, const NodeSnapshot& node_a,
                                    const NodeSnapshot& node_b, float px, float& py) {
    if (!node_a.op_info || !node_b.op_info) return;

    const float panel_w = kInspContentW;
    const float center_gap = panel_w - 2 * kPatchColW;  // gap between columns

    // --- Build port lists ---
    struct PortEntry {
        std::string name;
        VividPortType port_type;
        bool can_source;   // outputs + params
        bool can_dest;     // inputs + params
        bool is_param;
    };
    struct PortList {
        std::vector<PortEntry> ports;   // outputs + inputs (non-param)
        std::vector<PortEntry> params;  // params only
    };

    auto build_ports = [](const NodeSnapshot& ns) {
        PortList result;
        if (!ns.op_info) return result;

        // Outputs first (signal output ports)
        auto sorted_outs = sorted_ports(ns.output_port_indices);
        for (const auto& [idx, name] : sorted_outs) {
            VividPortType pt = VIVID_PORT_SCALAR;
            for (const auto& p : ns.op_info->ports)
                if (p.name == name && p.direction == VIVID_PORT_OUTPUT) { pt = p.type; break; }
            result.ports.push_back({name, pt, true, false, false});
        }

        // Inputs (signal input ports that aren't params)
        for (const auto& pi : ns.op_info->ports) {
            if (pi.direction != VIVID_PORT_INPUT) continue;
            bool is_param = ns.param_indices.count(pi.name) > 0;
            if (!is_param)
                result.ports.push_back({pi.name, pi.type, false, true, false});
        }

        // Params (non-FILE, excluding output port names)
        std::vector<std::pair<uint32_t, std::string>> sorted_params;
        for (const auto& [name, idx] : ns.param_indices)
            if (!ns.output_port_indices.count(name)) sorted_params.push_back({idx, name});
        std::sort(sorted_params.begin(), sorted_params.end());
        for (const auto& [idx, name] : sorted_params) {
            const ParamInfo* pd = ns.find_param(name);
            if (pd && (pd->type == VIVID_PARAM_FILE || pd->type == VIVID_PARAM_TEXT)) continue;
            result.params.push_back({name, VIVID_PORT_SCALAR, true, true, true});
        }
        return result;
    };
    auto type_suffix = [](VividPortType t) -> const char* {
        if (t == VIVID_PORT_STRING) return " \"";
        if (t == VIVID_PORT_STRING_LANES) return " [\"]";
        return "";
    };

    auto left_pl = build_ports(node_a);
    auto right_pl = build_ports(node_b);

    // Flatten into single list per column (ports then params) for drawing, tracking section offsets
    auto flatten = [](const PortList& pl) {
        std::vector<PortEntry> all;
        all.insert(all.end(), pl.ports.begin(), pl.ports.end());
        all.insert(all.end(), pl.params.begin(), pl.params.end());
        return all;
    };
    auto left_ports = flatten(left_pl);
    auto right_ports = flatten(right_pl);

    // --- Column headers ---
    const float* clr_a = node_accent_color(node_a.is_gpu, node_a.active_cadence);
    const float* clr_b = node_accent_color(node_b.is_gpu, node_b.active_cadence);

    // Left header: node A name, left-aligned
    tr.draw_text(px, py, node_a.op_info->name.c_str(), clr_a[0], clr_a[1], clr_a[2]);
    // Accent underline
    float name_a_w = tr.text_width(node_a.op_info->name.c_str());
    tr.draw_rect(px, py + kLineH - 2, name_a_w, 1, clr_a[0], clr_a[1], clr_a[2], 0.6f);

    // Right header: node B name, right-aligned
    float name_b_w = tr.text_width(node_b.op_info->name.c_str());
    float right_col_x = px + panel_w - kPatchColW;
    tr.draw_text(px + panel_w - name_b_w, py, node_b.op_info->name.c_str(), clr_b[0], clr_b[1], clr_b[2]);
    tr.draw_rect(px + panel_w - name_b_w, py + kLineH - 2, name_b_w, 1, clr_b[0], clr_b[1], clr_b[2], 0.6f);

    py += kPatchHeaderH + 4;

    // Helper: draw a diamond (rotated square) for param jacks
    auto draw_diamond = [&](float cx, float cy, float r, float cr, float cg, float cb, float ca) {
        tr.draw_line(cx, cy - r, cx + r, cy, 1.5f, cr, cg, cb, ca);
        tr.draw_line(cx + r, cy, cx, cy + r, 1.5f, cr, cg, cb, ca);
        tr.draw_line(cx, cy + r, cx - r, cy, 1.5f, cr, cg, cb, ca);
        tr.draw_line(cx - r, cy, cx, cy - r, 1.5f, cr, cg, cb, ca);
    };

    // Helper: draw a section header (dim text at 0.75 scale with 1px separator)
    auto draw_section_header = [&](float hx, float hy, float col_w, const char* text,
                                    const float* clr, bool right_align) {
        float tw = tr.text_width(text, 0.75f);
        float tx = right_align ? (hx + col_w - tw) : hx;
        tr.draw_text(tx, hy + 3, text,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f, 0.75f);
        tr.draw_rect(hx, hy + kPatchRowH - 2, col_w, 1,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.2f);
    };

    // Compute row Y positions accounting for section headers
    // Each column: optional "PORTS" header, port rows, optional "PARAMS" header, param rows
    bool left_has_ports = !left_pl.ports.empty();
    bool left_has_params = !left_pl.params.empty();
    bool right_has_ports = !right_pl.ports.empty();
    bool right_has_params = !right_pl.params.empty();

    int left_total = (int)left_ports.size() + (left_has_ports ? 1 : 0) + (left_has_params ? 1 : 0);
    int right_total = (int)right_ports.size() + (right_has_ports ? 1 : 0) + (right_has_params ? 1 : 0);
    int max_rows = std::max(left_total, right_total);

    // --- Draw left ports (node A) ---
    float left_jack_x = px + kPatchColW;  // jack at right edge of left column
    float start_y = py;

    {
        float cur_y = start_y;
        if (left_has_ports) {
            draw_section_header(px, cur_y, kPatchColW, "PORTS", clr_a, true);
            cur_y += kPatchRowH;
        }
        for (int i = 0; i < static_cast<int>(left_pl.ports.size()); ++i) {
            const auto& port = left_pl.ports[i];
            float row_y = cur_y;
            float jack_cy = row_y + kPatchRowH * 0.5f;

            std::string label = port.name;
            label += type_suffix(port.port_type);
            if (label.size() > 12) label = label.substr(0, 11) + "~";
            float label_w = tr.text_width(label.c_str(), 0.85f);
            tr.draw_text(left_jack_x - kPatchJackRadius * 2 - 2 - label_w, row_y + 2,
                         label.c_str(), clr_a[0], clr_a[1], clr_a[2], 0.8f, 0.85f);

            float jack_alpha = 1.0f;
            if (patch_dragging_ && patch_drag_from_idx_ >= 0 &&
                patch_drag_from_idx_ < static_cast<int>(patch_jacks_.size())) {
                const auto& src = patch_jacks_[patch_drag_from_idx_];
                bool compatible = (src.node_id != node_a.node_id) && port.can_dest &&
                                  port_type_compatible(src.port_type, port.port_type);
                jack_alpha = compatible ? 1.0f : 0.2f;
            }

            if (port.can_source) {
                tr.draw_rect(left_jack_x - kPatchJackRadius, jack_cy - kPatchJackRadius,
                             kPatchJackRadius * 2, kPatchJackRadius * 2,
                             clr_a[0], clr_a[1], clr_a[2], jack_alpha);
            } else {
                tr.draw_arc(left_jack_x, jack_cy, kPatchJackRadius, 0.0f, 6.283f,
                            1.5f, 12, clr_a[0], clr_a[1], clr_a[2], jack_alpha);
            }

            patch_jacks_.push_back({
                left_jack_x, jack_cy, node_a.node_id, port.name,
                port.port_type, port.can_source, port.can_dest, port.is_param
            });
            cur_y += kPatchRowH;
        }
        if (left_has_params) {
            draw_section_header(px, cur_y, kPatchColW, "PARAMS", clr_a, true);
            cur_y += kPatchRowH;
        }
        for (int i = 0; i < static_cast<int>(left_pl.params.size()); ++i) {
            const auto& port = left_pl.params[i];
            float row_y = cur_y;
            float jack_cy = row_y + kPatchRowH * 0.5f;

            std::string label = "\xC2\xB7" + port.name;
            label += type_suffix(port.port_type);
            if (label.size() > 12) label = label.substr(0, 11) + "~";
            float label_w = tr.text_width(label.c_str(), 0.85f);
            tr.draw_text(left_jack_x - kPatchJackRadius * 2 - 2 - label_w, row_y + 2,
                         label.c_str(), clr_a[0], clr_a[1], clr_a[2], 0.5f, 0.85f);

            float jack_alpha = 1.0f;
            if (patch_dragging_ && patch_drag_from_idx_ >= 0 &&
                patch_drag_from_idx_ < static_cast<int>(patch_jacks_.size())) {
                const auto& src = patch_jacks_[patch_drag_from_idx_];
                bool compatible = (src.node_id != node_a.node_id) && port.can_dest &&
                                  port_type_compatible(src.port_type, port.port_type);
                jack_alpha = compatible ? 1.0f : 0.2f;
            }

            draw_diamond(left_jack_x, jack_cy, kPatchJackRadius,
                         clr_a[0], clr_a[1], clr_a[2], 0.8f * jack_alpha);

            patch_jacks_.push_back({
                left_jack_x, jack_cy, node_a.node_id, port.name,
                port.port_type, port.can_source, port.can_dest, port.is_param
            });
            cur_y += kPatchRowH;
        }
    }

    // --- Draw right ports (node B) ---
    float right_jack_x = right_col_x;  // jack at left edge of right column

    {
        float cur_y = start_y;
        if (right_has_ports) {
            draw_section_header(right_col_x, cur_y, kPatchColW, "PORTS", clr_b, false);
            cur_y += kPatchRowH;
        }
        for (int i = 0; i < static_cast<int>(right_pl.ports.size()); ++i) {
            const auto& port = right_pl.ports[i];
            float row_y = cur_y;
            float jack_cy = row_y + kPatchRowH * 0.5f;

            std::string label = port.name;
            label += type_suffix(port.port_type);
            if (label.size() > 12) label = label.substr(0, 11) + "~";
            float label_alpha = 0.8f;
            tr.draw_text(right_jack_x + kPatchJackRadius * 2 + 2, row_y + 2,
                         label.c_str(), clr_b[0], clr_b[1], clr_b[2], label_alpha, 0.85f);

            float jack_alpha = 1.0f;
            if (patch_dragging_ && patch_drag_from_idx_ >= 0 &&
                patch_drag_from_idx_ < static_cast<int>(patch_jacks_.size())) {
                const auto& src = patch_jacks_[patch_drag_from_idx_];
                bool compatible = (src.node_id != node_b.node_id) && port.can_dest &&
                                  port_type_compatible(src.port_type, port.port_type);
                jack_alpha = compatible ? 1.0f : 0.2f;
            }

            if (port.can_source) {
                tr.draw_rect(right_jack_x - kPatchJackRadius, jack_cy - kPatchJackRadius,
                             kPatchJackRadius * 2, kPatchJackRadius * 2,
                             clr_b[0], clr_b[1], clr_b[2], jack_alpha);
            } else {
                tr.draw_arc(right_jack_x, jack_cy, kPatchJackRadius, 0.0f, 6.283f,
                            1.5f, 12, clr_b[0], clr_b[1], clr_b[2], jack_alpha);
            }

            patch_jacks_.push_back({
                right_jack_x, jack_cy, node_b.node_id, port.name,
                port.port_type, port.can_source, port.can_dest, port.is_param
            });
            cur_y += kPatchRowH;
        }
        if (right_has_params) {
            draw_section_header(right_col_x, cur_y, kPatchColW, "PARAMS", clr_b, false);
            cur_y += kPatchRowH;
        }
        for (int i = 0; i < static_cast<int>(right_pl.params.size()); ++i) {
            const auto& port = right_pl.params[i];
            float row_y = cur_y;
            float jack_cy = row_y + kPatchRowH * 0.5f;

            std::string label = "\xC2\xB7" + port.name;
            label += type_suffix(port.port_type);
            if (label.size() > 12) label = label.substr(0, 11) + "~";
            tr.draw_text(right_jack_x + kPatchJackRadius * 2 + 2, row_y + 2,
                         label.c_str(), clr_b[0], clr_b[1], clr_b[2], 0.5f, 0.85f);

            float jack_alpha = 1.0f;
            if (patch_dragging_ && patch_drag_from_idx_ >= 0 &&
                patch_drag_from_idx_ < static_cast<int>(patch_jacks_.size())) {
                const auto& src = patch_jacks_[patch_drag_from_idx_];
                bool compatible = (src.node_id != node_b.node_id) && port.can_dest &&
                                  port_type_compatible(src.port_type, port.port_type);
                jack_alpha = compatible ? 1.0f : 0.2f;
            }

            draw_diamond(right_jack_x, jack_cy, kPatchJackRadius,
                         clr_b[0], clr_b[1], clr_b[2], 0.8f * jack_alpha);

            patch_jacks_.push_back({
                right_jack_x, jack_cy, node_b.node_id, port.name,
                port.port_type, port.can_source, port.can_dest, port.is_param
            });
            cur_y += kPatchRowH;
        }
    }

    // --- Draw connection wires ---
    for (const auto& c : snap_.connections) {
        // Find wires between these two nodes (either direction)
        bool ab = (c.from_node == node_a.node_id && c.to_node == node_b.node_id);
        bool ba = (c.from_node == node_b.node_id && c.to_node == node_a.node_id);
        if (!ab && !ba) continue;

        // Find source and dest jack positions
        float src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
        bool found_src = false, found_dst = false;
        for (const auto& j : patch_jacks_) {
            if (j.node_id == c.from_node && j.port_name == c.from_port) {
                src_x = j.x; src_y = j.y; found_src = true;
            }
            if (j.node_id == c.to_node && j.port_name == c.to_port) {
                dst_x = j.x; dst_y = j.y; found_dst = true;
            }
            if (found_src && found_dst) break;
        }
        if (!found_src || !found_dst) continue;

        // Always draw left-to-right for clean beziers
        float wx0, wy0, wx1, wy1;
        if (src_x <= dst_x) {
            wx0 = src_x; wy0 = src_y; wx1 = dst_x; wy1 = dst_y;
        } else {
            wx0 = dst_x; wy0 = dst_y; wx1 = src_x; wy1 = src_y;
        }

        // Wire color from source node's env
        const float* wire_clr = ab ? clr_a : clr_b;
        float wire_alpha = c.has_remap() ? 0.7f : 1.0f;

        traverse_wire(wx0, wy0, wx1, wy1, true, [&](float x0, float y0, float x1, float y1) {
            tr.draw_line(x0, y0, x1, y1, kPatchWireThickness,
                         wire_clr[0], wire_clr[1], wire_clr[2], wire_alpha);
        });

        patch_wires_.push_back({
            src_x, src_y, dst_x, dst_y,
            c.from_node, c.from_port, c.to_node, c.to_port, c.has_remap()
        });
    }

    // --- Preview wire during drag ---
    if (patch_dragging_ && patch_drag_from_idx_ >= 0 &&
        patch_drag_from_idx_ < static_cast<int>(patch_jacks_.size())) {
        const auto& src_jack = patch_jacks_[patch_drag_from_idx_];
        float wx0, wy0, wx1, wy1;
        if (src_jack.x <= mouse_.x) {
            wx0 = src_jack.x; wy0 = src_jack.y; wx1 = mouse_.x; wy1 = mouse_.y;
        } else {
            wx0 = mouse_.x; wy0 = mouse_.y; wx1 = src_jack.x; wy1 = src_jack.y;
        }
        traverse_wire(wx0, wy0, wx1, wy1, true, [&](float x0, float y0, float x1, float y1) {
            tr.draw_line(x0, y0, x1, y1, kPatchWireThickness,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.5f);
        });
    }

    // --- Context menu popup ---
    if (patch_ctx_open_ && patch_ctx_wire_idx_ >= 0 &&
        patch_ctx_wire_idx_ < static_cast<int>(patch_wires_.size())) {
        const auto& w = patch_wires_[patch_ctx_wire_idx_];
        float menu_w = 160.0f;
        float menu_h = kCtxMenuItemH * 2 + kCtxMenuPadTop * 2;
        float mx = patch_ctx_x_;
        float my = patch_ctx_y_;

        draw_popup_bg(tr, style_, mx, my, menu_w, menu_h);

        // Connection label
        std::string label = w.from_port + " \xE2\x86\x92 " + w.to_port;
        if (w.has_remap)
            label += " (R)";
        tr.draw_text(mx + 6, my + kCtxMenuPadTop + 2, label.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.8f, 0.8f);

        // "Disconnect" item
        float item_y = my + kCtxMenuPadTop + kCtxMenuItemH;
        bool hovered = (mouse_.x >= mx && mouse_.x <= mx + menu_w &&
                        mouse_.y >= item_y && mouse_.y <= item_y + kCtxMenuItemH);
        if (hovered)
            tr.draw_rect(mx + 1, item_y, menu_w - 2, kCtxMenuItemH,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
        tr.draw_text(mx + 6, item_y + 2, T("disconnect", "Disconnect"), 1.0f, 1.0f, 1.0f, 0.9f);
    }

    py = start_y + max_rows * kPatchRowH + 8;
}

void NodeGraphUI::draw_preview_wire(Renderer2D& tr) {
    if (!dragging_wire_) return;
    float ssx = gx_to_sx(wire_from_gx_), ssy = gy_to_sy(wire_from_gy_);
    float sex = mouse_.x, sey = mouse_.y;

    if (!wire_from_is_output_) {
        // Param source: thin dashed preview
        float wire_th = std::max(1.0f, style_.wire_param_thickness * zoom_);
        draw_dashed_wire(tr, ssx, ssy, sex, sey, bezier_wires_, wire_th, 1.0f, 1.0f, 1.0f, 0.3f);
    } else {
        float wire_th = std::max(1.0f, style_.wire_thickness * zoom_);
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

} // namespace vivid::ui
