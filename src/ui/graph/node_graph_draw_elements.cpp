#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/style/i18n.h"
#include "common/string_util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

static OpEnvironment infer_environment(const OperatorInfo& op) {
    if (op.is_gpu) return OpEnvironment::GPU;
    for (const auto& p : op.ports) {
        if (p.type == VIVID_PORT_AUDIO_BUFFER)
            return OpEnvironment::Audio;
    }
    return OpEnvironment::Control;
}

void NodeGraphUI::draw_node_error_tooltip(Renderer2D& tr) {
    if (hovered_node_id_.empty()) return;

    const NodeSnapshot* hovered_ns = snap_.find_node(hovered_node_id_);
    if (!hovered_ns || hovered_ns->error_message.empty()) return;

    static constexpr float kMaxTooltipW = 500.0f;
    static constexpr int   kMaxLines    = 8;

    // Split error message on newlines
    std::vector<std::string> lines;
    {
        std::string tmp;
        for (char c : hovered_ns->error_message) {
            if (c == '\n') { if (!tmp.empty() || !lines.empty()) lines.push_back(tmp); tmp.clear(); }
            else            tmp += c;
        }
        if (!tmp.empty()) lines.push_back(tmp);
    }
    if (lines.empty()) return;

    // Cap number of lines
    if (static_cast<int>(lines.size()) > kMaxLines) {
        lines.resize(kMaxLines);
        lines.back() += " \xe2\x80\xa6"; // UTF-8 ellipsis
    }

    // Truncate each line to kMaxTooltipW
    float max_line_w = 0.f;
    for (auto& line : lines) {
        while (!line.empty() && tr.text_width(line.c_str()) > kMaxTooltipW)
            line.resize(line.size() - 1);
        max_line_w = std::max(max_line_w, tr.text_width(line.c_str()));
    }

    float pad    = kTooltipPad;
    float line_h = kTooltipLineH;
    float header_w = tr.text_width(T("error_label", "Error:"));
    float popup_w  = std::max(header_w, max_line_w) + pad * 2;
    float popup_h  = line_h * (1 + static_cast<float>(lines.size())) + pad * 2;

    float px = mouse_.x + kTooltipCursorOff;
    float py = mouse_.y + kTooltipCursorOff;
    if (px + popup_w > graph_right()) px = mouse_.x - popup_w - kTooltipClampMargin;
    if (py + popup_h > static_cast<float>(win_h_)) py = mouse_.y - popup_h - kTooltipClampMargin;

    // Shadow + Background
    draw_shadow(tr, px, py, popup_w, popup_h);
    tr.draw_rect(px, py, popup_w, popup_h,
                 style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], kTooltipBgAlpha);
    // Red accent line at top
    tr.draw_rect(px, py, popup_w, kTooltipAccentH, 1.0f, 0.3f, 0.3f, 0.9f);
    // "Error:" header in red
    tr.draw_text(px + pad, py + pad, T("error_label", "Error:"), 1.0f, 0.3f, 0.3f);
    // One line per message line
    for (int k = 0; k < static_cast<int>(lines.size()); ++k) {
        tr.draw_text(px + pad, py + pad + line_h * (1 + k), lines[k].c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
}

static constexpr float kDescriptionTooltipDelay = 1.0f;
static constexpr float kMaxDescriptionTooltipW  = 300.0f;

void NodeGraphUI::draw_description_popup(Renderer2D& tr, const std::string& desc) {
    if (desc.empty()) return;

    // Word-wrap into lines
    std::vector<std::string> lines;
    std::string word, line;
    for (size_t ci = 0; ci <= desc.size(); ++ci) {
        char ch = ci < desc.size() ? desc[ci] : ' ';
        if (ch == ' ' || ch == '\n' || ci == desc.size()) {
            if (!word.empty()) {
                std::string test = line.empty() ? word : line + " " + word;
                if (tr.text_width(test.c_str()) > kMaxDescriptionTooltipW && !line.empty()) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = test;
                }
                word.clear();
            }
            if (ch == '\n' && !line.empty()) { lines.push_back(line); line.clear(); }
        } else {
            word += ch;
        }
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.empty()) return;

    float max_line_w = 0.0f;
    for (const auto& l : lines)
        max_line_w = std::max(max_line_w, tr.text_width(l.c_str()));

    float pad = kTooltipPad;
    float line_h = kTooltipLineH;
    float popup_w = max_line_w + pad * 2;
    float popup_h = line_h * static_cast<float>(lines.size()) + pad * 2;

    float px = mouse_.x + kTooltipCursorOff;
    float py = mouse_.y + kTooltipCursorOff;
    if (px + popup_w > static_cast<float>(win_w_))
        px = mouse_.x - popup_w - kTooltipClampMargin;
    if (py + popup_h > static_cast<float>(win_h_))
        py = mouse_.y - popup_h - kTooltipClampMargin;

    draw_shadow(tr, px, py, popup_w, popup_h);
    tr.draw_rect(px, py, popup_w, popup_h,
                 style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], kTooltipBgAlpha);
    tr.draw_rect(px, py, popup_w, kTooltipAccentH,
                 style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    for (int k = 0; k < static_cast<int>(lines.size()); ++k) {
        tr.draw_text(px + pad, py + pad + line_h * static_cast<float>(k), lines[k].c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
}

void NodeGraphUI::draw_param_tooltip(Renderer2D& tr) {
    if (inspector_.hovered_label_idx < 0) return;
    if (inspector_.label_hover_time < kDescriptionTooltipDelay) return;
    if (inspector_.hovered_label_idx >= static_cast<int>(inspector_.label_rects.size())) return;

    const auto& r = inspector_.label_rects[inspector_.hovered_label_idx];
    const auto* ns = snap_.find_node(r.node_id);
    if (!ns || !ns->op_info) return;

    std::string desc;
    for (const auto& pi : ns->op_info->params) {
        if (pi.name == r.param_name) { desc = pi.description; break; }
    }
    draw_description_popup(tr, desc);
}

void NodeGraphUI::draw_port_tooltip(Renderer2D& tr) {
    if (hovered_port_.node_id.empty()) return;
    if (port_hover_time_ < kDescriptionTooltipDelay) return;

    const auto* ns = snap_.find_node(hovered_port_.node_id);
    if (!ns || !ns->op_info) return;

    const VividPortDirection want_dir = hovered_port_.is_output ? VIVID_PORT_OUTPUT : VIVID_PORT_INPUT;

    std::string desc;
    for (const auto& pi : ns->op_info->ports) {
        if (pi.name == hovered_port_.port_name && pi.direction == want_dir) {
            desc = pi.description;
            break;
        }
    }
    if (desc.empty()) {
        // Param-ports (inputs marked is_param in the NodeRect) share the param description.
        for (const auto& pi : ns->op_info->params) {
            if (pi.name == hovered_port_.port_name) {
                desc = pi.description;
                break;
            }
        }
    }
    draw_description_popup(tr, desc);
}

void NodeGraphUI::draw_box_select(Renderer2D& tr) {
    if (!box_selecting_) return;
    // Convert graph-space rect to screen-space
    float cur_gx = sx_to_gx(mouse_.x);
    float cur_gy = sy_to_gy(mouse_.y);
    float gx0 = std::min(box_start_gx_, cur_gx);
    float gy0 = std::min(box_start_gy_, cur_gy);
    float gx1 = std::max(box_start_gx_, cur_gx);
    float gy1 = std::max(box_start_gy_, cur_gy);
    float sx0 = gx_to_sx(gx0);
    float sy0 = gy_to_sy(gy0);
    float sx1 = gx_to_sx(gx1);
    float sy1 = gy_to_sy(gy1);
    float sw = sx1 - sx0;
    float sh = sy1 - sy0;

    // Highlight nodes inside the selection rectangle
    for (const auto& nr : node_rects_) {
        if (nr.x + nr.w >= gx0 && nr.x <= gx1 &&
            nr.y + nr.h >= gy0 && nr.y <= gy1) {
            float nsx = gx_to_sx(nr.x), nsy = gy_to_sy(nr.y);
            float nsw = g_to_s(nr.w), nsh = g_to_s(nr.h);
            tr.draw_rounded_rect(nsx, nsy, nsw, nsh, g_to_s(style_.corner_radius),
                                 style_.accent[0], style_.accent[1], style_.accent[2], kBoxSelectNodeAlpha);
        }
    }

    // Semi-transparent fill
    tr.draw_rect(sx0, sy0, sw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.12f);
    // Border
    draw_rect_border(tr, sx0, sy0, sw, sh, style_.accent[0], style_.accent[1], style_.accent[2], 0.6f);
}


void NodeGraphUI::draw_chooser(Renderer2D& tr) {
    if (!chooser_open_) return;

    bool show_tabs = (chooser_mode_ == ChooserMode::Operators);
    float tab_h = show_tabs ? kChooserTabH : 0.0f;
    bool map_view = show_tabs && chooser_tab_ == ChooserTab::Map;
    float panel_w = chooser_panel_w();

    int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
    if (visible == 0) visible = 1; // show at least the header area
    float error_h = chooser_error_.empty() ? 0.0f : 18.0f;
    float body_h = map_view ? kChooserMapH : (visible * kChooserItemH);
    float panel_h = kChooserHeaderH + tab_h + body_h + 4 + error_h;

    float px = chooser_x();
    float py = kChooserY;

    // Background
    tr.draw_rect(px, py, panel_w, panel_h, style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.97f);
    // Top accent bar
    tr.draw_rect(px, py, panel_w, 2, style_.accent[0], style_.accent[1], style_.accent[2]);

    // Filter text
    float tx = px + 8;
    float ty = py + 6;
    std::string display_filter = chooser_filter_ + "_";
    tr.draw_text(tx, ty, display_filter.c_str(), 1.0f, 1.0f, 1.0f);

    // Tab bar
    if (show_tabs) {
        float tab_y = py + kChooserHeaderH;
        constexpr int kTabCount = 6;
        float tab_w = panel_w / static_cast<float>(kTabCount);

        struct TabDef { const char* label; ChooserTab tab; const float* accent; };
        TabDef tabs[] = {
            {"All",   ChooserTab::All,        style_.accent.data()},
            {"GPU",   ChooserTab::GPU,        kGpuAccent.data()},
            {"Audio", ChooserTab::Audio,      kAudioAccent.data()},
            {"Ctrl",  ChooserTab::Control,    kControlAccent.data()},
            {"Inst",  ChooserTab::Instancing, kGpuAccent.data()},
            {"Map",   ChooserTab::Map,        style_.accent.data()},
        };
        for (int i = 0; i < kTabCount; ++i) {
            float tbx = px + i * tab_w;
            bool active = (chooser_tab_ == tabs[i].tab);
            // Tab label
            float label_alpha = active ? 1.0f : 0.5f;
            float tw = tr.text_width(tabs[i].label);
            float label_x = tbx + (tab_w - tw) * 0.5f;
            tr.draw_text(label_x, tab_y + 4, tabs[i].label,
                         tabs[i].accent[0] * label_alpha + (1.0f - label_alpha) * 0.6f,
                         tabs[i].accent[1] * label_alpha + (1.0f - label_alpha) * 0.6f,
                         tabs[i].accent[2] * label_alpha + (1.0f - label_alpha) * 0.6f);
            // Active underline
            if (active) {
                tr.draw_rect(tbx + 4, tab_y + kChooserTabH - 2, tab_w - 8, 2,
                             tabs[i].accent[0], tabs[i].accent[1], tabs[i].accent[2]);
            }
        }
    }

    // Body: either the scatter-plot map (Map tab) or the scored item list.
    float iy = py + kChooserHeaderH + tab_h;
    if (map_view) {
        draw_chooser_map(tr, px, iy, panel_w, kChooserMapH);
        if (!chooser_error_.empty()) {
            float err_y = py + panel_h - error_h + 1.0f;
            tr.draw_rect(px + 1, err_y - 2.0f, panel_w - 2.0f, error_h,
                         style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.98f);
            tr.push_clip_rect(px + 8.0f, err_y, panel_w - 16.0f, error_h);
            tr.draw_text(px + 8.0f, err_y, chooser_error_.c_str(), 0.95f, 0.36f, 0.36f);
            tr.pop_clip_rect();
        }
        return;
    }

    float ch_list_area_h = visible * kChooserItemH;
    int ch_first = std::max(0, static_cast<int>(std::floor(chooser_scroll_ / kChooserItemH)));
    float ch_offset = chooser_scroll_ - ch_first * kChooserItemH;
    int ch_draw_count = std::min(static_cast<int>(chooser_items_.size()) - ch_first, kChooserMaxVisible + 1);

    tr.push_clip_rect(px, iy, panel_w, ch_list_area_h);
    for (int vi = 0; vi < ch_draw_count; ++vi) {
        int idx = ch_first + vi;
        if (idx >= static_cast<int>(chooser_items_.size())) break;

        float item_y = iy - ch_offset + vi * kChooserItemH;

        // Highlight selected
        if (idx == chooser_sel_) {
            tr.draw_rect(px + 2, item_y, panel_w - 4, kChooserItemH,
                         style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
        }

        const std::string& name = chooser_items_[idx];
        const std::string subtitle =
            idx < static_cast<int>(chooser_subtitles_.size()) ? chooser_subtitles_[idx] : "";

        if (chooser_mode_ == ChooserMode::Operators && name == "+ New Operator...") {
            // Sentinel: accent-colored text, no env dot
            tr.draw_text(px + 10, item_y + 3, name.c_str(),
                         style_.accent[0], style_.accent[1], style_.accent[2]);
        } else if (chooser_mode_ == ChooserMode::FileDrop) {
            tr.draw_text(px + 10, item_y + 2, name.c_str(), 1.0f, 1.0f, 1.0f);
            if (!subtitle.empty()) {
                tr.draw_text(px + 10, item_y + 16, subtitle.c_str(),
                             style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            }
        } else {
            // Environment color dot
            const float* dcol = kControlAccent.data();
            auto cat_it = snap_.operator_catalog.find(name);
            OpEnvironment env = OpEnvironment::Control;
            const char* label = name.c_str();
            if (cat_it != snap_.operator_catalog.end() && cat_it->second) {
                env = infer_environment(*cat_it->second);
                dcol = (env == OpEnvironment::GPU) ? kGpuAccent.data()
                     : (env == OpEnvironment::Audio) ? kAudioAccent.data()
                     : kControlAccent.data();
                if (!cat_it->second->display_name.empty())
                    label = cat_it->second->display_name.c_str();
            }
            float dot_x = px + 10;
            float dot_y = item_y + (kChooserItemH - 6) * 0.5f;
            tr.draw_rect(dot_x, dot_y, 6, 6, dcol[0], dcol[1], dcol[2]);

            // Environment tag
            const char* tag = (env == OpEnvironment::GPU) ? "[G]"
                            : (env == OpEnvironment::Audio) ? "[A]"
                            : "[C]";
            tr.draw_text(px + 20, item_y + 3, tag, dcol[0], dcol[1], dcol[2]);

            // Display name (falls back to stable id if catalog entry missing)
            tr.draw_text(px + 42, item_y + 3, label, 0.85f, 0.87f, 0.90f);
        }
    }
    tr.pop_clip_rect();

    // Show "no matches" if empty
    if (chooser_items_.empty()) {
        tr.draw_text(px + 8, iy + 3, T("no_matches", "no matches"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    }

    if (!chooser_error_.empty()) {
        float err_y = py + panel_h - error_h + 1.0f;
        tr.draw_rect(px + 1, err_y - 2.0f, panel_w - 2.0f, error_h,
                     style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.98f);
        tr.push_clip_rect(px + 8.0f, err_y, panel_w - 16.0f, error_h);
        tr.draw_text(px + 8.0f, err_y, chooser_error_.c_str(), 0.95f, 0.36f, 0.36f);
        tr.pop_clip_rect();
    }

    // Scrollbar when items overflow
    int total_items = static_cast<int>(chooser_items_.size());
    if (total_items > kChooserMaxVisible) {
        float track_x = px + panel_w - kInspScrollbarW - 2.0f;
        float track_y = iy;
        float track_h = visible * kChooserItemH;

        // Track background
        tr.draw_rect(track_x, track_y, kInspScrollbarW, track_h,
                     style_.scrollbar_track[0], style_.scrollbar_track[1], style_.scrollbar_track[2], kScrollbarTrackAlpha);

        // Thumb
        float ratio = static_cast<float>(kChooserMaxVisible) / static_cast<float>(total_items);
        float thumb_h = std::max(kInspScrollbarMinThumb, track_h * ratio);
        float max_scroll_px = std::max(1.0f, (total_items - kChooserMaxVisible) * kChooserItemH);
        float scroll_ratio = chooser_scroll_ / max_scroll_px;
        float thumb_y = track_y + scroll_ratio * (track_h - thumb_h);
        tr.draw_rect(track_x, thumb_y, kInspScrollbarW, thumb_h,
                     style_.scrollbar_thumb[0], style_.scrollbar_thumb[1], style_.scrollbar_thumb[2], kScrollbarThumbIdle);
    }
}

// -----------------------------------------------------------------------
// Workspace grid
// -----------------------------------------------------------------------

void NodeGraphUI::draw_grid(Renderer2D& tr) {
    // Skip when zoomed out so far that grid lines would be < 8px apart
    float screen_spacing = kGridSpacing * zoom_;
    if (screen_spacing < 8.0f) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    // Find the range of graph-space coordinates visible on screen
    float g_left   = sx_to_gx(0.0f);
    float g_right  = sx_to_gx(wf);
    float g_top    = sy_to_gy(0.0f);
    float g_bottom = sy_to_gy(hf);

    // Snap to grid boundaries
    float gx_start = std::floor(g_left / kGridSpacing) * kGridSpacing;
    float gy_start = std::floor(g_top / kGridSpacing) * kGridSpacing;

    // Vertical lines
    for (float gx = gx_start; gx <= g_right; gx += kGridSpacing) {
        float sx = gx_to_sx(gx);
        tr.draw_rect(sx, 0.0f, 1.0f, hf,
                     kGpuAccent[0], kGpuAccent[1], kGpuAccent[2], kGridLineAlpha);
    }

    // Horizontal lines
    for (float gy = gy_start; gy <= g_bottom; gy += kGridSpacing) {
        float sy = gy_to_sy(gy);
        tr.draw_rect(0.0f, sy, wf, 1.0f,
                     kGpuAccent[0], kGpuAccent[1], kGpuAccent[2], kGridLineAlpha);
    }
}

// -----------------------------------------------------------------------
// Sticky notes
// -----------------------------------------------------------------------

void NodeGraphUI::draw_sticky_notes(Renderer2D& tr) {
    sticky_note_rects_.clear();
    sticky_link_rects_.clear();
    for (const auto& sn : snap_.sticky_notes) {
        float sx = gx_to_sx(sn.x);
        float sy = gy_to_sy(sn.y);
        float sw = g_to_s(sn.width);
        float sh = g_to_s(sn.height);

        // Live position override during drag
        int idx = static_cast<int>(&sn - &snap_.sticky_notes[0]);
        if (idx == dragging_sticky_idx_ && mouse_.left_down) {
            float gx = sx_to_gx(mouse_.x) - sticky_drag_offset_x_;
            float gy = sy_to_gy(mouse_.y) - sticky_drag_offset_y_;
            sx = gx_to_sx(gx);
            sy = gy_to_sy(gy);
        }
        // Live size/position override during resize
        if (idx == resizing_sticky_idx_ && mouse_.left_down) {
            float dgx = sx_to_gx(mouse_.x) - sticky_resize_start_gx_;
            float dgy = sy_to_gy(mouse_.y) - sticky_resize_start_gy_;
            float nx = sticky_resize_start_x_, ny = sticky_resize_start_y_;
            float nw = sticky_resize_start_w_, nh = sticky_resize_start_h_;
            if (sticky_resize_edge_ & 2) nw = std::max(kStickyMinW, sticky_resize_start_w_ + dgx);
            if (sticky_resize_edge_ & 1) { nw = std::max(kStickyMinW, sticky_resize_start_w_ - dgx); nx = sticky_resize_start_x_ + (sticky_resize_start_w_ - nw); }
            if (sticky_resize_edge_ & 8) nh = std::max(kStickyMinH, sticky_resize_start_h_ + dgy);
            if (sticky_resize_edge_ & 4) { nh = std::max(kStickyMinH, sticky_resize_start_h_ - dgy); ny = sticky_resize_start_y_ + (sticky_resize_start_h_ - nh); }
            sx = gx_to_sx(nx); sy = gy_to_sy(ny);
            sw = g_to_s(nw);   sh = g_to_s(nh);
        }

        sticky_note_rects_.push_back({sn.id, sx, sy, sw, sh});

        int ci = sn.color;
        if (ci < 0 || ci >= kStickyColorCount) ci = 0;
        float cr = kStickyColors[ci][0];
        float cg = kStickyColors[ci][1];
        float cb = kStickyColors[ci][2];

        // Background
        tr.draw_rounded_rect(sx, sy, sw, sh, kStickyCornerR, cr, cg, cb, 0.85f);

        // Selection border (draw slightly larger rect behind)
        if (sn.id == selected_sticky_id_) {
            tr.draw_rounded_rect(sx - 2, sy - 2, sw + 4, sh + 4, kStickyCornerR + 2,
                                 kAccent[0], kAccent[1], kAccent[2], 0.5f);
            // Re-draw background on top to create border effect
            tr.draw_rounded_rect(sx, sy, sw, sh, kStickyCornerR, cr, cg, cb, 0.85f);
        }

        // Text
        float text_x = sx + g_to_s(kStickyPad);
        float text_y = sy + g_to_s(kStickyPad);
        float text_scale = zoom_ * 0.85f;
        if (text_scale < 0.3f) text_scale = 0.3f;
        float line_h = 14.0f * text_scale;
        float line_spacing = line_h + 2.0f * text_scale;
        float max_text_w = sw - 2.0f * g_to_s(kStickyPad);

        // Lightweight markdown scanner. Produces a run list where each run is either
        // plain text, bold, italic, inline code, or a link. Bold and italic are mutually
        // exclusive (no nesting). Code spans suspend other parsing until the closing backtick.
        // Unterminated markers are emitted as literal characters.
        struct MdRun {
            std::string text;
            bool bold = false;
            bool italic = false;
            bool code = false;
            std::string link_url;  // non-empty = link
        };
        auto parse_md_runs = [](const std::string& s) -> std::vector<MdRun> {
            std::vector<MdRun> runs;
            std::string pending;
            auto flush_pending = [&]() {
                if (!pending.empty()) {
                    MdRun r; r.text = pending; runs.push_back(std::move(r));
                    pending.clear();
                }
            };
            size_t i = 0;
            while (i < s.size()) {
                char c = s[i];
                // Backslash-escape for markdown meta-chars
                if (c == '\\' && i + 1 < s.size()) {
                    char nx = s[i+1];
                    if (nx == '*' || nx == '_' || nx == '[' || nx == '`' || nx == '\\') {
                        pending += nx;
                        i += 2;
                        continue;
                    }
                }
                // Bold ** (longest match, before italic *)
                if (c == '*' && i + 1 < s.size() && s[i+1] == '*') {
                    size_t close = s.find("**", i + 2);
                    if (close != std::string::npos && close > i + 2) {
                        flush_pending();
                        MdRun r; r.text = s.substr(i + 2, close - i - 2); r.bold = true;
                        runs.push_back(std::move(r));
                        i = close + 2;
                        continue;
                    }
                }
                // Italic * (single, not part of **)
                if (c == '*') {
                    size_t j = i + 1;
                    size_t close = std::string::npos;
                    while (j < s.size()) {
                        if (s[j] == '*') {
                            bool is_double = (j + 1 < s.size() && s[j+1] == '*');
                            if (is_double) { j += 2; continue; }
                            close = j;
                            break;
                        }
                        j++;
                    }
                    if (close != std::string::npos && close > i + 1) {
                        flush_pending();
                        MdRun r; r.text = s.substr(i + 1, close - i - 1); r.italic = true;
                        runs.push_back(std::move(r));
                        i = close + 1;
                        continue;
                    }
                }
                // Italic _
                if (c == '_') {
                    size_t close = s.find('_', i + 1);
                    if (close != std::string::npos && close > i + 1) {
                        flush_pending();
                        MdRun r; r.text = s.substr(i + 1, close - i - 1); r.italic = true;
                        runs.push_back(std::move(r));
                        i = close + 1;
                        continue;
                    }
                }
                // Inline code
                if (c == '`') {
                    size_t close = s.find('`', i + 1);
                    if (close != std::string::npos && close > i + 1) {
                        flush_pending();
                        MdRun r; r.text = s.substr(i + 1, close - i - 1); r.code = true;
                        runs.push_back(std::move(r));
                        i = close + 1;
                        continue;
                    }
                }
                // Link [text](url)
                if (c == '[') {
                    size_t mid = s.find("](", i + 1);
                    size_t end = (mid != std::string::npos) ? s.find(')', mid + 2) : std::string::npos;
                    if (end != std::string::npos) {
                        flush_pending();
                        MdRun r;
                        r.text = s.substr(i + 1, mid - i - 1);
                        r.link_url = s.substr(mid + 2, end - mid - 2);
                        runs.push_back(std::move(r));
                        i = end + 1;
                        continue;
                    }
                }
                pending += c;
                i++;
            }
            flush_pending();
            return runs;
        };

        // Sum visual width of runs at a given scale.
        auto runs_width = [&](const std::vector<MdRun>& runs, float scale) -> float {
            float w = 0.0f;
            for (const auto& r : runs) w += tr.text_width(r.text.c_str(), scale);
            return w;
        };

        // Measure display width after stripping markdown markers.
        auto plain_width = [&](const std::string& s) -> float {
            return runs_width(parse_md_runs(s), text_scale);
        };

        // Word-wrap a line into visual lines that fit max_w.
        // Returns pairs of (display text, offset into src where this visual line starts).
        struct VisLine { std::string text; size_t src_offset; };
        auto raw_width = [&](const std::string& s) -> float {
            return tr.text_width(s.c_str(), text_scale);
        };

        auto wrap_line_impl = [&](const std::string& src, float max_w,
                                   size_t base_offset,
                                   const std::function<float(const std::string&)>& measure) -> std::vector<VisLine> {
            std::vector<VisLine> out;
            if (max_w <= 0 || src.empty()) { out.push_back({src, base_offset}); return out; }
            std::string cur;
            size_t cur_start = 0;
            size_t i = 0;
            while (i < src.size()) {
                size_t ws = i;
                while (ws < src.size() && src[ws] == ' ') ++ws;
                size_t we = ws;
                while (we < src.size() && src[we] != ' ') ++we;
                std::string token = src.substr(i, we - i);
                std::string test = cur.empty() ? token : cur + token;
                if (measure(test) > max_w && !cur.empty()) {
                    while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                    out.push_back({cur, base_offset + cur_start});
                    cur = src.substr(ws, we - ws);
                    cur_start = ws;
                } else {
                    cur = test;
                }
                i = we;
            }
            if (!cur.empty()) {
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                out.push_back({cur, base_offset + cur_start});
            }
            if (out.empty()) out.push_back({"", base_offset});
            return out;
        };

        // Display path: measure with markdown stripped
        auto wrap_line = [&](const std::string& src, float max_w, size_t base_offset = 0) {
            return wrap_line_impl(src, max_w, base_offset, plain_width);
        };
        // Edit path: measure raw text including markdown markers
        auto wrap_line_raw = [&](const std::string& src, float max_w, size_t base_offset = 0) {
            return wrap_line_impl(src, max_w, base_offset, raw_width);
        };

        if (editing_sticky_ && sticky_edit_id_ == sn.id) {
            // Render edit buffer with word-wrap
            const std::string& buf = sticky_edit_buffer_;
            float line_y = text_y;
            size_t cursor_pos = static_cast<size_t>(text_edit_.cursor);
            float cursor_draw_x = 0, cursor_draw_y = 0;
            bool cursor_found = false;
            size_t pos = 0;

            while (pos <= buf.size()) {
                size_t nl = buf.find('\n', pos);
                if (nl == std::string::npos) nl = buf.size();
                std::string line = buf.substr(pos, nl - pos);

                auto vis = wrap_line_raw(line, max_text_w, pos);
                for (size_t vi = 0; vi < vis.size(); ++vi) {
                    if (line_y > sy + sh - g_to_s(kStickyPad)) break;
                    size_t vstart = vis[vi].src_offset;
                    size_t vend = (vi + 1 < vis.size()) ? vis[vi + 1].src_offset : nl;

                    // Selection highlight: draw underneath text.
                    if (text_edit_.has_selection()) {
                        size_t sel_lo = static_cast<size_t>(text_edit_.sel_min());
                        size_t sel_hi = static_cast<size_t>(text_edit_.sel_max());
                        if (sel_hi > vstart && sel_lo < vend) {
                            int disp_size = static_cast<int>(vis[vi].text.size());
                            int lo_local = static_cast<int>(sel_lo > vstart ? sel_lo - vstart : 0);
                            int hi_local = static_cast<int>(sel_hi < vend ? sel_hi - vstart : vend - vstart);
                            lo_local = std::min(lo_local, disp_size);
                            hi_local = std::min(hi_local, disp_size);
                            // Pad highlight past last glyph when selection spans the newline,
                            // so users see the newline is part of the selection.
                            float trailing = (sel_hi > vend) ? 4.0f * text_scale : 0.0f;
                            if (hi_local > lo_local || trailing > 0.0f) {
                                std::string before = vis[vi].text.substr(0, lo_local);
                                std::string sel_str = vis[vi].text.substr(lo_local, hi_local - lo_local);
                                float x0 = text_x + tr.text_width(before.c_str(), text_scale);
                                float w = tr.text_width(sel_str.c_str(), text_scale) + trailing;
                                tr.draw_rect(x0, line_y, w, line_h,
                                             style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
                            }
                        }
                    }

                    tr.draw_text(text_x, line_y, vis[vi].text.c_str(),
                                 0.1f, 0.1f, 0.1f, 1.0f, text_scale);

                    // Map cursor into this visual line
                    if (!cursor_found && cursor_pos >= vstart && cursor_pos <= vend) {
                        if (cursor_pos == vend && vi + 1 < vis.size()) {
                            // Belongs to next visual line
                        } else {
                            size_t off = cursor_pos - vstart;
                            if (off > vis[vi].text.size()) off = vis[vi].text.size();
                            std::string before = vis[vi].text.substr(0, off);
                            cursor_draw_x = text_x + tr.text_width(before.c_str(), text_scale);
                            cursor_draw_y = line_y;
                            cursor_found = true;
                        }
                    }
                    line_y += line_spacing;
                }

                pos = nl + 1;
                if (pos > buf.size()) break;
                if (line_y > sy + sh - g_to_s(kStickyPad)) break;
            }

            if (!cursor_found) {
                cursor_draw_x = text_x;
                cursor_draw_y = std::max(text_y, line_y - line_spacing);
            }

            if (cursor_blink_on()) {
                tr.draw_rect(cursor_draw_x, cursor_draw_y, 1.0f, line_h,
                             0.1f, 0.1f, 0.1f, 0.8f);
            }
        } else {
            // Render text with lightweight markdown and word-wrap
            const std::string& text = sn.text;
            float line_y = text_y;

            // Render a single visual line with markdown runs.
            // `default_bold` is true for heading lines (all plain runs render bold).
            auto render_md_line = [&](const std::string& line, float lx, float ly,
                                      float scale, float lh, bool default_bold) {
                auto runs = parse_md_runs(line);
                for (auto& r : runs) {
                    float w = tr.text_width(r.text.c_str(), scale);
                    if (r.code) {
                        // Subtle background panel for code spans
                        tr.draw_rect(lx - 1.0f, ly - 1.0f, w + 2.0f, lh + 2.0f,
                                     0.0f, 0.0f, 0.0f, 0.08f);
                        tr.draw_text(lx, ly, r.text.c_str(),
                                     0.35f, 0.15f, 0.15f, 1.0f, scale);
                    } else if (!r.link_url.empty()) {
                        tr.draw_text(lx, ly, r.text.c_str(),
                                     kAccent[0] * 0.7f, kAccent[1] * 0.7f, kAccent[2] * 0.9f,
                                     1.0f, scale);
                        sticky_link_rects_.push_back({lx, ly, w, lh, r.link_url});
                    } else if (r.bold || default_bold) {
                        tr.draw_text(lx, ly, r.text.c_str(),
                                     0.05f, 0.05f, 0.05f, 1.0f, scale);
                    } else if (r.italic) {
                        // No italic font variant; tint the color so italic spans are visually distinct.
                        tr.draw_text(lx, ly, r.text.c_str(),
                                     0.20f, 0.20f, 0.40f, 1.0f, scale);
                    } else {
                        tr.draw_text(lx, ly, r.text.c_str(),
                                     0.15f, 0.15f, 0.15f, 1.0f, scale);
                    }
                    lx += w;
                }
            };

            size_t pos = 0;
            while (pos < text.size()) {
                size_t nl = text.find('\n', pos);
                if (nl == std::string::npos) nl = text.size();
                std::string line = text.substr(pos, nl - pos);

                bool is_bullet = false;
                if (line.size() >= 2 && line[0] == '-' && line[1] == ' ') {
                    is_bullet = true;
                    line = line.substr(2);
                }

                // Heading detection: `# `, `## `, `### ` after bullet strip.
                float head_scale_mul = 1.0f;
                bool is_heading = false;
                if (line.size() >= 4 && line[0] == '#' && line[1] == '#' && line[2] == '#' && line[3] == ' ') {
                    head_scale_mul = 1.15f;
                    is_heading = true;
                    line = line.substr(4);
                } else if (line.size() >= 3 && line[0] == '#' && line[1] == '#' && line[2] == ' ') {
                    head_scale_mul = 1.35f;
                    is_heading = true;
                    line = line.substr(3);
                } else if (line.size() >= 2 && line[0] == '#' && line[1] == ' ') {
                    head_scale_mul = 1.6f;
                    is_heading = true;
                    line = line.substr(2);
                }

                float h_scale = text_scale * head_scale_mul;
                float h_line_h = 14.0f * h_scale;
                float h_line_spacing = h_line_h + 2.0f * h_scale;
                float bullet_indent = is_bullet ? 10.0f * text_scale : 0.0f;
                auto wrapped = wrap_line_impl(line, max_text_w - bullet_indent, 0,
                                              [&](const std::string& s) {
                                                  return runs_width(parse_md_runs(s), h_scale);
                                              });

                for (size_t wi = 0; wi < wrapped.size(); ++wi) {
                    if (line_y > sy + sh - g_to_s(kStickyPad)) break;
                    float lx = text_x;
                    if (is_bullet && wi == 0)
                        tr.draw_text(lx, line_y, "\xe2\x80\xa2",
                                     0.1f, 0.1f, 0.1f, 1.0f, text_scale);
                    lx += bullet_indent;
                    render_md_line(wrapped[wi].text, lx, line_y, h_scale, h_line_h, is_heading);
                    line_y += h_line_spacing;
                }

                pos = nl + 1;
                if (line_y > sy + sh - g_to_s(kStickyPad)) break;
            }
        }

        // Resize grab handles at corners (small squares)
        if (sn.id == selected_sticky_id_) {
            float gs = kStickyResizeGrab;
            float ha = 0.3f;
            tr.draw_rect(sx + sw - gs, sy + sh - gs, gs, gs,
                         0.2f, 0.2f, 0.2f, ha);  // bottom-right
            tr.draw_rect(sx, sy + sh - gs, gs, gs,
                         0.2f, 0.2f, 0.2f, ha);  // bottom-left
            tr.draw_rect(sx + sw - gs, sy, gs, gs,
                         0.2f, 0.2f, 0.2f, ha);  // top-right
        }
    }

    // Sticky note color picker menu
    if (sticky_color_menu_open_) {
        float cmx = sticky_color_menu_x_;
        float cmy = sticky_color_menu_y_;
        float swatch_size = 20.0f;
        float gap = 4.0f;
        float total_w = kStickyColorCount * (swatch_size + gap) - gap + 8.0f;
        float total_h = swatch_size + 8.0f;
        tr.draw_rounded_rect(cmx, cmy, total_w, total_h, 4.0f,
                             0.15f, 0.16f, 0.18f, 0.95f);
        for (int i = 0; i < kStickyColorCount; ++i) {
            float sx2 = cmx + 4.0f + i * (swatch_size + gap);
            float sy2 = cmy + 4.0f;
            tr.draw_rounded_rect(sx2, sy2, swatch_size, swatch_size, 3.0f,
                                 kStickyColors[i][0], kStickyColors[i][1], kStickyColors[i][2], 1.0f);
        }
    }
}

// -----------------------------------------------------------------------
// Session grid (variation strip at bottom)
// -----------------------------------------------------------------------
void NodeGraphUI::draw_session_grid(Renderer2D& tr) {
    variation_cell_rects_.clear();
    session_button_rects_.clear();
    session_ctx_menu_rects_.clear();

    if (!session_grid_open_) {
        session_collapsed_rect_ = {};
        session_collapsed_rect_.visible = true;

        std::string summary = "SESSION";
        summary += " | ";
        summary += std::to_string(snap_.variations.size()) + " VARS";
        if (snap_.active_variation >= 0 &&
            snap_.active_variation < static_cast<int>(snap_.variations.size())) {
            summary += " | ";
            summary += snap_.variations[snap_.active_variation].name;
        }
        if (snap_.queued_variation >= 0 &&
            snap_.queued_variation < static_cast<int>(snap_.variations.size())) {
            summary += " | > ";
            summary += snap_.variations[snap_.queued_variation].name;
        }
        if (snap_.variation_dirty)
            summary += " | DIRTY";

        const float tab_h = 24.0f;
        const float tab_w = std::min(static_cast<float>(win_w_) - 24.0f,
                                     std::max(220.0f, tr.text_width(summary.c_str()) + 22.0f));
        const float tab_x = (static_cast<float>(win_w_) - tab_w) * 0.5f;
        const float tab_y = static_cast<float>(win_h_) - tab_h - 6.0f;
        const bool hovered = mouse_.x >= tab_x && mouse_.x <= tab_x + tab_w &&
                             mouse_.y >= tab_y && mouse_.y <= tab_y + tab_h;
        tr.draw_rounded_rect(tab_x, tab_y, tab_w, tab_h, 4.0f,
                             0.08f, 0.10f, 0.12f, hovered ? 0.96f : 0.88f);
        tr.draw_rect(tab_x, tab_y, tab_w, 1.0f,
                     style_.accent[0], style_.accent[1], style_.accent[2], hovered ? 0.80f : 0.52f);
        tr.draw_rect(tab_x + 8.0f, tab_y + 8.0f, 8.0f, 8.0f,
                     style_.accent[0], style_.accent[1], style_.accent[2], hovered ? 0.88f : 0.58f);
        tr.push_clip_rect(tab_x + 22.0f, tab_y + 4.0f, tab_w - 28.0f, tab_h - 8.0f);
        tr.draw_text(tab_x + 22.0f, tab_y + 5.0f, summary.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        tr.pop_clip_rect();
        session_collapsed_rect_.x = tab_x;
        session_collapsed_rect_.y = tab_y;
        session_collapsed_rect_.w = tab_w;
        session_collapsed_rect_.h = tab_h;
        return;
    }

    session_collapsed_rect_ = {};

    float strip_y = static_cast<float>(win_h_) - kSessionStripH;
    float strip_w = static_cast<float>(win_w_);

    // Background
    tr.draw_rect(0, strip_y, strip_w, kSessionStripH,
                 style_.dark_bg[0], style_.dark_bg[1], style_.dark_bg[2], 0.95f);
    // Top border
    tr.draw_rect(0, strip_y, strip_w, 1,
                 style_.accent[0], style_.accent[1], style_.accent[2], 0.5f);

    // --- Header row ---
    float hx = kSessionPadX;
    float hy = strip_y + 5;
    tr.draw_text(hx, hy + 2, T("session", "SESSION"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    hx += 70;

    // Quantize buttons
    const char* quantize_labels[] = {
        T("quantize_off", "Off"), T("quantize_beat", "Beat"),
        T("quantize_bar", "Bar"), T("quantize_4bar", "4Bar") };
    tr.draw_text(hx, hy + 2, T("quantize", "Quantize:"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    hx += 72;
    for (int i = 0; i < 4; ++i) {
        float bw = 38.0f;
        bool enabled = true;
        bool active = (session_quantize_mode_ == i);
        float r = active && enabled ? style_.accent[0] : style_.slider_track[0];
        float g = active && enabled ? style_.accent[1] : style_.slider_track[1];
        float b = active && enabled ? style_.accent[2] : style_.slider_track[2];
        tr.draw_rect(hx, hy, bw, 18, r, g, b, enabled ? (active ? 0.9f : 0.6f) : 0.22f);
        float trr = enabled ? style_.bright_text[0] : style_.dim_text[0];
        float trg = enabled ? style_.bright_text[1] : style_.dim_text[1];
        float trb = enabled ? style_.bright_text[2] : style_.dim_text[2];
        tr.draw_text(hx + 4, hy + 2, quantize_labels[i], trr, trg, trb);
        session_button_rects_.push_back({hx, hy, bw, 18.0f, 2 + i, enabled});
        hx += bw + 3;
    }

    hx += 10;

    // Branch button (duplicates active variation)
    if (snap_.active_variation >= 0) {
        float branch_w = 54.0f;
        tr.draw_rect(hx, hy, branch_w, 18,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.7f);
        tr.draw_text(hx + 6, hy + 2, T("branch", "Branch"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        session_button_rects_.push_back({hx, hy, branch_w, 18.0f, 6, true});
        hx += branch_w + 6;
    }

    // Update button (enabled only when dirty)
    if (snap_.active_variation >= 0 && snap_.variation_dirty) {
        float update_w = 54.0f;
        tr.draw_rect(hx, hy, update_w, 18,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
        tr.draw_text(hx + 6, hy + 2, T("update", "Update"),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        session_button_rects_.push_back({hx, hy, update_w, 18.0f, 1, true});
        hx += update_w + 6;
    }

    {
        const char* close_label = "X";
        float close_w = 18.0f;
        float close_x = strip_w - kSessionPadX - close_w;
        bool hovered = mouse_.x >= close_x && mouse_.x <= close_x + close_w &&
                       mouse_.y >= hy && mouse_.y <= hy + 18.0f;
        tr.draw_rect(close_x, hy, close_w, 18.0f,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2],
                     hovered ? 0.82f : 0.62f);
        tr.draw_text(close_x + 6.0f, hy + 2, close_label,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        session_button_rects_.push_back({close_x, hy, close_w, 18.0f, 7, true});
    }

    // --- Card row ---
    float cy = strip_y + kSessionHeaderH + 4;
    float cx = kSessionPadX - session_scroll_x_;

    for (int i = 0; i < static_cast<int>(snap_.variations.size()); ++i) {
        // Skip the card being dragged (drawn separately as ghost)
        if (session_drag_active_ && session_drag_idx_ == i) {
            variation_cell_rects_.push_back({cx, cy, kSessionCellW, kSessionCellH, i});
            cx += kSessionCellW + kSessionCellPad;
            continue;
        }

        bool is_active = (i == snap_.active_variation);
        bool is_queued = (i == snap_.queued_variation);
        bool is_selected = (i == session_selected_idx_);
        bool is_dirty = is_active && snap_.variation_dirty;

        // Card background
        float bg_r, bg_g, bg_b, bg_a;
        if (is_active) {
            bg_r = style_.accent[0] * 0.25f;
            bg_g = style_.accent[1] * 0.25f;
            bg_b = style_.accent[2] * 0.25f;
            bg_a = 0.85f;
        } else if (is_queued) {
            bg_r = style_.accent[0] * 0.15f;
            bg_g = style_.accent[1] * 0.15f;
            bg_b = style_.accent[2] * 0.15f;
            bg_a = 0.7f;
        } else {
            bg_r = style_.slider_track[0];
            bg_g = style_.slider_track[1];
            bg_b = style_.slider_track[2];
            bg_a = 0.5f;
        }
        tr.draw_rect(cx, cy, kSessionCellW, kSessionCellH, bg_r, bg_g, bg_b, bg_a);

        // Border: active = 2px accent, queued = pulsing accent, selected = bright highlight
        if (is_active) {
            draw_rect_border(tr, cx, cy, kSessionCellW, kSessionCellH,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.9f, 2.0f);
        } else if (is_queued) {
            float pulse = 0.5f + 0.3f * std::sin(cursor_blink_time_ * 6.0f);
            draw_rect_border(tr, cx, cy, kSessionCellW, kSessionCellH,
                             style_.accent[0], style_.accent[1], style_.accent[2], pulse);
        }
        if (is_selected) {
            draw_rect_border(tr, cx - 1, cy - 1, kSessionCellW + 2, kSessionCellH + 2,
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.7f);
        }

        // Drag insertion indicator
        if (session_drag_active_ && session_drag_target_idx_ == i) {
            tr.draw_rect(cx - 2, cy, 2, kSessionCellH,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
        }

        // Line 1: Variation name (truncated)
        float text_y = cy + 6;
        if (session_editing_name_ && session_edit_idx_ == i) {
            if (text_edit_.has_selection()) {
                int lo = text_edit_.sel_min();
                int hi = text_edit_.sel_max();
                float sel_x0 = cx + 8 + tr.text_width(session_edit_buffer_.substr(0, lo).c_str());
                float sel_x1 = cx + 8 + tr.text_width(session_edit_buffer_.substr(0, hi).c_str());
                tr.draw_rect(sel_x0, text_y - 1, sel_x1 - sel_x0, 14,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
            }
            tr.draw_text(cx + 8, text_y, session_edit_buffer_.c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            if (cursor_blink_on()) {
                int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(session_edit_buffer_.size())));
                float cur_x = cx + 8 + tr.text_width(session_edit_buffer_.substr(0, cpos).c_str());
                tr.draw_rect(cur_x, text_y - 1, 1.0f, 14,
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            }
        } else {
            const std::string& name = snap_.variations[i].name;
            // Truncate name to fit card width
            float max_text_w = kSessionCellW - 16;
            std::string label = name;
            if (tr.text_width(label.c_str()) > max_text_w) {
                while (label.size() > 1 && tr.text_width((label + "...").c_str()) > max_text_w)
                    label.pop_back();
                label += "...";
            }
            float text_r = is_active ? style_.bright_text[0] : style_.dim_text[0];
            float text_g = is_active ? style_.bright_text[1] : style_.dim_text[1];
            float text_b = is_active ? style_.bright_text[2] : style_.dim_text[2];
            tr.draw_text(cx + 8, text_y, label.c_str(), text_r, text_g, text_b);
        }

        // Line 2: Status markers
        float marker_y = cy + kSessionCellH - 14;
        float mx = cx + 8;
        if (is_active) {
            // Active dot
            tr.draw_rect(mx, marker_y + 4, 6, 6,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
            mx += 10;
        }
        if (is_queued) {
            // Queued arrow indicator
            tr.draw_text(mx, marker_y + 1, ">",
                         style_.accent[0], style_.accent[1], style_.accent[2]);
            mx += 12;
        }
        if (is_dirty) {
            // Dirty dot
            tr.draw_rect(mx, marker_y + 4, kSessionDirtyDotR * 2, kSessionDirtyDotR * 2,
                         1.0f, 0.8f, 0.2f, 0.9f);
        }

        variation_cell_rects_.push_back({cx, cy, kSessionCellW, kSessionCellH, i});
        cx += kSessionCellW + kSessionCellPad;
    }

    // Drag insertion indicator at the end
    if (session_drag_active_ && session_drag_target_idx_ == static_cast<int>(snap_.variations.size())) {
        tr.draw_rect(cx - 2, cy, 2, kSessionCellH,
                     style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
    }

    // [+ Save New] button
    const char* save_new_label = T("save_new", "+ Save New");
    float new_btn_w = tr.text_width(save_new_label) + 16.0f;
    tr.draw_rect(cx, cy, new_btn_w, kSessionCellH,
                 style_.slider_track[0], style_.slider_track[1], style_.slider_track[2], 0.5f);
    tr.draw_text(cx + 8, cy + 14, save_new_label,
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    session_button_rects_.push_back({cx, cy, new_btn_w, kSessionCellH, 0, true});

    // --- Drag ghost card ---
    if (session_drag_active_ && session_drag_idx_ >= 0 &&
        session_drag_idx_ < static_cast<int>(snap_.variations.size())) {
        float ghost_x = mouse_.x - kSessionCellW * 0.5f;
        float ghost_y = cy;
        tr.draw_rect(ghost_x, ghost_y, kSessionCellW, kSessionCellH,
                     style_.accent[0] * 0.3f, style_.accent[1] * 0.3f, style_.accent[2] * 0.3f, 0.6f);
        draw_rect_border(tr, ghost_x, ghost_y, kSessionCellW, kSessionCellH,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.8f);
        tr.draw_text(ghost_x + 8, ghost_y + 14,
                     snap_.variations[session_drag_idx_].name.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }

    // --- Context menu ---
    if (session_ctx_menu_open_ && session_ctx_menu_idx_ >= 0) {
        const char* ctx_labels[] = {
            T("variation_rename", "Rename"), T("variation_duplicate", "Duplicate"),
            T("variation_delete", "Delete"), T("variation_branch_from", "Branch From") };
        int item_count = 4;
        float menu_w = kSessionCtxMenuW;
        float menu_h = item_count * kSessionCtxMenuItemH + 4;
        float menu_x = session_ctx_menu_x_;
        float menu_y = session_ctx_menu_y_;
        // Clamp to screen
        if (menu_x + menu_w > strip_w) menu_x = strip_w - menu_w;
        if (menu_y + menu_h > static_cast<float>(win_h_)) menu_y -= menu_h;

        draw_popup_bg(tr, style_, menu_x, menu_y, menu_w, menu_h);

        for (int ci = 0; ci < item_count; ++ci) {
            float iy = menu_y + 2 + ci * kSessionCtxMenuItemH;
            bool hovered = (mouse_.x >= menu_x && mouse_.x <= menu_x + menu_w &&
                            mouse_.y >= iy && mouse_.y <= iy + kSessionCtxMenuItemH);
            if (hovered) {
                tr.draw_rect(menu_x + 2, iy, menu_w - 4, kSessionCtxMenuItemH,
                             style_.accent[0], style_.accent[1], style_.accent[2], 0.2f);
            }
            tr.draw_text(menu_x + 10, iy + 3, ctx_labels[ci],
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            session_ctx_menu_rects_.push_back({menu_x, iy, menu_w, kSessionCtxMenuItemH, ci});
        }
    }
}


namespace {

// Word-wrap `text` into lines no wider than `max_w` pixels (tr's font).
// Breaks at spaces; an overlong single word gets its own clipped line.
std::vector<std::string> wrap_text(Renderer2D& tr, const std::string& text,
                                   float max_w) {
    std::vector<std::string> lines;
    std::string line;
    size_t i = 0;
    while (i < text.size()) {
        size_t nl = text.find('\n', i);
        std::string paragraph = text.substr(
            i, nl == std::string::npos ? text.size() - i : nl - i);
        i = nl == std::string::npos ? text.size() : nl + 1;

        std::string cur;
        size_t j = 0;
        while (j < paragraph.size()) {
            size_t sp = paragraph.find(' ', j);
            std::string word = paragraph.substr(
                j, sp == std::string::npos ? paragraph.size() - j : sp - j);
            j = sp == std::string::npos ? paragraph.size() : sp + 1;

            std::string candidate = cur.empty() ? word : cur + " " + word;
            if (tr.text_width(candidate.c_str()) <= max_w) {
                cur = std::move(candidate);
            } else {
                if (!cur.empty()) lines.push_back(std::move(cur));
                cur = word;
            }
        }
        if (!cur.empty()) lines.push_back(std::move(cur));
        if (nl != std::string::npos && cur.empty())
            lines.push_back(std::string());
    }
    return lines;
}

}  // namespace

// Semantic operator map: renders a scatter plot of all operators with
// precomputed 2D positions, plus crosshair axes and a right-hand preview
// column showing rich details for the hovered operator.
//
// Layout inside the tab body:
//   ┌──────────────────────────┬──────────────┐
//   │                          │  preview     │
//   │    scatter + axes        │  panel       │
//   │                          │              │
//   └──────────────────────────┴──────────────┘
void NodeGraphUI::draw_chooser_map(Renderer2D& tr, float px, float py,
                                   float pw, float ph) {
    constexpr float kInset = 8.0f;
    constexpr float kGap = 8.0f;
    constexpr float kAxisLabelH = 14.0f;

    float scatter_x = px + kInset;
    float scatter_y = py + kInset;
    float scatter_w = pw - 2 * kInset - kGap - kChooserMapPreviewW;
    float scatter_h = ph - 2 * kInset;

    float preview_x = scatter_x + scatter_w + kGap;
    float preview_y = scatter_y;
    float preview_w = kChooserMapPreviewW;
    float preview_h = scatter_h;

    // Scatter background (slightly recessed from the panel).
    tr.draw_rect(scatter_x, scatter_y, scatter_w, scatter_h,
                 style_.inspector_bg[0] * 0.78f,
                 style_.inspector_bg[1] * 0.78f,
                 style_.inspector_bg[2] * 0.78f, 1.0f);

    chooser_map_dots_.clear();

    if (chooser_map_layout_.empty()) {
        tr.draw_text(scatter_x + 8, scatter_y + 8,
                     T("map_unavailable", "semantic map unavailable"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        return;
    }

    // ---- Axes ------------------------------------------------------------
    // The generator rotates UMAP output so X ≈ (audio → control → gpu) and
    // (after a sign flip) Y ≈ (source-like → sink-like) based on port
    // counts. Draw faint crosshairs + end labels to make that readable.
    const float axis_alpha = 0.18f;
    tr.draw_rect(scatter_x, scatter_y + scatter_h * 0.5f - 0.5f, scatter_w, 1.0f,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2],
                 axis_alpha);
    tr.draw_rect(scatter_x + scatter_w * 0.5f - 0.5f, scatter_y, 1.0f, scatter_h,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2],
                 axis_alpha);

    auto draw_axis_label = [&](const char* text, float cx, float cy,
                               const float* col) {
        float tw = tr.text_width(text);
        tr.draw_text(cx - tw * 0.5f, cy, text, col[0], col[1], col[2]);
    };

    // X axis: audio on the left, gpu on the right; control sits in the middle.
    // This matches the alignment target used by the generator and gives the
    // map an interpretable left-right gradient. The Y axis is UMAP-native
    // (perpendicular to X) and doesn't carry a strong enough single-word
    // signal to justify a label — the crosshair alone is enough anchor.
    draw_axis_label("audio", scatter_x + 22,
                    scatter_y + scatter_h - kAxisLabelH - 2,
                    kAudioAccent.data());
    draw_axis_label("ctrl", scatter_x + scatter_w * 0.5f,
                    scatter_y + scatter_h - kAxisLabelH - 2,
                    kControlAccent.data());
    draw_axis_label("gpu", scatter_x + scatter_w - 22,
                    scatter_y + scatter_h - kAxisLabelH - 2,
                    kGpuAccent.data());

    // ---- Dots ------------------------------------------------------------
    // Use the same haystack-based matcher as the list view so "Chord Progression"
    // dim/highlight is consistent with the chooser-list filter.
    std::string query_norm = vivid::normalize_for_search(chooser_filter_);

    tr.push_clip_rect(scatter_x, scatter_y, scatter_w, scatter_h);

    const auto& active_types = snap_.operator_types;
    std::string hovered_name;
    float hover_best_d2 = 1e9f;

    for (const auto& type_name : active_types) {
        const auto* entry = chooser_map_layout_.find(type_name);
        if (!entry) continue;

        float dx = scatter_x + (entry->x * chooser_map_zoom_ + chooser_map_pan_x_) * scatter_w;
        float dy = scatter_y + (entry->y * chooser_map_zoom_ + chooser_map_pan_y_) * scatter_h;

        bool dim = false;
        if (!query_norm.empty()) {
            auto cat_it = snap_.operator_catalog.find(type_name);
            if (cat_it != snap_.operator_catalog.end() && cat_it->second) {
                dim = score_match_v2(cat_it->second->search, query_norm) < 0;
            } else {
                ui::OperatorInfo fallback;
                fallback.name = type_name;
                fallback.display_name = vivid::default_display_name(type_name);
                ui::build_search_haystack(fallback);
                dim = score_match_v2(fallback.search, query_norm) < 0;
            }
        }

        const float* col = kControlAccent.data();
        if (entry->kind == "gpu") col = kGpuAccent.data();
        else if (entry->kind == "audio") col = kAudioAccent.data();

        float alpha = dim ? 0.18f : 0.95f;
        float r = dim ? 2.5f : 4.0f;
        tr.draw_rect(dx - r, dy - r, r * 2, r * 2, col[0], col[1], col[2], alpha);

        chooser_map_dots_.push_back({type_name, dx, dy});

        float mdx = mouse_.x - dx;
        float mdy = mouse_.y - dy;
        float d2 = mdx * mdx + mdy * mdy;
        if (d2 < hover_best_d2 && d2 < 144.0f) {  // 12px hit radius
            hover_best_d2 = d2;
            hovered_name = type_name;
        }
    }

    // Emphasize the hovered dot with a ring.
    if (!hovered_name.empty()) {
        for (const auto& dot : chooser_map_dots_) {
            if (dot.name != hovered_name) continue;
            float rr = 8.0f;
            tr.draw_rect(dot.sx - rr, dot.sy - rr, rr * 2, 1.0f,
                         style_.bright_text[0], style_.bright_text[1],
                         style_.bright_text[2], 0.85f);
            tr.draw_rect(dot.sx - rr, dot.sy + rr - 1, rr * 2, 1.0f,
                         style_.bright_text[0], style_.bright_text[1],
                         style_.bright_text[2], 0.85f);
            tr.draw_rect(dot.sx - rr, dot.sy - rr, 1.0f, rr * 2,
                         style_.bright_text[0], style_.bright_text[1],
                         style_.bright_text[2], 0.85f);
            tr.draw_rect(dot.sx + rr - 1, dot.sy - rr, 1.0f, rr * 2,
                         style_.bright_text[0], style_.bright_text[1],
                         style_.bright_text[2], 0.85f);
            break;
        }
    }

    tr.pop_clip_rect();
    chooser_map_hover_name_ = hovered_name;

    // ---- Preview panel ---------------------------------------------------
    tr.draw_rect(preview_x, preview_y, preview_w, preview_h,
                 style_.inspector_bg[0] * 0.9f,
                 style_.inspector_bg[1] * 0.9f,
                 style_.inspector_bg[2] * 0.9f, 1.0f);

    tr.push_clip_rect(preview_x, preview_y, preview_w, preview_h);

    const OperatorLayoutEntry* entry = hovered_name.empty()
        ? nullptr : chooser_map_layout_.find(hovered_name);

    float inner_x = preview_x + 10;
    float cursor_y = preview_y + 10;
    float text_w = preview_w - 20;

    if (!entry) {
        tr.draw_text(inner_x, cursor_y,
                     T("map_hover_hint", "Hover a dot to preview"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        tr.pop_clip_rect();
        return;
    }

    // Name (large-ish — just use normal text; this font is small enough)
    {
        const char* preview_label = hovered_name.c_str();
        auto cat_it = snap_.operator_catalog.find(hovered_name);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second &&
            !cat_it->second->display_name.empty()) {
            preview_label = cat_it->second->display_name.c_str();
        }
        tr.draw_text(inner_x, cursor_y, preview_label,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    }
    cursor_y += 16;

    // Domain tag (colored chip)
    const float* kind_col = kControlAccent.data();
    const char* kind_label = "Control";
    if (entry->kind == "gpu") {
        kind_col = kGpuAccent.data();
        kind_label = "GPU";
    } else if (entry->kind == "audio") {
        kind_col = kAudioAccent.data();
        kind_label = "Audio";
    }
    tr.draw_text(inner_x, cursor_y, kind_label, kind_col[0], kind_col[1], kind_col[2]);
    if (!entry->lane_behavior.empty()) {
        std::string lb = " · " + entry->lane_behavior;
        float kw = tr.text_width(kind_label);
        tr.draw_text(inner_x + kw, cursor_y, lb.c_str(),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    }
    cursor_y += 18;

    // Port counts
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d in / %d out",
                  entry->num_inputs, entry->num_outputs);
    tr.draw_text(inner_x, cursor_y, buf,
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    cursor_y += 16;

    // Description: prefer the operator's authored kSummary (v3 metadata, kept
    // in lock-step with the code), falling back to the source-comment brief
    // captured in the layout file. The author-supplied string wins because it
    // can outpace the layout file, which only refreshes on a layout rebuild.
    {
        std::string preview_text;
        auto cat_it = snap_.operator_catalog.find(hovered_name);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second &&
            !cat_it->second->summary.empty()) {
            preview_text = cat_it->second->summary;
        } else {
            preview_text = entry->brief;
        }
        if (!preview_text.empty()) {
            cursor_y += 4;
            for (const auto& line : wrap_text(tr, preview_text, text_w)) {
                if (cursor_y + 12 > preview_y + preview_h) break;
                tr.draw_text(inner_x, cursor_y, line.c_str(),
                             style_.bright_text[0] * 0.9f,
                             style_.bright_text[1] * 0.9f,
                             style_.bright_text[2] * 0.9f);
                cursor_y += 14;
            }
        }
    }

    // Related (@see) operators
    if (!entry->related.empty() && cursor_y + 30 < preview_y + preview_h) {
        cursor_y += 6;
        tr.draw_text(inner_x, cursor_y,
                     T("map_related", "Related:"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        cursor_y += 14;
        std::string joined;
        for (size_t i = 0; i < entry->related.size(); ++i) {
            if (i > 0) joined += ", ";
            joined += entry->related[i];
        }
        for (const auto& line : wrap_text(tr, joined, text_w)) {
            if (cursor_y + 12 > preview_y + preview_h) break;
            tr.draw_text(inner_x, cursor_y, line.c_str(),
                         style_.bright_text[0] * 0.8f,
                         style_.bright_text[1] * 0.8f,
                         style_.bright_text[2] * 0.8f);
            cursor_y += 14;
        }
    }

    tr.pop_clip_rect();
}

} // namespace vivid::ui
