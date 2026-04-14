#pragma once

#include <cstddef>
#include <cstdint>

namespace vivid::ui {

struct OverlayRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct OverlayPanelLayout {
    float wf = 0.0f;
    float hf = 0.0f;
    float pw = 0.0f;
    float ph = 0.0f;
    float px = 0.0f;
    float py = 0.0f;
    float cx = 0.0f;
    float inner_w = 0.0f;
    int visible_count = 0;
    float list_h = 0.0f;
    float header_y = 0.0f;
    float search_y = 0.0f;
    float list_top = 0.0f;
    float preview_top = 0.0f;
    float preview_h = 0.0f;
    float status_y = 0.0f;
    float tabs_y = 0.0f;   // kind tabs (All/Instruments/Examples)
    float tabs2_y = 0.0f;  // env tabs
    float tabs3_y = 0.0f;  // difficulty + toggles
    float tabs4_y = 0.0f;  // sort tabs
};

OverlayPanelLayout compute_create_operator_layout(uint32_t win_w, uint32_t win_h,
                                                   bool show_child_op);
OverlayPanelLayout compute_package_browser_layout(uint32_t win_w, uint32_t win_h, size_t entry_count);
OverlayPanelLayout compute_example_browser_layout(uint32_t win_w, uint32_t win_h,
                                                  size_t entry_count,
                                                  size_t preview_row_count = 0);
OverlayPanelLayout compute_graph_meta_editor_layout(uint32_t win_w, uint32_t win_h);
OverlayPanelLayout compute_about_layout(uint32_t win_w, uint32_t win_h);
OverlayRect compute_example_open_button_rect(const OverlayPanelLayout& layout, float item_y);
OverlayRect compute_package_action_button_rect(const OverlayPanelLayout& layout, float item_y);

inline bool overlay_contains(const OverlayPanelLayout& l, float x, float y) {
    return x >= l.px && x <= l.px + l.pw && y >= l.py && y <= l.py + l.ph;
}

inline bool overlay_contains(const OverlayRect& r, float x, float y) {
    return x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

} // namespace vivid::ui
