#include <nlohmann/json.hpp>
#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/renderer_2d.h"
#include "ui/i18n.h"
#include "common/string_util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vivid::ui {

using vivid::format_float;
using vivid::format_int;
using vivid::format_uint;

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

void NodeGraphUI::draw_inspector(Renderer2D& tr, uint32_t w, uint32_t h) {
    inspector_.slider_rects.clear();
    inspector_.xy_pad_rects.clear();
    inspector_.color_swatch_rects.clear();
    inspector_.bool_rects.clear();
    inspector_.value_text_rects.clear();
    inspector_.dropdown_rects.clear();
    inspector_.file_button_rects.clear();
    inspector_.resolution_rects.clear();
    inspector_.preset_dropdown_rects.clear();
    inspector_.preset_save_rects.clear();
    inspector_.midi_remove_rects.clear();
    inspector_.midi_range_rects.clear();
    patch_jacks_.clear();
    patch_wires_.clear();
    inspector_.group_header_rects.clear();
    inspector_.state_preset_rects.clear();
    inspector_.state_header_rects.clear();
    inspector_.lock_badge_rects.clear();
    inspector_.label_rects.clear();
    inspector_.wire_remap_rects.clear();
    inspector_.wire_clamp_rects.clear();

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

        if (!c.dropped) {
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
            bool editing_this = inspector_.editing_wire_remap && inspector_.edit_wire_remap_field == f;
            tr.draw_rect(fx, py, fw, fh,
                         editing_this ? style_.accent[0] * 0.3f : style_.inspector_bg[0] * 0.7f,
                         editing_this ? style_.accent[1] * 0.3f : style_.inspector_bg[1] * 0.7f,
                         editing_this ? style_.accent[2] * 0.3f : style_.inspector_bg[2] * 0.7f, 0.8f);
            tr.draw_rect(fx, py, fw, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
            tr.draw_rect(fx, py + fh - 1, fw, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);

            if (editing_this) {
                tr.draw_text(fx + 4, py + 2, inspector_.edit_buffer.c_str(),
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                if (cursor_blink_on()) {
                    int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(inspector_.edit_buffer.size())));
                    float cx = fx + 4 + tr.text_width(inspector_.edit_buffer.substr(0, cpos).c_str());
                    tr.draw_rect(cx, py + 1, 1.0f, fh - 2,
                                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                }
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.3g", vals[f]);
                tr.draw_text(fx + 4, py + 2, buf,
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            }

            inspector_.wire_remap_rects.push_back({fx, py, fw, fh, f});
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
        inspector_.wire_clamp_rects.push_back({px, py, cb_size, cb_size});
        tr.draw_text(px + cb_size + 6, py + 1, T("clamp", "Clamp"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);
        } // !c.dropped

        inspector_.insp_content_h = 0;
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
                inspector_.insp_content_h = 0;
                return;
            }

            // Reset scroll when the selected pair changes
            std::string scroll_key = id_a + "+" + id_b;
            if (scroll_key != inspector_.insp_scroll_node_id) {
                inspector_.insp_scroll_y = 0.0f;
                inspector_.insp_scroll_node_id = scroll_key;
            }

            float viewport_top = kPerfBarH;
            float viewport_h = static_cast<float>(h) - viewport_top;
            float max_scroll = std::max(0.0f, inspector_.insp_content_h - viewport_h);
            inspector_.insp_scroll_y = std::max(0.0f, std::min(inspector_.insp_scroll_y, max_scroll));

            tr.push_clip_rect(insp_x, viewport_top, kInspectorW, viewport_h);

            float px = insp_x + kInspPadX;
            float py = viewport_top + 8 - inspector_.insp_scroll_y;

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

            inspector_.insp_content_h = (py + inspector_.insp_scroll_y) - viewport_top;
            draw_inspector_scrollbar(tr);
        } else {
            // 3+ nodes: summary
            float px = insp_x + kInspPadX;
            float py = kPerfBarH + 8;
            std::string label = std::to_string(selected_node_ids_.size()) + " nodes selected";
            tr.draw_text(px, py, label.c_str(), 1.0f, 1.0f, 1.0f);
            py += kLineH + 4;
            tr.draw_text(px, py, T("delete_to_remove", "Delete / Backspace to remove"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

            inspector_.insp_content_h = 0;
        }
        return;
    }

    const auto& sel_id = single_selected_id();

    // Reset scroll when selection changes
    if (sel_id != inspector_.insp_scroll_node_id) {
        inspector_.insp_scroll_y = 0.0f;
        inspector_.insp_scroll_node_id = sel_id;
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
    float max_scroll = std::max(0.0f, inspector_.insp_content_h - viewport_h);
    inspector_.insp_scroll_y = std::max(0.0f, std::min(inspector_.insp_scroll_y, max_scroll));

    // Clip rect for scrollable content
    tr.push_clip_rect(insp_x, viewport_top, kInspectorW, viewport_h);

    float px = insp_x + kInspPadX;
    float py = viewport_top + 8 - inspector_.insp_scroll_y;

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
        bool has_state_presets = sel_node->param_indices.count("states") > 0;
        bool has_outputs = !sel_node->output_port_indices.empty();
        if (has_resolution || has_state_presets || has_outputs)
            draw_section_separator(tr, px, py, kInspContentW, "Technical");
        draw_inspector_resolution(tr, *sel_node, px, py);
        draw_inspector_state_presets(tr, *sel_node, px, py);
        draw_inspector_outputs(tr, *sel_node, px, py);
    }

    // Inspector widget hover highlights
    if (inspector_.hovered_slider_idx >= 0 && inspector_.hovered_slider_idx < static_cast<int>(inspector_.slider_rects.size())) {
        const auto& r = inspector_.slider_rects[inspector_.hovered_slider_idx];
        tr.draw_rect(r.x, r.y, r.w, r.h,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }
    if (inspector_.hovered_bool_idx >= 0 && inspector_.hovered_bool_idx < static_cast<int>(inspector_.bool_rects.size())) {
        const auto& r = inspector_.bool_rects[inspector_.hovered_bool_idx];
        tr.draw_rect(r.x - 2, r.y - 2, r.w + 4, r.h + 4,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }
    if (inspector_.hovered_dropdown_idx >= 0 && inspector_.hovered_dropdown_idx < static_cast<int>(inspector_.dropdown_rects.size())) {
        const auto& r = inspector_.dropdown_rects[inspector_.hovered_dropdown_idx];
        tr.draw_rect(r.x, r.y, r.w, r.h,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }

    tr.pop_clip_rect();

    // Compute content height from final py (relative to viewport top)
    inspector_.insp_content_h = (py + inspector_.insp_scroll_y) - viewport_top;

    // Draw scrollbar outside clip rect
    draw_inspector_scrollbar(tr);
}

void NodeGraphUI::draw_inspector_scrollbar(Renderer2D& tr) {
    float viewport_top = kPerfBarH;
    float viewport_h = static_cast<float>(win_h_) - viewport_top;

    // Only show scrollbar when content overflows
    if (inspector_.insp_content_h <= viewport_h) return;

    float insp_x = inspector_x();
    float track_x = insp_x + kInspectorW - kInspScrollbarW - 2.0f;
    float track_y = viewport_top + 2.0f;
    float track_h = viewport_h - 4.0f;

    // Track background
    tr.draw_rect(track_x, track_y, kInspScrollbarW, track_h,
                 style_.scrollbar_track[0], style_.scrollbar_track[1], style_.scrollbar_track[2], kScrollbarTrackAlpha);

    // Thumb size proportional to viewport/content ratio
    float ratio = viewport_h / inspector_.insp_content_h;
    float thumb_h = std::max(kInspScrollbarMinThumb, track_h * ratio);

    // Thumb position based on scroll ratio
    float max_scroll = inspector_.insp_content_h - viewport_h;
    float scroll_ratio = (max_scroll > 0) ? inspector_.insp_scroll_y / max_scroll : 0.0f;
    float thumb_y = track_y + scroll_ratio * (track_h - thumb_h);

    // Thumb
    bool hovered = mouse_.x >= track_x && mouse_.x <= track_x + kInspScrollbarW &&
                   mouse_.y >= thumb_y && mouse_.y <= thumb_y + thumb_h;
    float thumb_alpha = (hovered || inspector_.insp_scrollbar_dragging) ? kScrollbarThumbHovered : kScrollbarThumbIdle;
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

// draw_core_update_banner moved to DialogManager

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
        inspector_.preset_dropdown_rects.push_back({px, py, dd_w, dd_h, node.node_id, ""});

        // Save button
        float save_x = px + dd_w + gap;
        tr.draw_rect(save_x, py, save_w, dd_h,
                     style_.button_bg[0], style_.button_bg[1], style_.button_bg[2]);
        tr.draw_text(save_x + 8, py + 3, T("save", "Save"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        inspector_.preset_save_rects.push_back({save_x, py, save_w, dd_h, node.node_id, ""});

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
    inspector_.label_rects.push_back({label_x, label_y, label_w, tr.line_height() * 0.85f,
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
            inspector_.lock_badge_rects.push_back({badge_anchor_x, label_y, badge_w, kMidiBadgeH,
                                         node.node_id, pd.name});
        } else {
            inspector_.lock_badge_rects.push_back({badge_anchor_x, label_y, 14.0f, kMidiBadgeH,
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
    inspector_.slider_rects.push_back({knob_rect_x, knob_rect_y, knob_rect_w, knob_rect_h,
                             single_selected_id(), pd.name});

    // Value text rect for click-to-edit
    inspector_.value_text_rects.push_back({val_text_x, val_text_y, val_w, tr.line_height() * 0.8f,
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

    bool editing_x = inspector_.editing_param && inspector_.edit_node_id == single_selected_id()
                     && inspector_.edit_param_name == pd_x.name;
    bool editing_y = inspector_.editing_param && inspector_.edit_node_id == single_selected_id()
                     && inspector_.edit_param_name == pd_y.name;

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
                                inspector_.edit_buffer, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        cx += edit_w;
    } else {
        tr.draw_text(cx, label_y, val_x_str.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
        inspector_.value_text_rects.push_back({cx, label_y, val_x_str_w, lh,
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
                                inspector_.edit_buffer, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
    } else {
        tr.draw_text(cx, label_y, val_y_str.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
        inspector_.value_text_rects.push_back({cx, label_y, val_y_str_w, lh,
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
                inspector_.lock_badge_rects.push_back({badge_x, label_y, bw, kMidiBadgeH,
                                             node.node_id, pd_name});
                badge_x += bw + 3;
            }
        }
    }

    // Hit-test rect for XY pad drag
    inspector_.xy_pad_rects.push_back({pad_x, pad_y, pad_size, pad_size,
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

    inspector_.color_swatch_rects.push_back({px, py, sw, kColorSwatchH,
                                   single_selected_id(),
                                   op.params[pi_r].name, op.params[pi_g].name, op.params[pi_b].name});

    float total_h = kLineH + kColorSwatchH + 8.0f;
    layout.end_param(total_h);
}

// -----------------------------------------------------------------------
// Color picker popup (drawn over everything in overlay pass)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_color_popup(Renderer2D& tr) {
    if (!inspector_.color_popup_open) return;

    float pad = kColorPopupPad;
    float sv_size = kColorPopupSVSize;
    float hue_w = kColorHueBarW;
    float gap = kColorPopupGap;
    float hex_h = kColorHexFieldH;

    float rgb_gap = kColorRGBGap;
    float rgb_h = kColorRGBFieldH;
    float popup_w = pad + sv_size + gap + hue_w + pad;
    float popup_h = pad + sv_size + gap + hex_h + rgb_gap + rgb_h + pad;
    float px = inspector_.color_popup_x;
    float py = inspector_.color_popup_y;

    // Clamp to window bounds
    if (px + popup_w > static_cast<float>(win_w_)) px = static_cast<float>(win_w_) - popup_w;
    if (py + popup_h > static_cast<float>(win_h_)) py = static_cast<float>(win_h_) - popup_h;
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    inspector_.color_popup_x = px;
    inspector_.color_popup_y = py;

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
            hsv_to_rgb(inspector_.color_popup_h, s, v, cr, cg, cb);
            tr.draw_rect(sv_x + xi * cell_w, sv_y + yi * cell_h,
                         cell_w + 0.5f, cell_h + 0.5f, cr, cg, cb);
        }
    }

    // SV crosshair indicator
    float sv_ix = sv_x + inspector_.color_popup_s * sv_size;
    float sv_iy = sv_y + (1.0f - inspector_.color_popup_v) * sv_size;
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
    float hue_iy = hue_y + (inspector_.color_popup_h / 360.0f) * sv_size;
    tr.draw_rect(hue_x - 1, hue_iy - 1, hue_w + 2, 3, 1.0f, 1.0f, 1.0f, 0.9f);
    tr.draw_rect(hue_x - 2, hue_iy - 2, hue_w + 4, 1, 0.0f, 0.0f, 0.0f, 0.5f);
    tr.draw_rect(hue_x - 2, hue_iy + 2, hue_w + 4, 1, 0.0f, 0.0f, 0.0f, 0.5f);

    // Hex input field
    float hex_y = sv_y + sv_size + gap;
    float hex_w_full = sv_size + gap + hue_w;

    // Hex text
    float cr, cg, cb;
    hsv_to_rgb(inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v, cr, cg, cb);
    char hex[8];
    rgb_to_hex(cr, cg, cb, hex, sizeof(hex));

    if (inspector_.color_editing_hex) {
        draw_editing_text_field(tr, style_, sv_x, hex_y, hex_w_full, hex_h,
                                inspector_.color_hex_buffer, text_edit_, cursor_blink_on());
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
        if (inspector_.color_editing_rgb == ch) {
            std::string buf = std::string(rgb_labels[ch]) + " " + inspector_.color_rgb_buffer;
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
    bool is_editing_this = inspector_.editing_param &&
                           inspector_.edit_node_id == single_selected_id() &&
                           inspector_.edit_param_name == pd.name;
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
    inspector_.label_rects.push_back({px, py, tr.text_width(display_label.c_str(), label_scale),
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
        inspector_.lock_badge_rects.push_back({badge_x, py, badge_w, kMidiBadgeH, node.node_id, pd.name});
    } else {
        inspector_.lock_badge_rects.push_back({after_label_x, py, 18.0f, kMidiBadgeH, node.node_id, pd.name});
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
                                    inspector_.edit_buffer, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        } else {
            tr.draw_text(val_x, py, display_val.c_str(),
                         is_connected ? style_.dim_text[0] : 0.8f,
                         is_connected ? style_.dim_text[1] : 0.82f,
                         is_connected ? style_.dim_text[2] : 0.85f,
                         is_connected ? 0.75f : 1.0f, value_scale);
            inspector_.value_text_rects.push_back({val_x, val_y, vw, kLineH, single_selected_id(), pd.name});
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
        inspector_.file_button_rects.push_back({px, py, panel_w, btn_h, single_selected_id(), pd.name});
        py += btn_h + 6.0f;
    } else if (is_text) {
        std::string text_value;
        auto sp_it = node.file_param_values.find(pd.name);
        if (sp_it != node.file_param_values.end()) text_value = sp_it->second;
        float field_h = kDropdownH;
        if (is_editing_this) {
            draw_editing_text_field(tr, style_, px, py, panel_w, field_h,
                                    inspector_.edit_buffer, text_edit_, cursor_blink_on(), 6.0f, 1.0f);
        } else {
            tr.draw_rect(px, py, panel_w, field_h,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            std::string display = text_value.empty() ? "(empty)" : text_value;
            tr.draw_text(px + 6, py + 1, truncate_text(tr, display, panel_w - 12.0f).c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
        inspector_.value_text_rects.push_back({px, py, panel_w, field_h, single_selected_id(), pd.name});
        py += field_h + 6.0f;
    } else if (is_bool) {
        draw_checkbox(tr, style_, px, py, kCheckboxSize, val > 0.5f, is_connected ? 0.3f : 1.0f);
        inspector_.bool_rects.push_back({px, py, kCheckboxSize, kCheckboxSize, single_selected_id(), pd.name});
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
        inspector_.dropdown_rects.push_back({px, py, panel_w, dh, single_selected_id(), pd.name});
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
        inspector_.slider_rects.push_back({px, py - 4.0f, panel_w, sh + 8.0f, single_selected_id(), pd.name});
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

        bool is_editing_min = inspector_.editing_midi_range &&
                              inspector_.midi_range_node_id == single_selected_id() &&
                              inspector_.midi_range_param_name == pd.name &&
                              inspector_.midi_range_editing_min;
        bool is_editing_max = inspector_.editing_midi_range &&
                              inspector_.midi_range_node_id == single_selected_id() &&
                              inspector_.midi_range_param_name == pd.name &&
                              !inspector_.midi_range_editing_min;

        tr.draw_text(px, row_y, T("min", "min"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float min_x = px + 28.0f;
        if (is_editing_min) {
            draw_editing_text_field(tr, style_, min_x, row_y, field_w, kMidiRangeH - 2,
                                    inspector_.edit_buffer, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        } else {
            tr.draw_rect(min_x, row_y, field_w, kMidiRangeH - 2,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            tr.draw_text(min_x + 2, row_y, format_float(midi_mm->range_min, 2).c_str(), 0.8f, 0.82f, 0.85f);
        }
        inspector_.midi_range_rects.push_back({min_x, row_y, field_w, kMidiRangeH, single_selected_id(), pd.name, true});

        float max_label_x = min_x + field_w + 10.0f;
        tr.draw_text(max_label_x, row_y, T("max", "max"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float max_x = max_label_x + 30.0f;
        if (is_editing_max) {
            draw_editing_text_field(tr, style_, max_x, row_y, field_w, kMidiRangeH - 2,
                                    inspector_.edit_buffer, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
        } else {
            tr.draw_rect(max_x, row_y, field_w, kMidiRangeH - 2,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
            tr.draw_text(max_x + 2, row_y, format_float(midi_mm->range_max, 2).c_str(), 0.8f, 0.82f, 0.85f);
        }
        inspector_.midi_range_rects.push_back({max_x, row_y, field_w, kMidiRangeH, single_selected_id(), pd.name, false});

        float remove_x = max_x + field_w + 8.0f;
        tr.draw_rect(remove_x, row_y, 16.0f, kMidiRangeH - 2, 0.5f, 0.2f, 0.2f, 0.8f);
        tr.draw_text(remove_x + 3, row_y, "x", 0.9f, 0.6f, 0.6f);
        inspector_.midi_remove_rects.push_back({remove_x, row_y, 16.0f, kMidiRangeH, single_selected_id(), pd.name});

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

    inspector_.group_header_rects.push_back({hx, hy, hw, kGroupHeaderH, type_name, group_name});
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
// Custom inspector — draw API is populated via shared populate_draw_api() helper
// ---------------------------------------------------------------------------

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
    if (!inspector_.custom_inspector_cb) return;

    InspCmdCtx cmd_ctx{&commands_, node.node_id};

    auto to_vc = [](const std::array<float,3>& a, float alpha = 1.0f) -> VividColor {
        return {a[0], a[1], a[2], alpha};
    };

    VividInspectorContext ctx{};
    ctx.content_x = px;
    ctx.content_y = py;
    ctx.content_width = kInspContentW;

    // Draw API
    populate_draw_api(ctx.draw, tr);

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
    ctx.mouse.left_clicked = inspector_.insp_mouse_left_clicked ? 1 : 0;
    ctx.mouse.left_released = inspector_.insp_mouse_left_released ? 1 : 0;
    ctx.mouse.right_clicked = inspector_.insp_mouse_right_clicked ? 1 : 0;
    ctx.mouse.shift_down = mouse_.shift_down ? 1 : 0;

    // Key/char events
    ctx.key_events = inspector_.insp_key_events.empty() ? nullptr : inspector_.insp_key_events.data();
    ctx.key_event_count = static_cast<uint32_t>(inspector_.insp_key_events.size());
    ctx.char_events = inspector_.insp_char_events.empty() ? nullptr : inspector_.insp_char_events.data();
    ctx.char_event_count = static_cast<uint32_t>(inspector_.insp_char_events.size());

    ctx.time = 0.0;  // could be wired to glfwGetTime if needed
    ctx.consumed_height = 0.0f;
    ctx.wants_keyboard = 0;

    inspector_.custom_inspector_cb(node.node_id, &ctx);

    py += ctx.consumed_height;
    inspector_.custom_inspector_wants_keyboard = ctx.wants_keyboard != 0;

    // Drain event buffers after draw
    inspector_.insp_key_events.clear();
    inspector_.insp_char_events.clear();
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

    bool editing_w = inspector_.editing_resolution &&
                     inspector_.edit_res_node_id == single_selected_id() && inspector_.edit_res_is_width;
    bool editing_h = inspector_.editing_resolution &&
                     inspector_.edit_res_node_id == single_selected_id() && !inspector_.edit_res_is_width;

    std::string w_str = editing_w ? inspector_.edit_buffer : format_uint(node.gpu_tex_width);
    std::string h_str = editing_h ? inspector_.edit_buffer : format_uint(node.gpu_tex_height);

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
            int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(inspector_.edit_buffer.size())));
            float cur_x = val_x + tr.text_width(inspector_.edit_buffer.substr(0, cpos).c_str());
            tr.draw_rect(cur_x, py, 1.0f, kLineH,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
        inspector_.resolution_rects.push_back({val_x, py, kResInputW, kLineH,
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
            int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(inspector_.edit_buffer.size())));
            float cur_x = h_val_x + tr.text_width(inspector_.edit_buffer.substr(0, cpos).c_str());
            tr.draw_rect(cur_x, py, 1.0f, kLineH,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
        inspector_.resolution_rects.push_back({h_val_x, py, kResInputW, kLineH,
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
        auto cit = inspector_.group_collapsed.find(collapse_key);
        if (cit != inspector_.group_collapsed.end()) collapsed = cit->second;

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

        inspector_.state_header_rects.push_back({px, hy, panel_w, kGroupHeaderH, si});
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

            inspector_.state_preset_rects.push_back({dd_x, py, dd_w, dd_h, node.node_id, si, pn.node_id});

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

} // namespace vivid::ui
