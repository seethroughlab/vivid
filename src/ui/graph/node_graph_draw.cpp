#include <nlohmann/json.hpp>
#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/rendering/overlay_layouts.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/style/i18n.h"
#include "ui/rendering/thumbnail_cache.h"
#include "ui/rendering/thumbnail_renderer.h"
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
            const char* label = T("node_missing_label", "MISSING");
            float lw = tr.text_width(label, zoom_);
            float lx = sx + (sw - lw) * 0.5f;
            float ly = s_body_y + (s_body_h - tr.line_height() * zoom_) * 0.5f - g_to_s(6);
            tr.draw_text(lx, ly, label, kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.8f, zoom_);
            // Sub-label with reason
            if (sn && !sn->error_message.empty()) {
                const char* sub = sn->error_message.find("rebuild") != std::string::npos
                                  ? T("node_missing_try_rebuild", "try rebuild")
                                  : sn->error_message.find("ABI") != std::string::npos
                                      ? T("node_missing_abi_mismatch", "ABI mismatch")
                                      : T("node_missing_not_installed", "not installed");
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

        // Type name (centered, below accent bar + body).
        // Strip cadence suffixes ("Au"/"Fr") — cadence is conveyed by accent color.
        std::string display_name = r.type_name;
        if (display_name.size() > 2) {
            auto suffix = display_name.substr(display_name.size() - 2);
            if (suffix == "Au" || suffix == "Fr")
                display_name.resize(display_name.size() - 2);
        }
        float text_y = sy + s_accent_h + s_body_h + g_to_s(kNodePadY);
        float tw = tr.text_width(display_name.c_str(), zoom_);
        float tx = sx + (sw - tw) * 0.5f;
        tr.draw_text(tx, text_y, display_name.c_str(), 1.0f, 1.0f, 1.0f, 1.0f, zoom_);

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
    build_console_panel_.draw(tr, style_, win_w_, win_h_,
                              session_grid_open_ ? kSessionStripH : 0.0f);
    draw_perf_bar(tr);
    draw_midi_map_banner(tr);
    {
        float banner_y = kPerfBarH + (midi_map_mode_ ? kMidiMapBannerH : 0.0f);
        float max_w = has_selection() ? graph_right() : static_cast<float>(win_w_);
        dialogs_.draw_core_update_banner(tr, style_, banner_y, max_w);
    }
}

} // namespace vivid::ui
