#include <nlohmann/json.hpp>
#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/rendering/text_util.h"
#include "ui/style/i18n.h"
#include "common/string_util.h"
#include "runtime/graph/compiled_graph.h"
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
    inspector_.xy_toggle_rects.clear();
    inspector_.xy_tab_rects.clear();
    inspector_.surface.begin_frame();
    inspector_.color_swatch_rects.clear();
    inspector_.bool_rects.clear();
    inspector_.value_text_rects.clear();
    inspector_.dropdown_rects.clear();
    inspector_.file_button_rects.clear();
    inspector_.resolution_rects.clear();
    inspector_.preset_dropdown_rects.clear();
    inspector_.preset_save_rects.clear();
    inspector_.docs_link_rects.clear();
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
    inspector_.wire_curve_rects.clear();
    inspector_.mod_assign_rects.clear();
    inspector_.mod_amount_rects.clear();

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
            std::string broken = c.invalid_reason.empty() ? T("broken_connection", "Broken connection") : c.invalid_reason;
            tr.draw_text(px, py, broken.c_str(), 1.0f, 0.45f, 0.38f, 0.85f);
            py += 20;
        }

        if (!c.dropped && c.supports_remap()) {
        // Remap fields
        const char* field_labels[4] = {
            T("from_min", "From Min"), T("from_max", "From Max"),
            T("to_min", "To Min"), T("to_max", "To Max") };
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
                if (text_edit_.has_selection()) {
                    int lo = text_edit_.sel_min();
                    int hi = text_edit_.sel_max();
                    float sel_x0 = fx + 4 + tr.text_width(inspector_.edit_buffer.substr(0, lo).c_str());
                    float sel_x1 = fx + 4 + tr.text_width(inspector_.edit_buffer.substr(0, hi).c_str());
                    tr.draw_rect(sel_x0, py + 1, sel_x1 - sel_x0, fh - 2,
                                 style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
                }
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
        py += cb_size + 8;

        // Curve dropdown
        tr.draw_text(px, py + 2, T("curve", "Curve"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);
        float cx = px + 80;
        float cw = kInspectorW - kInspPadX * 2 - 80;
        float ch = 18;
        tr.draw_rect(cx, py, cw, ch,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        auto curve_enum = static_cast<RemapCurve>(c.curve);
        tr.draw_text(cx + 6, py + 2, remap_curve_label(curve_enum),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        tr.draw_text(cx + cw - 16, py + 2, "\xE2\x96\xBE",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        inspector_.wire_curve_rects.push_back({cx, py, cw, ch});
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

    // Error banner for errored nodes (includes compile errors where errored=false).
    // Safe-mode-suppressed nodes (disabled or quarantined) get an amber prefix
    // rather than red "Error:" — they were intentionally suppressed, not broken.
    if (!sel_node->error_message.empty()) {
        const bool quarantined = sel_node->quarantined;
        const bool disabled    = sel_node->disabled_by_safe_mode;
        const bool suppressed  = quarantined || disabled;
        const char* prefix =
              quarantined ? T("quarantined_label", "Quarantined:")
            : disabled    ? T("disabled_label",    "Disabled:")
            :               T("error_label",       "Error:");
        const auto& col = suppressed ? kDisabledAccent : kErrorAccent;
        const std::string label = std::string(prefix) + " " + sel_node->error_message;
        tr.draw_text(px, py, label.c_str(), col[0], col[1], col[2]);
        py += kLineH + 4;
        if (suppressed) {
            const char* hint = quarantined
                ? T("quarantined_hint",
                    "This operator has crashed repeatedly. "
                    "Launch without --safe-mode only after fixing the underlying issue.")
                : T("disabled_hint",
                    "Restart without --safe-mode to re-enable this operator.");
            tr.draw_text(px, py, hint, col[0], col[1], col[2], 0.7f);
            py += kLineH + 4;
        }
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
            draw_section_separator(tr, px, py, kInspContentW, T("controls", "Controls"));
            draw_inspector_params(tr, *sel_node, px, py);
        }
        if (has_custom_inspector) {
            draw_section_separator(tr, px, py, kInspContentW, T("custom", "Custom"));
        }
        draw_custom_inspector(tr, *sel_node, px, py);
    } else {
        if (has_visible_standard_params) {
            draw_section_separator(tr, px, py, kInspContentW, T("controls", "Controls"));
            draw_inspector_params(tr, *sel_node, px, py);
        }
    }
    if (sel_node->is_module_instance) {
        draw_inspector_modulation(tr, *sel_node, px, py);
    }
    // --- Performance section (module instances with performance-tagged params) ---
    if (sel_node->is_module_instance) {
        draw_inspector_performance(tr, *sel_node, px, py);
    }
    // --- Technical section ---
    {
        bool has_resolution = sel_node->is_gpu && sel_node->gpu_tex_width > 0;
        bool has_state_presets = sel_node->param_indices.count("states") > 0;
        bool has_outputs = !sel_node->output_port_indices.empty();
        if (has_resolution || has_state_presets || has_outputs)
            draw_section_separator(tr, px, py, kInspContentW, T("technical", "Technical"));
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


} // namespace vivid::ui
