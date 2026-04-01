#include <nlohmann/json.hpp>
#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/overlay_layouts.h"
#include "ui/renderer_2d.h"
#include "ui/i18n.h"
#include "ui/thumbnail_cache.h"
#include "ui/thumbnail_renderer.h"
#include "common/string_util.h"
#include "common/system_info.h"
#include <algorithm>
#include <fstream>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

namespace vivid::ui {

static constexpr uint64_t kMcpStaleMs = 30000;

using vivid::format_float;
using vivid::format_int;
using vivid::format_uint;

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

static std::string build_semantic_hint(const ParamInfo& pd) {
    if (pd.semantic_tag.empty() && pd.semantic_shape.empty() &&
        pd.semantic_unit.empty() && pd.semantic_intent.empty()) {
        return {};
    }

    std::string hint;
    if (!pd.semantic_tag.empty()) hint += pd.semantic_tag;
    if (!pd.semantic_shape.empty()) {
        if (!hint.empty()) hint += " ";
        hint += "(" + pd.semantic_shape + ")";
    }
    if (!pd.semantic_unit.empty()) {
        if (!hint.empty()) hint += " ";
        hint += "[" + pd.semantic_unit + "]";
    }
    if (!pd.semantic_intent.empty()) {
        if (!hint.empty()) hint += " - ";
        hint += pd.semantic_intent;
    }
    return hint;
}

// Truncate text with ellipsis if it exceeds max_w at the given scale.
static std::string truncate_text(Renderer2D& tr, const std::string& text,
                                 float max_w, float scale = 1.0f) {
    if (tr.text_width(text.c_str(), scale) <= max_w) return text;
    std::string result = text;
    while (result.size() > 1 && tr.text_width((result + "\xe2\x80\xa6").c_str(), scale) > max_w)
        result.pop_back();
    return result + "\xe2\x80\xa6";
}

struct InspectorCardBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float content_x = 0.0f;
    float content_w = 0.0f;
};

static InspectorCardBox draw_inspector_card(Renderer2D& tr, const UIStyle& style,
                                            float x, float y, float w, float h,
                                            float inner_pad, float alpha = 0.55f) {
    tr.draw_rect(x, y, w, h,
                 style.slider_track[0], style.slider_track[1], style.slider_track[2], alpha);
    return InspectorCardBox{ x, y, w, h, x + inner_pad, w - inner_pad * 2.0f };
}

static float draw_inspector_env_chip(Renderer2D& tr, float x, float y,
                                        const std::string& label,
                                        const float* color,
                                        float scale = 0.85f, float pad_x = 6.0f,
                                        float h = 18.0f) {
    float chip_w = tr.text_width(label.c_str(), scale) + pad_x * 2.0f;
    tr.draw_rect(x, y, chip_w, h,
                 color[0] * 0.3f, color[1] * 0.3f, color[2] * 0.3f, 0.8f);
    tr.draw_text(x + pad_x, y + 1.0f, label.c_str(), color[0], color[1], color[2], scale);
    return chip_w;
}

static float draw_inspector_text_button(Renderer2D& tr, const UIStyle& style,
                                        float x, float y, const char* label,
                                        float scale = 0.8f, float pad_x = 6.0f,
                                        float h = 18.0f) {
    float w = tr.text_width(label, scale) + pad_x * 2.0f;
    tr.draw_rect(x, y, w, h,
                 style.slider_track[0], style.slider_track[1], style.slider_track[2]);
    tr.draw_text(x + pad_x, y + 1.0f, label,
                 style.bright_text[0], style.bright_text[1], style.bright_text[2], scale);
    return w;
}

static void draw_inspector_left_accent(Renderer2D& tr, float x, float top, float bottom,
                                       const float* color, float alpha = 0.5f) {
    tr.draw_rect(x - 4.0f, top, 2.0f, bottom - top, color[0], color[1], color[2], alpha);
}

struct ParamConnectionInfo {
    bool connected = false;
    std::string from_node;
    std::string from_port;
    std::string source_label;
};

static ParamConnectionInfo find_param_connection(const GraphSnapshot& snap,
                                                 const std::string& node_id,
                                                 const std::string& param_name) {
    ParamConnectionInfo info;
    for (const auto& c : snap.connections) {
        if (c.to_node == node_id && c.to_port == param_name) {
            info.connected = true;
            info.from_node = c.from_node;
            info.from_port = c.from_port;
            info.source_label = "\xE2\x86\x90 " + c.from_node + "/" + c.from_port;
            break;
        }
    }
    return info;
}

// -----------------------------------------------------------------------
// Drawing
// -----------------------------------------------------------------------
void NodeGraphUI::draw_graph(Renderer2D& tr) {
    expand_affordance_rects_.clear();
    for (size_t i = 0; i < node_rects_.size(); ++i) {
        const auto& r = node_rects_[i];
        bool selected = selected_node_ids_.count(r.node_id) > 0;
        const float* bg = selected ? style_.node_sel_bg.data() : style_.node_bg.data();
        const NodeSnapshot* sn = snap_.find_node(r.node_id);
        bool node_errored = sn && sn->errored;
        bool node_missing = sn && sn->missing_operator;
        bool node_bad     = node_errored || node_missing || (sn && !sn->error_message.empty());
        const float* dcol = node_bad ? kErrorAccent.data() : node_accent_color(r.is_gpu, r.active_cadence);

        // Transform graph-space rect to screen space
        float sx = gx_to_sx(r.x), sy = gy_to_sy(r.y);
        float sw = g_to_s(r.w), sh = g_to_s(r.h);

        // Node background (brighten on hover with smooth animation)
        bool node_hovered = (r.node_id == hovered_node_id_);
        float sr = g_to_s(style_.corner_radius);
        float br = bg[0], bgr = bg[1], bb = bg[2];
        if (node_hovered && !selected) {
            float ha = (r.node_id == node_hover_anim_id_) ? node_hover_alpha_ : 0.0f;
            br += kNodeHoverBrighten * ha;
            bgr += kNodeHoverBrighten * ha;
            bb += kNodeHoverBrighten * ha;
        }
        tr.draw_rounded_rect(sx, sy, sw, sh, sr, br, bgr, bb);

        // Selection glow (subtle pulse on selected nodes)
        if (selected && selection_glow_ > 0.01f) {
            float glow_a = selection_glow_ * 0.08f;
            tr.draw_rounded_rect(sx, sy, sw, sh, sr,
                                 style_.accent[0], style_.accent[1], style_.accent[2], glow_a);
        }

        // Solo dimming: reduce alpha of non-active nodes
        bool node_soloed     = sn && sn->soloed;
        bool node_solo_dimmed = sn && sn->solo_dimmed;
        if (node_solo_dimmed) {
            // Overdraw a dark semi-transparent layer on top of the node background
            tr.draw_rounded_rect(sx, sy, sw, sh, g_to_s(style_.corner_radius),
                                 0.0f, 0.0f, 0.0f, 0.65f);
        }

        // Red border on errored or missing nodes
        if (node_bad) {
            float bw = g_to_s(kErrorBorderW);
            tr.draw_rect(sx, sy, sw, bw, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]);           // top
            tr.draw_rect(sx, sy + sh - bw, sw, bw, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]); // bottom
            tr.draw_rect(sx, sy, bw, sh, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]);           // left
            tr.draw_rect(sx + sw - bw, sy, bw, sh, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]); // right
        }

        // Gold border on soloed node
        if (node_soloed) {
            float bw = g_to_s(kSoloBorderW);
            tr.draw_rect(sx, sy, sw, bw, kSoloAccent[0], kSoloAccent[1], kSoloAccent[2]);           // top
            tr.draw_rect(sx, sy + sh - bw, sw, bw, kSoloAccent[0], kSoloAccent[1], kSoloAccent[2]); // bottom
            tr.draw_rect(sx, sy, bw, sh, kSoloAccent[0], kSoloAccent[1], kSoloAccent[2]);           // left
            tr.draw_rect(sx + sw - bw, sy, bw, sh, kSoloAccent[0], kSoloAccent[1], kSoloAccent[2]); // right
        }

        // Accent bar at top
        float s_accent_h = g_to_s(kAccentBarH);
        tr.draw_rect(sx, sy, sw, s_accent_h, dcol[0], dcol[1], dcol[2]);

        // --- Env body region ---
        float s_body_y = sy + s_accent_h;
        bool has_ct = custom_thumb_nodes_.count(r.node_id) > 0;
        float body_h = node_body_height(r.is_gpu, r.active_cadence, has_ct);
        float s_body_h = g_to_s(body_h);

        if (node_missing) {
            // Dark red-tinted background
            tr.draw_rect(sx + g_to_s(2), s_body_y + g_to_s(2),
                         sw - g_to_s(4), s_body_h - g_to_s(4),
                         kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.15f);
            // Centered "MISSING" label (shifted up to make room for sub-label)
            const char* label = "MISSING";
            float lw = tr.text_width(label, zoom_);
            float lx = sx + (sw - lw) * 0.5f;
            float ly = s_body_y + (s_body_h - tr.line_height() * zoom_) * 0.5f - g_to_s(6);
            tr.draw_text(lx, ly, label, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.8f, zoom_);
            // Sub-label with reason
            if (sn && !sn->error_message.empty()) {
                const char* sub = sn->error_message.find("rebuild") != std::string::npos
                                  ? "try rebuild" : sn->error_message.find("ABI") != std::string::npos
                                  ? "ABI mismatch" : "not installed";
                float sub_scale = zoom_ * 0.75f;
                float sub_w = tr.text_width(sub, sub_scale);
                float sub_x = sx + (sw - sub_w) * 0.5f;
                float sub_y = ly + tr.line_height() * zoom_ + g_to_s(2);
                tr.draw_text(sub_x, sub_y, sub, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.5f, sub_scale);
            }
        } else if (!r.is_gpu && r.active_cadence == Cadence::Frame && !has_ct) {
            // Sparkline
            tr.draw_rect(sx + g_to_s(2), s_body_y + g_to_s(2),
                         sw - g_to_s(4), s_body_h - g_to_s(4),
                         style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.9f);

            // Find sparkline data for this node's first output
            std::string spark_key;
            if (sn) {
                auto sorted_outs = sorted_ports(sn->output_port_indices);
                if (!sorted_outs.empty())
                    spark_key = sn->node_id + "/" + sorted_outs[0].second;
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
                    uint32_t first_idx = sd.filled ? sd.write_idx % kSparklineLen : 0;
                    float vmin = sd.values[first_idx], vmax = sd.values[first_idx];
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
                        tr.draw_rect(bx, by, std::max(1.0f, bar_w * 0.55f), bh,
                                     dcol[0], dcol[1], dcol[2], kSparklineBarAlpha);
                    }
                }
            }

            // Sparse tick strip at bottom — "discrete events" visual
            {
                float tick_y = s_body_y + s_body_h - g_to_s(6);
                float tick_x0 = sx + g_to_s(4);
                float tick_w = sw - g_to_s(8);
                for (int ti = 0; ti < kDensityTickCount; ++ti) {
                    float tx = tick_x0 + ti * (tick_w / (kDensityTickCount - 1));
                    tr.draw_rect(tx, tick_y, 1.0f, g_to_s(2),
                                 dcol[0], dcol[1], dcol[2], kDensityTickAlpha);
                }
            }
        } else if (!r.is_gpu && r.active_cadence == Cadence::Audio && !has_ct) {
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
                                 dcol[0], dcol[1], dcol[2], kWaveformCenterAlpha);

                    // Waveform bars — downsample 1024 source samples to
                    // ~256 visual bars (max-amplitude per bucket) to cut
                    // vertex cost from 6144 to ~1536 with no visible loss.
                    constexpr uint32_t kWaveN = AudioNodeAnalysis::kWaveformSamples;
                    constexpr uint32_t kVisualBars = 256;
                    constexpr uint32_t kStride = kWaveN / kVisualBars; // 4
                    float bar_w = wave_w / kVisualBars;

                    // Find peak absolute amplitude across all buckets so
                    // signals outside [-1,1] (e.g. LFO) are scaled to fit.
                    float max_abs = 0.0f;
                    for (uint32_t bi = 0; bi < kVisualBars; ++bi) {
                        for (uint32_t j = 0; j < kStride; ++j) {
                            float a = std::fabs(analysis.waveform[bi * kStride + j]);
                            if (a > max_abs) max_abs = a;
                        }
                    }
                    float scale = (max_abs > 1.0f) ? 1.0f / max_abs : 1.0f;

                    for (uint32_t bi = 0; bi < kVisualBars; ++bi) {
                        // Find the sample with the largest absolute amplitude in this bucket
                        float amp = analysis.waveform[bi * kStride];
                        for (uint32_t j = 1; j < kStride; ++j) {
                            float s = analysis.waveform[bi * kStride + j];
                            if (std::fabs(s) > std::fabs(amp)) amp = s;
                        }
                        float bh = std::fabs(amp) * scale * wave_h * 0.5f;
                        bh = std::max(0.5f, bh);
                        float bx = wave_x + bi * bar_w;
                        float by = (amp >= 0) ? center_y - bh : center_y;
                        tr.draw_rect(bx, by, std::max(0.5f, bar_w), bh,
                                     dcol[0], dcol[1], dcol[2], kWaveformBarAlpha);
                    }

                    // Peak meter strip at bottom
                    float peak_y = s_body_y + s_body_h - g_to_s(4);
                    float pk = std::min(1.0f, analysis.peak);
                    tr.draw_rect(wave_x, peak_y, wave_w * pk, g_to_s(2),
                                 dcol[0], dcol[1], dcol[2], 0.9f);
                }
            }
        }
        // GPU env: body region left blank (thumbnails drawn in separate pass)

        // Type name (centered, below accent bar + body)
        float text_y = sy + s_accent_h + s_body_h + g_to_s(kNodePadY);
        float tw = tr.text_width(r.type_name.c_str(), zoom_);
        float tx = sx + (sw - tw) * 0.5f;
        tr.draw_text(tx, text_y, r.type_name.c_str(), 1.0f, 1.0f, 1.0f, 1.0f, zoom_);

        // Lane behavior badge (S/R/K) for non-Pointwise operators
        if (r.lane_behavior > 0) {
            const char* lb_badge = (r.lane_behavior == 1) ? "S"
                                 : (r.lane_behavior == 2) ? "R"
                                 : (r.lane_behavior == 3) ? "K" : nullptr;
            if (lb_badge) {
                float bx = tx + tw + 4.0f * zoom_;
                tr.draw_text(bx, text_y, lb_badge,
                             0.6f, 0.8f, 1.0f, 0.7f, zoom_);  // subtle blue tint
            }
        }

        // Node ID below type
        float iw = tr.text_width(r.node_id.c_str(), zoom_);
        float ix = sx + (sw - iw) * 0.5f;
        tr.draw_text(ix, text_y + g_to_s(kLineH), r.node_id.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, zoom_);

        // Input port dots and labels (use env color)
        float s_dot = kPortDotSize * zoom_;
        float s_line_h = tr.line_height() * zoom_;
        for (const auto& p : r.inputs) {
            if (p.is_param && !show_param_wires_) continue;
            float spx = gx_to_sx(p.x), spy = gy_to_sy(p.y);
            bool port_hov = (hovered_port_.node_id == r.node_id &&
                             hovered_port_.port_name == p.name && !hovered_port_.is_output);
            float dot_scale = p.is_param ? kParamDotScale : 1.0f;
            float dot_alpha = p.is_param ? kParamDotAlpha : 1.0f;
            if (port_hov) { dot_scale *= kPortHoverScale; dot_alpha = kPortHoverAlpha; }
            float sd = s_dot * dot_scale;
            tr.draw_rect(spx - sd, spy - sd * 0.5f,
                         sd, sd,
                         dcol[0], dcol[1], dcol[2], dot_alpha);
            tr.draw_text(spx + g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2],
                         port_hov ? 1.0f : (p.is_param ? kParamDotAlpha : 1.0f), zoom_);
        }
        // Output port dots and labels (use env color)
        for (const auto& p : r.outputs) {
            if (p.is_param && !show_param_wires_) continue;
            float spx = gx_to_sx(p.x), spy = gy_to_sy(p.y);
            bool port_hov = (hovered_port_.node_id == r.node_id &&
                             hovered_port_.port_name == p.name && hovered_port_.is_output);
            float dot_scale = p.is_param ? kParamDotScale : 1.0f;
            float dot_alpha = p.is_param ? kParamDotAlpha : 1.0f;
            if (port_hov) { dot_scale *= kPortHoverScale; dot_alpha = kPortHoverAlpha; }
            float sd = s_dot * dot_scale;
            tr.draw_rect(spx, spy - sd * 0.5f,
                         sd, sd,
                         dcol[0], dcol[1], dcol[2], dot_alpha);
            float lw = tr.text_width(p.name.c_str(), zoom_);
            tr.draw_text(spx - lw - g_to_s(4), spy - s_line_h * 0.5f, p.name.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], dot_alpha, zoom_);
        }

        // Expand/collapse affordance row for nodes with >3 outputs
        if (r.outputs_expandable) {
            float aspy = gy_to_sy(r.affordance_gy);
            char buf[48];
            if (r.outputs_expanded)
                snprintf(buf, sizeof(buf), "\xe2\x96\xb4 hide");
            else
                snprintf(buf, sizeof(buf), "\xe2\x96\xb8 %u more\xe2\x80\xa6",
                         r.hidden_output_count);
            float lw  = tr.text_width(buf, zoom_);
            float spx = gx_to_sx(r.x + r.w);
            float tx  = spx - lw - g_to_s(4);
            tr.draw_text(tx, aspy - s_line_h * 0.5f, buf,
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.45f, zoom_);
            expand_affordance_rects_.push_back({tx, aspy - s_line_h * 0.5f,
                                                lw + g_to_s(8), s_line_h, r.node_id});
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
                // Fallback: materialized runtime spread length
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

void NodeGraphUI::draw_node_error_tooltip(Renderer2D& tr) {
    if (hovered_node_id_.empty()) return;

    const NodeSnapshot* hovered_ns = snap_.find_node(hovered_node_id_);
    if (!hovered_ns || hovered_ns->error_message.empty()) return;

    static constexpr float kMaxTooltipW = 500.0f;
    static constexpr int   kMaxLines    = 8;

    // Split error message on newlines
    std::vector<std::string> lines;
    {
        std::string tmp;
        for (char c : hovered_ns->error_message) {
            if (c == '\n') { if (!tmp.empty() || !lines.empty()) lines.push_back(tmp); tmp.clear(); }
            else            tmp += c;
        }
        if (!tmp.empty()) lines.push_back(tmp);
    }
    if (lines.empty()) return;

    // Cap number of lines
    if (static_cast<int>(lines.size()) > kMaxLines) {
        lines.resize(kMaxLines);
        lines.back() += " \xe2\x80\xa6"; // UTF-8 ellipsis
    }

    // Truncate each line to kMaxTooltipW
    float max_line_w = 0.f;
    for (auto& line : lines) {
        while (!line.empty() && tr.text_width(line.c_str()) > kMaxTooltipW)
            line.resize(line.size() - 1);
        max_line_w = std::max(max_line_w, tr.text_width(line.c_str()));
    }

    float pad    = kTooltipPad;
    float line_h = kTooltipLineH;
    float header_w = tr.text_width(T("error_label", "Error:"));
    float popup_w  = std::max(header_w, max_line_w) + pad * 2;
    float popup_h  = line_h * (1 + static_cast<float>(lines.size())) + pad * 2;

    float px = mouse_.x + kTooltipCursorOff;
    float py = mouse_.y + kTooltipCursorOff;
    if (px + popup_w > graph_right()) px = mouse_.x - popup_w - kTooltipClampMargin;
    if (py + popup_h > static_cast<float>(win_h_)) py = mouse_.y - popup_h - kTooltipClampMargin;

    // Shadow + Background
    draw_shadow(tr, px, py, popup_w, popup_h);
    tr.draw_rect(px, py, popup_w, popup_h,
                 style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], kTooltipBgAlpha);
    // Red accent line at top
    tr.draw_rect(px, py, popup_w, kTooltipAccentH, 1.0f, 0.3f, 0.3f, 0.9f);
    // "Error:" header in red
    tr.draw_text(px + pad, py + pad, T("error_label", "Error:"), 1.0f, 0.3f, 0.3f);
    // One line per message line
    for (int k = 0; k < static_cast<int>(lines.size()); ++k) {
        tr.draw_text(px + pad, py + pad + line_h * (1 + k), lines[k].c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
}

void NodeGraphUI::draw_param_tooltip(Renderer2D& tr) {
    if (hovered_label_idx_ < 0) return;
    static constexpr float kParamTooltipDelay = 1.0f;
    if (label_hover_time_ < kParamTooltipDelay) return;
    if (hovered_label_idx_ >= static_cast<int>(label_rects_.size())) return;

    const auto& r = label_rects_[hovered_label_idx_];
    const auto* ns = snap_.find_node(r.node_id);
    if (!ns || !ns->op_info) return;

    std::string desc;
    for (const auto& pi : ns->op_info->params) {
        if (pi.name == r.param_name) { desc = pi.description; break; }
    }
    if (desc.empty()) return;

    static constexpr float kMaxParamTooltipW = 300.0f;

    // Word-wrap into lines
    std::vector<std::string> lines;
    std::string word, line;
    for (size_t ci = 0; ci <= desc.size(); ++ci) {
        char ch = ci < desc.size() ? desc[ci] : ' ';
        if (ch == ' ' || ch == '\n' || ci == desc.size()) {
            if (!word.empty()) {
                std::string test = line.empty() ? word : line + " " + word;
                if (tr.text_width(test.c_str()) > kMaxParamTooltipW && !line.empty()) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = test;
                }
                word.clear();
            }
            if (ch == '\n' && !line.empty()) { lines.push_back(line); line.clear(); }
        } else {
            word += ch;
        }
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) return;

    float max_line_w = 0.0f;
    for (const auto& l : lines)
        max_line_w = std::max(max_line_w, tr.text_width(l.c_str()));

    float pad = kTooltipPad;
    float line_h = kTooltipLineH;
    float popup_w = max_line_w + pad * 2;
    float popup_h = line_h * static_cast<float>(lines.size()) + pad * 2;

    float px = mouse_.x + kTooltipCursorOff;
    float py = mouse_.y + kTooltipCursorOff;
    if (px + popup_w > static_cast<float>(win_w_))
        px = mouse_.x - popup_w - kTooltipClampMargin;
    if (py + popup_h > static_cast<float>(win_h_))
        py = mouse_.y - popup_h - kTooltipClampMargin;

    draw_shadow(tr, px, py, popup_w, popup_h);
    tr.draw_rect(px, py, popup_w, popup_h,
                 style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], kTooltipBgAlpha);
    tr.draw_rect(px, py, popup_w, kTooltipAccentH,
                 style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    for (int k = 0; k < static_cast<int>(lines.size()); ++k) {
        tr.draw_text(px + pad, py + pad + line_h * static_cast<float>(k), lines[k].c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
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
    resolution_rects_.clear();
    cadence_rects_.clear();
    preset_dropdown_rects_.clear();
    preset_save_rects_.clear();
    midi_remove_rects_.clear();
    midi_range_rects_.clear();
    patch_jacks_.clear();
    patch_wires_.clear();
    group_header_rects_.clear();
    state_preset_rects_.clear();
    state_header_rects_.clear();
    lock_badge_rects_.clear();
    label_rects_.clear();
    wire_remap_rects_.clear();
    wire_clamp_rects_.clear();

    // Wire inspector (when a wire is selected and no nodes are)
    if (selected_node_ids_.empty() && wire_inspector_visible()) {
        const auto& c = snap_.connections[selected_wire_idx_];
        float insp_x = inspector_x();
        tr.draw_rect(insp_x, 0, kInspectorW, static_cast<float>(h),
                     style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.95f);
        tr.draw_rect(insp_x, 0, 2, static_cast<float>(h),
                     style_.separator[0], style_.separator[1], style_.separator[2]);

        float px = insp_x + kInspPadX;
        float py = kPerfBarH + 8;

        // Header
        tr.draw_text(px, py, T("wire", "Wire"), style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 1.0f, 1.2f);
        py += 22;
        std::string label = c.from_node + "/" + c.from_port + " \xE2\x86\x92 " + c.to_node + "/" + c.to_port;
        tr.draw_text(px, py, label.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);
        py += 20;
        if (c.invalid) {
            std::string broken = c.invalid_reason.empty() ? "Broken connection" : c.invalid_reason;
            tr.draw_text(px, py, broken.c_str(), 1.0f, 0.45f, 0.38f, 0.85f);
            py += 20;
        }

        // Remap fields
        static const char* field_labels[4] = { "From Min", "From Max", "To Min", "To Max" };
        float vals[4] = { c.from_min, c.from_max, c.to_min, c.to_max };
        float field_w = kInspectorW - kInspPadX * 2 - 80;

        for (int f = 0; f < 4; ++f) {
            py += 4;
            tr.draw_text(px, py + 2, field_labels[f],
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);

            float fx = px + 80;
            float fw = field_w;
            float fh = 18;

            // Background for text field
            bool editing_this = editing_wire_remap_ && edit_wire_remap_field_ == f;
            tr.draw_rect(fx, py, fw, fh,
                         editing_this ? style_.accent[0] * 0.3f : style_.inspector_bg[0] * 0.7f,
                         editing_this ? style_.accent[1] * 0.3f : style_.inspector_bg[1] * 0.7f,
                         editing_this ? style_.accent[2] * 0.3f : style_.inspector_bg[2] * 0.7f, 0.8f);
            tr.draw_rect(fx, py, fw, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
            tr.draw_rect(fx, py + fh - 1, fw, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);

            if (editing_this) {
                tr.draw_text(fx + 4, py + 2, edit_buffer_.c_str(),
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                if (cursor_blink_on()) {
                    int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(edit_buffer_.size())));
                    float cx = fx + 4 + tr.text_width(edit_buffer_.substr(0, cpos).c_str());
                    tr.draw_rect(cx, py + 1, 1.0f, fh - 2,
                                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                }
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.3g", vals[f]);
                tr.draw_text(fx + 4, py + 2, buf,
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            }

            wire_remap_rects_.push_back({fx, py, fw, fh, f});
            py += fh;
        }

        // Clamp checkbox
        py += 8;
        float cb_size = 14;
        tr.draw_rect(px, py, cb_size, cb_size,
                     style_.inspector_bg[0] * 0.7f, style_.inspector_bg[1] * 0.7f, style_.inspector_bg[2] * 0.7f, 0.8f);
        tr.draw_rect(px, py, cb_size, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
        tr.draw_rect(px, py + cb_size - 1, cb_size, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
        if (c.clamp) {
            tr.draw_rect(px + 3, py + 3, cb_size - 6, cb_size - 6,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
        }
        wire_clamp_rects_.push_back({px, py, cb_size, cb_size});
        tr.draw_text(px + cb_size + 6, py + 1, T("clamp", "Clamp"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);

        insp_content_h_ = 0;
        return;
    }

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
                tr.draw_text(px, py, T("node_not_found", "Node not found"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
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

            // Header: both node names with env colors
            const float* clr_a = node_accent_color(node_a->is_gpu, node_a->active_cadence);
            const float* clr_b = node_accent_color(node_b->is_gpu, node_b->active_cadence);
            tr.draw_text(px, py, node_a->op_info->name.c_str(), clr_a[0], clr_a[1], clr_a[2]);
            float name_w = tr.text_width(node_a->op_info->name.c_str());
            tr.draw_text(px + name_w + 4, py, " + ", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            float plus_w = tr.text_width(" + ");
            tr.draw_text(px + name_w + 4 + plus_w, py, node_b->op_info->name.c_str(), clr_b[0], clr_b[1], clr_b[2]);
            py += kLineH;

            tr.draw_text(px, py, T("delete_to_remove", "Delete / Backspace to remove"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            py += kLineH + 8;

            tr.draw_rect(px, py, kInspContentW, 1, style_.separator[0], style_.separator[1], style_.separator[2]);
            py += 8;

            draw_patch_panel(tr, *node_a, *node_b, px, py);

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
            tr.draw_text(px, py, T("delete_to_remove", "Delete / Backspace to remove"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

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
        tr.draw_text(insp_x + kInspPadX, 20, T("node_not_found", "Node not found"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
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

    // Error banner for errored nodes (includes compile errors where errored=false)
    if (!sel_node->error_message.empty()) {
        tr.draw_text(px, py, ("ERROR: " + sel_node->error_message).c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2]);
        py += kLineH + 4;
    }

    bool has_visible_standard_params = false;
    if (sel_node->op_info) {
        for (const auto& pd : sel_node->op_info->params) {
            if (pd.display_hint != VIVID_DISPLAY_HIDDEN) {
                has_visible_standard_params = true;
                break;
            }
        }
    }
    bool has_custom_inspector = sel_node->op_info && sel_node->op_info->has_custom_inspector;

    if (sel_node->op_info && sel_node->op_info->has_custom_inspector) {
        if (sel_node->op_info->inspector_mode == VIVID_INSPECTOR_STANDARD && has_visible_standard_params) {
            draw_section_separator(tr, px, py, kInspContentW, "Controls");
            draw_inspector_params(tr, *sel_node, px, py);
        }
        if (has_custom_inspector) {
            draw_section_separator(tr, px, py, kInspContentW, "Custom");
        }
        draw_custom_inspector(tr, *sel_node, px, py);
    } else {
        if (has_visible_standard_params) {
            draw_section_separator(tr, px, py, kInspContentW, "Controls");
            draw_inspector_params(tr, *sel_node, px, py);
        }
    }
    // --- Technical section ---
    {
        bool has_resolution = sel_node->is_gpu && sel_node->gpu_tex_width > 0;
        bool has_cadence = sel_node->cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE;
        bool has_state_presets = sel_node->param_indices.count("states") > 0;
        bool has_outputs = !sel_node->output_port_indices.empty();
        if (has_resolution || has_cadence || has_state_presets || has_outputs)
            draw_section_separator(tr, px, py, kInspContentW, "Technical");
        draw_inspector_resolution(tr, *sel_node, px, py);
        draw_inspector_cadence(tr, *sel_node, px, py);
        draw_inspector_state_presets(tr, *sel_node, px, py);
        draw_inspector_outputs(tr, *sel_node, px, py);
    }

    // Inspector widget hover highlights
    if (hovered_slider_idx_ >= 0 && hovered_slider_idx_ < static_cast<int>(slider_rects_.size())) {
        const auto& r = slider_rects_[hovered_slider_idx_];
        tr.draw_rect(r.x, r.y, r.w, r.h,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }
    if (hovered_bool_idx_ >= 0 && hovered_bool_idx_ < static_cast<int>(bool_rects_.size())) {
        const auto& r = bool_rects_[hovered_bool_idx_];
        tr.draw_rect(r.x - 2, r.y - 2, r.w + 4, r.h + 4,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }
    if (hovered_dropdown_idx_ >= 0 && hovered_dropdown_idx_ < static_cast<int>(dropdown_rects_.size())) {
        const auto& r = dropdown_rects_[hovered_dropdown_idx_];
        tr.draw_rect(r.x, r.y, r.w, r.h,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }

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
                 style_.scrollbar_track[0], style_.scrollbar_track[1], style_.scrollbar_track[2], kScrollbarTrackAlpha);

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
    float thumb_alpha = (hovered || insp_scrollbar_dragging_) ? kScrollbarThumbHovered : kScrollbarThumbIdle;
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

void NodeGraphUI::draw_core_update_banner(Renderer2D& tr) {
    core_update_button_rects_.clear();
    if (!core_update_notice_open_) return;

    float y = kPerfBarH + (midi_map_mode_ ? kMidiMapBannerH : 0.0f);
    float h = 28.0f;
    float w = static_cast<float>(win_w_);
    if (has_selection()) w = graph_right();

    tr.draw_rect(0.0f, y, w, h, 0.14f, 0.20f, 0.26f, 0.95f);
    tr.draw_rect(0.0f, y, w, 1.0f, 0.28f, 0.46f, 0.58f, 0.8f);

    std::string label = "Core update available: v" + core_update_notice_version_;
    if (!core_update_notice_summary_.empty()) label += " - " + core_update_notice_summary_;
    tr.draw_text(10.0f, y + 6.0f, label.c_str(), 0.86f, 0.92f, 0.98f);

    float bx = w - 12.0f;
    auto draw_btn = [&](const char* text, int action, float r, float g, float b) {
        float bw = tr.text_width(text) + 14.0f;
        bx -= bw;
        tr.draw_rounded_rect(bx, y + 4.0f, bw, 20.0f, 3.0f, r, g, b, 0.85f);
        tr.draw_text(bx + 7.0f, y + 6.0f, text, 0.95f, 0.97f, 1.0f);
        core_update_button_rects_.push_back({bx, y + 4.0f, bw, 20.0f, action});
        bx -= 6.0f;
    };

    draw_btn("Later", 2, 0.26f, 0.30f, 0.34f);
    draw_btn("Skip", 1, 0.33f, 0.25f, 0.23f);
    draw_btn("Install", 0, 0.22f, 0.42f, 0.28f);
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

    // Preset row (if node has user or factory presets)
    if (!node.preset_names.empty() || !node.factory_preset_names.empty()) {
        float save_w = 46.0f;
        float gap = 4.0f;
        float dd_w = kInspContentW - save_w - gap;
        float dd_h = 20.0f;

        // Dropdown background
        tr.draw_rect(px, py, dd_w, dd_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);

        // Label: show leaf segment of active preset path, or "(none)"
        std::string preset_display;
        if (!node.active_preset.empty()) {
            auto slash = node.active_preset.rfind('/');
            preset_display = (slash != std::string::npos)
                ? node.active_preset.substr(slash + 1) : node.active_preset;
        }
        const char* label = preset_display.empty()
            ? "(none)" : preset_display.c_str();
        tr.draw_text(px + 6, py + 3, label,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

        // Dropdown indicator
        tr.draw_text(px + dd_w - 14, py + 3, "\xe2\x96\xbe",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

        // Hit-test rect for dropdown
        preset_dropdown_rects_.push_back({px, py, dd_w, dd_h, node.node_id, ""});

        // Save button
        float save_x = px + dd_w + gap;
        tr.draw_rect(save_x, py, save_w, dd_h,
                     style_.button_bg[0], style_.button_bg[1], style_.button_bg[2]);
        tr.draw_text(save_x + 8, py + 3, T("save", "Save"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        preset_save_rects_.push_back({save_x, py, save_w, dd_h, node.node_id, ""});

        py += dd_h + 6;
    }
}

void NodeGraphUI::draw_inspector_knob(Renderer2D& tr, const NodeSnapshot& node,
                                       InspectorLayout& layout,
                                       const ParamLayoutPlan& plan, uint32_t pi) {
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
    float t = (range > 0) ? (val - pd.min_value) / range : 0.5f;
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
    std::string label_text = truncate_text(tr, pd.name, panel_w - 18.0f, 0.8f);
    float label_w = tr.text_width(label_text.c_str(), 0.8f);
    float label_x = cx - label_w * 0.5f;
    tr.draw_text(label_x, label_y, label_text.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
    label_rects_.push_back({label_x, label_y, label_w, tr.line_height() * 0.85f,
                            node.node_id, pd.name});

    // Lock badge next to knob label
    {
        uint8_t lock = (pi < node.param_lock_flags.size()) ? node.param_lock_flags[pi] : 0;
        float badge_anchor_x = label_x + label_w + 3;
        if (plan.allow_inline_lock_badge && lock != kParamLockNone) {
            const char* lock_text =
                (lock == (kParamLockWires | kParamLockPresets)) ? "WP" :
                (lock & kParamLockWires) ? "W" : "P";
            float badge_w = tr.text_width(lock_text, 0.75f) + 6;
            tr.draw_rect(badge_anchor_x, label_y, badge_w, kMidiBadgeH,
                         0.6f, 0.45f, 0.15f, 0.85f);
            tr.draw_text(badge_anchor_x + 3, label_y, lock_text, 1.0f, 0.85f, 0.4f, 1.0f, 0.75f);
            lock_badge_rects_.push_back({badge_anchor_x, label_y, badge_w, kMidiBadgeH,
                                         node.node_id, pd.name});
        } else {
            lock_badge_rects_.push_back({badge_anchor_x, label_y, 14.0f, kMidiBadgeH,
                                         node.node_id, pd.name});
        }
    }

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

    // Labels below pad (segment-drawn for click-to-edit support)
    float label_y = pad_y + pad_size + kXYPadLabelGap;
    float lh = tr.line_height() * 0.85f;

    bool editing_x = editing_param_ && edit_node_id_ == single_selected_id()
                     && edit_param_name_ == pd_x.name;
    bool editing_y = editing_param_ && edit_node_id_ == single_selected_id()
                     && edit_param_name_ == pd_y.name;

    std::string val_x_str = format_float(val_x, 2);
    std::string val_y_str = format_float(val_y, 2);
    std::string name_x_str = std::string(pd_x.name) + ": ";
    std::string sep_name_y_str = "  " + std::string(pd_y.name) + ": ";

    float name_x_w    = tr.text_width(name_x_str.c_str(), 0.85f);
    float val_x_str_w = tr.text_width(val_x_str.c_str(), 0.85f);
    float name_y_w    = tr.text_width(sep_name_y_str.c_str(), 0.85f);
    float val_y_str_w = tr.text_width(val_y_str.c_str(), 0.85f);

    float label_w = name_x_w + val_x_str_w + name_y_w + val_y_str_w;
    float label_lx = layout.base_x + (content_w - label_w) * 0.5f;

    float cx = label_lx;

    // Segment: "X: "
    tr.draw_text(cx, label_y, name_x_str.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
    cx += name_x_w;

    // Segment: X value (or edit field)
    if (editing_x) {
        float edit_w = std::max(val_x_str_w + 8.0f, pad_size * 0.45f);
        draw_editing_text_field(tr, style_, cx, label_y, edit_w, lh,
                                edit_buffer_, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        cx += edit_w;
    } else {
        tr.draw_text(cx, label_y, val_x_str.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
        value_text_rects_.push_back({cx, label_y, val_x_str_w, lh,
                                     single_selected_id(), pd_x.name});
        cx += val_x_str_w;
    }

    // Segment: "  Y: "
    tr.draw_text(cx, label_y, sep_name_y_str.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
    cx += name_y_w;

    // Segment: Y value (or edit field)
    if (editing_y) {
        float edit_w = std::max(val_y_str_w + 8.0f, pad_size * 0.45f);
        draw_editing_text_field(tr, style_, cx, label_y, edit_w, lh,
                                edit_buffer_, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
    } else {
        tr.draw_text(cx, label_y, val_y_str.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
        value_text_rects_.push_back({cx, label_y, val_y_str_w, lh,
                                     single_selected_id(), pd_y.name});
    }

    // Lock badges for XY params (shown after label)
    {
        float badge_x = label_lx + label_w + 4;
        for (uint32_t pi : {pi_x, pi_y}) {
            uint8_t lock = (pi < node.param_lock_flags.size()) ? node.param_lock_flags[pi] : 0;
            const auto& pd_name = op.params[pi].name;
            if (lock != kParamLockNone) {
                const char* lock_text =
                    (lock == (kParamLockWires | kParamLockPresets)) ? "WP" :
                    (lock & kParamLockWires) ? "W" : "P";
                std::string badge_label = std::string(pd_name) + ":" + lock_text;
                float bw = tr.text_width(badge_label.c_str(), 0.75f) + 6;
                tr.draw_rect(badge_x, label_y, bw, kMidiBadgeH,
                             0.6f, 0.45f, 0.15f, 0.85f);
                tr.draw_text(badge_x + 3, label_y, badge_label.c_str(), 1.0f, 0.85f, 0.4f, 1.0f, 0.75f);
                lock_badge_rects_.push_back({badge_x, label_y, bw, kMidiBadgeH,
                                             node.node_id, pd_name});
                badge_x += bw + 3;
            }
        }
    }

    // Hit-test rect for XY pad drag
    xy_pad_rects_.push_back({pad_x, pad_y, pad_size, pad_size,
                             single_selected_id(), pd_x.name, pd_y.name});

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
    tr.draw_text(px, py, T("color", "color"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
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

    // Hex text
    float cr, cg, cb;
    hsv_to_rgb(color_popup_h_, color_popup_s_, color_popup_v_, cr, cg, cb);
    char hex[8];
    rgb_to_hex(cr, cg, cb, hex, sizeof(hex));

    if (color_editing_hex_) {
        draw_editing_text_field(tr, style_, sv_x, hex_y, hex_w_full, hex_h,
                                color_hex_buffer_, text_edit_, cursor_blink_on());
    } else {
        tr.draw_rect(sv_x, hex_y, hex_w_full, hex_h,
                     style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
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
        if (color_editing_rgb_ == ch) {
            std::string buf = std::string(rgb_labels[ch]) + " " + color_rgb_buffer_;
            draw_editing_text_field(tr, style_, fx, rgb_y, field_w, rgb_h,
                                    buf, text_edit_, cursor_blink_on(), 3.0f, 2.0f);
        } else {
            tr.draw_rect(fx, rgb_y, field_w, rgb_h,
                         style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
            char label[16];
            snprintf(label, sizeof(label), "%s %d", rgb_labels[ch], rgb_values[ch]);
            tr.draw_text(fx + 3, rgb_y + 2, label,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }
}

void NodeGraphUI::draw_one_inspector_param(Renderer2D& tr, const NodeSnapshot& node,
                                           InspectorLayout& layout,
                                           const ParamLayoutPlan& plan, uint32_t pi) {
    const auto& op = *node.op_info;
    float start_y = layout.y;
    float py = layout.y;
    float px = layout.x;
    float panel_w = layout.col_w;
    const auto& pd = op.params[pi];
    float val = node.param_values[pi];
    const std::string semantic_hint = build_semantic_hint(pd);
    const bool has_semantic_hint = !semantic_hint.empty();
    const auto conn = find_param_connection(snap_, node.node_id, pd.name);
    const bool is_connected = conn.connected;
    bool is_editing_this = editing_param_ &&
                           edit_node_id_ == single_selected_id() &&
                           edit_param_name_ == pd.name;
    const auto* midi_mm = snap_.find_midi_mapping(single_selected_id(), pd.name);
    uint8_t lock = (pi < node.param_lock_flags.size()) ? node.param_lock_flags[pi] : 0;
    const bool is_file = pd.type == VIVID_PARAM_FILE;
    const bool is_text = pd.type == VIVID_PARAM_TEXT;
    const bool is_bool = pd.type == VIVID_PARAM_BOOL;
    const bool is_dropdown = pd.choice_count > 0;
    const bool is_numeric = !is_file && !is_text && !is_bool && !is_dropdown;

    if (pd.display_hint == VIVID_DISPLAY_KNOB && (pd.type == VIVID_PARAM_FLOAT || pd.type == VIVID_PARAM_INT)) {
        draw_inspector_knob(tr, node, layout, plan, pi);
        return;
    }

    auto draw_secondary_line = [&](const std::string& text, float scale = 0.62f) {
        std::string display = truncate_text(tr, text, panel_w, scale);
        tr.draw_text(px, py - 1.0f, display.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.72f, scale);
        py += tr.line_height() * scale + 2.0f;
    };

    if (is_connected) {
        const auto* src_ns = snap_.find_node(conn.from_node);
        const float* dot_clr = src_ns ? node_accent_color(src_ns->is_gpu, src_ns->active_cadence) : style_.accent.data();
        float dot_sz = 5.0f;
        float dot_x = px - dot_sz - 2.0f;
        float dot_y = py + (kLineH - dot_sz) * 0.5f;
        tr.draw_rect(dot_x, dot_y, dot_sz, dot_sz, dot_clr[0], dot_clr[1], dot_clr[2], 0.9f);
    }

    float label_scale = plan.compact ? 0.8f : 0.85f;
    float value_scale = plan.compact ? 0.8f : 1.0f;
    bool show_inline_value = is_numeric && plan.allow_inline_value;
    float max_label_w = panel_w * (show_inline_value ? (plan.compact ? 0.62f : 0.52f)
                                                     : (plan.compact ? 0.78f : 0.82f));
    std::string display_label = truncate_text(tr, pd.name, max_label_w, label_scale);
    tr.draw_text(px, py, display_label.c_str(),
                 is_connected ? style_.dim_text[0] : 0.8f,
                 is_connected ? style_.dim_text[1] : 0.82f,
                 is_connected ? style_.dim_text[2] : 0.85f,
                 is_connected ? 0.75f : 1.0f, label_scale);
    label_rects_.push_back({px, py, tr.text_width(display_label.c_str(), label_scale),
                            tr.line_height() * label_scale, node.node_id, pd.name});

    float after_label_x = px + tr.text_width(display_label.c_str(), label_scale) + 6.0f;
    if (plan.allow_inline_midi_badge && midi_mm) {
        std::string badge = "CC " + std::to_string(midi_mm->cc_number);
        float badge_x = after_label_x;
        float badge_w = tr.text_width(badge.c_str(), 0.8f) + 8.0f;
        tr.draw_rect(badge_x, py, badge_w, kMidiBadgeH,
                     kMidiMapBadge[0], kMidiMapBadge[1], kMidiMapBadge[2], kMidiMapBadge[3]);
        tr.draw_text(badge_x + 4, py, badge.c_str(), 0.85f, 0.90f, 1.0f, 1.0f, 0.8f);
        after_label_x = badge_x + badge_w + 4.0f;
    }

    if (plan.allow_inline_lock_badge && lock != kParamLockNone) {
        const char* lock_text =
            (lock == (kParamLockWires | kParamLockPresets)) ? "WP" :
            (lock & kParamLockWires) ? "W" : "P";
        float badge_w = tr.text_width(lock_text, 0.8f) + 8.0f;
        float badge_x = after_label_x;
        tr.draw_rect(badge_x, py, badge_w, kMidiBadgeH,
                     0.6f, 0.45f, 0.15f, 0.85f);
        tr.draw_text(badge_x + 4, py, lock_text, 1.0f, 0.85f, 0.4f, 1.0f, 0.8f);
        lock_badge_rects_.push_back({badge_x, py, badge_w, kMidiBadgeH, node.node_id, pd.name});
    } else {
        lock_badge_rects_.push_back({after_label_x, py, 18.0f, kMidiBadgeH, node.node_id, pd.name});
    }

    if (midi_map_waiting_ && midi_map_node_id_ == single_selected_id() &&
        midi_map_param_name_ == pd.name) {
        float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(perf_frame_counter_) * 0.15f);
        tr.draw_rect(px - 2, py - 2, panel_w + 4, kLineH + 4,
                     0.3f, 0.5f, 0.9f, pulse * 0.6f);
    }

    std::string val_str;
    if (is_bool) {
        val_str = val > 0.5f ? "true" : "false";
    } else if (is_dropdown) {
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

    if (show_inline_value) {
        float max_val_w = panel_w * (plan.compact ? 0.34f : 0.40f);
        std::string display_val = truncate_text(tr, val_str, max_val_w, value_scale);
        float vw = tr.text_width(display_val.c_str(), value_scale);
        float val_x = px + panel_w - vw;
        float val_y = py;

        if (is_editing_this) {
            float edit_w = panel_w * (plan.compact ? 0.38f : 0.4f);
            float edit_x = px + panel_w - edit_w;
            draw_editing_text_field(tr, style_, edit_x, val_y, edit_w, kLineH,
                                    edit_buffer_, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        } else {
            tr.draw_text(val_x, py, display_val.c_str(),
                         is_connected ? style_.dim_text[0] : 0.8f,
                         is_connected ? style_.dim_text[1] : 0.82f,
                         is_connected ? style_.dim_text[2] : 0.85f,
                         is_connected ? 0.75f : 1.0f, value_scale);
            value_text_rects_.push_back({val_x, val_y, vw, kLineH, single_selected_id(), pd.name});
        }
    }

    py += kLineH + 3.0f;

    if (is_file) {
        std::string file_path;
        auto fp_it = node.file_param_values.find(pd.name);
        if (fp_it != node.file_param_values.end())
            file_path = fp_it->second;

        std::string display_name = "Browse\xe2\x80\xa6";
        if (!file_path.empty()) {
            auto slash = file_path.rfind('/');
            display_name = (slash != std::string::npos) ? file_path.substr(slash + 1) : file_path;
        }

        float btn_h = kDropdownH;
        tr.draw_rect(px, py, panel_w, btn_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        tr.draw_text(px + 6, py + 1,
                     truncate_text(tr, display_name, panel_w - 12.0f).c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        file_button_rects_.push_back({px, py, panel_w, btn_h, single_selected_id(), pd.name});
        py += btn_h + 6.0f;
    } else if (is_text) {
        std::string text_value;
        auto sp_it = node.file_param_values.find(pd.name);
        if (sp_it != node.file_param_values.end()) text_value = sp_it->second;
        float field_h = kDropdownH;
        if (is_editing_this) {
            draw_editing_text_field(tr, style_, px, py, panel_w, field_h,
                                    edit_buffer_, text_edit_, cursor_blink_on(), 6.0f, 1.0f);
        } else {
            tr.draw_rect(px, py, panel_w, field_h,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            std::string display = text_value.empty() ? "(empty)" : text_value;
            tr.draw_text(px + 6, py + 1, truncate_text(tr, display, panel_w - 12.0f).c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
        value_text_rects_.push_back({px, py, panel_w, field_h, single_selected_id(), pd.name});
        py += field_h + 6.0f;
    } else if (is_bool) {
        draw_checkbox(tr, style_, px, py, kCheckboxSize, val > 0.5f, is_connected ? 0.3f : 1.0f);
        bool_rects_.push_back({px, py, kCheckboxSize, kCheckboxSize, single_selected_id(), pd.name});
        tr.draw_text(px + kCheckboxSize + 8.0f, py - 1.0f, val_str.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f, 0.8f);
        py += kCheckboxSize + 6.0f;
    } else if (is_dropdown) {
        float dh = kDropdownH;
        tr.draw_rect(px, py, panel_w, dh,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        int idx = static_cast<int>(val);
        const char* choice_label = (idx >= 0 && idx < static_cast<int>(pd.choice_labels.size()))
            ? pd.choice_labels[idx].c_str() : "?";
        std::string display_choice = truncate_text(tr, choice_label, panel_w - 22.0f);
        tr.draw_text(px + 6, py + 1, display_choice.c_str(),
                     is_connected ? style_.dim_text[0] : style_.bright_text[0],
                     is_connected ? style_.dim_text[1] : style_.bright_text[1],
                     is_connected ? style_.dim_text[2] : style_.bright_text[2],
                     is_connected ? 0.75f : 1.0f);
        tr.draw_text(px + panel_w - 16, py + 1, "\xE2\x96\xBE",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        dropdown_rects_.push_back({px, py, panel_w, dh, single_selected_id(), pd.name});
        py += dh + 6.0f;
    } else {
        float sh = kSliderH;
        tr.draw_rect(px, py, panel_w, sh,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        float range = pd.max_value - pd.min_value;
        float t = (range > 0) ? (val - pd.min_value) / range : 0.5f;
        t = std::max(0.0f, std::min(1.0f, t));
        const float* sc = node_accent_color(node.is_gpu, node.active_cadence);
        if (is_connected) {
            tr.draw_rect(px, py, panel_w * t, sh, sc[0], sc[1], sc[2], 0.3f);
            if (!conn.from_node.empty()) {
                auto ni = snap_.node_index.find(conn.from_node);
                if (ni != snap_.node_index.end()) {
                    const auto& src = snap_.nodes[ni->second];
                    auto pi_it = src.output_port_indices.find(conn.from_port);
                    if (pi_it != src.output_port_indices.end() && pi_it->second < src.output_values.size()) {
                        float mod_val = src.output_values[pi_it->second];
                        float mod_t = (range > 0) ? std::clamp((mod_val - pd.min_value) / range, 0.0f, 1.0f) : 0.5f;
                        float t_min = std::min(t, mod_t);
                        float t_max = std::max(t, mod_t);
                        tr.draw_rect(px + panel_w * t_min, py, panel_w * (t_max - t_min), sh,
                                     sc[0], sc[1], sc[2], 0.20f);
                    }
                }
            }
        } else {
            tr.draw_rect(px, py, panel_w * t, sh, sc[0], sc[1], sc[2]);
            float thumb_x = px + panel_w * t - 3.0f;
            tr.draw_rect(thumb_x, py - 2.0f, 6.0f, sh + 4.0f,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        }
        slider_rects_.push_back({px, py - 4.0f, panel_w, sh + 8.0f, single_selected_id(), pd.name});
        py += sh + 10.0f;
    }

    if (plan.allow_secondary_text) {
        if (midi_mm && !plan.allow_inline_midi_badge) {
            draw_secondary_line("CC " + std::to_string(midi_mm->cc_number));
        }
        if (has_semantic_hint && plan.allow_semantic_hint) {
            draw_secondary_line(semantic_hint);
        }
        if (is_connected && plan.allow_connection_source) {
            draw_secondary_line(conn.source_label);
        }
    }

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

        tr.draw_text(px, row_y, T("min", "min"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float min_x = px + 28.0f;
        if (is_editing_min) {
            draw_editing_text_field(tr, style_, min_x, row_y, field_w, kMidiRangeH - 2,
                                    edit_buffer_, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        } else {
            tr.draw_rect(min_x, row_y, field_w, kMidiRangeH - 2,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            tr.draw_text(min_x + 2, row_y, format_float(midi_mm->range_min, 2).c_str(), 0.8f, 0.82f, 0.85f);
        }
        midi_range_rects_.push_back({min_x, row_y, field_w, kMidiRangeH, single_selected_id(), pd.name, true});

        float max_label_x = min_x + field_w + 10.0f;
        tr.draw_text(max_label_x, row_y, T("max", "max"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float max_x = max_label_x + 30.0f;
        if (is_editing_max) {
            draw_editing_text_field(tr, style_, max_x, row_y, field_w, kMidiRangeH - 2,
                                    edit_buffer_, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        } else {
            tr.draw_rect(max_x, row_y, field_w, kMidiRangeH - 2,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            tr.draw_text(max_x + 2, row_y, format_float(midi_mm->range_max, 2).c_str(), 0.8f, 0.82f, 0.85f);
        }
        midi_range_rects_.push_back({max_x, row_y, field_w, kMidiRangeH, single_selected_id(), pd.name, false});

        float remove_x = max_x + field_w + 8.0f;
        tr.draw_rect(remove_x, row_y, 16.0f, kMidiRangeH - 2, 0.5f, 0.2f, 0.2f, 0.8f);
        tr.draw_text(remove_x + 3, row_y, "x", 0.9f, 0.6f, 0.6f);
        midi_remove_rects_.push_back({remove_x, row_y, 16.0f, kMidiRangeH, single_selected_id(), pd.name});

        py += kMidiRangeH + 4.0f;
    }

    layout.end_param(py - start_y);
}

void NodeGraphUI::draw_one_inspector_param_simple(Renderer2D& tr, const NodeSnapshot& node,
                                                  float px, float& py, uint32_t pi) {
    InspectorLayout layout;
    layout.x = px; layout.y = py;
    layout.col_w = kInspContentW; layout.full_w = kInspContentW; layout.base_x = px;
    ParamLayoutPlan plan;
    layout.begin_param(plan);
    draw_one_inspector_param(tr, node, layout, plan, pi);
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
    layout.y = hy + kGroupHeaderH + kGroupHeaderPadBottom;
}

void NodeGraphUI::draw_inspector_params(Renderer2D& tr, const NodeSnapshot& node,
                                        float px, float& py) {
    const auto& op = *node.op_info;

    {
        InspectorLayout layout;
        layout.base_x = px; layout.x = px; layout.y = py;
        layout.full_w = kInspContentW; layout.col_w = kInspContentW;

        std::string current_group;
        bool drew_secondary_controls = false;
        auto build_request = [&](uint32_t param_idx) {
            const auto& rpd = op.params[param_idx];
            ParamLayoutRequest req;
            req.columns = rpd.layout_columns;
            req.col_index = rpd.layout_column_index;
            req.hint = rpd.display_hint;
            req.type = rpd.type;
            req.choice_count = rpd.choice_count;
            req.long_label = tr.text_width(rpd.name.c_str(), 0.85f) > 92.0f;
            req.metadata_heavy =
                snap_.find_midi_mapping(node.node_id, rpd.name) != nullptr ||
                ((param_idx < node.param_lock_flags.size()) ? node.param_lock_flags[param_idx] : 0) != 0 ||
                !build_semantic_hint(rpd).empty() ||
                find_param_connection(snap_, node.node_id, rpd.name).connected;
            return req;
        };
        auto measure_param_clip_height = [&](uint32_t param_idx, const ParamLayoutPlan& plan) {
            const auto& mpd = op.params[param_idx];
            const auto* midi_mm = snap_.find_midi_mapping(node.node_id, mpd.name);
            const bool is_file = mpd.type == VIVID_PARAM_FILE;
            const bool is_text = mpd.type == VIVID_PARAM_TEXT;
            const bool is_bool = mpd.type == VIVID_PARAM_BOOL;
            const bool is_dropdown = mpd.choice_count > 0;
            float total_h = kLineH + 3.0f;
            if (mpd.display_hint == VIVID_DISPLAY_KNOB && (mpd.type == VIVID_PARAM_FLOAT || mpd.type == VIVID_PARAM_INT)) {
                total_h = kKnobDiameter + kKnobLabelGap + tr.line_height() * 0.85f +
                          kKnobValueGap + tr.line_height() * 0.8f + 4.0f;
            } else if (is_file || is_text || is_dropdown) {
                total_h += kDropdownH + 6.0f;
            } else if (is_bool) {
                total_h += kCheckboxSize + 6.0f;
            } else {
                total_h += kSliderH + 10.0f;
            }

            if (plan.allow_secondary_text) {
                auto secondary_line_h = tr.line_height() * 0.62f + 2.0f;
                if (midi_mm && !plan.allow_inline_midi_badge)
                    total_h += secondary_line_h;
                if (plan.allow_semantic_hint && !build_semantic_hint(mpd).empty())
                    total_h += secondary_line_h;
                auto conn = find_param_connection(snap_, node.node_id, mpd.name);
                if (plan.allow_connection_source && conn.connected)
                    total_h += secondary_line_h;
            }

            if (midi_map_mode_ && midi_mm)
                total_h += kMidiRangeH + 4.0f;

            return total_h + 4.0f;
        };

        uint32_t param_count = static_cast<uint32_t>(op.params.size());
        for (uint32_t pi = 0; pi < param_count; ) {
            const auto& pd = op.params[pi];

            if (pd.display_hint == VIVID_DISPLAY_HIDDEN) { ++pi; continue; }

            if (pd.group != current_group) {
                layout.flush_row();
                current_group = pd.group;

                if (!current_group.empty()) {
                    if (!drew_secondary_controls) {
                        draw_section_separator(tr, px, layout.y, kInspContentW, "Secondary Controls");
                        drew_secondary_controls = true;
                    }
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

            ParamLayoutRequest req = build_request(pi);

            // --- Auto-row: consecutive knobs with no explicit column layout ---
            // Scan ahead for a run of knobs that can share a row.
            // Knobs are self-contained (fixed 40px diameter) so metadata_heavy
            // and long_label don't block auto-row — the compact layout
            // suppresses secondary text anyway.
            auto is_auto_row_knob = [&](uint32_t idx) {
                const auto& cpd = op.params[idx];
                if (cpd.display_hint == VIVID_DISPLAY_HIDDEN) return false;
                if (cpd.group != current_group) return false;
                if (cpd.display_hint != VIVID_DISPLAY_KNOB) return false;
                if (cpd.layout_columns != 0) return false;
                if (cpd.type == VIVID_PARAM_BOOL || cpd.type == VIVID_PARAM_FILE ||
                    cpd.type == VIVID_PARAM_TEXT || cpd.choice_count > 0)
                    return false;
                return true;
            };

            if (req.columns == 0 && req.hint == VIVID_DISPLAY_KNOB &&
                is_auto_row_knob(pi)) {
                uint32_t run = 1;
                while (pi + run < param_count && run < 4 &&
                       is_auto_row_knob(pi + run))
                    ++run;

                if (run >= 2) {
                    for (uint32_t k = 0; k < run; ++k) {
                        auto col_plan = InspectorLayout::multi_up_plan(
                            static_cast<uint8_t>(run), static_cast<uint8_t>(k));
                        layout.begin_param(col_plan);
                        tr.push_clip_rect(layout.x, layout.y, layout.col_w,
                                          measure_param_clip_height(pi + k, col_plan));
                        draw_one_inspector_param(tr, node, layout, col_plan, pi + k);
                        tr.pop_clip_rect();
                    }
                    pi += run;
                    continue;
                }
            }

            // --- Explicit multi-column run (columns >= 3, e.g. layout_row(p, 4, k)) ---
            if (req.columns >= 3 && req.col_index == 0) {
                uint8_t target = req.columns;
                ParamLayoutRequest run_reqs[4];
                run_reqs[0] = req;
                uint8_t found = 1;
                for (uint8_t k = 1; k < target && pi + k < param_count; ++k) {
                    const auto& cpd = op.params[pi + k];
                    if (cpd.display_hint == VIVID_DISPLAY_HIDDEN) break;
                    if (cpd.group != current_group) break;
                    run_reqs[k] = build_request(pi + k);
                    ++found;
                }
                if (InspectorLayout::requests_form_multi_up_run(run_reqs, found)) {
                    for (uint8_t k = 0; k < found; ++k) {
                        auto col_plan = InspectorLayout::multi_up_plan(found, k);
                        layout.begin_param(col_plan);
                        tr.push_clip_rect(layout.x, layout.y, layout.col_w,
                                          measure_param_clip_height(pi + k, col_plan));
                        draw_one_inspector_param(tr, node, layout, col_plan, pi + k);
                        tr.pop_clip_rect();
                    }
                    pi += found;
                    continue;
                }
            }

            // --- Two-up pair (columns == 2) ---
            bool drew_pair = false;
            if (pi + 1 < param_count) {
                const auto& next_pd = op.params[pi + 1];
                bool next_is_compound =
                    (next_pd.display_hint == VIVID_DISPLAY_XY_PAD && pi + 2 < param_count &&
                     op.params[pi + 2].display_hint == VIVID_DISPLAY_XY_PAD) ||
                    (next_pd.display_hint == VIVID_DISPLAY_COLOR && pi + 3 < param_count &&
                     op.params[pi + 2].display_hint == VIVID_DISPLAY_COLOR &&
                     op.params[pi + 3].display_hint == VIVID_DISPLAY_COLOR);
                if (next_pd.display_hint != VIVID_DISPLAY_HIDDEN &&
                    next_pd.group == current_group &&
                    !next_is_compound) {
                    ParamLayoutRequest next_req = build_request(pi + 1);
                    if (InspectorLayout::requests_form_two_up_pair(req, next_req)) {
                        ParamLayoutPlan left_plan = InspectorLayout::two_up_plan(0);
                        ParamLayoutPlan right_plan = InspectorLayout::two_up_plan(1);

                        layout.begin_param(left_plan);
                        tr.push_clip_rect(layout.x, layout.y, layout.col_w,
                                          measure_param_clip_height(pi, left_plan));
                        draw_one_inspector_param(tr, node, layout, left_plan, pi);
                        tr.pop_clip_rect();

                        layout.begin_param(right_plan);
                        tr.push_clip_rect(layout.x, layout.y, layout.col_w,
                                          measure_param_clip_height(pi + 1, right_plan));
                        draw_one_inspector_param(tr, node, layout, right_plan, pi + 1);
                        tr.pop_clip_rect();

                        pi += 2;
                        drew_pair = true;
                    }
                }
            }

            if (drew_pair)
                continue;

            ParamLayoutPlan plan = layout.plan_param(req);
            layout.begin_param(plan);
            draw_one_inspector_param(tr, node, layout, plan, pi);
            ++pi;
        }
        layout.flush_row();
        py = layout.y;
    }
}



// ---------------------------------------------------------------------------
// Custom inspector — thunks bridging VividInspectorDrawAPI to Renderer2D
// ---------------------------------------------------------------------------

static void insp_draw_rect(void* o, float x, float y, float w, float h, VividColor c) {
    static_cast<Renderer2D*>(o)->draw_rect(x, y, w, h, c.r, c.g, c.b, c.a);
}
static void insp_draw_rounded_rect(void* o, float x, float y, float w, float h, float radius, VividColor c) {
    static_cast<Renderer2D*>(o)->draw_rounded_rect(x, y, w, h, radius, c.r, c.g, c.b, c.a);
}
static void insp_draw_text(void* o, float x, float y, const char* text, VividColor c, float scale) {
    static_cast<Renderer2D*>(o)->draw_text(x, y, text, c.r, c.g, c.b, c.a, scale);
}
static void insp_draw_line(void* o, float x1, float y1, float x2, float y2, float thickness, VividColor c) {
    static_cast<Renderer2D*>(o)->draw_line(x1, y1, x2, y2, thickness, c.r, c.g, c.b, c.a);
}
static float insp_text_width(void* o, const char* text, float scale) {
    return static_cast<Renderer2D*>(o)->text_width(text, scale);
}
static float insp_line_height(void* o) {
    return static_cast<Renderer2D*>(o)->line_height();
}
static void insp_push_clip_rect(void* o, float x, float y, float w, float h) {
    static_cast<Renderer2D*>(o)->push_clip_rect(x, y, w, h);
}
static void insp_pop_clip_rect(void* o) {
    static_cast<Renderer2D*>(o)->pop_clip_rect();
}

// Command thunk context
struct InspCmdCtx {
    UICommandSink* sink;
    std::string node_id;
};
static void insp_set_param(void* o, const char* p, float v) {
    auto* c = static_cast<InspCmdCtx*>(o);
    c->sink->set_param(c->node_id, p, v);
}
static void insp_set_string_param(void* o, const char* p, const char* v) {
    auto* c = static_cast<InspCmdCtx*>(o);
    c->sink->set_string_param(c->node_id, p, v);
}

void NodeGraphUI::draw_custom_inspector(Renderer2D& tr, const NodeSnapshot& node,
                                        float px, float& py) {
    if (!custom_inspector_cb_) return;

    InspCmdCtx cmd_ctx{&commands_, node.node_id};

    auto to_vc = [](const std::array<float,3>& a, float alpha = 1.0f) -> VividColor {
        return {a[0], a[1], a[2], alpha};
    };

    VividInspectorContext ctx{};
    ctx.content_x = px;
    ctx.content_y = py;
    ctx.content_width = kInspContentW;

    // Draw API
    ctx.draw.opaque = &tr;
    ctx.draw.draw_rect = insp_draw_rect;
    ctx.draw.draw_rounded_rect = insp_draw_rounded_rect;
    ctx.draw.draw_text = insp_draw_text;
    ctx.draw.draw_line = insp_draw_line;
    ctx.draw.text_width = insp_text_width;
    ctx.draw.line_height = insp_line_height;
    ctx.draw.push_clip_rect = insp_push_clip_rect;
    ctx.draw.pop_clip_rect = insp_pop_clip_rect;

    // Command API
    ctx.commands.opaque = &cmd_ctx;
    ctx.commands.set_param = insp_set_param;
    ctx.commands.set_string_param = insp_set_string_param;

    // Theme
    ctx.theme.bg = to_vc(style_.inspector_bg);
    ctx.theme.accent = to_vc(style_.accent);
    ctx.theme.dim_text = to_vc(style_.dim_text);
    ctx.theme.bright_text = to_vc(style_.bright_text);
    ctx.theme.separator = to_vc(style_.separator);
    ctx.theme.dark_bg = to_vc(style_.dark_bg);
    ctx.theme.slider_fill = to_vc(style_.slider_fill);
    ctx.theme.slider_track = to_vc(style_.slider_track);
    ctx.theme.corner_radius = style_.corner_radius;

    // Operator state
    ctx.param_values = node.param_values.data();
    ctx.param_count = static_cast<uint32_t>(node.param_values.size());
    ctx.output_values = node.output_values.data();
    ctx.output_count = static_cast<uint32_t>(node.output_values.size());

    // String param values — build C string array from map
    // Collect file_param_values in param order
    std::vector<const char*> string_ptrs;
    if (node.op_info) {
        for (const auto& pi : node.op_info->params) {
            if (pi.type == VIVID_PARAM_FILE || pi.type == VIVID_PARAM_TEXT) {
                auto it = node.file_param_values.find(pi.name);
                string_ptrs.push_back(it != node.file_param_values.end()
                                      ? it->second.c_str() : "");
            }
        }
    }
    ctx.string_param_values = string_ptrs.empty() ? nullptr : string_ptrs.data();
    ctx.string_param_count = static_cast<uint32_t>(string_ptrs.size());

    // Mouse input — translate to inspector-relative coordinates
    ctx.mouse.x = mouse_.x - px;
    ctx.mouse.y = mouse_.y - py;
    ctx.mouse.prev_x = mouse_.prev_x - px;
    ctx.mouse.prev_y = mouse_.prev_y - py;
    ctx.mouse.left_down = mouse_.left_down ? 1 : 0;
    ctx.mouse.left_clicked = insp_mouse_left_clicked_ ? 1 : 0;
    ctx.mouse.left_released = insp_mouse_left_released_ ? 1 : 0;
    ctx.mouse.right_clicked = insp_mouse_right_clicked_ ? 1 : 0;
    ctx.mouse.shift_down = mouse_.shift_down ? 1 : 0;

    // Key/char events
    ctx.key_events = insp_key_events_.empty() ? nullptr : insp_key_events_.data();
    ctx.key_event_count = static_cast<uint32_t>(insp_key_events_.size());
    ctx.char_events = insp_char_events_.empty() ? nullptr : insp_char_events_.data();
    ctx.char_event_count = static_cast<uint32_t>(insp_char_events_.size());

    ctx.time = 0.0;  // could be wired to glfwGetTime if needed
    ctx.consumed_height = 0.0f;
    ctx.wants_keyboard = 0;

    custom_inspector_cb_(node.node_id, &ctx);

    py += ctx.consumed_height;
    custom_inspector_wants_keyboard_ = ctx.wants_keyboard != 0;

    // Drain event buffers after draw
    insp_key_events_.clear();
    insp_char_events_.clear();
}

void NodeGraphUI::draw_section_separator(Renderer2D& tr, float px, float& py,
                                         float panel_w, const char* label) {
    py += kSectionGapBefore;
    tr.draw_rect(px, py, panel_w, 1,
                 style_.separator[0], style_.separator[1], style_.separator[2]);
    py += 6;
    if (label) {
        tr.draw_text(px, py, label,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2],
                     kSectionLabelScale);
        py += kLineH * kSectionLabelScale + kSectionGapAfter;
    } else {
        py += kSectionGapAfter;
    }
}

void NodeGraphUI::draw_inspector_resolution(Renderer2D& tr, const NodeSnapshot& node,
                                            float px, float& py) {
    if (!node.is_gpu || node.gpu_tex_width == 0 || node.gpu_tex_height == 0)
        return;

    bool is_generator = node.is_generator;
    float panel_w = kInspContentW;

    tr.draw_text(px, py, T("resolution", "Resolution"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    py += kLineH;

    bool editing_w = editing_resolution_ &&
                     edit_res_node_id_ == single_selected_id() && edit_res_is_width_;
    bool editing_h = editing_resolution_ &&
                     edit_res_node_id_ == single_selected_id() && !edit_res_is_width_;

    std::string w_str = editing_w ? edit_buffer_ : format_uint(node.gpu_tex_width);
    std::string h_str = editing_h ? edit_buffer_ : format_uint(node.gpu_tex_height);

    float val_x = px + 4;

    if (is_generator) {
        if (editing_w) {
            tr.draw_rect(val_x, py, kResInputW, kLineH, 0.12f, 0.14f, 0.18f);
        }
        tr.draw_text(val_x, py, w_str.c_str(),
                     editing_w ? 1.0f : 0.8f,
                     editing_w ? 1.0f : 0.82f,
                     editing_w ? 1.0f : 0.85f);
        if (editing_w && cursor_blink_on()) {
            int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(edit_buffer_.size())));
            float cur_x = val_x + tr.text_width(edit_buffer_.substr(0, cpos).c_str());
            tr.draw_rect(cur_x, py, 1.0f, kLineH,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
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
        if (editing_h && cursor_blink_on()) {
            int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(edit_buffer_.size())));
            float cur_x = h_val_x + tr.text_width(edit_buffer_.substr(0, cpos).c_str());
            tr.draw_rect(cur_x, py, 1.0f, kLineH,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
        resolution_rects_.push_back({h_val_x, py, kResInputW, kLineH,
                                     single_selected_id(), false});
    } else {
        tr.draw_text(h_val_x, py, h_str.c_str(), 0.5f, 0.52f, 0.55f);
    }

    if (!is_generator && node.gpu_tex_inherited) {
        float label_x = h_val_x + kResInputW + 4.0f;
        tr.draw_text(label_x, py, T("from_input", "(from input)"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    }

    py += kLineH;
}

void NodeGraphUI::draw_inspector_cadence(Renderer2D& tr, const NodeSnapshot& node,
                                         float px, float& py) {
    if (node.cadence_capability != VIVID_CADENCE_AUDIO_CAPABLE) return;

    tr.draw_text(px, py, T("cadence", "Cadence"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

    // Determine current cadence display
    const char* label;
    if (node.cadence_override == CadenceOverride::Frame) label = "Frame";
    else if (node.cadence_override == CadenceOverride::Audio) label = "Audio";
    else label = (node.active_cadence == Cadence::Audio) ? "Audio (auto)" : "Frame (auto)";

    float val_x = px + 4;
    tr.draw_text(val_x + 80, py, label, 0.8f, 0.82f, 0.85f);

    // Clickable area for cycling
    cadence_rects_.push_back({val_x + 80, py, 100.0f, kLineH, node.node_id});

    py += kLineH;
}

void NodeGraphUI::draw_inspector_state_presets(Renderer2D& tr, const NodeSnapshot& node,
                                               float px, float& py) {
    // Only show for StateMachine nodes (have a "states" param and state_preset_map data or are SM type)
    auto states_it = node.param_indices.find("states");
    if (states_it == node.param_indices.end()) return;
    if (node.state_preset_map.empty() && node.type_name != "StateMachine") return;

    int state_count = static_cast<int>(node.param_values[states_it->second]);
    if (state_count < 1) state_count = 1;
    if (state_count > 8) state_count = 8;

    float panel_w = kInspContentW;

    // Collect all nodes in graph that have presets (excluding this SM node)
    struct PresetNode { std::string node_id; const NodeSnapshot* ns; };
    std::vector<PresetNode> preset_nodes;
    for (const auto& sn : snap_.nodes) {
        if (sn.node_id == node.node_id) continue;
        if (sn.preset_names.empty() && sn.factory_preset_names.empty()) continue;
        preset_nodes.push_back({sn.node_id, &sn});
    }
    if (preset_nodes.empty()) return;

    tr.draw_text(px, py, T("state_presets", "State Presets"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    py += kLineH;

    float dd_h = 20.0f;
    float label_w = panel_w * 0.35f;
    float dd_w = panel_w - label_w - 4.0f;

    for (int si = 0; si < state_count; ++si) {
        auto collapse_key = "__state_preset\t" + std::to_string(si);
        bool collapsed = false;
        auto cit = group_collapsed_.find(collapse_key);
        if (cit != group_collapsed_.end()) collapsed = cit->second;

        // Draw collapsible header
        float hy = py;
        tr.draw_rect(px, hy, panel_w, kGroupHeaderH,
                     style_.group_header_bg[0], style_.group_header_bg[1], style_.group_header_bg[2]);

        float cx = px + 8.0f;
        float cy = hy + kGroupHeaderH * 0.5f;
        float cs = kGroupChevronSize * 0.5f;

        if (collapsed) {
            tr.draw_tri(cx, cy - cs, cx, cy + cs, cx + cs, cy,
                        style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.8f);
        } else {
            tr.draw_tri(cx - cs, cy - cs * 0.5f, cx + cs, cy - cs * 0.5f, cx, cy + cs * 0.5f,
                        style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.8f);
        }

        std::string header_label = "State " + std::to_string(si);
        tr.draw_text(cx + cs + 6.0f, hy + 3.0f, header_label.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.9f);

        state_header_rects_.push_back({px, hy, panel_w, kGroupHeaderH, si});
        py = hy + kGroupHeaderH;

        if (collapsed) continue;

        // Draw rows for each preset-bearing node
        for (const auto& pn : preset_nodes) {
            // Find current mapping for this state+target
            std::string current_preset;
            if (si < static_cast<int>(node.state_preset_map.size())) {
                auto mit = node.state_preset_map[si].find(pn.node_id);
                if (mit != node.state_preset_map[si].end())
                    current_preset = mit->second;
            }

            // Node label
            tr.draw_text(px + 4, py + 3, pn.node_id.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

            // Dropdown
            float dd_x = px + label_w;
            tr.draw_rect(dd_x, py, dd_w, dd_h,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);

            const char* label = current_preset.empty() ? "(none)" : current_preset.c_str();
            tr.draw_text(dd_x + 6, py + 3, label,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

            // Dropdown indicator
            tr.draw_text(dd_x + dd_w - 14, py + 3, "\xe2\x96\xbe",
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

            state_preset_rects_.push_back({dd_x, py, dd_w, dd_h, node.node_id, si, pn.node_id});

            py += dd_h + 2;
        }
        py += 4;
    }
}

void NodeGraphUI::draw_inspector_outputs(Renderer2D& tr, const NodeSnapshot& node,
                                         float px, float& py) {
    float panel_w = kInspContentW;

    tr.draw_text(px, py, T("outputs", "Outputs"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    py += kLineH;

    auto sorted_outs = sorted_ports(node.output_port_indices);

    for (const auto& [idx, name] : sorted_outs) {
        std::string line;
        if (idx < node.output_string_lanes.size() && !node.output_string_lanes[idx].empty()) {
            const auto& sp = node.output_string_lanes[idx];
            line = name + " = [\"" + sp[0] + "\" ..] (" + std::to_string(sp.size()) + ")";
        } else if (idx < node.output_lanes.size() && !node.output_lanes[idx].empty()) {
            line = name + " = [" + std::to_string(node.output_lanes[idx].size()) + " bins]";
        } else if (idx < node.output_string_values.size() && !node.output_string_values[idx].empty()) {
            line = name + " = \"" + node.output_string_values[idx] + "\"";
        } else if (idx < node.output_values.size()) {
            line = name + " = " + format_float(node.output_values[idx]);
        } else {
            line = name + " = ?";
        }
        line = truncate_text(tr, line, panel_w, 0.85f);
        tr.draw_text(px, py, line.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
        py += kLineH;
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
            VividPortType pt = VIVID_PORT_SIGNAL;
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
            result.params.push_back({name, VIVID_PORT_SIGNAL, true, true, true});
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

void NodeGraphUI::draw_box_select(Renderer2D& tr) {
    if (!box_selecting_) return;
    // Convert graph-space rect to screen-space
    float cur_gx = sx_to_gx(mouse_.x);
    float cur_gy = sy_to_gy(mouse_.y);
    float gx0 = std::min(box_start_gx_, cur_gx);
    float gy0 = std::min(box_start_gy_, cur_gy);
    float gx1 = std::max(box_start_gx_, cur_gx);
    float gy1 = std::max(box_start_gy_, cur_gy);
    float sx0 = gx_to_sx(gx0);
    float sy0 = gy_to_sy(gy0);
    float sx1 = gx_to_sx(gx1);
    float sy1 = gy_to_sy(gy1);
    float sw = sx1 - sx0;
    float sh = sy1 - sy0;

    // Highlight nodes inside the selection rectangle
    for (const auto& nr : node_rects_) {
        if (nr.x + nr.w >= gx0 && nr.x <= gx1 &&
            nr.y + nr.h >= gy0 && nr.y <= gy1) {
            float nsx = gx_to_sx(nr.x), nsy = gy_to_sy(nr.y);
            float nsw = g_to_s(nr.w), nsh = g_to_s(nr.h);
            tr.draw_rounded_rect(nsx, nsy, nsw, nsh, g_to_s(style_.corner_radius),
                                 style_.accent[0], style_.accent[1], style_.accent[2], kBoxSelectNodeAlpha);
        }
    }

    // Semi-transparent fill
    tr.draw_rect(sx0, sy0, sw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.12f);
    // Border
    draw_rect_border(tr, sx0, sy0, sw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.6f);
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
    float ch_list_area_h = visible * kChooserItemH;
    int ch_first = std::max(0, static_cast<int>(std::floor(chooser_scroll_ / kChooserItemH)));
    float ch_offset = chooser_scroll_ - ch_first * kChooserItemH;
    int ch_draw_count = std::min(static_cast<int>(chooser_items_.size()) - ch_first, kChooserMaxVisible + 1);

    tr.push_clip_rect(px, iy, kChooserW, ch_list_area_h);
    for (int vi = 0; vi < ch_draw_count; ++vi) {
        int idx = ch_first + vi;
        if (idx >= static_cast<int>(chooser_items_.size())) break;

        float item_y = iy - ch_offset + vi * kChooserItemH;

        // Highlight selected
        if (idx == chooser_sel_) {
            tr.draw_rect(px + 2, item_y, kChooserW - 4, kChooserItemH,
                         style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
        }

        const std::string& name = chooser_items_[idx];
        const std::string subtitle =
            idx < static_cast<int>(chooser_subtitles_.size()) ? chooser_subtitles_[idx] : "";

        if (chooser_mode_ == ChooserMode::Operators && name == "+ New Operator...") {
            // Sentinel: accent-colored text, no env dot
            tr.draw_text(px + 10, item_y + 3, name.c_str(),
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        } else if (chooser_mode_ == ChooserMode::FileDrop) {
            tr.draw_text(px + 10, item_y + 2, name.c_str(), 1.0f, 1.0f, 1.0f);
            if (!subtitle.empty()) {
                tr.draw_text(px + 10, item_y + 16, subtitle.c_str(),
                             style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            }
        } else {
            // Cadence color dot
            const float* dcol = kControlAccent.data(); // default
            auto cat_it = snap_.operator_catalog.find(name);
            if (cat_it != snap_.operator_catalog.end()) {
                dcol = node_accent_color(cat_it->second->is_gpu,
                    cat_it->second->cadence_capability == VIVID_CADENCE_AUDIO_ONLY ? Cadence::Audio : Cadence::Frame);
            }
            float dot_x = px + 10;
            float dot_y = item_y + (kChooserItemH - 6) * 0.5f;
            tr.draw_rect(dot_x, dot_y, 6, 6, dcol[0], dcol[1], dcol[2]);

            // Cadence tag
            const char* tag = "[C]";
            if (cat_it != snap_.operator_catalog.end()) {
                tag = cat_it->second->is_gpu ? "[G]" :
                      cat_it->second->cadence_capability == VIVID_CADENCE_AUDIO_ONLY ? "[A]" : "[C]";
            }
            tr.draw_text(px + 20, item_y + 3, tag, dcol[0], dcol[1], dcol[2]);

            // Type name
            tr.draw_text(px + 42, item_y + 3, name.c_str(), 0.85f, 0.87f, 0.90f);
        }
    }
    tr.pop_clip_rect();

    // Show "no matches" if empty
    if (chooser_items_.empty()) {
        tr.draw_text(px + 8, iy + 3, T("no_matches", "no matches"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    }

    // Scrollbar when items overflow
    int total_items = static_cast<int>(chooser_items_.size());
    if (total_items > kChooserMaxVisible) {
        float track_x = px + kChooserW - kInspScrollbarW - 2.0f;
        float track_y = iy;
        float track_h = visible * kChooserItemH;

        // Track background
        tr.draw_rect(track_x, track_y, kInspScrollbarW, track_h,
                     style_.scrollbar_track[0], style_.scrollbar_track[1], style_.scrollbar_track[2], kScrollbarTrackAlpha);

        // Thumb
        float ratio = static_cast<float>(kChooserMaxVisible) / static_cast<float>(total_items);
        float thumb_h = std::max(kInspScrollbarMinThumb, track_h * ratio);
        float max_scroll_px = std::max(1.0f, (total_items - kChooserMaxVisible) * kChooserItemH);
        float scroll_ratio = chooser_scroll_ / max_scroll_px;
        float thumb_y = track_y + scroll_ratio * (track_h - thumb_h);
        tr.draw_rect(track_x, thumb_y, kInspScrollbarW, thumb_h,
                     style_.scrollbar_thumb[0], style_.scrollbar_thumb[1], style_.scrollbar_thumb[2], kScrollbarThumbIdle);
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
// Sticky notes
// -----------------------------------------------------------------------
void NodeGraphUI::draw_sticky_notes(Renderer2D& tr) {
    sticky_note_rects_.clear();
    sticky_link_rects_.clear();
    for (const auto& sn : snap_.sticky_notes) {
        float sx = gx_to_sx(sn.x);
        float sy = gy_to_sy(sn.y);
        float sw = g_to_s(sn.width);
        float sh = g_to_s(sn.height);

        // Live position override during drag
        int idx = static_cast<int>(&sn - &snap_.sticky_notes[0]);
        if (idx == dragging_sticky_idx_ && mouse_.left_down) {
            float gx = sx_to_gx(mouse_.x) - sticky_drag_offset_x_;
            float gy = sy_to_gy(mouse_.y) - sticky_drag_offset_y_;
            sx = gx_to_sx(gx);
            sy = gy_to_sy(gy);
        }
        // Live size/position override during resize
        if (idx == resizing_sticky_idx_ && mouse_.left_down) {
            float dgx = sx_to_gx(mouse_.x) - sticky_resize_start_gx_;
            float dgy = sy_to_gy(mouse_.y) - sticky_resize_start_gy_;
            float nx = sticky_resize_start_x_, ny = sticky_resize_start_y_;
            float nw = sticky_resize_start_w_, nh = sticky_resize_start_h_;
            if (sticky_resize_edge_ & 2) nw = std::max(kStickyMinW, sticky_resize_start_w_ + dgx);
            if (sticky_resize_edge_ & 1) { nw = std::max(kStickyMinW, sticky_resize_start_w_ - dgx); nx = sticky_resize_start_x_ + (sticky_resize_start_w_ - nw); }
            if (sticky_resize_edge_ & 8) nh = std::max(kStickyMinH, sticky_resize_start_h_ + dgy);
            if (sticky_resize_edge_ & 4) { nh = std::max(kStickyMinH, sticky_resize_start_h_ - dgy); ny = sticky_resize_start_y_ + (sticky_resize_start_h_ - nh); }
            sx = gx_to_sx(nx); sy = gy_to_sy(ny);
            sw = g_to_s(nw);   sh = g_to_s(nh);
        }

        sticky_note_rects_.push_back({sn.id, sx, sy, sw, sh});

        int ci = sn.color;
        if (ci < 0 || ci >= kStickyColorCount) ci = 0;
        float cr = kStickyColors[ci][0];
        float cg = kStickyColors[ci][1];
        float cb = kStickyColors[ci][2];

        // Background
        tr.draw_rounded_rect(sx, sy, sw, sh, kStickyCornerR, cr, cg, cb, 0.85f);

        // Selection border (draw slightly larger rect behind)
        if (sn.id == selected_sticky_id_) {
            tr.draw_rounded_rect(sx - 2, sy - 2, sw + 4, sh + 4, kStickyCornerR + 2,
                                 kAccent[0], kAccent[1], kAccent[2], 0.5f);
            // Re-draw background on top to create border effect
            tr.draw_rounded_rect(sx, sy, sw, sh, kStickyCornerR, cr, cg, cb, 0.85f);
        }

        // Text
        float text_x = sx + g_to_s(kStickyPad);
        float text_y = sy + g_to_s(kStickyPad);
        float text_scale = zoom_ * 0.85f;
        if (text_scale < 0.3f) text_scale = 0.3f;
        float line_h = 14.0f * text_scale;
        float line_spacing = line_h + 2.0f * text_scale;
        float max_text_w = sw - 2.0f * g_to_s(kStickyPad);

        // Measure display width after stripping markdown markers.
        auto plain_width = [&](const std::string& s) -> float {
            std::string out;
            out.reserve(s.size());
            size_t i = 0;
            while (i < s.size()) {
                if (i + 1 < s.size() && s[i] == '*' && s[i+1] == '*') { i += 2; continue; }
                if (s[i] == '[') {
                    size_t mid = s.find("](", i);
                    size_t end = (mid != std::string::npos) ? s.find(')', mid + 2) : std::string::npos;
                    if (end != std::string::npos) {
                        out += s.substr(i + 1, mid - i - 1);
                        i = end + 1; continue;
                    }
                }
                out += s[i++];
            }
            return tr.text_width(out.c_str(), text_scale);
        };

        // Word-wrap a line into visual lines that fit max_w.
        // Returns pairs of (display text, offset into src where this visual line starts).
        struct VisLine { std::string text; size_t src_offset; };
        auto raw_width = [&](const std::string& s) -> float {
            return tr.text_width(s.c_str(), text_scale);
        };

        auto wrap_line_impl = [&](const std::string& src, float max_w,
                                   size_t base_offset,
                                   const std::function<float(const std::string&)>& measure) -> std::vector<VisLine> {
            std::vector<VisLine> out;
            if (max_w <= 0 || src.empty()) { out.push_back({src, base_offset}); return out; }
            std::string cur;
            size_t cur_start = 0;
            size_t i = 0;
            while (i < src.size()) {
                size_t ws = i;
                while (ws < src.size() && src[ws] == ' ') ++ws;
                size_t we = ws;
                while (we < src.size() && src[we] != ' ') ++we;
                std::string token = src.substr(i, we - i);
                std::string test = cur.empty() ? token : cur + token;
                if (measure(test) > max_w && !cur.empty()) {
                    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                    out.push_back({cur, base_offset + cur_start});
                    cur = src.substr(ws, we - ws);
                    cur_start = ws;
                } else {
                    cur = test;
                }
                i = we;
            }
            if (!cur.empty()) {
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                out.push_back({cur, base_offset + cur_start});
            }
            if (out.empty()) out.push_back({"", base_offset});
            return out;
        };

        // Display path: measure with markdown stripped
        auto wrap_line = [&](const std::string& src, float max_w, size_t base_offset = 0) {
            return wrap_line_impl(src, max_w, base_offset, plain_width);
        };
        // Edit path: measure raw text including markdown markers
        auto wrap_line_raw = [&](const std::string& src, float max_w, size_t base_offset = 0) {
            return wrap_line_impl(src, max_w, base_offset, raw_width);
        };

        if (editing_sticky_ && sticky_edit_id_ == sn.id) {
            // Render edit buffer with word-wrap
            const std::string& buf = sticky_edit_buffer_;
            float line_y = text_y;
            size_t cursor_pos = static_cast<size_t>(text_edit_.cursor);
            float cursor_draw_x = 0, cursor_draw_y = 0;
            bool cursor_found = false;
            size_t pos = 0;

            while (pos <= buf.size()) {
                size_t nl = buf.find('\n', pos);
                if (nl == std::string::npos) nl = buf.size();
                std::string line = buf.substr(pos, nl - pos);

                auto vis = wrap_line_raw(line, max_text_w, pos);
                for (size_t vi = 0; vi < vis.size(); ++vi) {
                    if (line_y > sy + sh - g_to_s(kStickyPad)) break;
                    tr.draw_text(text_x, line_y, vis[vi].text.c_str(),
                                 0.1f, 0.1f, 0.1f, 1.0f, text_scale);

                    // Map cursor into this visual line
                    size_t vstart = vis[vi].src_offset;
                    size_t vend = (vi + 1 < vis.size()) ? vis[vi + 1].src_offset : nl;
                    if (!cursor_found && cursor_pos >= vstart && cursor_pos <= vend) {
                        if (cursor_pos == vend && vi + 1 < vis.size()) {
                            // Belongs to next visual line
                        } else {
                            size_t off = cursor_pos - vstart;
                            if (off > vis[vi].text.size()) off = vis[vi].text.size();
                            std::string before = vis[vi].text.substr(0, off);
                            cursor_draw_x = text_x + tr.text_width(before.c_str(), text_scale);
                            cursor_draw_y = line_y;
                            cursor_found = true;
                        }
                    }
                    line_y += line_spacing;
                }

                pos = nl + 1;
                if (pos > buf.size()) break;
                if (line_y > sy + sh - g_to_s(kStickyPad)) break;
            }

            if (!cursor_found) {
                cursor_draw_x = text_x;
                cursor_draw_y = std::max(text_y, line_y - line_spacing);
            }

            if (cursor_blink_on()) {
                tr.draw_rect(cursor_draw_x, cursor_draw_y, 1.0f, line_h,
                             0.1f, 0.1f, 0.1f, 0.8f);
            }
        } else {
            // Render text with lightweight markdown and word-wrap
            const std::string& text = sn.text;
            float line_y = text_y;

            // Render a single visual line with markdown (bold, links, plain).
            auto render_md_line = [&](const std::string& line, float lx) {
                size_t bold_start = line.find("**");
                if (bold_start != std::string::npos) {
                    size_t bold_end = line.find("**", bold_start + 2);
                    if (bold_end != std::string::npos) {
                        std::string before = line.substr(0, bold_start);
                        std::string bold_text = line.substr(bold_start + 2, bold_end - bold_start - 2);
                        std::string after = line.substr(bold_end + 2);
                        if (!before.empty()) {
                            tr.draw_text(lx, line_y, before.c_str(),
                                         0.15f, 0.15f, 0.15f, 1.0f, text_scale);
                            lx += tr.text_width(before.c_str(), text_scale);
                        }
                        tr.draw_text(lx, line_y, bold_text.c_str(),
                                     0.05f, 0.05f, 0.05f, 1.0f, text_scale);
                        lx += tr.text_width(bold_text.c_str(), text_scale);
                        if (!after.empty())
                            tr.draw_text(lx, line_y, after.c_str(),
                                         0.15f, 0.15f, 0.15f, 1.0f, text_scale);
                        return;
                    }
                }
                size_t link_start = line.find('[');
                size_t link_mid = (link_start != std::string::npos) ? line.find("](", link_start) : std::string::npos;
                size_t link_end = (link_mid != std::string::npos) ? line.find(')', link_mid + 2) : std::string::npos;
                if (link_start != std::string::npos && link_end != std::string::npos) {
                    std::string before = line.substr(0, link_start);
                    std::string link_text = line.substr(link_start + 1, link_mid - link_start - 1);
                    std::string after = line.substr(link_end + 1);
                    if (!before.empty()) {
                        tr.draw_text(lx, line_y, before.c_str(),
                                     0.15f, 0.15f, 0.15f, 1.0f, text_scale);
                        lx += tr.text_width(before.c_str(), text_scale);
                    }
                    std::string link_url = line.substr(link_mid + 2, link_end - link_mid - 2);
                    float link_x = lx;
                    float link_w = tr.text_width(link_text.c_str(), text_scale);
                    tr.draw_text(lx, line_y, link_text.c_str(),
                                 kAccent[0] * 0.7f, kAccent[1] * 0.7f, kAccent[2] * 0.9f, 1.0f, text_scale);
                    lx += link_w;
                    sticky_link_rects_.push_back({link_x, line_y, link_w, line_h, std::move(link_url)});
                    if (!after.empty())
                        tr.draw_text(lx, line_y, after.c_str(),
                                     0.15f, 0.15f, 0.15f, 1.0f, text_scale);
                    return;
                }
                tr.draw_text(lx, line_y, line.c_str(),
                             0.15f, 0.15f, 0.15f, 1.0f, text_scale);
            };

            size_t pos = 0;
            while (pos < text.size()) {
                size_t nl = text.find('\n', pos);
                if (nl == std::string::npos) nl = text.size();
                std::string line = text.substr(pos, nl - pos);

                bool is_bullet = false;
                if (line.size() >= 2 && line[0] == '-' && line[1] == ' ') {
                    is_bullet = true;
                    line = line.substr(2);
                }

                float bullet_indent = is_bullet ? 10.0f * text_scale : 0.0f;
                auto wrapped = wrap_line(line, max_text_w - bullet_indent);

                for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                    if (line_y > sy + sh - g_to_s(kStickyPad)) break;
                    float lx = text_x;
                    if (is_bullet && wi == 0)
                        tr.draw_text(lx, line_y, "\xe2\x80\xa2",
                                     0.1f, 0.1f, 0.1f, 1.0f, text_scale);
                    lx += bullet_indent;
                    render_md_line(wrapped[wi].text, lx);
                    line_y += line_spacing;
                }

                pos = nl + 1;
                if (line_y > sy + sh - g_to_s(kStickyPad)) break;
            }
        }

        // Resize grab handles at corners (small squares)
        if (sn.id == selected_sticky_id_) {
            float gs = kStickyResizeGrab;
            float ha = 0.3f;
            tr.draw_rect(sx + sw - gs, sy + sh - gs, gs, gs,
                         0.2f, 0.2f, 0.2f, ha);  // bottom-right
            tr.draw_rect(sx, sy + sh - gs, gs, gs,
                         0.2f, 0.2f, 0.2f, ha);  // bottom-left
            tr.draw_rect(sx + sw - gs, sy, gs, gs,
                         0.2f, 0.2f, 0.2f, ha);  // top-right
        }
    }

    // Sticky note color picker menu
    if (sticky_color_menu_open_) {
        float cmx = sticky_color_menu_x_;
        float cmy = sticky_color_menu_y_;
        float swatch_size = 20.0f;
        float gap = 4.0f;
        float total_w = kStickyColorCount * (swatch_size + gap) - gap + 8.0f;
        float total_h = swatch_size + 8.0f;
        tr.draw_rounded_rect(cmx, cmy, total_w, total_h, 4.0f,
                             0.15f, 0.16f, 0.18f, 0.95f);
        for (int i = 0; i < kStickyColorCount; ++i) {
            float sx2 = cmx + 4.0f + i * (swatch_size + gap);
            float sy2 = cmy + 4.0f;
            tr.draw_rounded_rect(sx2, sy2, swatch_size, swatch_size, 3.0f,
                                 kStickyColors[i][0], kStickyColors[i][1], kStickyColors[i][2], 1.0f);
        }
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

    // One-shot: reposition unconnected output sinks to the right edge of the
    // window.  layout_nodes() may run from update() before draw() has set
    // win_w_/win_h_, so we fix up positions here on the first real frame.
    if (!output_sink_positioned_ && !node_rects_.empty()) {
        output_sink_positioned_ = true;
        reposition_output_sinks();
    }

    // Semi-transparent scrim so wires are visible over the visualization
    tr.draw_rect(0, 0, static_cast<float>(w), static_cast<float>(h), 0.05f, 0.06f, 0.07f, 0.55f);

    draw_grid(tr);
    draw_sticky_notes(tr);
    draw_graph(tr);
    draw_connections(tr);
    draw_preview_wire(tr);
    draw_box_select(tr);
    draw_wire_tooltip(tr);
    draw_session_grid(tr);
    draw_perf_bar(tr);
    draw_midi_map_banner(tr);
    draw_core_update_banner(tr);
}

// -----------------------------------------------------------------------
// Overlays — rendered in a separate pass after GPU thumbnails so that
// popups (context menu, dropdown) appear on top of everything.
// -----------------------------------------------------------------------
void NodeGraphUI::draw_overlays(Renderer2D& tr) {
    // Perf bar expanded panels
    if (perf_mem_hovered_) draw_perf_expanded(tr);
    if (perf_audio_hovered_) draw_perf_audio_expanded(tr);

    // Inspector — drawn in overlay pass so it paints over GPU thumbnails
    draw_inspector(tr, win_w_, win_h_);

    // Error tooltip — drawn after inspector so it appears above GPU thumbnails
    draw_node_error_tooltip(tr);

    // Param description tooltip — shown after hovering a param label for ~1s
    draw_param_tooltip(tr);

    // Operator chooser — drawn here (overlay pass) so it appears above GPU thumbnails
    draw_chooser(tr);

    // Parameter picker popup
    draw_param_picker(tr);

    // Stash renderer ref for input-time submenu width calculations
    dropdown_tr_ = &tr;

    // Dropdown popup
    if (dropdown_open_ && !dropdown_labels_.empty()) {
        float item_h = kDropdownItemH;

        // Preset dropdowns use hierarchical submenu rendering
        if ((dropdown_is_preset_ || dropdown_is_state_preset_) && !dropdown_submenu_stack_.empty()) {
            for (size_t lvl = 0; lvl < dropdown_submenu_stack_.size(); ++lvl) {
                auto& level = dropdown_submenu_stack_[lvl];
                if (!level.items || level.items->empty()) continue;
                int count = static_cast<int>(level.items->size());
                float popup_h = count * item_h + 4;
                // Auto-size width from content
                float auto_w = level.w;
                for (const auto& n : *level.items) {
                    float tw = tr.text_width(n.label.c_str()) + 24.0f;
                    if (n.is_folder) tw += 12.0f;
                    if (tw > auto_w) auto_w = tw;
                }
                level.w = auto_w;
                float lw = level.w;
                float lx = level.x;
                float ly = level.y;
                draw_popup_bg(tr, style_, lx, ly, lw, popup_h);

                for (int i = 0; i < count; ++i) {
                    const auto& node = (*level.items)[i];
                    float iy = ly + 2 + i * item_h;

                    // Highlight hovered item
                    if (i == level.hovered_idx) {
                        tr.draw_rect(lx + 2, iy, lw - 4, item_h,
                                     style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
                    }

                    // Color: folders use bright, factory leaves use dim, user leaves use bright
                    float cr, cg, cb;
                    if (node.is_folder) {
                        cr = style_.bright_text[0]; cg = style_.bright_text[1]; cb = style_.bright_text[2];
                    } else if (node.is_factory) {
                        cr = style_.dim_text[0]; cg = style_.dim_text[1]; cb = style_.dim_text[2];
                    } else {
                        cr = style_.bright_text[0]; cg = style_.bright_text[1]; cb = style_.bright_text[2];
                    }

                    tr.draw_text(lx + 8, iy + 2, node.label.c_str(), cr, cg, cb);

                    // Folder indicator arrow on right side
                    if (node.is_folder) {
                        tr.draw_text(lx + lw - 16, iy + 2, ">",
                                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
                    }
                }
            }
        } else {
            // Non-preset flat dropdown (param selectors, etc.)
            float popup_h = dropdown_labels_.size() * item_h + 4;
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
    }

    // Record codec dropdown
    if (record_dropdown_open_) {
        static const char* codec_labels[] = { "H.264", "H.265", "ProRes 4444" };
        constexpr int codec_count = 3;
        float item_h = kDropdownItemH;
        float popup_h = codec_count * item_h + 4;
        float popup_w = kPerfCodecDropW;
        float dx = record_dropdown_x_;
        float dy = record_dropdown_y_;
        draw_popup_bg(tr, style_, dx, dy, popup_w, popup_h);
        for (int i = 0; i < codec_count; ++i) {
            float iy = dy + 2 + i * item_h;
            bool hovered = mouse_.x >= dx && mouse_.x <= dx + popup_w &&
                           mouse_.y >= iy && mouse_.y <= iy + item_h;
            if (i == record_codec_sel_ || hovered) {
                tr.draw_rect(dx + 2, iy, popup_w - 4, item_h,
                             style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2],
                             hovered ? 0.9f : 0.5f);
            }
            tr.draw_text(dx + 8, iy + 2, codec_labels[i],
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
        if (context_bg_menu_)
            item_count = 2;  // "Re-layout All" + "Add Sticky Note"
        // Solo item for node context menus
        bool show_solo = !context_node_id_.empty() && !context_bg_menu_;
        if (show_solo) item_count++;

        float menu_h = kCtxMenuPadTop + item_count * kCtxMenuItemH + 2.0f;
        float mx = context_menu_x_, my = context_menu_y_;

        // Background
        draw_popup_bg(tr, style_, mx, my, kCtxMenuW, menu_h);

        // Item labels
        std::string delete_label;
        const char* labels[5];
        int label_idx = 0;
        bool is_sticky_ctx = (context_node_id_ == "__sticky__");
        if (is_sticky_ctx) {
            item_count = 2;
            menu_h = kCtxMenuPadTop + item_count * kCtxMenuItemH + 2.0f;
        }
        if (context_bg_menu_) {
            labels[label_idx++] = "Re-layout All";
            labels[label_idx++] = "Add Sticky Note";
        } else if (is_sticky_ctx) {
            labels[label_idx++] = "Delete Note";
            labels[label_idx++] = "Change Color";
        } else if (!context_node_id_.empty()) {
            if (selected_node_ids_.count(context_node_id_) && selected_node_ids_.size() > 1) {
                delete_label = "Delete " + std::to_string(selected_node_ids_.size()) + " Nodes";
                labels[label_idx++] = delete_label.c_str();
            } else {
                labels[label_idx++] = "Delete Node";
            }
            if (context_node_has_shader_)
                labels[label_idx++] = "Clone & Edit";
            // Solo/Unsolo
            bool is_soloed = (!snap_.solo_node_id.empty() && snap_.solo_node_id == context_node_id_);
            labels[label_idx++] = is_soloed ? "Unsolo" : "Solo";
        } else {
            labels[label_idx++] = "Delete Wire";
            labels[label_idx++] = "Insert Node";
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
    draw_save_confirm(tr);
    draw_clone_confirm(tr);
    draw_create_popup(tr);
    draw_preset_name_popup(tr);
    draw_preferences(tr);
    draw_package_browser(tr);
    draw_example_browser(tr);
    draw_graph_meta_editor(tr);
    draw_about(tr);
    draw_mcp_setup_dialog(tr);
}

// -----------------------------------------------------------------------
// Save confirmation dialog (unsaved changes before New / New Project)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_save_confirm(Renderer2D& tr) {
    if (!save_confirm_open_) return;

    // Scrim over entire window
    tr.draw_rect(0, 0, static_cast<float>(win_w_), static_cast<float>(win_h_),
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

    // Dialog panel (centered)
    float dw = 360.0f, dh = 90.0f;
    float dx = (static_cast<float>(win_w_) - dw) * 0.5f;
    float dy = (static_cast<float>(win_h_) - dh) * 0.5f;

    // Shadow + Background
    draw_shadow(tr, dx, dy, dw, dh, style_.corner_radius);
    tr.draw_rounded_rect(dx, dy, dw, dh, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    // Accent bar at top
    tr.draw_rect(dx, dy, dw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    // Label text
    tr.draw_text(dx + 12, dy + 12, T("save_changes_prompt", "Save changes before closing?"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    // Three buttons: Cancel | Don't Save | Save
    float btn_w = 80.0f, btn_h = 22.0f;
    float btn_y = dy + dh - btn_h - 8.0f;
    float total_btn_w = btn_w * 3 + 12.0f * 2;
    float btn_start_x = dx + (dw - total_btn_w) * 0.5f;
    float cancel_x = btn_start_x;
    float dont_save_x = btn_start_x + btn_w + 12.0f;
    float save_x = btn_start_x + (btn_w + 12.0f) * 2;

    // Cancel button
    bool cancel_hover = mouse_.x >= cancel_x && mouse_.x <= cancel_x + btn_w &&
                        mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h;
    tr.draw_rect(cancel_x, btn_y, btn_w, btn_h,
                 cancel_hover ? style_.button_hover[0] : style_.button_bg[0],
                 cancel_hover ? style_.button_hover[1] : style_.button_bg[1],
                 cancel_hover ? style_.button_hover[2] : style_.button_bg[2], 0.9f);
    tr.draw_text(cancel_x + 16, btn_y + 3, T("cancel", "Cancel"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

    // Don't Save button
    bool dont_hover = mouse_.x >= dont_save_x && mouse_.x <= dont_save_x + btn_w &&
                      mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h;
    tr.draw_rect(dont_save_x, btn_y, btn_w, btn_h,
                 dont_hover ? style_.button_hover[0] : style_.button_bg[0],
                 dont_hover ? style_.button_hover[1] : style_.button_bg[1],
                 dont_hover ? style_.button_hover[2] : style_.button_bg[2], 0.9f);
    tr.draw_text(dont_save_x + 4, btn_y + 3, T("dont_save", "Don't Save"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

    // Save button (accent)
    bool save_hover = mouse_.x >= save_x && mouse_.x <= save_x + btn_w &&
                      mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h;
    if (save_hover)
        tr.draw_rect(save_x, btn_y, btn_w, btn_h,
                     style_.accent[0], style_.accent[1], style_.accent[2], 1.0f);
    else
        tr.draw_rect(save_x, btn_y, btn_w, btn_h,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
    tr.draw_text(save_x + 24, btn_y + 3, T("save", "Save"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
}

// -----------------------------------------------------------------------
// Clone confirmation dialog
// -----------------------------------------------------------------------
void NodeGraphUI::draw_clone_confirm(Renderer2D& tr) {
    if (!clone_confirm_open_) return;

    // Scrim over entire window
    tr.draw_rect(0, 0, static_cast<float>(win_w_), static_cast<float>(win_h_),
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

    // Dialog panel (centered)
    float dw = 360.0f, dh = 108.0f;
    float dx = (static_cast<float>(win_w_) - dw) * 0.5f;
    float dy = (static_cast<float>(win_h_) - dh) * 0.5f;

    // Shadow + Background
    draw_shadow(tr, dx, dy, dw, dh, style_.corner_radius);
    tr.draw_rounded_rect(dx, dy, dw, dh, style_.corner_radius, style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    // Accent bar at top
    tr.draw_rect(dx, dy, dw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    // Label text
    std::string label = "Clone " + clone_confirm_type_ + " for editing";
    tr.draw_text(dx + 12, dy + 10, label.c_str(), style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    float toggle_x = dx + 12.0f;
    float toggle_y = dy + 38.0f;
    float toggle_w = dw - 24.0f;
    float toggle_h = 24.0f;
    float left_w = toggle_w * 0.5f;

    float border = 0.6f;
    tr.draw_rect(toggle_x, toggle_y, toggle_w, toggle_h,
                 style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.8f);
    tr.draw_rect(toggle_x, toggle_y, toggle_w, 1.0f,
                 style_.separator[0], style_.separator[1], style_.separator[2], border);
    tr.draw_rect(toggle_x, toggle_y + toggle_h - 1.0f, toggle_w, 1.0f,
                 style_.separator[0], style_.separator[1], style_.separator[2], border);
    tr.draw_rect(toggle_x, toggle_y, 1.0f, toggle_h,
                 style_.separator[0], style_.separator[1], style_.separator[2], border);
    tr.draw_rect(toggle_x + toggle_w - 1.0f, toggle_y, 1.0f, toggle_h,
                 style_.separator[0], style_.separator[1], style_.separator[2], border);
    tr.draw_rect(toggle_x + left_w - 0.5f, toggle_y, 1.0f, toggle_h,
                 style_.separator[0], style_.separator[1], style_.separator[2], border);

    if (clone_confirm_project_available_ && clone_confirm_destination_ == 0) {
        tr.draw_rect(toggle_x + 1.0f, toggle_y + 1.0f, left_w - 2.0f, toggle_h - 2.0f,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
    }
    if (clone_confirm_destination_ == 1) {
        tr.draw_rect(toggle_x + left_w + 1.0f, toggle_y + 1.0f, left_w - 2.0f, toggle_h - 2.0f,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
    }

    tr.push_clip_rect(toggle_x, toggle_y, left_w, toggle_h);
    if (clone_confirm_project_available_) {
        tr.draw_text(toggle_x + 12.0f, toggle_y + 4.0f, T("project_package", "Project Package"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    } else {
        tr.draw_text(toggle_x + 12.0f, toggle_y + 4.0f, T("project_package_unavailable", "Project Package (unavailable)"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    }
    tr.pop_clip_rect();
    tr.push_clip_rect(toggle_x + left_w, toggle_y, left_w, toggle_h);
    tr.draw_text(toggle_x + left_w + 12.0f, toggle_y + 4.0f, T("core", "Core"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    tr.pop_clip_rect();

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
    tr.draw_text(clone_x + 16, btn_y + 3, T("clone", "Clone"), style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    // Cancel button
    bool cancel_hover = mouse_.x >= cancel_x && mouse_.x <= cancel_x + btn_w &&
                        mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h;
    if (cancel_hover)
        tr.draw_rect(cancel_x, btn_y, btn_w, btn_h, style_.button_hover[0], style_.button_hover[1], style_.button_hover[2], 0.9f);
    else
        tr.draw_rect(cancel_x, btn_y, btn_w, btn_h, style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    tr.draw_text(cancel_x + 13, btn_y + 3, T("cancel", "Cancel"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
}

// -----------------------------------------------------------------------
// Create operator modal
// -----------------------------------------------------------------------
void NodeGraphUI::draw_create_popup(Renderer2D& tr) {
    if (!create_popup_open_) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);
    bool blink_on = (static_cast<int>(perf_frame_counter_ / 30) % 2 == 0);
    bool show_composite = (create_env_sel_ == 0);

    auto layout = compute_create_operator_layout(win_w_, win_h_, show_composite);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

    // Shadow + Panel
    draw_shadow(tr, layout.px, layout.py, layout.pw, layout.ph, style_.corner_radius);
    tr.draw_rounded_rect(layout.px, layout.py, layout.pw, layout.ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(layout.px, layout.py, layout.pw, 2,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = layout.cx;
    float inner_w = layout.inner_w;
    float cy = layout.py + kCreateModalPadY;

    // 1. Title
    tr.draw_text(cx, cy, T("new_operator", "New Starter Operator"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    cy += 24.0f;

    // 2. Env selector buttons
    const char* env_labels[] = { "control", "audio", "gpu" };
    const std::array<float, 3>* env_colors[] = { &kControlAccent, &kAudioAccent, &kGpuAccent };
    float btn_gap = 8.0f;
    float total_btn_w = 3 * kCreateEnvBtnW + 2 * btn_gap;
    float bx = layout.px + (layout.pw - total_btn_w) * 0.5f;

    for (int i = 0; i < 3; ++i) {
        float btn_x = bx + i * (kCreateEnvBtnW + btn_gap);
        const auto& dc = *env_colors[i];
        if (i == create_env_sel_) {
            tr.draw_rect(btn_x, cy, kCreateEnvBtnW, kCreateEnvBtnH,
                         dc[0], dc[1], dc[2], 0.9f);
            tr.draw_text(btn_x + 8, cy + 3, env_labels[i], 0.0f, 0.0f, 0.0f);
        } else {
            tr.draw_rect(btn_x, cy, kCreateEnvBtnW, kCreateEnvBtnH,
                         style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
            tr.draw_text(btn_x + 8, cy + 3, env_labels[i], dc[0], dc[1], dc[2]);
        }
    }
    cy += kCreateEnvBtnH + 10.0f;

    // 3. Composite checkbox (control env only)
    if (show_composite) {
        draw_checkbox(tr, style_, cx, cy + 2, 16.0f, create_composite_);
        tr.draw_text(cx + 22, cy + 2, T("composite_template", "Composite (ChildOp template)"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        cy += 24.0f + kCreateModalRowGap;
    }

    // 4. Name field
    draw_editing_text_field(tr, style_, cx, cy, inner_w, 22.0f,
                           create_name_buf_, text_edit_, blink_on);
    if (create_name_buf_.empty()) {
        tr.draw_text(cx + 4, cy + 2, "operator_name",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
    }
    cy += kCreateModalFieldH + kCreateModalRowGap;

    // 5. MCP hint
    cy += kCreateModalSectionGap;
    tr.draw_text(cx, cy, "Use MCP opdev tools for custom ports and parameters",
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.6f);
    cy += 18.0f + kCreateModalRowGap;

    // 6. Destination radio buttons
    cy += kCreateModalSectionGap;
    const char* dest_labels[] = { "Auto", "Project", "Core" };
    float dest_x = cx;
    bool project_available = commands_.has_project_clone_destination();
    for (int i = 0; i < 3; ++i) {
        bool selected = (create_destination_ == i);
        bool disabled = (i == 1 && !project_available);
        float dw = tr.text_width(dest_labels[i]) + 24.0f;
        // Radio circle
        float circle_x = dest_x + 2;
        float circle_y = cy + 4;
        tr.draw_rect(circle_x, circle_y, 12.0f, 12.0f,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        if (selected) {
            tr.draw_rect(circle_x + 3, circle_y + 3, 6.0f, 6.0f,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        }
        float alpha = disabled ? 0.3f : 1.0f;
        tr.draw_text(dest_x + 18, cy + 2, dest_labels[i],
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], alpha);
        dest_x += dw + 12.0f;
    }
    cy += 22.0f + kCreateModalRowGap;

    // 7. Error area
    if (!create_error_.empty()) {
        tr.draw_text(cx, cy, create_error_.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.9f);
    }
    cy += 18.0f + kCreateModalRowGap;

    // 8. Button row: [Create Empty] left, [Create] [Cancel] right
    float btn_y = cy;
    // Create Empty (left-aligned)
    tr.draw_rect(cx, btn_y, kCreateModalBtnW, kCreateModalBtnH,
                 style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    tr.draw_text(cx + 8, btn_y + 5, T("create_empty", "Create Empty"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

    // Cancel (right-aligned)
    float cancel_x = cx + inner_w - kCreateModalBtnW;
    tr.draw_rect(cancel_x, btn_y, kCreateModalBtnW, kCreateModalBtnH,
                 style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    tr.draw_text(cancel_x + 28, btn_y + 5, T("cancel", "Cancel"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

    // Create (right of Create Empty... actually to the left of Cancel)
    float create_x = cancel_x - kCreateModalBtnW - 8.0f;
    tr.draw_rect(create_x, btn_y, kCreateModalBtnW, kCreateModalBtnH,
                 style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    tr.draw_text(create_x + 28, btn_y + 5, T("create", "Create"), 0.0f, 0.0f, 0.0f);
}

// -----------------------------------------------------------------------
// Preset name popup (Save with no active preset)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_preset_name_popup(Renderer2D& tr) {
    if (!preset_name_popup_open_) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

    float pw = 280.0f, ph = 70.0f;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    draw_shadow(tr, px, py, pw, ph, style_.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = px + 16.0f;
    float cy = py + 12.0f;

    tr.draw_text(cx, cy, T("save_preset", "Save Preset"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    cy += 20.0f;

    // Text field
    float field_w = pw - 32.0f;
    tr.draw_rect(cx, cy, field_w, 22.0f,
                 style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
    tr.draw_rect(cx, cy, field_w, 1,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    if (preset_name_buffer_.empty()) {
        tr.draw_text(cx + 4, cy + 3, "Folder/Name",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
    } else {
        tr.draw_text(cx + 4, cy + 3, preset_name_buffer_.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
    if (cursor_blink_on()) {
        int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(preset_name_buffer_.size())));
        float cur_x = cx + 4 + tr.text_width(preset_name_buffer_.substr(0, cpos).c_str());
        tr.draw_rect(cur_x, cy + 1, 1.0f, 20.0f,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
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
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

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
        + kPrefsRowH                              // "MOUSE" section header
        + 3 * kPrefsRowH                          // pan gesture radio items
        + kPrefsSectionGap
        + kPrefsBtnH                              // buttons
        + kPrefsPadY;

    float pw = kPrefsW;
    float ph = content_h;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    // Shadow + Panel background
    draw_shadow(tr, px, py, pw, ph, style_.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    // Accent bar
    tr.draw_rect(px, py, pw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = px + kPrefsPadX;
    float cy = py + kPrefsPadY;
    float inner_w = pw - 2 * kPrefsPadX;

    // Title
    tr.draw_text(cx, cy, T("preferences", "Preferences"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    cy += kPrefsRowH + kPrefsSectionGap;

    // --- TEXT EDITOR section ---
    tr.draw_text(cx, cy, T("text_editor", "TEXT EDITOR"),
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
        if (display.empty() && !prefs_editing_custom_) display = "/usr/local/bin/code {file}";
        float text_alpha = prefs_custom_command_.empty() && !prefs_editing_custom_ ? 0.4f : 1.0f;
        tr.draw_text(cx + 22, cy + 2, display.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], text_alpha);
        if (prefs_editing_custom_ && cursor_blink_on()) {
            int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(prefs_custom_command_.size())));
            float cur_x = cx + 22 + tr.text_width(prefs_custom_command_.substr(0, cpos).c_str());
            tr.draw_rect(cur_x, cy + 1, 1.0f, kPrefsRowH - 4,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
        cy += kPrefsRowH + 2;
    }

    cy += kPrefsSectionGap;

    // --- STYLE section ---
    tr.draw_text(cx, cy, T("style", "STYLE"),
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

    // --- MOUSE section ---
    tr.draw_text(cx, cy, T("mouse", "MOUSE"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    cy += kPrefsRowH;

    const char* pan_labels[] = { "Middle drag", "Left drag (empty canvas)", "Right drag" };
    for (int i = 0; i < 3; ++i) {
        bool sel = (i == prefs_pan_gesture_sel_);
        float radio_x = cx + 2;
        float radio_y = cy + kPrefsRowH * 0.5f - 5;
        tr.draw_rect(radio_x, radio_y, 10, 10,
                     style_.separator[0], style_.separator[1], style_.separator[2]);
        if (sel) {
            tr.draw_rect(radio_x + 2, radio_y + 2, 6, 6,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        }
        tr.draw_text(cx + 18, cy + 1, pan_labels[i],
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
    float save_tw = tr.text_width(T("save", "Save"));
    tr.draw_text(save_x + (kPrefsBtnW - save_tw) * 0.5f, cy + 4, T("save", "Save"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    if (cancel_hover)
        tr.draw_rounded_rect(cancel_x, cy, kPrefsBtnW, kPrefsBtnH, style_.corner_radius,
                             style_.button_hover[0], style_.button_hover[1], style_.button_hover[2], 0.9f);
    else
        tr.draw_rounded_rect(cancel_x, cy, kPrefsBtnW, kPrefsBtnH, style_.corner_radius,
                             style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.9f);
    float cancel_tw = tr.text_width(T("cancel", "Cancel"));
    tr.draw_text(cancel_x + (kPrefsBtnW - cancel_tw) * 0.5f, cy + 4, T("cancel", "Cancel"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
}

// -----------------------------------------------------------------------
// MCP project config detection
// -----------------------------------------------------------------------
void NodeGraphUI::scan_mcp_project_config() {
    const std::string& gpath = graph_meta_data_.path;
    if (mcp_project_config_.scanned && mcp_project_config_.scanned_for_path == gpath)
        return;

    mcp_project_config_.scanned = true;
    mcp_project_config_.scanned_for_path = gpath;
    mcp_project_config_.vivid_configured = false;
    mcp_project_config_.opdev_configured = false;
    mcp_project_config_.mcp_json_dir.clear();

    if (gpath.empty()) return;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::path(gpath).parent_path();

    for (int i = 0; i < 5; ++i) {
        fs::path candidate = dir / ".mcp.json";
        if (fs::exists(candidate, ec)) {
            try {
                std::ifstream ifs(candidate);
                auto j = nlohmann::json::parse(ifs);
                if (j.is_object() && j.contains("mcpServers")) {
                    auto& servers = j["mcpServers"];
                    if (servers.contains("vivid"))
                        mcp_project_config_.vivid_configured = true;
                    if (servers.contains("opdev"))
                        mcp_project_config_.opdev_configured = true;
                }
                mcp_project_config_.mcp_json_dir = dir.string();
            } catch (...) {}
            break;  // stop at first .mcp.json found
        }
        fs::path parent = dir.parent_path();
        if (parent == dir) break;  // reached root
        dir = parent;
    }
}

// -----------------------------------------------------------------------
// MCP setup dialog
// -----------------------------------------------------------------------
void NodeGraphUI::draw_mcp_setup_dialog(Renderer2D& tr) {
    if (!mcp_setup_open_) return;

    scan_mcp_project_config();

    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    // Scrim
    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

    constexpr float kDlgW      = 480.0f;
    constexpr float kDlgPadX   = 20.0f;
    constexpr float kDlgPadY   = 16.0f;
    constexpr float kRowH      = 20.0f;
    constexpr float kSep       = 10.0f;
    constexpr float kBtnH      = 24.0f;
    constexpr float kBtnW      = 72.0f;
    constexpr float kCopyBtnW  = 48.0f;
    constexpr float kCodePad   = 6.0f;

    float inner_w = kDlgW - 2 * kDlgPadX;
    float code_text_w = inner_w - kCopyBtnW - 6.0f - 2 * kCodePad;

    // Build JSON snippets
    std::string mcp_py_path = mcp_dir_.empty() ? "<path_to_vivid>/mcp/vivid_mcp.py"
                                                : mcp_dir_ + "/vivid_mcp.py";
    std::string opdev_py_path = mcp_dir_.empty() ? "<path_to_vivid>/mcp/vivid_opdev_mcp.py"
                                                 : mcp_dir_ + "/vivid_opdev_mcp.py";

    const std::string vivid_json =
        "{\"vivid\":{\"command\":\"python\",\"args\":[\"" + mcp_py_path + "\"],\"type\":\"stdio\"}}";
    const std::string opdev_json =
        "{\"opdev\":{\"command\":\"python\",\"args\":[\"" + opdev_py_path + "\"],\"type\":\"stdio\"}}";

    struct ServerDef {
        const char*  label;
        const char*  desc;
        const std::string* json_snippet;
        uint64_t     last_ping_ms;
        int          copy_action;  // 0=copy_vivid, 1=copy_opdev
        bool         configured;
    };
    bool vivid_connected = (snap_.mcp_main_last_ping_ms  > 0 && now_ms - snap_.mcp_main_last_ping_ms  < kMcpStaleMs);
    bool opdev_connected = (snap_.mcp_opdev_last_ping_ms > 0 && now_ms - snap_.mcp_opdev_last_ping_ms < kMcpStaleMs);

    ServerDef servers[2] = {
        { "Graph Server",   "Controls the Vivid node graph via AI.",  &vivid_json, snap_.mcp_main_last_ping_ms,  0, mcp_project_config_.vivid_configured },
        { "Operator Dev",   "Helps build custom operators.",           &opdev_json, snap_.mcp_opdev_last_ping_ms, 1, mcp_project_config_.opdev_configured },
    };
    bool connected[2] = { vivid_connected, opdev_connected };

    // Pre-pass: compute wrapped code block heights
    float code_h[2];
    for (int i = 0; i < 2; ++i) {
        auto lines = tr.wrap_text(servers[i].json_snippet->c_str(), code_text_w);
        code_h[i] = lines.size() * tr.line_height() + 2 * kCodePad;
    }

    // Panel height: title + 2 server sections + Done button
    float section_h_0 = kRowH + kRowH + code_h[0] + kSep;
    float section_h_1 = kRowH + kRowH + code_h[1] + kSep;
    float dlg_h = kDlgPadY + kRowH + kSep            // title
                + section_h_0 + section_h_1           // two servers
                + kBtnH + kDlgPadY;                   // Done + padding
    float dlg_x = (wf - kDlgW) * 0.5f;
    float dlg_y = (hf - dlg_h) * 0.5f;

    draw_shadow(tr, dlg_x, dlg_y, kDlgW, dlg_h, style_.corner_radius);
    tr.draw_rounded_rect(dlg_x, dlg_y, kDlgW, dlg_h, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(dlg_x, dlg_y, kDlgW, 2,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = dlg_x + kDlgPadX;
    float cy = dlg_y + kDlgPadY;

    // Title + close [✕]
    tr.draw_text(cx, cy, T("mcp_servers", "MCP Servers"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    const char* close_x = "\xe2\x9c\x95";
    float close_tw = tr.text_width(close_x);
    float close_bx = dlg_x + kDlgW - kDlgPadX - close_tw;
    mcp_dialog_button_rects_.clear();
    bool close_hov = mouse_.x >= close_bx - 4 && mouse_.x <= close_bx + close_tw + 4 &&
                     mouse_.y >= cy - 2 && mouse_.y <= cy + kRowH + 2;
    tr.draw_text(close_bx, cy,
                 close_x,
                 close_hov ? style_.bright_text[0] : style_.dim_text[0],
                 close_hov ? style_.bright_text[1] : style_.dim_text[1],
                 close_hov ? style_.bright_text[2] : style_.dim_text[2]);
    mcp_dialog_button_rects_.push_back({close_bx - 4, cy - 2, close_tw + 8, kRowH + 4, 3 /*close*/});
    cy += kRowH + kSep;

    for (int i = 0; i < 2; ++i) {
        const auto& s = servers[i];
        bool conn = connected[i];

        // Status dot + server name + status text
        float dot_diam = 7.0f;
        float dot_cx = cx + dot_diam * 0.5f;
        float dot_cy = cy + kRowH * 0.5f;
        float dot_r = conn ? 0.30f : 0.40f;
        float dot_g = conn ? 0.85f : 0.40f;
        float dot_b = conn ? 0.40f : 0.45f;
        tr.draw_rounded_rect(dot_cx - dot_diam * 0.5f, dot_cy - dot_diam * 0.5f,
                             dot_diam, dot_diam, dot_diam * 0.5f, dot_r, dot_g, dot_b, 0.9f);
        tr.draw_text(cx + dot_diam + 5.0f, cy,
                     s.label,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        const char* status_str = conn ? "connected" : "not connected";
        float status_x = dlg_x + kDlgW - kDlgPadX - tr.text_width(status_str);

        // "configured" badge (muted blue, drawn left of connection status)
        if (s.configured) {
            const char* cfg_str = "configured";
            float cfg_tw = tr.text_width(cfg_str);
            float cfg_x = status_x - cfg_tw - 10.0f;
            tr.draw_text(cfg_x, cy, cfg_str, 0.45f, 0.65f, 0.90f);
        }

        tr.draw_text(status_x, cy, status_str,
                     conn ? dot_r : style_.dim_text[0],
                     conn ? dot_g : style_.dim_text[1],
                     conn ? dot_b : style_.dim_text[2]);
        cy += kRowH;

        // Description
        tr.draw_text(cx, cy, s.desc,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        cy += kRowH;

        // Code block with dynamic height
        float code_x = cx;
        float code_w = inner_w - kCopyBtnW - 6.0f;
        tr.draw_rect(code_x, cy, code_w, code_h[i], 0.06f, 0.07f, 0.08f, 0.95f);

        const std::string& jsnip = *s.json_snippet;
        float code_tx = code_x + kCodePad;
        float code_ty = cy + kCodePad;
        tr.draw_text_wrapped(code_tx, code_ty, jsnip.c_str(), code_text_w,
                             0.75f, 0.85f, 0.95f);

        // Copy button (vertically centered in code block)
        float copy_bx = code_x + code_w + 6.0f;
        float copy_by = cy + (code_h[i] - kBtnH) * 0.5f;
        bool copy_hov = mouse_.x >= copy_bx && mouse_.x <= copy_bx + kCopyBtnW &&
                        mouse_.y >= copy_by && mouse_.y <= copy_by + kBtnH;
        tr.draw_rounded_rect(copy_bx, copy_by, kCopyBtnW, kBtnH, style_.corner_radius,
                             copy_hov ? style_.button_hover[0] : style_.button_bg[0],
                             copy_hov ? style_.button_hover[1] : style_.button_bg[1],
                             copy_hov ? style_.button_hover[2] : style_.button_bg[2], 0.9f);
        const char* copy_lbl = "Copy";
        float copy_lbl_w = tr.text_width(copy_lbl);
        tr.draw_text(copy_bx + (kCopyBtnW - copy_lbl_w) * 0.5f,
                     copy_by + (kBtnH - tr.line_height()) * 0.5f,
                     copy_lbl,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        mcp_dialog_button_rects_.push_back({copy_bx, copy_by, kCopyBtnW, kBtnH, s.copy_action});
        cy += code_h[i] + kSep;
    }

    // Done button
    float done_x = dlg_x + kDlgW - kDlgPadX - kBtnW;
    bool done_hov = mouse_.x >= done_x && mouse_.x <= done_x + kBtnW &&
                    mouse_.y >= cy     && mouse_.y <= cy + kBtnH;
    tr.draw_rounded_rect(done_x, cy, kBtnW, kBtnH, style_.corner_radius,
                         done_hov ? style_.button_hover[0] : style_.button_bg[0],
                         done_hov ? style_.button_hover[1] : style_.button_bg[1],
                         done_hov ? style_.button_hover[2] : style_.button_bg[2], 0.9f);
    float done_tw = tr.text_width(T("done", "Done"));
    tr.draw_text(done_x + (kBtnW - done_tw) * 0.5f, cy + (kBtnH - tr.line_height()) * 0.5f, T("done", "Done"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    mcp_dialog_button_rects_.push_back({done_x, cy, kBtnW, kBtnH, 2 /*done*/});
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
    constexpr int kPerfDisplayInterval = 30;  // update display text ~2x/sec at 60fps
    if (perf_frame_counter_ == 0 || perf_frame_counter_ % kPerfDisplayInterval == 0) {
        display_fps_ = smoothed_fps_;
        display_ms_ = smoothed_ms_;
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
    if (display_fps_ >= 55.0f) {
        fr = kPerfFpsColor[0]; fg = kPerfFpsColor[1]; fb = kPerfFpsColor[2];
    } else if (display_fps_ >= 30.0f) {
        fr = 0.95f; fg = 0.85f; fb = 0.30f; // yellow
    } else {
        fr = 0.95f; fg = 0.35f; fb = 0.30f; // red
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f FPS", display_fps_);
    float fps_field_w = tr.text_width("000 FPS");
    float fps_text_w = tr.text_width(buf);
    tr.draw_text(x + (fps_field_w - fps_text_w), text_y, buf, fr, fg, fb);
    x += fps_field_w + kPerfSepMargin;

    // Separator
    tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
    x += kPerfSepW + kPerfSepMargin;

    // --- Audio rate ---
    audio_load_history_.push(snap_.audio_load);
    {
        float audio_section_start = x;

        // "48kHz" label
        char audio_buf[32];
        if (snap_.audio_sample_rate > 0)
            std::snprintf(audio_buf, sizeof(audio_buf), "%ukHz", snap_.audio_sample_rate / 1000);
        else
            std::snprintf(audio_buf, sizeof(audio_buf), "--kHz");
        tr.draw_text(x, text_y, audio_buf,
                     kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2]);
        x += tr.text_width(audio_buf) + kPerfSepMargin;

        // Separator
        tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
        x += kPerfSepW + kPerfSepMargin;

        // Audio load "N%" label (color-coded)
        std::snprintf(buf, sizeof(buf), "%.0f%%", snap_.audio_load * 100.0f);
        float load_field_w = tr.text_width("100%");
        float load_text_w = tr.text_width(buf);
        float load = snap_.audio_load;
        float lr, lg, lb;
        if (load < 0.5f) {
            lr = kPerfAudioColor[0]; lg = kPerfAudioColor[1]; lb = kPerfAudioColor[2];
        } else if (load < 0.8f) {
            lr = 0.95f; lg = 0.85f; lb = 0.30f;
        } else {
            lr = 0.95f; lg = 0.35f; lb = 0.30f;
        }
        tr.draw_text(x + (load_field_w - load_text_w), text_y, buf, lr, lg, lb);
        x += load_field_w + 4.0f;

        // Mini sparkline for audio load
        float ag_x = x;
        float ag_y = (kPerfBarH - kPerfMiniGraphH) * 0.5f;
        perf_audio_graph_x_ = ag_x;
        perf_audio_graph_y_ = ag_y;

        tr.draw_rect(ag_x, ag_y, kPerfMiniGraphW, kPerfMiniGraphH,
                     0.04f, 0.05f, 0.06f, 0.8f);
        draw_perf_sparkline(tr, audio_load_history_.values, kPerfHistoryLen,
                            audio_load_history_.write_idx, audio_load_history_.filled,
                            ag_x, ag_y, kPerfMiniGraphW, kPerfMiniGraphH,
                            lr, lg, lb, 0.7f);
        x = ag_x + kPerfMiniGraphW + kPerfSepMargin;

        // Track full audio section bounds for hover
        perf_audio_section_x_ = audio_section_start;
        perf_audio_section_w_ = x - audio_section_start;
    }

    // Separator
    tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
    x += kPerfSepW + kPerfSepMargin;

    // --- Frame time ---
    std::snprintf(buf, sizeof(buf), "%.1f ms", display_ms_);
    float ms_field_w = tr.text_width("000.0 ms");
    float ms_text_w = tr.text_width(buf);
    tr.draw_text(x + (ms_field_w - ms_text_w), text_y, buf, kPerfMsColor[0], kPerfMsColor[1], kPerfMsColor[2]);
    x += ms_field_w + kPerfSepMargin;

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

    // --- Right-aligned Record / Snapshot buttons ---
    perf_button_rects_.clear();
    {
        float btn_y = (kPerfBarH - kPerfBtnH) * 0.5f;
        float rx = fw - kPerfBarPadX;  // right edge cursor
        bool can_undo = commands_.can_undo();
        bool can_redo = commands_.can_redo();

        // Snapshot button
        {
            const char* snap_label = "Snap";
            float tw = tr.text_width(snap_label);
            float btn_w = tw + kPerfBtnPadX * 2;
            rx -= btn_w;
            // Button background
            bool snap_hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                                mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = snap_hovered ? 0.35f : 0.20f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         snap_label, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 1, true});
            rx -= kPerfBtnMargin;
        }

        // Redo button
        {
            const char* redo_label = "Redo";
            float tw = tr.text_width(redo_label);
            float btn_w = tw + kPerfBtnPadX * 2;
            rx -= btn_w;
            bool hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                           mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = can_redo ? (hovered ? 0.35f : 0.20f) : 0.10f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);
            float trr = can_redo ? style_.bright_text[0] : kDimText[0];
            float trg = can_redo ? style_.bright_text[1] : kDimText[1];
            float trb = can_redo ? style_.bright_text[2] : kDimText[2];
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         redo_label, trr, trg, trb);
            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 3, can_redo});
            rx -= kPerfBtnMargin;
        }

        // Undo button
        {
            const char* undo_label = "Undo";
            float tw = tr.text_width(undo_label);
            float btn_w = tw + kPerfBtnPadX * 2;
            rx -= btn_w;
            bool hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                           mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = can_undo ? (hovered ? 0.35f : 0.20f) : 0.10f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);
            float tur = can_undo ? style_.bright_text[0] : kDimText[0];
            float tug = can_undo ? style_.bright_text[1] : kDimText[1];
            float tub = can_undo ? style_.bright_text[2] : kDimText[2];
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         undo_label, tur, tug, tub);
            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 2, can_undo});
            rx -= kPerfBtnMargin;
        }

        // MCP status dots — [● MCP] [● DEV]
        mcp_dot_rects_.clear();
        {
            // Current steady_clock time for staleness check (30 s threshold)
            auto now_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            struct McpDotDef { const char* label; uint64_t last_ping_ms; int idx; };
            McpDotDef dots[2] = {
                { "MCP", snap_.mcp_main_last_ping_ms,  0 },
                { "DEV", snap_.mcp_opdev_last_ping_ms, 1 },
            };

            // Place rightmost dot first (DEV), then MCP, going left
            for (int di = 1; di >= 0; --di) {
                const auto& dot = dots[di];
                bool connected = (dot.last_ping_ms > 0 &&
                                  now_ms - dot.last_ping_ms < kMcpStaleMs);
                float dot_r   = connected ? 0.30f : 0.40f;
                float dot_g   = connected ? 0.85f : 0.40f;
                float dot_b   = connected ? 0.40f : 0.45f;

                float lbl_w = tr.text_width(dot.label);
                float dot_diam = 7.0f;
                float pill_w = kPerfBtnPadX + dot_diam + 3.0f + lbl_w + kPerfBtnPadX;
                rx -= pill_w;

                bool hovered = mouse_.x >= rx && mouse_.x <= rx + pill_w &&
                               mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
                float bg_a = hovered ? 0.28f : 0.14f;
                tr.draw_rounded_rect(rx, btn_y, pill_w, kPerfBtnH, 3.0f,
                                     0.30f, 0.32f, 0.35f, bg_a);

                float dot_cx = rx + kPerfBtnPadX + dot_diam * 0.5f;
                float dot_cy = btn_y + kPerfBtnH * 0.5f;
                tr.draw_rounded_rect(dot_cx - dot_diam * 0.5f, dot_cy - dot_diam * 0.5f,
                                     dot_diam, dot_diam, dot_diam * 0.5f,
                                     dot_r, dot_g, dot_b, 0.9f);
                tr.draw_text(rx + kPerfBtnPadX + dot_diam + 3.0f,
                             btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                             dot.label, kDimText[0], kDimText[1], kDimText[2]);

                mcp_dot_rects_.push_back({rx, btn_y, pill_w, kPerfBtnH, dot.idx});
                rx -= kPerfBtnMargin;
            }

            // Tooltip for hovered dot
            for (const auto& dr : mcp_dot_rects_) {
                if (mouse_.x >= dr.x && mouse_.x <= dr.x + dr.w &&
                    mouse_.y >= dr.y && mouse_.y <= dr.y + dr.h) {
                    uint64_t ping_ms = (dr.idx == 0) ? snap_.mcp_main_last_ping_ms
                                                     : snap_.mcp_opdev_last_ping_ms;
                    bool connected = (ping_ms > 0 && now_ms - ping_ms < kMcpStaleMs);
                    const char* srv = (dr.idx == 0) ? "vivid" : "opdev";
                    const char* status = connected ? "connected" : "not connected";
                    char tip[64];
                    std::snprintf(tip, sizeof(tip), "%s \xe2\x80\x94 %s", srv, status);
                    float tip_w = tr.text_width(tip) + 12.0f;
                    float tip_x = dr.x + dr.w * 0.5f - tip_w * 0.5f;
                    float tip_y = kPerfBarH + 4.0f;
                    tr.draw_rounded_rect(tip_x - 2.0f, tip_y, tip_w, tr.line_height() + 6.0f,
                                         3.0f, 0.10f, 0.11f, 0.13f, 0.95f);
                    tr.draw_text(tip_x + 4.0f, tip_y + 3.0f, tip,
                                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                    break;
                }
            }
        }

        // Unsaved graph badge (non-interactive)
        if (snap_.graph_dirty) {
            const char* dirty_label = "Unsaved";
            float tw = tr.text_width(dirty_label);
            float badge_w = tw + kPerfBtnPadX * 2;
            rx -= badge_w;
            tr.draw_rounded_rect(rx, btn_y, badge_w, kPerfBtnH, 3.0f,
                                 0.72f, 0.40f, 0.14f, 0.32f);
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         dirty_label, 1.0f, 0.88f, 0.74f);
            rx -= kPerfBtnMargin;
        }

        // Record / Stop button
        if (snap_.is_recording) {
            // --- Recording active: [● REC  MM:SS  NNNf  Stop] ---

            // Stop button
            {
                const char* stop_label = "Stop";
                float tw = tr.text_width(stop_label);
                float btn_w = tw + kPerfBtnPadX * 2;
                rx -= btn_w;
                bool stop_hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                                    mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
                float bg_a = stop_hovered ? 0.50f : 0.35f;
                tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                     0.60f, 0.20f, 0.20f, bg_a);
                tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                             stop_label, 1.0f, 0.85f, 0.85f);
                perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 0, true});
                rx -= kPerfBtnMargin;
            }

            // Frame count
            {
                char fc[32];
                std::snprintf(fc, sizeof(fc), "%lluf",
                              static_cast<unsigned long long>(snap_.recording_frame_count));
                float tw = tr.text_width(fc);
                rx -= tw;
                tr.draw_text(rx, text_y, fc, kDimText[0], kDimText[1], kDimText[2]);
                rx -= kPerfBtnMargin;
            }

            // Duration MM:SS
            {
                int total_sec = static_cast<int>(snap_.recording_duration_sec);
                int mm = total_sec / 60;
                int ss = total_sec % 60;
                char dur[16];
                std::snprintf(dur, sizeof(dur), "%02d:%02d", mm, ss);
                float tw = tr.text_width(dur);
                rx -= tw;
                tr.draw_text(rx, text_y, dur, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                rx -= kPerfBtnMargin;
            }

            // Animated red dot + REC label
            {
                float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(perf_frame_counter_) * 0.12f);
                const char* rec_label = "REC";
                float tw = tr.text_width(rec_label);
                float dot_space = kPerfRecDotR * 2 + 4.0f;
                rx -= tw + dot_space;
                float dot_cx = rx + kPerfRecDotR;
                float dot_cy = kPerfBarH * 0.5f;
                tr.draw_rounded_rect(dot_cx - kPerfRecDotR, dot_cy - kPerfRecDotR,
                                     kPerfRecDotR * 2, kPerfRecDotR * 2, kPerfRecDotR,
                                     0.95f, 0.15f, 0.15f, 0.5f + 0.5f * pulse);
                tr.draw_text(rx + dot_space, text_y, rec_label, 0.95f, 0.30f, 0.30f);
            }
        } else {
            // --- Not recording: [● REC ▾] button ---
            const char* rec_label = "REC";
            float tw = tr.text_width(rec_label);
            float arrow_w = tr.text_width("\xe2\x96\xbe"); // ▾
            float dot_space = kPerfRecDotR * 2 + 4.0f;
            float btn_w = kPerfBtnPadX + dot_space + tw + 4.0f + arrow_w + kPerfBtnPadX;
            rx -= btn_w;

            bool rec_hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                               mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = rec_hovered ? 0.35f : 0.20f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);

            float ix = rx + kPerfBtnPadX;
            // Red dot
            float dot_cx = ix + kPerfRecDotR;
            float dot_cy = kPerfBarH * 0.5f;
            tr.draw_rounded_rect(dot_cx - kPerfRecDotR, dot_cy - kPerfRecDotR,
                                 kPerfRecDotR * 2, kPerfRecDotR * 2, kPerfRecDotR,
                                 0.85f, 0.20f, 0.20f, 0.9f);
            ix += dot_space;
            // REC text
            float label_y = btn_y + (kPerfBtnH - tr.line_height()) * 0.5f;
            tr.draw_text(ix, label_y, rec_label, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            ix += tw + 4.0f;
            // Dropdown arrow
            tr.draw_text(ix, label_y, "\xe2\x96\xbe", kDimText[0], kDimText[1], kDimText[2]);

            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 0, true});
        }
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

    // perf_mem_hovered_ drawn in draw_overlays()

    // Check hover over audio section (kHz label + load% + sparkline)
    {
        bool in_audio_section = mouse_.x >= perf_audio_section_x_ &&
                                mouse_.x <= perf_audio_section_x_ + perf_audio_section_w_ &&
                                mouse_.y >= 0 && mouse_.y <= kPerfBarH;

        float audio_exp_x = perf_audio_section_x_;
        if (audio_exp_x + kPerfExpandedW > fw - 10.0f)
            audio_exp_x = fw - 10.0f - kPerfExpandedW;
        constexpr float kAudioExpandedH = 200.0f;
        bool in_audio_expanded = perf_audio_hovered_ &&
                                 mouse_.x >= audio_exp_x && mouse_.x <= audio_exp_x + kPerfExpandedW &&
                                 mouse_.y >= kPerfBarH && mouse_.y <= kPerfBarH + kAudioExpandedH;

        perf_audio_hovered_ = in_audio_section || in_audio_expanded;

        // perf_audio_hovered_ drawn in draw_overlays()
    }
}

void NodeGraphUI::draw_perf_sparkline(Renderer2D& tr, const float* buf, uint32_t buf_len,
                                      uint32_t write_idx, bool filled,
                                      float x, float y, float w, float h,
                                      float r, float g, float b, float a) {
    uint32_t count = filled ? buf_len : write_idx;
    if (count == 0) return;

    // Find min/max for auto-scaling
    uint32_t first_idx = filled ? write_idx % buf_len : 0;
    float vmin = buf[first_idx], vmax = buf[first_idx];
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
    tr.draw_text(tx, ty, T("memory", "Memory"), 0.85f, 0.87f, 0.90f);

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

void NodeGraphUI::draw_perf_audio_expanded(Renderer2D& tr) {
    float fw = static_cast<float>(win_w_);

    float ex = perf_audio_section_x_;
    if (ex + kPerfExpandedW > fw - 10.0f)
        ex = fw - 10.0f - kPerfExpandedW;
    float ey = kPerfBarH;

    float pad = 8.0f;
    float line_h = tr.line_height();
    float row_h = line_h + 2.0f;

    // Count info rows: rate, buffer, latency, channels, nodes, xruns, load
    constexpr int kInfoRows = 7;
    float info_h = kInfoRows * row_h;
    float sparkline_h = 60.0f;
    float total_h = pad + line_h + 4.0f + info_h + 4.0f + sparkline_h + pad;

    // Panel background
    tr.draw_rect(ex, ey, kPerfExpandedW, total_h, 0.08f, 0.09f, 0.10f, 0.95f);
    // Top accent
    tr.draw_rect(ex, ey, kPerfExpandedW, 2,
                 kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2], 0.8f);

    float tx = ex + pad;
    float ty = ey + pad;
    float right_x = ex + kPerfExpandedW - pad;

    // Title + current load %
    tr.draw_text(tx, ty, T("audio", "Audio"), 0.85f, 0.87f, 0.90f);
    char load_buf[32];
    std::snprintf(load_buf, sizeof(load_buf), "%.0f%%", snap_.audio_load * 100.0f);
    float load_w = tr.text_width(load_buf);
    tr.draw_text(right_x - load_w, ty, load_buf,
                 kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2]);

    ty += line_h + 4.0f;

    // Helper to draw a label: value row
    auto draw_row = [&](const char* label, const char* value) {
        tr.draw_text(tx, ty, label, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float vw = tr.text_width(value);
        tr.draw_text(right_x - vw, ty, value, 0.85f, 0.87f, 0.90f);
        ty += row_h;
    };

    // Sample rate
    char val[64];
    std::snprintf(val, sizeof(val), "%u Hz", snap_.audio_sample_rate);
    draw_row(T("rate", "Rate"), val);

    // Buffer size
    std::snprintf(val, sizeof(val), "%u samples", snap_.audio_buffer_size);
    draw_row(T("buffer", "Buffer"), val);

    // Latency
    if (snap_.audio_sample_rate > 0) {
        float latency_ms = static_cast<float>(snap_.audio_buffer_size)
                           / static_cast<float>(snap_.audio_sample_rate) * 1000.0f;
        std::snprintf(val, sizeof(val), "%.1f ms", latency_ms);
    } else {
        std::snprintf(val, sizeof(val), "-- ms");
    }
    draw_row(T("latency", "Latency"), val);

    // Channels
    draw_row(T("channels", "Channels"), "2");

    // Nodes
    std::snprintf(val, sizeof(val), "%u", snap_.audio_node_count);
    draw_row(T("nodes", "Nodes"), val);

    // XRUNs
    std::snprintf(val, sizeof(val), "%u", snap_.audio_underrun_count);
    draw_row(T("xruns", "XRUNs"), val);

    // Load
    std::snprintf(val, sizeof(val), "%.1f%%", snap_.audio_load * 100.0f);
    draw_row(T("load", "Load"), val);

    // Sparkline
    ty += 4.0f;
    float graph_x = tx;
    float graph_w = kPerfExpandedW - pad * 2;

    tr.draw_rect(graph_x, ty, graph_w, sparkline_h, 0.04f, 0.05f, 0.06f, 0.8f);

    // Color by current load
    float load = snap_.audio_load;
    float lr, lg, lb;
    if (load < 0.5f) {
        lr = kPerfAudioColor[0]; lg = kPerfAudioColor[1]; lb = kPerfAudioColor[2];
    } else if (load < 0.8f) {
        lr = 0.95f; lg = 0.85f; lb = 0.30f;
    } else {
        lr = 0.95f; lg = 0.35f; lb = 0.30f;
    }

    draw_perf_sparkline(tr, audio_load_history_.values, kPerfHistoryLen,
                        audio_load_history_.write_idx, audio_load_history_.filled,
                        graph_x, ty, graph_w, sparkline_h,
                        lr, lg, lb, 0.7f);
}

// -----------------------------------------------------------------------
// GPU thumbnail overlay
// -----------------------------------------------------------------------
void NodeGraphUI::draw_thumbnails(ThumbnailRenderer& renderer, const ThumbnailCache& cache,
                                  WGPUCommandEncoder encoder, WGPUTextureView surface,
                                  uint32_t w, uint32_t h) {
    renderer.begin(encoder, surface, w, h);
    for (const auto& r : node_rects_) {
        bool should_draw_thumb = (r.is_gpu) ||
                                 (custom_thumb_nodes_.count(r.node_id) > 0);
        if (!should_draw_thumb) continue;
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
    float pk_list_top = py + 2;
    float pk_list_area_h = visible * kPickerItemH;
    int pk_first = std::max(0, static_cast<int>(std::floor(param_picker_scroll_ / kPickerItemH)));
    float pk_offset = param_picker_scroll_ - pk_first * kPickerItemH;
    int pk_draw_count = std::min(static_cast<int>(param_picker_items_.size()) - pk_first, kPickerMaxVisible + 1);

    tr.push_clip_rect(px, pk_list_top, kPickerW, pk_list_area_h);
    for (int vi = 0; vi < pk_draw_count; ++vi) {
        int idx = pk_first + vi;
        if (idx >= static_cast<int>(param_picker_items_.size())) break;

        float iy = pk_list_top - pk_offset + vi * kPickerItemH;
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
    tr.pop_clip_rect();
}

// -----------------------------------------------------------------------
// Session grid (variation strip at bottom)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_session_grid(Renderer2D& tr) {
    if (!session_grid_open_) return;

    variation_cell_rects_.clear();
    session_button_rects_.clear();
    session_ctx_menu_rects_.clear();

    float strip_y = static_cast<float>(win_h_) - kSessionStripH;
    float strip_w = static_cast<float>(win_w_);

    // Background
    tr.draw_rect(0, strip_y, strip_w, kSessionStripH,
                 style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.95f);
    // Top border
    tr.draw_rect(0, strip_y, strip_w, 1,
                 style_.accent[0], style_.accent[1], style_.accent[2], 0.5f);

    // --- Header row ---
    float hx = kSessionPadX;
    float hy = strip_y + 5;

    tr.draw_text(hx, hy + 2, T("session", "SESSION"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    hx += 70;

    // Quantize buttons
    static const char* quantize_labels[] = { "Off", "Beat", "Bar", "4Bar" };
    tr.draw_text(hx, hy + 2, T("quantize", "Quantize:"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    hx += 72;
    for (int i = 0; i < 4; ++i) {
        float bw = 38.0f;
        bool active = (session_quantize_mode_ == i);
        float r = active ? style_.accent[0] : style_.slider_track[0];
        float g = active ? style_.accent[1] : style_.slider_track[1];
        float b = active ? style_.accent[2] : style_.slider_track[2];
        tr.draw_rect(hx, hy, bw, 18, r, g, b, active ? 0.9f : 0.6f);
        tr.draw_text(hx + 4, hy + 2, quantize_labels[i],
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        session_button_rects_.push_back({hx, hy, bw, 18.0f, 2 + i});
        hx += bw + 3;
    }

    hx += 10;

    // Branch button (duplicates active variation)
    if (snap_.active_variation >= 0) {
        float branch_w = 54.0f;
        tr.draw_rect(hx, hy, branch_w, 18,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.7f);
        tr.draw_text(hx + 6, hy + 2, T("branch", "Branch"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        session_button_rects_.push_back({hx, hy, branch_w, 18.0f, 6});
        hx += branch_w + 6;
    }

    // Update button (enabled only when dirty)
    if (snap_.active_variation >= 0 && snap_.variation_dirty) {
        float update_w = 54.0f;
        tr.draw_rect(hx, hy, update_w, 18,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
        tr.draw_text(hx + 6, hy + 2, T("update", "Update"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        session_button_rects_.push_back({hx, hy, update_w, 18.0f, 1});
        hx += update_w + 6;
    }

    // --- Card row ---
    float cy = strip_y + kSessionHeaderH + 4;
    float cx = kSessionPadX - session_scroll_x_;

    for (int i = 0; i < static_cast<int>(snap_.variations.size()); ++i) {
        // Skip the card being dragged (drawn separately as ghost)
        if (session_drag_active_ && session_drag_idx_ == i) {
            variation_cell_rects_.push_back({cx, cy, kSessionCellW, kSessionCellH, i});
            cx += kSessionCellW + kSessionCellPad;
            continue;
        }

        bool is_active = (i == snap_.active_variation);
        bool is_queued = (i == snap_.queued_variation);
        bool is_selected = (i == session_selected_idx_);
        bool is_dirty = is_active && snap_.variation_dirty;

        // Card background
        float bg_r, bg_g, bg_b, bg_a;
        if (is_active) {
            bg_r = style_.accent[0] * 0.25f;
            bg_g = style_.accent[1] * 0.25f;
            bg_b = style_.accent[2] * 0.25f;
            bg_a = 0.85f;
        } else if (is_queued) {
            bg_r = style_.accent[0] * 0.15f;
            bg_g = style_.accent[1] * 0.15f;
            bg_b = style_.accent[2] * 0.15f;
            bg_a = 0.7f;
        } else {
            bg_r = style_.slider_track[0];
            bg_g = style_.slider_track[1];
            bg_b = style_.slider_track[2];
            bg_a = 0.5f;
        }
        tr.draw_rect(cx, cy, kSessionCellW, kSessionCellH, bg_r, bg_g, bg_b, bg_a);

        // Border: active = 2px accent, queued = pulsing accent, selected = bright highlight
        if (is_active) {
            draw_rect_border(tr, cx, cy, kSessionCellW, kSessionCellH,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.9f, 2.0f);
        } else if (is_queued) {
            float pulse = 0.5f + 0.3f * std::sin(cursor_blink_time_ * 6.0f);
            draw_rect_border(tr, cx, cy, kSessionCellW, kSessionCellH,
                             style_.accent[0], style_.accent[1], style_.accent[2], pulse);
        }
        if (is_selected) {
            draw_rect_border(tr, cx - 1, cy - 1, kSessionCellW + 2, kSessionCellH + 2,
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.7f);
        }

        // Drag insertion indicator
        if (session_drag_active_ && session_drag_target_idx_ == i) {
            tr.draw_rect(cx - 2, cy, 2, kSessionCellH,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
        }

        // Line 1: Variation name (truncated)
        float text_y = cy + 6;
        if (session_editing_name_ && session_edit_idx_ == i) {
            tr.draw_text(cx + 8, text_y, session_edit_buffer_.c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            if (cursor_blink_on()) {
                int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(session_edit_buffer_.size())));
                float cur_x = cx + 8 + tr.text_width(session_edit_buffer_.substr(0, cpos).c_str());
                tr.draw_rect(cur_x, text_y - 1, 1.0f, 14,
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            }
        } else {
            const std::string& name = snap_.variations[i].name;
            // Truncate name to fit card width
            float max_text_w = kSessionCellW - 16;
            std::string label = name;
            if (tr.text_width(label.c_str()) > max_text_w) {
                while (label.size() > 1 && tr.text_width((label + "...").c_str()) > max_text_w)
                    label.pop_back();
                label += "...";
            }
            float text_r = is_active ? style_.bright_text[0] : style_.dim_text[0];
            float text_g = is_active ? style_.bright_text[1] : style_.dim_text[1];
            float text_b = is_active ? style_.bright_text[2] : style_.dim_text[2];
            tr.draw_text(cx + 8, text_y, label.c_str(), text_r, text_g, text_b);
        }

        // Line 2: Status markers
        float marker_y = cy + kSessionCellH - 14;
        float mx = cx + 8;
        if (is_active) {
            // Active dot
            tr.draw_rect(mx, marker_y + 4, 6, 6,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
            mx += 10;
        }
        if (is_queued) {
            // Queued arrow indicator
            tr.draw_text(mx, marker_y + 1, ">",
                         style_.accent[0], style_.accent[1], style_.accent[2]);
            mx += 12;
        }
        if (is_dirty) {
            // Dirty dot
            tr.draw_rect(mx, marker_y + 4, kSessionDirtyDotR * 2, kSessionDirtyDotR * 2,
                         1.0f, 0.8f, 0.2f, 0.9f);
        }

        variation_cell_rects_.push_back({cx, cy, kSessionCellW, kSessionCellH, i});
        cx += kSessionCellW + kSessionCellPad;
    }

    // Drag insertion indicator at the end
    if (session_drag_active_ && session_drag_target_idx_ == static_cast<int>(snap_.variations.size())) {
        tr.draw_rect(cx - 2, cy, 2, kSessionCellH,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    }

    // [+ Save New] button
    float new_btn_w = 74.0f;
    tr.draw_rect(cx, cy, new_btn_w, kSessionCellH,
                 style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.5f);
    tr.draw_text(cx + 8, cy + 14, T("save_new", "+ Save New"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    session_button_rects_.push_back({cx, cy, new_btn_w, kSessionCellH, 0});

    // --- Drag ghost card ---
    if (session_drag_active_ && session_drag_idx_ >= 0 &&
        session_drag_idx_ < static_cast<int>(snap_.variations.size())) {
        float ghost_x = mouse_.x - kSessionCellW * 0.5f;
        float ghost_y = cy;
        tr.draw_rect(ghost_x, ghost_y, kSessionCellW, kSessionCellH,
                     style_.accent[0] * 0.3f, style_.accent[1] * 0.3f, style_.accent[2] * 0.3f, 0.6f);
        draw_rect_border(tr, ghost_x, ghost_y, kSessionCellW, kSessionCellH,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
        tr.draw_text(ghost_x + 8, ghost_y + 14,
                     snap_.variations[session_drag_idx_].name.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }

    // --- Context menu ---
    if (session_ctx_menu_open_ && session_ctx_menu_idx_ >= 0) {
        static const char* ctx_labels[] = { "Rename", "Duplicate", "Delete", "Branch From" };
        int item_count = 4;
        float menu_w = kSessionCtxMenuW;
        float menu_h = item_count * kSessionCtxMenuItemH + 4;
        float menu_x = session_ctx_menu_x_;
        float menu_y = session_ctx_menu_y_;
        // Clamp to screen
        if (menu_x + menu_w > strip_w) menu_x = strip_w - menu_w;
        if (menu_y + menu_h > static_cast<float>(win_h_)) menu_y -= menu_h;

        draw_popup_bg(tr, style_, menu_x, menu_y, menu_w, menu_h);

        for (int ci = 0; ci < item_count; ++ci) {
            float iy = menu_y + 2 + ci * kSessionCtxMenuItemH;
            bool hovered = (mouse_.x >= menu_x && mouse_.x <= menu_x + menu_w &&
                            mouse_.y >= iy && mouse_.y <= iy + kSessionCtxMenuItemH);
            if (hovered) {
                tr.draw_rect(menu_x + 2, iy, menu_w - 4, kSessionCtxMenuItemH,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.2f);
            }
            tr.draw_text(menu_x + 10, iy + 3, ctx_labels[ci],
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            session_ctx_menu_rects_.push_back({menu_x, iy, menu_w, kSessionCtxMenuItemH, ci});
        }
    }
}

} // namespace vivid::ui
