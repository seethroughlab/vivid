#include <nlohmann/json.hpp>
#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/inspector/inspector_widget_registry.h"
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

static bool param_visible(const ParamInfo& pd, const NodeSnapshot& node) {
    return param_info_visible(pd, node.param_values);
}

static bool param_run_visible(const OperatorInfo& op, const NodeSnapshot& node,
                              uint32_t start, uint32_t count) {
    return param_info_run_visible(op.params, node.param_values, start, count);
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
    // Brighten label for non-default params
    const float* knob_label_clr = (val != pd.default_value) ? style_.bright_text.data()
                                                            : style_.dim_text.data();
    tr.draw_text(label_x, label_y, label_text.c_str(),
                 knob_label_clr[0], knob_label_clr[1], knob_label_clr[2], 1.0f, 0.85f);
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
// ADSR envelope widget (inline in inspector)
// -----------------------------------------------------------------------

// Curve shaping helpers (mirroring adsr_inspector.h, local to avoid header dep)
static float adsr_shape_attack(float t, int curve) {
    t = std::max(0.0f, std::min(1.0f, t));
    switch (curve) {
        case 0: return t;
        case 1: return 1.0f - std::exp(-4.0f * t);
        case 2: return t * t;
        default: return t;
    }
}
static float adsr_shape_decay(float t, int curve) {
    t = std::max(0.0f, std::min(1.0f, t));
    switch (curve) {
        case 0: return t;
        case 1: return 1.0f - std::exp(-4.0f * t);
        case 2: return t * t;
        default: return t;
    }
}

void NodeGraphUI::draw_inspector_adsr(Renderer2D& tr, const NodeSnapshot& node,
                                       InspectorLayout& layout,
                                       uint32_t pi_a, uint32_t pi_d,
                                       uint32_t pi_s, uint32_t pi_r) {
    const auto& op = *node.op_info;
    float attack  = std::max(0.0001f, node.param_values[pi_a]);
    float decay   = std::max(0.001f,  node.param_values[pi_d]);
    float sustain = std::max(0.0f, std::min(1.0f, node.param_values[pi_s]));
    float release = std::max(0.001f,  node.param_values[pi_r]);

    float px = layout.base_x;
    float py = layout.y;
    float w  = layout.full_w;
    float h  = kADSRWidgetH;
    float pad = kADSRPad;

    // Dark background
    tr.draw_rect(px, py, w, h,
                 style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2]);

    // Envelope evaluation
    float sustain_width = 0.3f * (attack + decay + release);
    float total_time = attack + decay + sustain_width + release;

    auto env_at = [&](float t) -> float {
        if (t <= attack) return adsr_shape_attack(t / attack, 0);
        t -= attack;
        if (t <= decay) return 1.0f - (1.0f - sustain) * adsr_shape_decay(t / decay, 0);
        t -= decay;
        if (t <= sustain_width) return sustain;
        t -= sustain_width;
        if (t <= release) return sustain * (1.0f - adsr_shape_decay(t / release, 0));
        return 0.0f;
    };

    auto time_to_x = [&](float t) -> float {
        return px + pad + (t / total_time) * (w - 2.0f * pad);
    };
    auto env_to_y = [&](float e) -> float {
        return py + pad + (1.0f - e) * (h - 2.0f * pad);
    };

    // Filled area under curve (column fill)
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
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.15f);
        }
    }

    // Envelope line
    int segments = std::max(4, cols / 2);
    float lx = time_to_x(0.0f);
    float ly = env_to_y(env_at(0.0f));
    for (int i = 1; i <= segments; ++i) {
        float t = (static_cast<float>(i) / static_cast<float>(segments)) * total_time;
        float cx = time_to_x(t);
        float cy = env_to_y(env_at(t));
        tr.draw_line(lx, ly, cx, cy, 1.5f,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
        lx = cx;
        ly = cy;
    }

    // Dashed vertical markers at A, A+D, A+D+S transitions
    float marker_times[3] = { attack, attack + decay, attack + decay + sustain_width };
    for (float mt : marker_times) {
        float mx = time_to_x(mt);
        float top_y = py + pad;
        for (float dy = top_y; dy < bottom_y; dy += 8.0f) {
            float dash_end = std::min(dy + 4.0f, bottom_y);
            tr.draw_line(mx, dy, mx, dash_end, 1.0f,
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.3f);
        }
    }

    // Control points (small filled rects acting as dots)
    float dot_r = kADSRDotRadius;
    struct CtrlPt { float cx, cy; };
    CtrlPt pts[3] = {
        { time_to_x(attack), env_to_y(1.0f) },                          // attack peak
        { time_to_x(attack + decay), env_to_y(sustain) },               // decay/sustain junction
        { time_to_x(attack + decay + sustain_width + release), env_to_y(0.0f) }  // release end
    };

    const auto& adsr_target = inspector_.surface.add_adsr(px, py, w, h,
        single_selected_id(),
        op.params[pi_a].name, op.params[pi_d].name,
        op.params[pi_s].name, op.params[pi_r].name);
    bool is_dragging = inspector_.surface.is_active(adsr_target.id);
    for (int i = 0; i < 3; ++i) {
        float alpha = (is_dragging && inspector_.surface.active_part() == i) ? 1.0f : 0.8f;
        tr.draw_rounded_rect(pts[i].cx - dot_r, pts[i].cy - dot_r,
                             dot_r * 2, dot_r * 2, dot_r,
                             style_.accent[0], style_.accent[1], style_.accent[2], alpha);
    }

    // --- Value labels below the curve ---
    float label_y = py + h + kADSRLabelGap;
    float lh = tr.line_height() * 0.85f;

    uint32_t adsr_pis[4] = { pi_a, pi_d, pi_s, pi_r };
    const char* adsr_labels[4] = { "A", "D", "S", "R" };

    // Measure total label width to center it
    float total_label_w = 0;
    for (int i = 0; i < 4; ++i) {
        total_label_w += tr.text_width(adsr_labels[i], 0.85f) + tr.text_width(": ", 0.85f);
        total_label_w += tr.text_width(format_float(node.param_values[adsr_pis[i]], 2).c_str(), 0.85f);
        if (i < 3) total_label_w += tr.text_width("  ", 0.85f);
    }
    float cx = px + (w - total_label_w) * 0.5f;

    for (int i = 0; i < 4; ++i) {
        uint32_t pi = adsr_pis[i];
        float val = node.param_values[pi];
        std::string val_str = format_float(val, 2);
        std::string name_part = std::string(adsr_labels[i]) + ": ";

        bool editing_this = inspector_.editing_param &&
                            inspector_.edit_node_id == single_selected_id() &&
                            inspector_.edit_param_name == op.params[pi].name;

        // Name portion
        tr.draw_text(cx, label_y, name_part.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
        cx += tr.text_width(name_part.c_str(), 0.85f);

        // Value portion (or edit field)
        if (editing_this) {
            float edit_w = std::max(tr.text_width(val_str.c_str(), 0.85f) + 8.0f, 40.0f);
            draw_editing_text_field(tr, style_, cx, label_y, edit_w, lh,
                                    inspector_.edit_buffer, text_edit_, cursor_blink_on(), 2.0f, 0.0f);
            cx += edit_w;
        } else {
            float val_w = tr.text_width(val_str.c_str(), 0.85f);
            tr.draw_text(cx, label_y, val_str.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
            inspector_.value_text_rects.push_back({cx, label_y, val_w, lh,
                                         single_selected_id(), op.params[pi].name});
            cx += val_w;
        }

        if (i < 3) {
            tr.draw_text(cx, label_y, "  ",
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 1.0f, 0.85f);
            cx += tr.text_width("  ", 0.85f);
        }
    }

    // --- Badges row (lock, MIDI, connection source) ---
    float badge_y = label_y + lh + 2.0f;
    float badge_x = px;
    bool has_badges = false;

    for (int i = 0; i < 4; ++i) {
        uint32_t pi = adsr_pis[i];
        const auto& pd = op.params[pi];

        // Lock badge
        uint8_t lock = (pi < node.param_lock_flags.size()) ? node.param_lock_flags[pi] : 0;
        if (lock != kParamLockNone) {
            const char* lock_text =
                (lock == (kParamLockWires | kParamLockPresets)) ? "WP" :
                (lock & kParamLockWires) ? "W" : "P";
            std::string badge_label = std::string(adsr_labels[i]) + ":" + lock_text;
            float bw = tr.text_width(badge_label.c_str(), 0.75f) + 6;
            tr.draw_rect(badge_x, badge_y, bw, kMidiBadgeH,
                         0.6f, 0.45f, 0.15f, 0.85f);
            tr.draw_text(badge_x + 3, badge_y, badge_label.c_str(), 1.0f, 0.85f, 0.4f, 1.0f, 0.75f);
            inspector_.lock_badge_rects.push_back({badge_x, badge_y, bw, kMidiBadgeH,
                                         node.node_id, pd.name});
            badge_x += bw + 3;
            has_badges = true;
        }

        // MIDI CC badge
        const auto* midi_mm = snap_.find_midi_mapping(single_selected_id(), pd.name);
        if (midi_mm) {
            std::string badge = std::string(adsr_labels[i]) + ":CC" + std::to_string(midi_mm->cc_number);
            float bw = tr.text_width(badge.c_str(), 0.75f) + 8.0f;
            tr.draw_rect(badge_x, badge_y, bw, kMidiBadgeH,
                         kMidiMapBadge[0], kMidiMapBadge[1], kMidiMapBadge[2], kMidiMapBadge[3]);
            tr.draw_text(badge_x + 4, badge_y, badge.c_str(), 0.85f, 0.90f, 1.0f, 1.0f, 0.75f);
            badge_x += bw + 3;
            has_badges = true;
        }

        // Connection source dot
        auto conn = find_param_connection(snap_, node.node_id, pd.name);
        if (conn.connected) {
            const auto* src_ns = snap_.find_node(conn.from_node);
            const float* dot_clr = src_ns ? node_accent_color(src_ns->is_gpu, src_ns->active_cadence)
                                          : style_.accent.data();
            float dot_sz = 5.0f;
            tr.draw_rect(badge_x, badge_y + (kMidiBadgeH - dot_sz) * 0.5f, dot_sz, dot_sz,
                         dot_clr[0], dot_clr[1], dot_clr[2], 0.9f);
            badge_x += dot_sz + 3;
            has_badges = true;
        }
    }

    float total_h = h + kADSRLabelGap + lh + 2.0f + (has_badges ? kMidiBadgeH + 2.0f : 0.0f) + 8.0f;
    layout.end_param(total_h);
}

// -----------------------------------------------------------------------
// LFO waveform preview widget (inline in inspector)
// -----------------------------------------------------------------------

static float lfo_waveform_sample(float phase, int waveform) {
    float p = phase - std::floor(phase);
    switch (waveform) {
        case 0: return std::sin(p * 2.0f * static_cast<float>(M_PI));    // sine
        case 1: return 2.0f * p - 1.0f;                                  // saw
        case 2: return (p < 0.5f) ? 1.0f : -1.0f;                       // square
        case 3: return 4.0f * ((p < 0.5f) ? p : 1.0f - p) - 1.0f;      // triangle
        case 4: {                                                         // sample & hold
            int step = static_cast<int>(std::floor(p * 8.0f));
            uint32_t x = static_cast<uint32_t>(step) * 1664525u + 17u * 1013904223u;
            x ^= x >> 16; x *= 2246822519u; x ^= x >> 13;
            return static_cast<float>(x & 0xffffu) / 65535.0f * 2.0f - 1.0f;
        }
        case 5: {                                                         // smooth random
            float fp = p * 6.0f;
            int step = static_cast<int>(std::floor(fp));
            float t = fp - static_cast<float>(step);
            auto hash = [](int i) {
                uint32_t x = static_cast<uint32_t>(i) * 1664525u + 29u * 1013904223u;
                x ^= x >> 16; x *= 2246822519u; x ^= x >> 13;
                return static_cast<float>(x & 0xffffu) / 65535.0f * 2.0f - 1.0f;
            };
            float a = hash(step), b = hash(step + 1);
            return a + (b - a) * (t * t * (3.0f - 2.0f * t));
        }
        case 6:                                                           // noise
        default: {
            uint32_t x = static_cast<uint32_t>(p * 24.0f) * 1664525u + 47u * 1013904223u;
            x ^= x >> 16; x *= 2246822519u; x ^= x >> 13;
            return static_cast<float>(x & 0xffffu) / 65535.0f * 2.0f - 1.0f;
        }
    }
}

static const char* lfo_wave_name(int waveform) {
    switch (waveform) {
        case 0: return "SIN";  case 1: return "SAW";  case 2: return "SQR";
        case 3: return "TRI";  case 4: return "S&H";  case 5: return "SMTH";
        case 6: return "NOISE"; default: return "LFO";
    }
}

void NodeGraphUI::draw_inspector_lfo_preview(Renderer2D& tr, const NodeSnapshot& node,
                                              InspectorLayout& layout, uint32_t pi) {
    const auto& op = *node.op_info;
    const auto& pd = op.params[pi];
    float val = node.param_values[pi];
    int waveform = static_cast<int>(val);

    float px = layout.base_x;
    float py = layout.y;
    float w  = layout.full_w;
    float h  = kLFOPreviewH;
    float pad = kLFOPreviewPad;

    // Dark background
    tr.draw_rect(px, py, w, h,
                 style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2]);

    // Waveform plot: 2 cycles, sampled at ~80 points
    float plot_x = px + pad;
    float plot_y = py + pad;
    float plot_w = w - 2.0f * pad;
    float plot_h = h - 2.0f * pad;
    float center_y = plot_y + plot_h * 0.5f;

    // Center line
    tr.draw_line(plot_x, center_y, plot_x + plot_w, center_y, 1.0f,
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.15f);

    // Waveform line
    constexpr int segments = 80;
    float prev_x = plot_x;
    float prev_y = center_y - lfo_waveform_sample(0.0f, waveform) * (plot_h * 0.5f - 2.0f);
    for (int i = 1; i <= segments; ++i) {
        float phase = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f; // 2 cycles
        float sx = plot_x + (static_cast<float>(i) / static_cast<float>(segments)) * plot_w;
        float sy = center_y - lfo_waveform_sample(phase, waveform) * (plot_h * 0.5f - 2.0f);
        tr.draw_line(prev_x, prev_y, sx, sy, 1.5f,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
        prev_x = sx;
        prev_y = sy;
    }

    // Waveform name label (top-left)
    tr.draw_text(px + pad + 2, py + 2, lfo_wave_name(waveform),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.6f, 0.75f);

    py += h + 2.0f;

    // Dropdown selector below the preview (reuse standard dropdown drawing)
    float dh = kDropdownH;
    tr.draw_rect(px, py, w, dh,
                 style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
    int idx = static_cast<int>(val);
    const char* choice_label = (idx >= 0 && idx < static_cast<int>(pd.choice_labels.size()))
        ? pd.choice_labels[idx].c_str() : "?";
    std::string display_choice = truncate_text(tr, choice_label, w - 22.0f);
    auto conn = find_param_connection(snap_, node.node_id, pd.name);
    bool is_connected = conn.connected;
    tr.draw_text(px + 6, py + 1, display_choice.c_str(),
                 is_connected ? style_.dim_text[0] : style_.bright_text[0],
                 is_connected ? style_.dim_text[1] : style_.bright_text[1],
                 is_connected ? style_.dim_text[2] : style_.bright_text[2],
                 is_connected ? 0.75f : 1.0f);
    tr.draw_text(px + w - 16, py + 1, "\xE2\x96\xBE",
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    inspector_.dropdown_rects.push_back({px, py, w, dh, single_selected_id(), pd.name});
    py += dh;

    // Badges row (lock, MIDI, connection)
    float badge_y = py + 2.0f;
    float badge_x = px;
    bool has_badges = false;

    uint8_t lock = (pi < node.param_lock_flags.size()) ? node.param_lock_flags[pi] : 0;
    if (lock != kParamLockNone) {
        const char* lock_text =
            (lock == (kParamLockWires | kParamLockPresets)) ? "WP" :
            (lock & kParamLockWires) ? "W" : "P";
        float bw = tr.text_width(lock_text, 0.8f) + 8.0f;
        tr.draw_rect(badge_x, badge_y, bw, kMidiBadgeH,
                     0.6f, 0.45f, 0.15f, 0.85f);
        tr.draw_text(badge_x + 4, badge_y, lock_text, 1.0f, 0.85f, 0.4f, 1.0f, 0.8f);
        inspector_.lock_badge_rects.push_back({badge_x, badge_y, bw, kMidiBadgeH, node.node_id, pd.name});
        badge_x += bw + 3;
        has_badges = true;
    }

    const auto* midi_mm = snap_.find_midi_mapping(single_selected_id(), pd.name);
    if (midi_mm) {
        std::string badge = "CC " + std::to_string(midi_mm->cc_number);
        float bw = tr.text_width(badge.c_str(), 0.8f) + 8.0f;
        tr.draw_rect(badge_x, badge_y, bw, kMidiBadgeH,
                     kMidiMapBadge[0], kMidiMapBadge[1], kMidiMapBadge[2], kMidiMapBadge[3]);
        tr.draw_text(badge_x + 4, badge_y, badge.c_str(), 0.85f, 0.90f, 1.0f, 1.0f, 0.8f);
        badge_x += bw + 3;
        has_badges = true;
    }

    if (is_connected) {
        const auto* src_ns = snap_.find_node(conn.from_node);
        const float* dot_clr = src_ns ? node_accent_color(src_ns->is_gpu, src_ns->active_cadence)
                                      : style_.accent.data();
        float dot_sz = 5.0f;
        tr.draw_rect(badge_x, badge_y + (kMidiBadgeH - dot_sz) * 0.5f, dot_sz, dot_sz,
                     dot_clr[0], dot_clr[1], dot_clr[2], 0.9f);
        has_badges = true;
    }

    float total_h = h + 2.0f + dh + (has_badges ? kMidiBadgeH + 4.0f : 0.0f) + 8.0f;
    layout.end_param(total_h);
}

// -----------------------------------------------------------------------
// Step sequencer grid widget (inline in inspector)
// -----------------------------------------------------------------------

void NodeGraphUI::draw_inspector_step_seq(Renderer2D& tr, const NodeSnapshot& node,
                                           InspectorLayout& layout,
                                           uint32_t pi_start, uint32_t param_run_count) {
    const auto& op = *node.op_info;
    float px = layout.base_x;
    float py = layout.y;
    float w  = layout.full_w;
    float h  = kStepSeqWidgetH;
    float pad = kStepSeqPad;

    // First param is the step count
    int max_steps = static_cast<int>(param_run_count - 1);
    int num_steps = std::max(1, std::min(max_steps, static_cast<int>(node.param_values[pi_start])));

    // Identify value and gate params by naming convention
    uint32_t pi_first_param = pi_start + 1;
    uint32_t total_step_params = param_run_count - 1;
    uint32_t value_count = 0;
    uint32_t gate_count = 0;
    uint32_t pi_values = pi_first_param;
    uint32_t pi_gates = 0;

    // Scan for gate params — they come after value params
    for (uint32_t i = 0; i < total_step_params; ++i) {
        const auto& pname = op.params[pi_first_param + i].name;
        if (pname.find("gate") != std::string::npos) {
            if (gate_count == 0) {
                pi_gates = pi_first_param + i;
                value_count = i;
            }
            gate_count++;
        }
    }
    if (gate_count == 0) {
        value_count = total_step_params;
    }

    // Dark background
    tr.draw_rect(px, py, w, h,
                 style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2]);

    float plot_x = px + pad;
    float plot_y = py + pad;
    float plot_w = w - 2.0f * pad;
    float plot_h = h - 2.0f * pad;
    float bar_w = plot_w / static_cast<float>(num_steps);

    // Draw step bars
    for (int i = 0; i < num_steps && i < static_cast<int>(value_count); ++i) {
        uint32_t vi = pi_values + static_cast<uint32_t>(i);
        float sv = node.param_values[vi];
        // Normalize to [0, 1] using param range
        const auto& vpd = op.params[vi];
        float range = vpd.max_value - vpd.min_value;
        float norm = (range > 0) ? (sv - vpd.min_value) / range : sv;
        norm = std::max(0.0f, std::min(1.0f, norm));

        float bx = plot_x + static_cast<float>(i) * bar_w + kStepSeqBarGap;
        float bw = bar_w - 2.0f * kStepSeqBarGap;
        if (bw < 1.0f) bw = 1.0f;

        float bar_h = norm * plot_h;
        float by = plot_y + plot_h - bar_h;

        // Bar fill
        tr.draw_rect(bx, by, bw, bar_h,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.4f);

        // Gate-off overlay
        if (gate_count > 0 && i < static_cast<int>(gate_count)) {
            float sg = node.param_values[pi_gates + static_cast<uint32_t>(i)];
            if (sg < 0.99f) {
                float gate_off_h = bar_h * (1.0f - sg);
                tr.draw_rect(bx, by, bw, gate_off_h,
                             style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.5f);
            }
        }
    }

    inspector_.surface.add_step_seq(px, py, w, h,
                                    single_selected_id(),
                                    pi_start, pi_values, value_count,
                                    pi_gates, gate_count);

    float total_h = h + 8.0f;
    layout.end_param(total_h);
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

    // Non-default check (used for label brightening)
    bool is_non_default = false;
    if (is_file || is_text) {
        auto fit = node.file_param_values.find(pd.name);
        std::string current_str = (fit != node.file_param_values.end()) ? fit->second : "";
        is_non_default = (current_str != pd.default_string);
    } else {
        is_non_default = (val != pd.default_value);
    }

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
    // Brighten label for non-default params; dim for connected params
    const float* label_clr = is_connected ? style_.dim_text.data()
                           : is_non_default ? style_.bright_text.data()
                           : style_.dim_text.data();
    float label_alpha = is_connected ? 0.75f : 1.0f;
    tr.draw_text(px, py, display_label.c_str(),
                 label_clr[0], label_clr[1], label_clr[2],
                 label_alpha, label_scale);
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

        std::string display_name = T("browse_file", "Browse\xe2\x80\xa6");
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

            if (!param_visible(pd, node)) { ++pi; continue; }

            if (pd.group != current_group) {
                layout.flush_row();
                current_group = pd.group;

                if (!current_group.empty()) {
                    if (!drew_secondary_controls) {
                        draw_section_separator(tr, px, layout.y, kInspContentW, T("secondary_controls", "Secondary Controls"));
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

            InspectorWidgetRun widget_run = inspector_widget_run_at(op.params, pi);
            if (widget_run.kind == InspectorWidgetKind::kCustom &&
                !node.op_info->has_custom_inspector) {
                widget_run = {};
            }
            if (widget_run.kind != InspectorWidgetKind::kNone &&
                param_run_visible(op, node, pi, widget_run.length)) {
                layout.flush_row();
                layout.begin_param(0, 0);

                switch (widget_run.kind) {
                case InspectorWidgetKind::kXYPad:
                    draw_inspector_xy_pad(tr, node, layout, pi, pi + 1);
                    break;
                case InspectorWidgetKind::kColor:
                    draw_inspector_color_swatch(tr, node, layout, pi, pi + 1, pi + 2);
                    break;
                case InspectorWidgetKind::kADSR:
                    draw_inspector_adsr(tr, node, layout, pi, pi + 1, pi + 2, pi + 3);
                    break;
                case InspectorWidgetKind::kLFO:
                    draw_inspector_lfo_preview(tr, node, layout, pi);
                    break;
                case InspectorWidgetKind::kStepSeq:
                    draw_inspector_step_seq(tr, node, layout, pi, widget_run.length);
                    break;
                case InspectorWidgetKind::kCustom:
                    layout.end_param(0.0f);
                    break;
                case InspectorWidgetKind::kNone:
                    break;
                }

                pi += widget_run.length;
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
                if (!param_visible(cpd, node)) return false;
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
                    if (!param_visible(cpd, node)) break;
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
                    inspector_widget_run_at(op.params, pi + 1).kind != InspectorWidgetKind::kNone;
                if (param_visible(next_pd, node) &&
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

} // namespace vivid::ui
