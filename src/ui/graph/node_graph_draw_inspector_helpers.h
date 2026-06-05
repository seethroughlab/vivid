#pragma once
// Shared inspector draw-helpers (audit 08-R2-F1).
//
// These small, pure draw/format helpers were previously duplicated as byte-
// identical `static` copies in node_graph_draw_inspector.cpp,
// node_graph_draw_inspector_sections.cpp, and node_graph_draw_inspector_params.cpp.
// Hoisted here as `inline` so the three inspector TUs share one definition.

#include "ui/graph/graph_snapshot.h"   // ParamInfo, GraphSnapshot
#include "ui/rendering/renderer_2d.h"  // Renderer2D
#include "ui/style/ui_style.h"         // UIStyle
#include <string>

namespace vivid::ui {

inline std::string build_semantic_hint(const ParamInfo& pd) {
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

inline InspectorCardBox draw_inspector_card(Renderer2D& tr, const UIStyle& style,
                                            float x, float y, float w, float h,
                                            float inner_pad, float alpha = 0.55f) {
    tr.draw_rect(x, y, w, h,
                 style.slider_track[0], style.slider_track[1], style.slider_track[2], alpha);
    return InspectorCardBox{ x, y, w, h, x + inner_pad, w - inner_pad * 2.0f };
}

inline float draw_inspector_env_chip(Renderer2D& tr, float x, float y,
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

inline float draw_inspector_text_button(Renderer2D& tr, const UIStyle& style,
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

inline void draw_inspector_left_accent(Renderer2D& tr, float x, float top, float bottom,
                                       const float* color, float alpha = 0.5f) {
    tr.draw_rect(x - 4.0f, top, 2.0f, bottom - top, color[0], color[1], color[2], alpha);
}

struct ParamConnectionInfo {
    bool connected = false;
    std::string from_node;
    std::string from_port;
    std::string source_label;
};

inline ParamConnectionInfo find_param_connection(const GraphSnapshot& snap,
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

}  // namespace vivid::ui
