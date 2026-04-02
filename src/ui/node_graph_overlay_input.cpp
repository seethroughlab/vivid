#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/overlay_layouts.h"
#include "ui/file_dialog.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vivid::ui {

void NodeGraphUI::update_package_browser() {
    if (!pkg_browser_open_) return;

    OverlayPanelLayout layout =
        compute_package_browser_layout(win_w_, win_h_, pkg_browser_entries_.size());
    int visible_count = layout.visible_count;
    float ph = layout.ph;
    float pw = layout.pw;
    float px = layout.px;
    float py = layout.py;

    float cx = layout.cx;
    float inner_w = layout.inner_w;

    if (!mouse_.left_clicked) return;

    if (!overlay_contains(layout, mouse_.x, mouse_.y)) {
        pkg_browser_open_ = false;
        pkg_browser_filter_.clear();
        pkg_browser_search_focused_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    // Search field click-to-focus
    float search_cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH;
    if (mouse_.x >= cx && mouse_.x <= cx + inner_w &&
        mouse_.y >= search_cy && mouse_.y <= search_cy + kPkgBrowserSearchH) {
        pkg_browser_search_focused_ = true;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    pkg_browser_search_focused_ = false;

    // Hit-test "Link Local..." button
    static const float kLinkBtnW = 96.0f;
    float link_btn_x = cx + inner_w - kLinkBtnW;
    float link_btn_y = py + kPkgBrowserPadY + (kPkgBrowserHeaderH - kPkgBrowserBtnH) / 2.0f - 2.0f;
    if (mouse_.x >= link_btn_x && mouse_.x <= link_btn_x + kLinkBtnW &&
        mouse_.y >= link_btn_y && mouse_.y <= link_btn_y + kPkgBrowserBtnH) {
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        std::string path = open_directory_dialog();
        if (!path.empty() && pkg_browser_callbacks_.link && !pkg_action_pending_) {
            pkg_action_error_.clear();
            pkg_action_pending_ = true;
            pkg_action_name_ = path;
            pkg_browser_callbacks_.link(path, pkg_action_error_);
        }
        return;
    }

    float cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6;
    static const char* tab_labels[] = { "All", "Audio", "GPU", "Control", "Utility", "Installed" };
    float tab_x = cx;
    float tab_gap = 4.0f;
    for (int i = 0; i < 6; ++i) {
        float tw = pkg_browser_tab_widths_[i] > 0 ? pkg_browser_tab_widths_[i]
                 : static_cast<float>(std::strlen(tab_labels[i])) * 8.0f + 16.0f;
        if (mouse_.x >= tab_x && mouse_.x <= tab_x + tw &&
            mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH) {
            pkg_browser_category_ = i;
            pkg_browser_scroll_ = 0;
            pkg_browser_sel_ = 0;
            rebuild_pkg_browser_items();
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
        tab_x += tw + tab_gap;
    }

    float list_top = layout.list_top;
    int total = static_cast<int>(pkg_browser_entries_.size());
    float pkg_list_area_h = layout.visible_count * kPkgBrowserItemH;
    int pkg_first = std::max(0, static_cast<int>(std::floor(pkg_browser_scroll_ / kPkgBrowserItemH)));
    float pkg_pix_offset = pkg_browser_scroll_ - pkg_first * kPkgBrowserItemH;
    int pkg_draw_count = std::min(total - pkg_first, kPkgBrowserMaxVisible + 1);

    for (int vi = 0; vi < pkg_draw_count; ++vi) {
        int i = pkg_first + vi;
        float iy = list_top - pkg_pix_offset + vi * kPkgBrowserItemH;
        if (mouse_.y < std::max(iy, list_top) || mouse_.y > std::min(iy + kPkgBrowserItemH, list_top + pkg_list_area_h)) continue;
        if (mouse_.x < cx || mouse_.x > cx + inner_w) continue;

        OverlayRect btn = compute_package_action_button_rect(layout, iy);
        if (overlay_contains(btn, mouse_.x, mouse_.y)) {
            if (!pkg_action_pending_) {
                const auto& entry = pkg_browser_entries_[i];
                pkg_action_error_.clear();
                pkg_action_pending_ = true;
                pkg_action_name_ = entry.name;
                if (entry.needs_rebuild) {
                    if (!pkg_browser_callbacks_.rebuild ||
                        !pkg_browser_callbacks_.rebuild(entry.name, pkg_action_error_)) {
                        pkg_action_pending_ = false;
                        pkg_action_name_.clear();
                        if (pkg_action_error_.empty())
                            pkg_action_error_ = "Failed to rebuild " + entry.name;
                    }
                } else if (entry.installed) {
                    if (entry.linked) {
                        if (!pkg_browser_callbacks_.unlink ||
                            !pkg_browser_callbacks_.unlink(entry.name, pkg_action_error_)) {
                            pkg_action_pending_ = false;
                            pkg_action_name_.clear();
                            if (pkg_action_error_.empty())
                                pkg_action_error_ = "Failed to unlink " + entry.name;
                        }
                    } else if (!pkg_browser_callbacks_.uninstall ||
                               !pkg_browser_callbacks_.uninstall(entry.name, pkg_action_error_)) {
                        pkg_action_pending_ = false;
                        pkg_action_name_.clear();
                        if (pkg_action_error_.empty())
                            pkg_action_error_ = "Failed to uninstall " + entry.name;
                    }
                } else {
                    if (!pkg_browser_callbacks_.install ||
                        !pkg_browser_callbacks_.install(entry.name, pkg_action_error_)) {
                        pkg_action_pending_ = false;
                        pkg_action_name_.clear();
                        if (pkg_action_error_.empty())
                            pkg_action_error_ = "Failed to install " + entry.name;
                    }
                }
            }
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }

        pkg_browser_sel_ = i;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

void NodeGraphUI::update_example_browser() {
    if (!example_browser_open_) return;

    OverlayPanelLayout layout =
        compute_example_browser_layout(win_w_, win_h_, example_entries_.size());
    int visible_count = layout.visible_count;
    float px = layout.px;
    float py = layout.py;
    float cx = layout.cx;
    float inner_w = layout.inner_w;
    if (!mouse_.left_clicked) return;

    if (!overlay_contains(layout, mouse_.x, mouse_.y)) {
        example_browser_open_ = false;
        example_browser_filter_.clear();
        example_browser_search_focused_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    // Search field click-to-focus
    float search_cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH;
    if (mouse_.x >= cx && mouse_.x <= cx + inner_w &&
        mouse_.y >= search_cy && mouse_.y <= search_cy + kPkgBrowserSearchH) {
        example_browser_search_focused_ = true;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    example_browser_search_focused_ = false;

    float cy = py + kPkgBrowserPadY + kPkgBrowserHeaderH + kPkgBrowserSearchH + 6;
    static const char* env_tabs[] = { "All", "GPU", "Audio", "Control", "I/O" };
    float tx = cx;
    for (int i = 0; i < 5; ++i) {
        float tw = example_env_tab_widths_[i] > 0 ? example_env_tab_widths_[i]
                 : static_cast<float>(std::strlen(env_tabs[i])) * 8.0f + 16.0f;
        if (mouse_.x >= tx && mouse_.x <= tx + tw && mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH) {
            example_browser_env_ = i;
            example_browser_scroll_ = 0;
            example_browser_sel_ = 0;
            rebuild_example_items();
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
        tx += tw + 4.0f;
    }
    cy += kPkgBrowserTabH + 8;

    static const char* diff_tabs[] = { "All", "Beginner", "Intermediate", "Advanced" };
    tx = cx;
    for (int i = 0; i < 4; ++i) {
        float tw = example_diff_tab_widths_[i] > 0 ? example_diff_tab_widths_[i]
                 : static_cast<float>(std::strlen(diff_tabs[i])) * 8.0f + 16.0f;
        if (mouse_.x >= tx && mouse_.x <= tx + tw && mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH) {
            example_browser_difficulty_ = i;
            example_browser_scroll_ = 0;
            example_browser_sel_ = 0;
            rebuild_example_items();
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
        tx += tw + 4.0f;
    }

    float right_x = cx + inner_w - 210.0f;
    float toggle_w = 88.0f;
    if (mouse_.x >= right_x && mouse_.x <= right_x + toggle_w &&
        mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH) {
        example_browser_core_only_ = !example_browser_core_only_;
        if (example_browser_core_only_) example_browser_package_only_ = false;
        rebuild_example_items();
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    if (mouse_.x >= right_x + toggle_w + 6.0f && mouse_.x <= right_x + 2 * toggle_w + 6.0f &&
        mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH) {
        example_browser_package_only_ = !example_browser_package_only_;
        if (example_browser_package_only_) example_browser_core_only_ = false;
        rebuild_example_items();
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    cy += kPkgBrowserTabH + 8;

    float sort_x = cx;
    float sort_w0 = example_sort_tab_widths_[0] > 0 ? example_sort_tab_widths_[0] : 92.0f;
    float sort_w1 = example_sort_tab_widths_[1] > 0 ? example_sort_tab_widths_[1] : 104.0f;
    if (mouse_.x >= sort_x && mouse_.x <= sort_x + sort_w0 &&
        mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH) {
        example_browser_sort_ = 0;
        rebuild_example_items();
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    if (mouse_.x >= sort_x + sort_w0 + 4.0f && mouse_.x <= sort_x + sort_w0 + 4.0f + sort_w1 &&
        mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH) {
        example_browser_sort_ = 1;
        rebuild_example_items();
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    cy += kPkgBrowserTabH + 8;

    if (!example_entries_.empty()) {
        float ex_list_area_h = visible_count * kPkgBrowserItemH;
        int idx = static_cast<int>(std::floor((mouse_.y - cy + example_browser_scroll_) / kPkgBrowserItemH));
        int ex_first = static_cast<int>(std::floor(example_browser_scroll_ / kPkgBrowserItemH));
        float ex_offset = example_browser_scroll_ - ex_first * kPkgBrowserItemH;
        float iy = cy - ex_offset + (idx - ex_first) * kPkgBrowserItemH;
        if (idx >= 0 && idx < static_cast<int>(example_entries_.size()) &&
            mouse_.y >= cy && mouse_.y < cy + ex_list_area_h) {
            example_browser_sel_ = idx;
            OverlayRect open_btn = compute_example_open_button_rect(layout, iy);
            if (overlay_contains(open_btn, mouse_.x, mouse_.y) &&
                example_open_callback_) {
                const auto& e = example_entries_[idx];
                std::string missing;
                bool ok = true;
                if (example_package_checker_) {
                    ok = example_package_checker_(e.requires_packages, missing);
                }
                if (ok) {
                    example_action_error_.clear();
                    example_warn_id_.clear();
                    example_open_callback_(e.path);
                    example_browser_open_ = false;
                } else if (example_warn_id_ == e.id) {
                    example_action_error_ = "Opening anyway with missing package: " + missing;
                    example_open_callback_(e.path);
                    example_browser_open_ = false;
                } else {
                    example_warn_id_ = e.id;
                    example_action_error_ =
                        "Missing package: " + missing + " (click Open again to continue)";
                }
            }
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
    }

    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

void NodeGraphUI::update_graph_meta_editor() {
    if (!graph_meta_editor_open_) return;
    if (!mouse_.left_clicked) return;

    OverlayPanelLayout layout = compute_graph_meta_editor_layout(win_w_, win_h_);
    float pw = layout.pw;
    float ph = layout.ph;
    float px = layout.px;
    float py = layout.py;

    if (!overlay_contains(layout, mouse_.x, mouse_.y)) {
        graph_meta_editor_open_ = false;
        graph_meta_error_.clear();
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    float cx = px + 16.0f;
    float cy = py + 52.0f;
    float label_w = 160.0f;
    float field_h = 24.0f;
    float field_w = pw - 32.0f - label_w;
    float row_gap = 8.0f;
    const int kFieldCount = 8;
    for (int i = 0; i < kFieldCount; ++i) {
        float fy = cy + i * (field_h + row_gap);
        float fx = cx + label_w;
        if (mouse_.x >= fx && mouse_.x <= fx + field_w &&
            mouse_.y >= fy && mouse_.y <= fy + field_h) {
            graph_meta_active_field_ = i;
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
    }

    float by = py + ph - 42.0f;
    float save_w = 80.0f;
    float cancel_w = 90.0f;
    float save_x = px + pw - 16.0f - save_w - 8.0f - cancel_w;
    float cancel_x = save_x + save_w + 8.0f;

    if (mouse_.x >= save_x && mouse_.x <= save_x + save_w &&
        mouse_.y >= by && mouse_.y <= by + 24.0f) {
        if (graph_meta_save_callback_) {
            std::string err;
            if (graph_meta_save_callback_(graph_meta_data_, err)) {
                graph_meta_editor_open_ = false;
                graph_meta_error_.clear();
            } else {
                graph_meta_error_ = err.empty() ? "Failed to save meta" : err;
            }
        }
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    if (mouse_.x >= cancel_x && mouse_.x <= cancel_x + cancel_w &&
        mouse_.y >= by && mouse_.y <= by + 24.0f) {
        graph_meta_editor_open_ = false;
        graph_meta_error_.clear();
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

void NodeGraphUI::update_about() {
    if (!about_open_) return;
    if (!mouse_.left_clicked) return;

    OverlayPanelLayout layout = compute_about_layout(win_w_, win_h_);

    // Click outside panel closes modal
    if (!overlay_contains(layout, mouse_.x, mouse_.y)) {
        about_open_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    // Close button
    float btn_w = 80.0f, btn_h = 24.0f;
    float btn_x = layout.px + (layout.pw - btn_w) * 0.5f;
    float btn_y = layout.status_y;
    if (mouse_.x >= btn_x && mouse_.x <= btn_x + btn_w &&
        mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h) {
        about_open_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

} // namespace vivid::ui
