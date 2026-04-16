#include <nlohmann/json.hpp>
#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/style/i18n.h"
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
        ? T("midi_map_wiggle", "MIDI MAP: Wiggle a knob...")
        : T("midi_map_click", "MIDI MAP: Click a parameter...");
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
            ? T("preset_none", "(none)") : preset_display.c_str();
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
            if (text_edit_.has_selection()) {
                int lo = text_edit_.sel_min();
                int hi = text_edit_.sel_max();
                float sel_x0 = val_x + tr.text_width(inspector_.edit_buffer.substr(0, lo).c_str());
                float sel_x1 = val_x + tr.text_width(inspector_.edit_buffer.substr(0, hi).c_str());
                tr.draw_rect(sel_x0, py + 1, sel_x1 - sel_x0, kLineH - 2,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
            }
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
            if (text_edit_.has_selection()) {
                int lo = text_edit_.sel_min();
                int hi = text_edit_.sel_max();
                float sel_x0 = h_val_x + tr.text_width(inspector_.edit_buffer.substr(0, lo).c_str());
                float sel_x1 = h_val_x + tr.text_width(inspector_.edit_buffer.substr(0, hi).c_str());
                tr.draw_rect(sel_x0, py + 1, sel_x1 - sel_x0, kLineH - 2,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
            }
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

        char header_buf[64];
        std::snprintf(header_buf, sizeof(header_buf), T("state_header", "State %d"), si);
        std::string header_label = header_buf;
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

            const char* label = current_preset.empty() ? T("preset_none", "(none)") : current_preset.c_str();
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

void NodeGraphUI::draw_inspector_modulation(Renderer2D& tr, const NodeSnapshot& node,
                                            float px, float& py) {
    if (!node.is_module_instance) return;
    if (node.mod_sources.empty() || node.mod_destinations.empty()) return;

    draw_section_separator(tr, px, py, kInspContentW, T("modulation", "Modulation"));

    float row_h = 20.0f;
    float source_w = 100.0f;
    float dest_w = 100.0f;
    float polarity_w = 64.0f;
    float remove_w = 18.0f;
    float gap = 4.0f;
    float amount_x = px + source_w + gap + dest_w + gap + polarity_w + gap;
    float amount_w = kInspContentW - (amount_x - px) - gap - remove_w;

    if (!inspector_.modulation_error.empty()) {
        tr.draw_text(px, py, inspector_.modulation_error.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.85f);
        py += kLineH;
    }

    for (const auto& a : node.mod_assignments) {
        float range = std::max(1.0f, std::ceil(std::fabs(a.amount)));
        float t = std::clamp((a.amount + range) / (range * 2.0f), 0.0f, 1.0f);

        tr.draw_rect(px, py, source_w, row_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        tr.draw_text(px + 6.0f, py + 3.0f, truncate_text(tr, a.source, source_w - 18.0f, 0.75f).c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.75f);
        tr.draw_text(px + source_w - 12.0f, py + 3.0f, "\xe2\x86\x92",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.75f);
        inspector_.mod_assign_rects.push_back({px, py, source_w, row_h, node.node_id, a.source, a.destination, 0});

        float dx = px + source_w + gap;
        tr.draw_rect(dx, py, dest_w, row_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        tr.draw_text(dx + 6.0f, py + 3.0f, truncate_text(tr, a.destination, dest_w - 18.0f, 0.75f).c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.75f);
        tr.draw_text(dx + dest_w - 12.0f, py + 3.0f, "\xe2\x86\x92",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.75f);
        inspector_.mod_assign_rects.push_back({dx, py, dest_w, row_h, node.node_id, a.source, a.destination, 1});

        float pol_x = dx + dest_w + gap;
        tr.draw_rect(pol_x, py, polarity_w, row_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        tr.draw_text(pol_x + 6.0f, py + 3.0f, a.polarity.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.72f);
        inspector_.mod_assign_rects.push_back({pol_x, py, polarity_w, row_h, node.node_id, a.source, a.destination, 2});

        tr.draw_rect(amount_x, py, amount_w, row_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        const float* sc = node_accent_color(node.is_gpu, node.active_cadence);
        tr.draw_rect(amount_x, py, amount_w * t, row_h, sc[0], sc[1], sc[2], 0.85f);
        std::string amount_label = format_float(a.amount, 2);
        tr.draw_text(amount_x + 4.0f, py + 3.0f, amount_label.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.72f);
        inspector_.mod_amount_rects.push_back({amount_x, py, amount_w, row_h, node.node_id, a.source, a.destination, range});

        float rx = amount_x + amount_w + gap;
        tr.draw_rect(rx, py, remove_w, row_h,
                     0.45f, 0.18f, 0.18f, 0.9f);
        tr.draw_text(rx + 5.0f, py + 3.0f, "x",
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.75f);
        inspector_.mod_assign_rects.push_back({rx, py, remove_w, row_h, node.node_id, a.source, a.destination, 3});

        py += row_h + 4.0f;
    }

    float add_w = 92.0f;
    tr.draw_rect(px, py, add_w, row_h,
                 style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.85f);
    tr.draw_text(px + 8.0f, py + 3.0f, T("add_assignment", "+ assignment"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.72f);
    inspector_.mod_assign_rects.push_back({px, py, add_w, row_h, node.node_id, "", "", 4});
    py += row_h + 6.0f;
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

// ---------------------------------------------------------------------------
// Performance surface section — curated view of performance-tagged params
// ---------------------------------------------------------------------------

void NodeGraphUI::draw_inspector_performance(Renderer2D& tr, const NodeSnapshot& node,
                                              float px, float& py) {
    if (!node.op_info) return;
    const auto& params = node.op_info->params;

    // Collect indices of params that have a performance_page set
    struct PerfEntry {
        uint32_t param_idx;
        const std::string* page;
        int order;
    };
    std::vector<PerfEntry> entries;
    for (uint32_t i = 0; i < static_cast<uint32_t>(params.size()); ++i) {
        if (!params[i].performance_page.empty()) {
            entries.push_back({i, &params[i].performance_page, params[i].performance_order});
        }
    }
    if (entries.empty()) return;

    // Sort by (page, order, original_index)
    std::sort(entries.begin(), entries.end(), [](const PerfEntry& a, const PerfEntry& b) {
        if (*a.page != *b.page) return *a.page < *b.page;
        // -1 (unset) sorts after valid orders
        int oa = (a.order >= 0) ? a.order : 0x7FFFFFFF;
        int ob = (b.order >= 0) ? b.order : 0x7FFFFFFF;
        if (oa != ob) return oa < ob;
        return a.param_idx < b.param_idx;
    });

    draw_section_separator(tr, px, py, kInspContentW, T("performance", "Performance"));

    // Draw page sub-headers if there are multiple pages
    bool multi_page = false;
    for (size_t i = 1; i < entries.size(); ++i) {
        if (*entries[i].page != *entries[0].page) { multi_page = true; break; }
    }

    std::string current_page;
    for (const auto& e : entries) {
        if (multi_page && *e.page != current_page) {
            current_page = *e.page;
            tr.draw_text(px, py, current_page.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2],
                         1.0f, kSectionLabelScale);
            py += kLineH;
        }
        draw_one_inspector_param_simple(tr, node, px, py, e.param_idx);
    }
}

} // namespace vivid::ui
