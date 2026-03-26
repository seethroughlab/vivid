#include "ui/overlay_layouts.h"
#include "ui/node_graph_constants.h"
#include <algorithm>

namespace vivid::ui {

OverlayPanelLayout compute_create_operator_layout(uint32_t win_w, uint32_t win_h,
                                                   bool show_composite) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.pw = kCreateModalW;

    // Calculate content height:
    //   pad_y + title(24) + env_btns(22+10) + [composite_row(24+8)] + name_field(24+8)
    //   + hint_line(18+8) + destination_row(22+8)
    //   + error_area(18+8) + button_row(26) + pad_y
    float h = kCreateModalPadY;
    h += 24.0f;  // title
    h += kCreateEnvBtnH + 10.0f;  // env buttons
    if (show_composite) h += 24.0f + kCreateModalRowGap;  // composite checkbox row
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
    float content_h = kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6.0f
                    + kPkgBrowserTabH + 8.0f + l.list_h + 8.0f + 18.0f + kPkgBrowserPadY;
    l.pw = kPkgBrowserW;
    l.ph = std::min(kPkgBrowserMaxH, std::min(content_h, l.hf - 40.0f));
    l.px = (l.wf - l.pw) * 0.5f;
    l.py = (l.hf - l.ph) * 0.5f;
    l.cx = l.px + kPkgBrowserPadX;
    l.inner_w = l.pw - 2.0f * kPkgBrowserPadX;
    l.tabs_y = l.py + kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6.0f;
    l.list_top = l.tabs_y + kPkgBrowserTabH + 8.0f;
    l.status_y = l.list_top + l.visible_count * kPkgBrowserItemH + 8.0f;
    return l;
}

OverlayPanelLayout compute_example_browser_layout(uint32_t win_w, uint32_t win_h, size_t entry_count) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.visible_count = std::min(static_cast<int>(entry_count), kPkgBrowserMaxVisible);
    l.list_h = l.visible_count * kPkgBrowserItemH;
    float content_h = kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6.0f
                    + kPkgBrowserTabH + 8.0f + kPkgBrowserTabH + 8.0f + kPkgBrowserTabH + 8.0f
                    + l.list_h + 8.0f + 18.0f + kPkgBrowserPadY;
    l.pw = kPkgBrowserW + 120.0f;
    l.ph = std::min(kPkgBrowserMaxH + 70.0f, std::min(content_h, l.hf - 40.0f));
    l.px = (l.wf - l.pw) * 0.5f;
    l.py = (l.hf - l.ph) * 0.5f;
    l.cx = l.px + kPkgBrowserPadX;
    l.inner_w = l.pw - 2.0f * kPkgBrowserPadX;
    l.tabs_y = l.py + kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6.0f;
    l.tabs2_y = l.tabs_y + kPkgBrowserTabH + 8.0f;
    l.tabs3_y = l.tabs2_y + kPkgBrowserTabH + 8.0f;
    l.list_top = l.tabs3_y + kPkgBrowserTabH + 8.0f;
    l.status_y = l.list_top + l.visible_count * kPkgBrowserItemH + 8.0f;
    return l;
}

OverlayPanelLayout compute_graph_meta_editor_layout(uint32_t win_w, uint32_t win_h) {
    OverlayPanelLayout l;
    l.wf = static_cast<float>(win_w);
    l.hf = static_cast<float>(win_h);
    l.pw = 720.0f;
    l.ph = 420.0f;
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
    // Header: title (22px) + version (16px) + copyright (18px) + separator+gap (11px) = 67px + top pad 17px = 84px
    l.list_top = l.py + 84.0f;
    l.status_y = l.py + l.ph - 44.0f;
    l.list_h = l.status_y - l.list_top - 8.0f;
    return l;
}

OverlayRect compute_example_open_button_rect(const OverlayPanelLayout& layout, int visible_row) {
    OverlayRect r;
    r.w = 64.0f;
    r.h = 22.0f;
    r.x = layout.cx + layout.inner_w - r.w - 8.0f;
    float row_y = layout.list_top + visible_row * kPkgBrowserItemH;
    r.y = row_y + (kPkgBrowserItemH - r.h) * 0.5f;
    return r;
}

OverlayRect compute_package_action_button_rect(const OverlayPanelLayout& layout, int visible_row) {
    OverlayRect r;
    r.w = kPkgBrowserBtnW;
    r.h = kPkgBrowserBtnH;
    r.x = layout.cx + layout.inner_w - r.w - 8.0f;
    float row_y = layout.list_top + visible_row * kPkgBrowserItemH;
    r.y = row_y + (kPkgBrowserItemH - r.h) * 0.5f;
    return r;
}

} // namespace vivid::ui
