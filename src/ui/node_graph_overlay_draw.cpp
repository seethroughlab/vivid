#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/overlay_layouts.h"
#include "ui/renderer_2d.h"
#include "ui/i18n.h"
#include <algorithm>

namespace vivid::ui {

namespace {
std::string fit_text_to_width(Renderer2D& tr, const std::string& text, float max_w) {
    if (max_w <= 0.0f) return {};
    if (tr.text_width(text.c_str()) <= max_w) return text;
    static const char* kEllipsis = "...";
    const float ell_w = tr.text_width(kEllipsis);
    if (ell_w >= max_w) return {};
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        std::string candidate = out + kEllipsis;
        if (tr.text_width(candidate.c_str()) <= max_w) return candidate;
    }
    return {};
}
} // namespace

void NodeGraphUI::draw_package_browser(Renderer2D& tr) {
    if (!pkg_browser_open_) return;

    OverlayPanelLayout layout =
        compute_package_browser_layout(win_w_, win_h_, pkg_browser_entries_.size());
    float wf = layout.wf;
    float hf = layout.hf;

    refresh_package_browser_snapshot_if_ready();

    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

    int visible_count = layout.visible_count;
    float ph = layout.ph;
    float pw = layout.pw;
    float px = layout.px;
    float py = layout.py;

    draw_shadow(tr, px, py, pw, ph, style_.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = layout.cx;
    float inner_w = layout.inner_w;
    float cy = py + kPkgBrowserPadY;

    tr.draw_text(cx, cy + 6, T("packages", "Packages"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    static const float kLinkBtnW = 96.0f;
    float link_btn_x = cx + inner_w - kLinkBtnW;
    float link_btn_y = cy + (kPkgBrowserHeaderH - kPkgBrowserBtnH) / 2.0f - 2.0f;
    bool link_btn_hovered = mouse_.x >= link_btn_x && mouse_.x <= link_btn_x + kLinkBtnW &&
                            mouse_.y >= link_btn_y && mouse_.y <= link_btn_y + kPkgBrowserBtnH;
    tr.draw_rect(link_btn_x, link_btn_y, kLinkBtnW, kPkgBrowserBtnH,
                 link_btn_hovered ? style_.accent[0] : style_.button_bg[0],
                 link_btn_hovered ? style_.accent[1] : style_.button_bg[1],
                 link_btn_hovered ? style_.accent[2] : style_.button_bg[2],
                 link_btn_hovered ? 0.9f : 0.8f);
    float link_lbl_x = link_btn_x + (kLinkBtnW - tr.text_width(T("link_local", "Link Local..."))) * 0.5f;
    tr.draw_text(link_lbl_x, link_btn_y + 3, T("link_local", "Link Local..."),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    cy += kPkgBrowserHeaderH;

    tr.draw_rect(cx, cy, inner_w, kPkgBrowserSearchH,
                 style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
    tr.draw_rect(cx, cy, inner_w, 1,
                 style_.accent[0], style_.accent[1], style_.accent[2]);

    std::string search_display = pkg_browser_filter_;
    if (static_cast<int>(perf_frame_counter_ / 30) % 2 == 0)
        search_display += "_";
    else
        search_display += " ";

    if (pkg_browser_filter_.empty() && search_display.size() <= 1) {
        tr.draw_text(cx + 4, cy + 5, T("search_packages", "Search packages..."),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
    } else {
        tr.draw_text(cx + 4, cy + 5, search_display.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
    cy += kPkgBrowserSearchH + 6;

    static const char* tab_labels[] = { "All", "Audio", "GPU", "Control", "Utility", "Installed" };
    static const int tab_count = 6;
    float tab_x = cx;
    float tab_gap = 4.0f;
    for (int i = 0; i < tab_count; ++i) {
        bool selected = (i == pkg_browser_category_);
        float approx_tw = tr.text_width(tab_labels[i]) + 16.0f;
        bool hovered = mouse_.x >= tab_x && mouse_.x <= tab_x + approx_tw &&
                       mouse_.y >= cy && mouse_.y <= cy + kPkgBrowserTabH;
        float tw = draw_tab_button(tr, style_, tab_x, cy, kPkgBrowserTabH, tab_labels[i], selected, hovered);
        pkg_browser_tab_widths_[i] = tw;
        tab_x += tw + tab_gap;
    }
    cy += kPkgBrowserTabH + 8;

    float list_top = layout.list_top;
    int total = static_cast<int>(pkg_browser_entries_.size());
    int end = std::min(total, pkg_browser_scroll_ + kPkgBrowserMaxVisible);

    for (int i = pkg_browser_scroll_; i < end; ++i) {
        const auto& entry = pkg_browser_entries_[i];
        float iy = list_top + (i - pkg_browser_scroll_) * kPkgBrowserItemH;
        bool hovered = mouse_.x >= cx && mouse_.x <= cx + inner_w &&
                       mouse_.y >= iy && mouse_.y <= iy + kPkgBrowserItemH;
        if (hovered || i == pkg_browser_sel_) {
            tr.draw_rect(cx, iy, inner_w, kPkgBrowserItemH,
                         style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2],
                         hovered ? 0.5f : 0.3f);
        }
        if (i > pkg_browser_scroll_) {
            tr.draw_rect(cx + 4, iy, inner_w - 8, 1,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.3f);
        }

        OverlayRect btn = compute_package_action_button_rect(layout, i - pkg_browser_scroll_);
        const float text_left = cx + 8.0f;
        const float text_right = btn.x - 10.0f;
        const float text_w = std::max(0.0f, text_right - text_left);
        tr.push_clip_rect(text_left, iy + 2.0f, text_w, kPkgBrowserItemH - 4.0f);

        std::string ver_str = "v" + entry.version;
        const float ver_w = tr.text_width(ver_str.c_str());
        const char* state = entry.needs_rebuild ? T("needs_rebuild", "Try Rebuild")
                          : entry.linked        ? T("linked", "Linked")
                          :                       T("installed", "Installed");
        const float chip_w = tr.text_width(state) + 16.0f;
        float available_name_w = text_w - 8.0f - ver_w;
        if (entry.installed) available_name_w -= (10.0f + chip_w);
        std::string display_name = fit_text_to_width(tr, entry.name, available_name_w);
        tr.draw_text(text_left, iy + 6, display_name.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        float name_w = tr.text_width(display_name.c_str());
        tr.draw_text(text_left + name_w + 8, iy + 6, ver_str.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
        if (entry.installed) {
            float state_x = text_left + name_w + 8 + ver_w + 10.0f;
            bool error_chip = entry.needs_rebuild;
            tr.draw_rect(state_x, iy + 4, chip_w, 16.0f,
                         error_chip   ? kErrorAccent[0] : entry.linked ? style_.accent[0] : style_.button_bg[0],
                         error_chip   ? kErrorAccent[1] : entry.linked ? style_.accent[1] : style_.button_bg[1],
                         error_chip   ? kErrorAccent[2] : entry.linked ? style_.accent[2] : style_.button_bg[2],
                         error_chip   ? 0.85f : entry.linked ? 0.85f : 0.75f);
            tr.draw_text(state_x + 6.0f, iy + 6, state,
                         error_chip   ? 1.0f : entry.linked ? 0.0f : style_.bright_text[0],
                         error_chip   ? 1.0f : entry.linked ? 0.0f : style_.bright_text[1],
                         error_chip   ? 1.0f : entry.linked ? 0.0f : style_.bright_text[2],
                         0.9f);
        }

        std::string desc = fit_text_to_width(tr, entry.description, text_w);
        tr.draw_text(text_left, iy + 22, desc.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

        std::string meta;
        if (!entry.category.empty()) meta = entry.category;
        if (!entry.author.empty()) {
            if (!meta.empty()) meta += " · ";
            meta += entry.author;
        }
        if (!meta.empty()) {
            meta = fit_text_to_width(tr, meta, text_w);
            tr.draw_text(text_left, iy + 37, meta.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
        }
        tr.pop_clip_rect();

        float btn_x = btn.x;
        float btn_y = btn.y;
        const char* btn_label = entry.needs_rebuild ? T("rebuild", "Rebuild")
                              : entry.installed     ? (entry.linked ? T("unlink", "Unlink") : T("remove", "Remove"))
                              :                       T("install", "Install");
        bool this_pending = pkg_action_pending_ && (entry.name == pkg_action_name_);
        bool any_pending  = pkg_action_pending_;
        bool btn_hover = !any_pending && overlay_contains(btn, mouse_.x, mouse_.y);

        if (this_pending) {
            // Draw spinner arc instead of button text
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.8f);
            float arc_cx = btn_x + kPkgBrowserBtnW * 0.5f;
            float arc_cy = btn_y + kPkgBrowserBtnH * 0.5f;
            float arc_r  = kPkgBrowserBtnH * 0.30f;
            float a0 = static_cast<float>(perf_frame_counter_) * 0.08f;
            tr.draw_arc(arc_cx, arc_cy, arc_r, a0, a0 + 4.5f, 1.5f, 12,
                        style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
        } else if (any_pending) {
            // Grey out — no hover, no interaction
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.35f);
            float label_x = btn_x + (btn.w - tr.text_width(btn_label)) * 0.5f;
            tr.draw_text(label_x, btn_y + 3, btn_label,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.35f);
        } else {
        if (entry.needs_rebuild) {
            // Rebuild button — accent color to draw attention
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         btn_hover ? style_.accent[0] : kErrorAccent[0],
                         btn_hover ? style_.accent[1] : kErrorAccent[1],
                         btn_hover ? style_.accent[2] : kErrorAccent[2],
                         btn_hover ? 0.9f : 0.8f);
        } else if (entry.installed) {
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         btn_hover ? kErrorAccent[0] : style_.button_bg[0],
                         btn_hover ? kErrorAccent[1] : style_.button_bg[1],
                         btn_hover ? kErrorAccent[2] : style_.button_bg[2],
                         0.8f);
        } else {
            tr.draw_rect(btn_x, btn_y, kPkgBrowserBtnW, kPkgBrowserBtnH,
                         btn_hover ? style_.accent[0] : style_.button_bg[0],
                         btn_hover ? style_.accent[1] : style_.button_bg[1],
                         btn_hover ? style_.accent[2] : style_.button_bg[2],
                         btn_hover ? 0.9f : 0.8f);
        }

        float label_x = btn_x + (btn.w - tr.text_width(btn_label)) * 0.5f;
        tr.draw_text(label_x, btn_y + 3, btn_label,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }

    if (total > kPkgBrowserMaxVisible) {
        float sb_x = cx + inner_w - 4;
        float sb_h = visible_count * kPkgBrowserItemH;
        float thumb_h = std::max(20.0f, sb_h * kPkgBrowserMaxVisible / static_cast<float>(total));
        float thumb_y = list_top + (sb_h - thumb_h) * pkg_browser_scroll_ /
                        static_cast<float>(total - kPkgBrowserMaxVisible);
        tr.draw_rect(sb_x, list_top, 4, sb_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.3f);
        tr.draw_rect(sb_x, thumb_y, 4, thumb_h,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
    }

    std::string status;
    if (pkg_browser_callbacks_.fetch_state) {
        auto state = pkg_browser_callbacks_.fetch_state();
        if (state == PackageBrowserFetchState::Fetching) {
            status = T("fetching_catalog", "Fetching catalog...");
        } else if (state == PackageBrowserFetchState::Error) {
            if (pkg_browser_callbacks_.fetch_error) status = pkg_browser_callbacks_.fetch_error();
        } else {
            status = std::to_string(pkg_browser_entries_.size()) + " package" +
                     (pkg_browser_entries_.size() != 1 ? "s" : "");
            PackageBrowserUpdateSummary summary{};
            if (pkg_browser_callbacks_.update_summary) summary = pkg_browser_callbacks_.update_summary();
            if (summary.updates_available > 0) {
                status += " • " + std::to_string(summary.updates_available) + " update";
                if (summary.updates_available != 1) status += "s";
                if (summary.incompatible_updates > 0) {
                    status += " (" + std::to_string(summary.incompatible_updates) + " incompatible)";
                }
                status += " • run `vivid package-check-updates`";
            }
        }
    }
    if (!pkg_action_error_.empty()) {
        tr.draw_text(cx, layout.status_y, pkg_action_error_.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.9f);
    } else if (!status.empty()) {
        tr.draw_text(cx, layout.status_y, status.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    }
}

void NodeGraphUI::draw_example_browser(Renderer2D& tr) {
    if (!example_browser_open_) return;

    OverlayPanelLayout layout =
        compute_example_browser_layout(win_w_, win_h_, example_entries_.size());
    float wf = layout.wf;
    float hf = layout.hf;
    tr.draw_rect(0, 0, wf, hf,
                 style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);

    int visible_count = layout.visible_count;
    float ph = layout.ph;
    float pw = layout.pw;
    float px = layout.px;
    float py = layout.py;

    draw_shadow(tr, px, py, pw, ph, style_.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    float cx = layout.cx;
    float inner_w = layout.inner_w;
    float cy = py + kPkgBrowserPadY;

    tr.draw_text(cx, cy + 6, T("open_example", "Open Example"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    cy += kPkgBrowserHeaderH;

    tr.draw_rect(cx, cy, inner_w, kPkgBrowserSearchH,
                 style_.input_field_bg[0], style_.input_field_bg[1], style_.input_field_bg[2]);
    tr.draw_rect(cx, cy, inner_w, 1, style_.accent[0], style_.accent[1], style_.accent[2]);
    std::string s = example_browser_filter_;
    s += (static_cast<int>(perf_frame_counter_ / 30) % 2 == 0) ? "_" : " ";
    if (example_browser_filter_.empty() && s.size() <= 1) {
        tr.draw_text(cx + 4, cy + 5, T("search_examples", "Search by title, tags, id, path..."),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.55f);
    } else {
        tr.draw_text(cx + 4, cy + 5, s.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
    cy += kPkgBrowserSearchH + 6;

    static const char* domain_tabs[] = { "All", "GPU", "Audio", "Control", "I/O" };
    float tx = cx;
    for (int i = 0; i < 5; ++i) {
        bool sel = (i == example_browser_domain_);
        float tw = draw_tab_button(tr, style_, tx, cy, kPkgBrowserTabH, domain_tabs[i], sel, false);
        example_domain_tab_widths_[i] = tw;
        tx += tw + 4.0f;
    }
    cy += kPkgBrowserTabH + 8;

    static const char* diff_tabs[] = { "All", "Beginner", "Intermediate", "Advanced" };
    tx = cx;
    for (int i = 0; i < 4; ++i) {
        bool sel = (i == example_browser_difficulty_);
        float tw = draw_tab_button(tr, style_, tx, cy, kPkgBrowserTabH, diff_tabs[i], sel, false);
        example_diff_tab_widths_[i] = tw;
        tx += tw + 4.0f;
    }

    float toggle_w = 88.0f;
    float toggles_x = cx + inner_w - 210.0f;
    tr.draw_rect(toggles_x, cy, toggle_w, kPkgBrowserTabH,
                 example_browser_core_only_ ? style_.accent[0] : style_.button_bg[0],
                 example_browser_core_only_ ? style_.accent[1] : style_.button_bg[1],
                 example_browser_core_only_ ? style_.accent[2] : style_.button_bg[2],
                 example_browser_core_only_ ? 0.9f : 0.65f);
    tr.draw_text(toggles_x + 8, cy + 3, T("core_only", "Core only"),
                 example_browser_core_only_ ? 0.0f : style_.dim_text[0],
                 example_browser_core_only_ ? 0.0f : style_.dim_text[1],
                 example_browser_core_only_ ? 0.0f : style_.dim_text[2]);
    tr.draw_rect(toggles_x + toggle_w + 6.0f, cy, toggle_w, kPkgBrowserTabH,
                 example_browser_package_only_ ? style_.accent[0] : style_.button_bg[0],
                 example_browser_package_only_ ? style_.accent[1] : style_.button_bg[1],
                 example_browser_package_only_ ? style_.accent[2] : style_.button_bg[2],
                 example_browser_package_only_ ? 0.9f : 0.65f);
    tr.draw_text(toggles_x + toggle_w + 14.0f, cy + 3, T("package", "Package"),
                 example_browser_package_only_ ? 0.0f : style_.dim_text[0],
                 example_browser_package_only_ ? 0.0f : style_.dim_text[1],
                 example_browser_package_only_ ? 0.0f : style_.dim_text[2]);
    cy += kPkgBrowserTabH + 8;

    static const char* sort_tabs[] = { "Featured", "A-Z" };
    tx = cx;
    for (int i = 0; i < 2; ++i) {
        bool sel = (i == example_browser_sort_);
        float tw = draw_tab_button(tr, style_, tx, cy, kPkgBrowserTabH, sort_tabs[i], sel, false);
        example_sort_tab_widths_[i] = tw;
        tx += tw + 4.0f;
    }
    cy += kPkgBrowserTabH + 8;

    int total = static_cast<int>(example_entries_.size());
    int end = std::min(total, example_browser_scroll_ + kPkgBrowserMaxVisible);
    for (int i = example_browser_scroll_; i < end; ++i) {
        const auto& e = example_entries_[i];
        float iy = cy + (i - example_browser_scroll_) * kPkgBrowserItemH;
        bool hovered = mouse_.x >= cx && mouse_.x <= cx + inner_w &&
                       mouse_.y >= iy && mouse_.y <= iy + kPkgBrowserItemH;
        if (hovered || i == example_browser_sel_) {
            tr.draw_rect(cx, iy, inner_w, kPkgBrowserItemH,
                         style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2],
                         hovered ? 0.5f : 0.3f);
        }
        if (i > example_browser_scroll_) {
            tr.draw_rect(cx + 4, iy, inner_w - 8, 1,
                         style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.3f);
        }

        OverlayRect open_btn = compute_example_open_button_rect(layout, i - example_browser_scroll_);
        float bx = open_btn.x;
        float by = open_btn.y;
        const bool has_required_packages = !e.requires_packages.empty();
        bool has_missing_packages = false;
        if (has_required_packages && example_package_checker_) {
            std::string missing_pkg_name;
            has_missing_packages = !example_package_checker_(e.requires_packages, missing_pkg_name);
        }

        const float badge_gap = 8.0f;
        const float badge_w = 128.0f;
        const float text_left = cx + 8.0f;
        float text_right = bx - 10.0f;
        if (has_missing_packages) text_right -= (badge_w + badge_gap);
        const float text_w = std::max(0.0f, text_right - text_left);

        tr.push_clip_rect(text_left, iy + 2.0f, text_w, kPkgBrowserItemH - 4.0f);
        const std::string title = fit_text_to_width(tr, e.title, text_w);
        tr.draw_text(text_left, iy + 6, title.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        std::string meta = e.id + " · " + e.path;
        meta = fit_text_to_width(tr, meta, text_w);
        tr.draw_text(text_left, iy + 22, meta.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
        std::string summary = fit_text_to_width(tr, e.summary, text_w);
        tr.draw_text(text_left, iy + 37, summary.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
        tr.pop_clip_rect();

        tr.draw_rect(bx, by, open_btn.w, open_btn.h,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
        float tw = tr.text_width(T("open", "Open"));
        tr.draw_text(bx + (open_btn.w - tw) * 0.5f, by + 3, T("open", "Open"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

        if (has_missing_packages) {
            const float badge_x = bx - badge_gap - badge_w;
            tr.draw_rect(badge_x, by, badge_w, open_btn.h,
                         0.42f, 0.30f, 0.12f, 0.9f);
            tr.draw_text(badge_x + 8.0f, by + 3.0f, T("needs_packages", "needs package(s)"),
                         0.95f, 0.78f, 0.32f, 0.95f);
        }
    }

    if (total > kPkgBrowserMaxVisible) {
        float sb_x = cx + inner_w - 4;
        float sb_h = layout.visible_count * kPkgBrowserItemH;
        float thumb_h = std::max(20.0f, sb_h * kPkgBrowserMaxVisible / static_cast<float>(total));
        float thumb_y = cy + (sb_h - thumb_h) * example_browser_scroll_ /
                        static_cast<float>(total - kPkgBrowserMaxVisible);
        tr.draw_rect(sb_x, cy, 4, sb_h,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.3f);
        tr.draw_rect(sb_x, thumb_y, 4, thumb_h,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.5f);
    }

    if (!example_action_error_.empty()) {
        tr.draw_text(cx, layout.status_y, example_action_error_.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.9f);
    } else {
        std::string status = std::to_string(example_entries_.size()) + " graph";
        if (example_entries_.size() != 1) status += "s";
        status += " · Enter opens selection";
        tr.draw_text(cx, layout.status_y, status.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    }
}

void NodeGraphUI::draw_graph_meta_editor(Renderer2D& tr) {
    if (!graph_meta_editor_open_) return;

    OverlayPanelLayout layout = compute_graph_meta_editor_layout(win_w_, win_h_);
    float wf = layout.wf;
    float hf = layout.hf;
    float pw = layout.pw;
    float ph = layout.ph;
    float px = layout.px;
    float py = layout.py;

    tr.draw_rect(0, 0, wf, hf, style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);
    draw_shadow(tr, px, py, pw, ph, style_.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);
    tr.draw_text(px + 16.0f, py + 16.0f, T("edit_meta", "Edit Meta"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    if (!graph_meta_data_.path.empty()) {
        std::string p = graph_meta_data_.path;
        if (p.size() > 70) p = "..." + p.substr(p.size() - 67);
        tr.draw_text(px + 16.0f, py + 32.0f, p.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    }

    static const char* labels[] = {
        "id", "title", "description", "tags (csv)", "difficulty",
        "domains (csv)", "requires_packages (csv)", "featured_rank"
    };
    const std::string values[] = {
        graph_meta_data_.id,
        graph_meta_data_.title,
        graph_meta_data_.description,
        graph_meta_data_.tags_csv,
        graph_meta_data_.difficulty,
        graph_meta_data_.domains_csv,
        graph_meta_data_.requires_packages_csv,
        graph_meta_data_.featured_rank
    };

    float cx = px + 16.0f;
    float cy = py + 52.0f;
    float label_w = 160.0f;
    float field_h = 24.0f;
    float field_w = pw - 32.0f - label_w;
    float row_gap = 8.0f;
    for (int i = 0; i < 8; ++i) {
        float fy = cy + i * (field_h + row_gap);
        float fx = cx + label_w;
        tr.draw_text(cx, fy + 4, labels[i],
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        bool active = (i == graph_meta_active_field_);
        tr.draw_rect(fx, fy, field_w, field_h,
                     active ? style_.node_sel_bg[0] : style_.input_field_bg[0],
                     active ? style_.node_sel_bg[1] : style_.input_field_bg[1],
                     active ? style_.node_sel_bg[2] : style_.input_field_bg[2],
                     active ? 0.95f : 0.85f);
        std::string txt = values[i];
        if (txt.size() > 94) txt = txt.substr(0, 91) + "...";
        tr.draw_text(fx + 6.0f, fy + 4.0f, txt.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        if (active && cursor_blink_on()) {
            int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(values[i].size())));
            float cur_x = fx + 6.0f + tr.text_width(values[i].substr(0, cpos).c_str());
            tr.draw_rect(cur_x, fy + 1.0f, 1.0f, field_h - 2.0f,
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }

    if (!graph_meta_error_.empty()) {
        tr.draw_text(px + 16.0f, py + ph - 66.0f, graph_meta_error_.c_str(),
                     kErrorAccent[0], kErrorAccent[1], kErrorAccent[2], 0.95f);
    }

    float by = py + ph - 42.0f;
    float save_w = 80.0f;
    float cancel_w = 90.0f;
    float save_x = px + pw - 16.0f - save_w - 8.0f - cancel_w;
    float cancel_x = save_x + save_w + 8.0f;
    tr.draw_rect(save_x, by, save_w, 24.0f, style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    tr.draw_rect(cancel_x, by, cancel_w, 24.0f,
                 style_.button_bg[0], style_.button_bg[1], style_.button_bg[2], 0.85f);
    tr.draw_text(save_x + 20.0f, by + 4.0f, T("save", "Save"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    tr.draw_text(cancel_x + 18.0f, by + 4.0f, T("cancel", "Cancel"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
}

void NodeGraphUI::draw_about(Renderer2D& tr) {
    if (!about_open_) return;

    OverlayPanelLayout layout = compute_about_layout(win_w_, win_h_);
    float wf = layout.wf, hf = layout.hf;
    float pw = layout.pw, ph = layout.ph;
    float px = layout.px, py = layout.py;

    tr.draw_rect(0, 0, wf, hf, style_.scrim[0], style_.scrim[1], style_.scrim[2], style_.scrim[3] * popup_opacity_);
    draw_shadow(tr, px, py, pw, ph, style_.corner_radius);
    tr.draw_rounded_rect(px, py, pw, ph, style_.corner_radius,
                         style_.popup_bg[0], style_.popup_bg[1], style_.popup_bg[2], style_.popup_bg[3]);
    tr.draw_rect(px, py, pw, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    // Fixed header
    float hx = px + 20.0f;
    float hy = py + 17.0f;
    tr.draw_text(hx, hy, "Vivid",
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 1.0f, 1.4f);
    hy += 22.0f;
    tr.draw_text(hx, hy, "Version " VIVID_CORE_VERSION,
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    hy += 16.0f;
    tr.draw_text(hx, hy, "\xC2\xA9 2024-present See-Through Lab LLC",
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    hy += 19.0f;
    tr.draw_rect(px + 8.0f, hy, pw - 16.0f, 1.0f,
                 style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);

    float list_top = layout.list_top;
    float list_h = layout.list_h;

    struct LibNotice { const char* name; const char* text; };
    static const LibNotice kNotices[] = {
        { "GLFW",
          "Copyright (c) 2002-2006 Marcus Geelnard\n"
          "Copyright (c) 2006-2019 Camilla Loewy\n"
          "License: zlib/libpng\n"
          "This software is provided 'as-is', without any express or implied warranty.\n"
          "Permission is granted to use, alter and redistribute it freely, subject to:\n"
          "1. The origin must not be misrepresented.\n"
          "2. Altered versions must be plainly marked as such.\n"
          "3. This notice may not be removed or altered from any source distribution."
        },
        { "glfw3webgpu",
          "Copyright (c) 2022-2024 Elie Michel and the wgpu-native authors\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "oscpack",
          "Copyright (c) 2004-2013 Ross Bencina <rossb@audiomulch.com>\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "nlohmann/json",
          "Copyright (c) 2013-2022 Niels Lohmann <https://nlohmann.me>\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "miniaudio",
          "Copyright 2025 David Reid\n"
          "License: Choice of Public Domain (Unlicense) or MIT-0\n"
          "This software is dedicated to the public domain. Alternatively available\n"
          "under MIT-0 (MIT with no attribution requirement).\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "RtMidi",
          "Copyright (c) 2003-2023 Gary P. Scavone\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "stb_truetype, stb_image, stb_image_write",
          "Copyright (c) 2017 Sean Barrett\n"
          "License: MIT or Public Domain\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "Alternatively released into the public domain (www.unlicense.org).\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "Syphon",
          "Copyright 2010 bangnoise (Tom Butterworth) & vade (Anton Marini). All rights reserved.\n"
          "License: BSD 2-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice\n"
          "and disclaimer; (2) binary distributions reproduce the copyright notice in docs.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED."
        },
        { "IXWebSocket",
          "Copyright (c) 2018 Machine Zone, Inc. All rights reserved.\n"
          "License: BSD 3-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice;\n"
          "(2) binary distributions reproduce the notice in docs; (3) neither the name of the\n"
          "copyright holder nor contributor names may be used to endorse derived products.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "CLI11",
          "CLI11 2.6 Copyright (c) 2017-2025 University of Cincinnati,\n"
          "developed by Henry Schreiner under NSF AWARD 1414736. All rights reserved.\n"
          "License: BSD 3-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice;\n"
          "(2) binary distributions reproduce the notice in docs; (3) the name of the copyright\n"
          "holder and contributors may not be used without specific prior written permission.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "WebGPU-distribution",
          "Copyright (c) 2022-2024 Elie Michel\n"
          "License: MIT\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "wgpu-native",
          "Copyright (c) 2021 The gfx-rs developers\n"
          "License: MIT or Apache 2.0\n"
          "MIT: Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "Apache 2.0: See THIRD_PARTY_NOTICES.txt for full Apache License 2.0 text.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "Sparkle",
          "Copyright (c) 2006-2016 Andy Matuschak and Sparkle Project contributors\n"
          "License: MIT  (loaded at runtime on macOS via Sparkle.framework)\n"
          "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
          "of this software to deal in it without restriction, subject to: the above\n"
          "copyright notice and this permission notice shall be included in all copies.\n"
          "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
        { "NanoSVG",
          "Copyright (c) 2013-14 Mikko Mononen\n"
          "License: zlib/libpng\n"
          "This software is provided 'as-is', without any express or implied warranty.\n"
          "Permission is granted to use, alter and redistribute it freely, subject to:\n"
          "1. The origin must not be misrepresented.\n"
          "2. Altered versions must be plainly marked as such.\n"
          "3. This notice may not be removed or altered from any source distribution."
        },
        { "Snappy",
          "Copyright 2011, Google Inc. All rights reserved.\n"
          "License: BSD 3-Clause\n"
          "Redistribution and use in source and binary forms, with or without modification,\n"
          "are permitted provided that: (1) source distributions retain the copyright notice;\n"
          "(2) binary distributions reproduce the notice in docs; (3) neither the name of\n"
          "Google Inc. nor contributor names may be used to endorse derived products.\n"
          "THIS SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND."
        },
    };

    constexpr int kNoticeCount = static_cast<int>(sizeof(kNotices) / sizeof(kNotices[0]));
    constexpr float kLineHHeader = 16.0f;
    constexpr float kLineHText   = 13.0f;
    constexpr float kSectionGap  = 10.0f;

    // Compute total scrollable content height
    float total_h = 0.0f;
    for (int i = 0; i < kNoticeCount; ++i) {
        total_h += kLineHHeader + 4.0f;
        int line_count = 1;
        for (const char* p = kNotices[i].text; *p; ++p)
            if (*p == '\n') ++line_count;
        total_h += line_count * kLineHText + 4.0f;
        total_h += kSectionGap;
    }
    about_max_scroll_ = std::max(0.0f, total_h - list_h);
    about_scroll_     = std::max(0.0f, std::min(about_scroll_, about_max_scroll_));

    // Scrollable notices area
    tr.push_clip_rect(px, list_top, pw, list_h);
    float cy = list_top - about_scroll_;
    float text_x = px + 20.0f;

    for (int i = 0; i < kNoticeCount; ++i) {
        if (cy + kLineHHeader > list_top && cy < list_top + list_h)
            tr.draw_text(text_x, cy, kNotices[i].name,
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        cy += kLineHHeader + 4.0f;

        const char* p = kNotices[i].text;
        const char* line_start = p;
        auto emit_line = [&]() {
            if (cy + kLineHText > list_top && cy < list_top + list_h) {
                std::string line(line_start, p - line_start);
                tr.draw_text(text_x + 4.0f, cy, line.c_str(),
                             style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f, 0.75f);
            }
            cy += kLineHText;
        };
        while (*p) {
            if (*p == '\n') { emit_line(); line_start = p + 1; }
            ++p;
        }
        if (p > line_start) emit_line();
        cy += kSectionGap;
    }
    tr.pop_clip_rect();

    // Close button
    float btn_w = 80.0f, btn_h = 24.0f;
    float btn_x = px + (pw - btn_w) * 0.5f;
    float btn_y = layout.status_y;
    bool btn_hovered = mouse_.x >= btn_x && mouse_.x <= btn_x + btn_w &&
                       mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h;
    tr.draw_rect(btn_x, btn_y, btn_w, btn_h,
                 btn_hovered ? style_.button_hover[0] : style_.button_bg[0],
                 btn_hovered ? style_.button_hover[1] : style_.button_bg[1],
                 btn_hovered ? style_.button_hover[2] : style_.button_bg[2], 0.85f);
    tr.draw_text(btn_x + 22.0f, btn_y + 4.0f, T("close", "Close"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    // Scrollbar indicator
    if (about_max_scroll_ > 0.0f) {
        float sb_x = px + pw - 7.0f;
        float thumb_h = list_h * list_h / (list_h + about_max_scroll_);
        float thumb_y = list_top + (about_scroll_ / about_max_scroll_) * (list_h - thumb_h);
        tr.draw_rect(sb_x, list_top, 3.0f, list_h,
                     style_.separator[0], style_.separator[1], style_.separator[2], 0.25f);
        tr.draw_rect(sb_x, thumb_y, 3.0f, thumb_h,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.6f);
    }
}

} // namespace vivid::ui
