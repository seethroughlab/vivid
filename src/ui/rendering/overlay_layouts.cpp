#include "ui/rendering/overlay_layouts.h"
#include "ui/graph/node_graph_constants.h"
#include <algorithm>

namespace vivid::ui {

OverlayPanelLayout compute_create_operator_layout(uint32_t win_w, uint32_t win_h,
                                                   bool show_child_op) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.pw = kCreateModalW;

    // Calculate content height:
    //   pad_y + title(24) + env_btns(22+10) + [child_op_row(24+8)] + name_field(24+8)
    //   + hint_line(18+8) + destination_row(22+8)
    //   + error_area(18+8) + button_row(26) + pad_y
    float h = kCreateModalPadY;
    h += 24.0f;  // title
    h += kCreateEnvBtnH + 10.0f;  // env buttons
    if (show_child_op) h += 24.0f + kCreateModalRowGap;  // child-op checkbox row
    h += kCreateModalFieldH + kCreateModalRowGap;  // name field
    h += kCreateModalSectionGap + 18.0f + kCreateModalRowGap;  // MCP hint line
    h += kCreateModalSectionGap + 22.0f + kCreateModalRowGap;  // destination
    h += 18.0f + kCreateModalRowGap;  // error area
    h += kCreateModalBtnH + kCreateModalPadY;  // buttons + bottom pad

    l.ph = std::min(h, l.hf - 40.0f);
    l.px = (l.wf - l.pw) * 0.5f;
    l.py = (l.hf - l.ph) * 0.5f;
    l.cx = l.px + kCreateModalPadX;
    l.inner_w = l.pw - 2.0f * kCreateModalPadX;
    return l;
}

OverlayPanelLayout compute_package_browser_layout(uint32_t win_w, uint32_t win_h, size_t entry_count) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.visible_count = std::min(static_cast<int>(entry_count), kPkgBrowserMaxVisible);
    l.list_h = l.visible_count * kPkgBrowserItemH;

    // Build positions with a single cursor (relative to panel top)
    float cy = kPkgBrowserPadY;
    float header_y = cy;
    cy += kPkgBrowserHeaderH;
    float search_y = cy;
    cy += kPkgBrowserSearchH + 6.0f;
    float tabs_y = cy;
    cy += kPkgBrowserTabH + 8.0f;
    float list_top = cy;
    cy += l.list_h;
    cy += 8.0f + 18.0f + kPkgBrowserPadY;  // status + bottom pad
    float content_h = cy;

    l.pw = kPkgBrowserW;
    l.ph = std::min(kPkgBrowserMaxH, std::min(content_h, l.hf - 40.0f));
    l.px = (l.wf - l.pw) * 0.5f;
    l.py = (l.hf - l.ph) * 0.5f;
    l.cx = l.px + kPkgBrowserPadX;
    l.inner_w = l.pw - 2.0f * kPkgBrowserPadX;

    // Absolute positions = panel top + relative offsets
    l.header_y = l.py + header_y;
    l.search_y = l.py + search_y;
    l.tabs_y = l.py + tabs_y;
    l.list_top = l.py + list_top;
    l.status_y = l.list_top + l.list_h + 8.0f;
    return l;
}

OverlayPanelLayout compute_example_browser_layout(uint32_t win_w, uint32_t win_h,
                                                  size_t entry_count,
                                                  size_t preview_row_count) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.visible_count = std::min(static_cast<int>(entry_count), kPkgBrowserMaxVisible);
    const size_t visible_preview_rows = std::min<size_t>(preview_row_count, 3);
    l.preview_h = visible_preview_rows == 0 ? 0.0f
                : (8.0f + 16.0f + static_cast<float>(visible_preview_rows) * 18.0f + 8.0f);

    // Build positions with a single cursor (relative to panel top).
    // Every element's Y is derived from this cursor — no separate sum to keep in sync.
    float cy = kPkgBrowserPadY;
    float header_y = cy;
    cy += kPkgBrowserHeaderH;
    float search_y = cy;
    cy += kPkgBrowserSearchH + 6.0f;
    float tabs_y = cy;
    cy += kPkgBrowserTabH + 8.0f;
    float tabs2_y = cy;
    cy += kPkgBrowserTabH + 8.0f;
    float tabs3_y = cy;
    cy += kPkgBrowserTabH + 8.0f;
    float tabs4_y = cy;
    cy += kPkgBrowserTabH + 8.0f;
    float list_top = cy;
    // list_h filled in below after clamping
    float fixed_above = cy;
    float fixed_below = (l.preview_h > 0.0f ? 8.0f + l.preview_h : 0.0f)
                      + 8.0f + 18.0f + kPkgBrowserPadY;

    l.pw = kPkgBrowserW + 120.0f;
    float max_ph = std::min(kPkgBrowserMaxH + 70.0f, l.hf - 40.0f);

    // Reduce visible items if panel height is constrained
    float avail_for_list = max_ph - fixed_above - fixed_below;
    int max_fit = std::max(1, static_cast<int>(std::floor(avail_for_list / kPkgBrowserItemH)));
    l.visible_count = std::min(l.visible_count, max_fit);
    l.list_h = l.visible_count * kPkgBrowserItemH;
    l.ph = std::min(max_ph, fixed_above + l.list_h + fixed_below);

    l.px = (l.wf - l.pw) * 0.5f;
    l.py = (l.hf - l.ph) * 0.5f;
    l.cx = l.px + kPkgBrowserPadX;
    l.inner_w = l.pw - 2.0f * kPkgBrowserPadX;

    // Absolute positions = panel top + relative offsets
    l.header_y = l.py + header_y;
    l.search_y = l.py + search_y;
    l.tabs_y = l.py + tabs_y;
    l.tabs2_y = l.py + tabs2_y;
    l.tabs3_y = l.py + tabs3_y;
    l.tabs4_y = l.py + tabs4_y;
    l.list_top = l.py + list_top;
    l.preview_top = l.list_top + l.list_h + 8.0f;
    l.status_y = l.preview_top + (l.preview_h > 0.0f ? l.preview_h + 8.0f : 0.0f);
    return l;
}

OverlayPanelLayout compute_graph_meta_editor_layout(uint32_t win_w, uint32_t win_h) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.pw = 720.0f;
    l.ph = 580.0f;
    l.px = (l.wf - l.pw) * 0.5f;
    l.py = (l.hf - l.ph) * 0.5f;
    return l;
}

OverlayPanelLayout compute_about_layout(uint32_t win_w, uint32_t win_h) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.pw = 640.0f;
    l.ph = 500.0f;
    l.px = (l.wf - l.pw) * 0.5f;
    l.py = (l.hf - l.ph) * 0.5f;
    l.cx = l.px + 20.0f;
    l.inner_w = l.pw - 40.0f;

    // Header cursor: title + version + copyright + separator
    float cy = 17.0f;       // top pad
    l.header_y = l.py + cy;
    cy += 22.0f;             // title
    cy += 16.0f;             // version
    cy += 18.0f;             // copyright
    cy += 11.0f;             // separator + gap

    l.list_top = l.py + cy;
    l.status_y = l.py + l.ph - 44.0f;
    l.list_h = l.status_y - l.list_top - 8.0f;
    return l;
}

OverlayRect compute_example_open_button_rect(const OverlayPanelLayout& layout, float item_y) {
    OverlayRect r;
    r.w = 64.0f;
    r.h = 22.0f;
    r.x = layout.cx + layout.inner_w - r.w - 8.0f;
    r.y = item_y + (kPkgBrowserItemH - r.h) * 0.5f;
    return r;
}

OverlayRect compute_package_action_button_rect(const OverlayPanelLayout& layout, float item_y) {
    OverlayRect r;
    r.w = kPkgBrowserBtnW;
    r.h = kPkgBrowserBtnH;
    r.x = layout.cx + layout.inner_w - r.w - 8.0f;
    r.y = item_y + (kPkgBrowserItemH - r.h) * 0.5f;
    return r;
}

} // namespace vivid::ui
