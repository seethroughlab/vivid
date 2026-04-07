#include "ui/build_console_panel.h"

#include "ui/rendering/renderer_2d.h"
#include "ui/style/i18n.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace vivid::ui {

namespace {

constexpr float kMinConsoleH = 120.0f;
constexpr float kMaxConsoleH = 360.0f;
constexpr float kHeaderPadX = 10.0f;
constexpr float kTextPadX = 8.0f;
constexpr float kColumnGap = 12.0f;
constexpr float kMinTaskLabelW = 64.0f;
constexpr float kMaxTaskLabelW = 180.0f;
constexpr float kMinMessageW = 120.0f;
constexpr float kMinGutterW = 112.0f;
constexpr const char* kEllipsis = "\xe2\x80\xa6";

std::string timestamp_label(uint64_t timestamp_ms) {
    std::time_t secs = static_cast<std::time_t>(timestamp_ms / 1000);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &secs);
#else
    localtime_r(&secs, &tm_buf);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return buf;
}

std::string truncate_text(Renderer2D& tr, const std::string& text, float max_w, float scale = 1.0f) {
    if (text.empty() || max_w <= 0.0f) return {};
    if (tr.text_width(text.c_str(), scale) <= max_w) return text;

    const float ell_w = tr.text_width(kEllipsis, scale);
    if (ell_w > max_w) return {};

    std::string out = text;
    while (!out.empty() && tr.text_width((out + kEllipsis).c_str(), scale) > max_w)
        out.pop_back();
    return out.empty() ? std::string(kEllipsis) : out + kEllipsis;
}

}  // namespace

void BuildConsolePanel::sync_from_model() {
    if (!console_) return;
    auto snapshot = console_->snapshot();
    if (snapshot.auto_reveal_generation != seen_auto_reveal_generation_) {
        seen_auto_reveal_generation_ = snapshot.auto_reveal_generation;
        open_ = true;
    }
    snapshot_ = std::move(snapshot);
}

BuildConsolePanel::Layout BuildConsolePanel::layout_for(uint32_t win_w, uint32_t win_h, float bottom_offset) const {
    Layout layout;
    layout.w = static_cast<float>(win_w);
    layout.h = std::clamp(height_, kMinConsoleH, kMaxConsoleH);
    layout.x = 0.0f;
    layout.y = static_cast<float>(win_h) - bottom_offset - layout.h;
    layout.content_x = 0.0f;
    layout.content_y = layout.y + layout.header_h;
    layout.content_w = layout.w;
    layout.content_h = layout.h - layout.header_h;

    const float row_left = layout.x + kTextPadX;
    const float row_right = layout.x + layout.w - kTextPadX;
    const float available_w = std::max(0.0f, row_right - row_left);

    layout.gutter_w = std::clamp(layout.gutter_w, kMinGutterW, 136.0f);
    float task_w = std::clamp(available_w * 0.18f, kMinTaskLabelW, kMaxTaskLabelW);
    float message_w = available_w - layout.gutter_w - task_w - kColumnGap * 2.0f;
    if (message_w < kMinMessageW) {
        const float deficit = kMinMessageW - message_w;
        const float shrink_task = std::min(std::max(0.0f, task_w - kMinTaskLabelW), deficit);
        task_w -= shrink_task;
        message_w += shrink_task;
    }
    if (message_w < kMinMessageW) {
        const float deficit = kMinMessageW - message_w;
        const float shrink_gutter = std::min(std::max(0.0f, layout.gutter_w - kMinGutterW), deficit);
        layout.gutter_w -= shrink_gutter;
        message_w += shrink_gutter;
    }
    message_w = std::max(0.0f, available_w - layout.gutter_w - task_w - kColumnGap * 2.0f);

    layout.gutter_x = row_left;
    layout.task_x = layout.gutter_x + layout.gutter_w + kColumnGap;
    layout.task_w = std::max(0.0f, task_w);
    layout.message_x = layout.task_x + layout.task_w + kColumnGap;
    layout.message_w = std::max(0.0f, row_right - layout.message_x);
    return layout;
}

bool BuildConsolePanel::contains(float mouse_x, float mouse_y, uint32_t win_w, uint32_t win_h, float bottom_offset) const {
    if (!open_) return false;
    auto layout = layout_for(win_w, win_h, bottom_offset);
    return mouse_x >= layout.x && mouse_x <= layout.x + layout.w &&
           mouse_y >= layout.y && mouse_y <= layout.y + layout.h;
}

void BuildConsolePanel::toggle_open() {
    set_open(!open_);
}

void BuildConsolePanel::set_open(bool open) {
    open_ = open;
    if (!open_) {
        focused_ = false;
        resizing_ = false;
        selecting_ = false;
    }
}

int BuildConsolePanel::hit_line_index(const Layout& layout, float mouse_y, float line_h) const {
    if (mouse_y < layout.content_y || mouse_y > layout.content_y + layout.content_h) return -1;
    const float local_y = mouse_y - layout.content_y + scroll_y_;
    if (line_h <= 0.0f) return -1;
    int index = static_cast<int>(std::floor(local_y / line_h));
    if (index < 0 || index >= static_cast<int>(snapshot_.lines.size())) return -1;
    return index;
}

void BuildConsolePanel::scroll_to_bottom(float line_h, const Layout& layout) {
    float total_h = line_h * static_cast<float>(snapshot_.lines.size());
    scroll_y_ = std::max(0.0f, total_h - layout.content_h);
    pinned_to_bottom_ = true;
}

void BuildConsolePanel::update_bottom_pin(float line_h, const Layout& layout) {
    float total_h = line_h * static_cast<float>(snapshot_.lines.size());
    float max_scroll = std::max(0.0f, total_h - layout.content_h);
    pinned_to_bottom_ = (max_scroll - scroll_y_) <= 2.0f;
    scroll_y_ = std::max(0.0f, std::min(scroll_y_, max_scroll));
}

const char* BuildConsolePanel::tag_for(vivid::BuildTaskKind kind) const {
    switch (kind) {
        case vivid::BuildTaskKind::HotReload: return "reload";
        case vivid::BuildTaskKind::PackageBuild: return "build";
        case vivid::BuildTaskKind::PackageConfigure: return "config";
        case vivid::BuildTaskKind::PackageInstall: return "install";
        case vivid::BuildTaskKind::PackageTestCompile: return "test-c";
        case vivid::BuildTaskKind::PackageTestRun: return "test-r";
        case vivid::BuildTaskKind::GitClone: return "git";
    }
    return "task";
}

std::string BuildConsolePanel::format_gutter(const vivid::BuildConsoleLine& line) const {
    std::ostringstream oss;
    oss << timestamp_label(line.timestamp_ms) << "  " << tag_for(line.task_kind);
    return oss.str();
}

std::string BuildConsolePanel::selected_text() const {
    if (selection_anchor_line_ < 0 || selection_current_line_ < 0 || snapshot_.lines.empty())
        return {};
    int start = std::min(selection_anchor_line_, selection_current_line_);
    int end = std::max(selection_anchor_line_, selection_current_line_);
    start = std::max(0, start);
    end = std::min(end, static_cast<int>(snapshot_.lines.size()) - 1);

    std::ostringstream oss;
    for (int i = start; i <= end; ++i) {
        if (i > start) oss << '\n';
        const auto& line = snapshot_.lines[static_cast<size_t>(i)];
        oss << format_gutter(line) << "  " << line.task_label;
        if (!line.text.empty())
            oss << "  " << line.text;
    }
    return oss.str();
}

bool BuildConsolePanel::handle_scroll(float mouse_x, float mouse_y, float x_offset, float y_offset,
                                      uint32_t win_w, uint32_t win_h, float bottom_offset) {
    (void)x_offset;
    if (!contains(mouse_x, mouse_y, win_w, win_h, bottom_offset)) return false;
    auto layout = layout_for(win_w, win_h, bottom_offset);
    float line_h = line_h_;
    scroll_y_ = std::max(0.0f, scroll_y_ - y_offset * line_h * 2.0f);
    update_bottom_pin(line_h, layout);
    focused_ = true;
    return true;
}

bool BuildConsolePanel::handle_left_press(float mouse_x, float mouse_y, uint32_t win_w, uint32_t win_h, float bottom_offset) {
    if (!contains(mouse_x, mouse_y, win_w, win_h, bottom_offset)) return false;
    focused_ = true;
    auto layout = layout_for(win_w, win_h, bottom_offset);
    if (mouse_y >= layout.y && mouse_y <= layout.y + layout.resize_h) {
        resizing_ = true;
        return true;
    }
    if (mouse_x >= close_btn_.x && mouse_x <= close_btn_.x + close_btn_.w &&
        mouse_y >= close_btn_.y && mouse_y <= close_btn_.y + close_btn_.h) {
        set_open(false);
        return true;
    }
    float line_h = line_h_;
    int line_idx = hit_line_index(layout, mouse_y, line_h);
    selecting_ = true;
    selection_dragged_ = false;
    selection_anchor_line_ = line_idx;
    selection_current_line_ = line_idx;
    return true;
}

bool BuildConsolePanel::handle_left_release(float, float, uint32_t, uint32_t, float) {
    if (!open_ || (!resizing_ && !selecting_)) return false;
    resizing_ = false;
    if (selecting_ && !selection_dragged_) {
        selection_anchor_line_ = -1;
        selection_current_line_ = -1;
    }
    selecting_ = false;
    return true;
}

bool BuildConsolePanel::update_drag(float mouse_x, float mouse_y, bool left_down,
                                    uint32_t win_w, uint32_t win_h, float bottom_offset) {
    if (!open_ || !left_down) return false;
    auto layout = layout_for(win_w, win_h, bottom_offset);
    if (resizing_) {
        float new_height = static_cast<float>(win_h) - bottom_offset - mouse_y;
        height_ = std::clamp(new_height, kMinConsoleH, std::min(kMaxConsoleH, static_cast<float>(win_h) * 0.6f));
        return true;
    }
    if (selecting_) {
        float line_h = line_h_;
        int line_idx = hit_line_index(layout, mouse_y, line_h);
        if (line_idx >= 0) {
            selection_current_line_ = line_idx;
            selection_dragged_ = true;
        }
        return true;
    }
    return false;
}

bool BuildConsolePanel::handle_key(GLFWwindow* window, int key, int action, int mods) {
    if (!wants_keyboard()) return false;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return false;
    bool mod = (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0;
    if (mod && key == GLFW_KEY_A) {
        if (last_visible_first_line_ >= 0 && last_visible_last_line_ >= last_visible_first_line_) {
            selection_anchor_line_ = last_visible_first_line_;
            selection_current_line_ = last_visible_last_line_;
        }
        return true;
    }
    if (mod && key == GLFW_KEY_C) {
        std::string text = selected_text();
        if (!text.empty())
            glfwSetClipboardString(window, text.c_str());
        return true;
    }
    if (key == GLFW_KEY_ESCAPE) {
        selection_anchor_line_ = -1;
        selection_current_line_ = -1;
        focused_ = false;
        return true;
    }
    return false;
}

void BuildConsolePanel::draw(Renderer2D& tr, const UIStyle& style, uint32_t win_w, uint32_t win_h, float bottom_offset,
                             float mouse_x, float mouse_y) {
    sync_from_model();
    if (!open_) return;

    auto layout = layout_for(win_w, win_h, bottom_offset);
    float line_h = tr.line_height();
    line_h_ = line_h;
    if (pinned_to_bottom_)
        scroll_to_bottom(line_h, layout);
    else
        update_bottom_pin(line_h, layout);

    tr.draw_rect(layout.x, layout.y, layout.w, layout.h,
                 style.dark_bg[0], style.dark_bg[1], style.dark_bg[2], 0.97f);
    tr.draw_rect(layout.x, layout.y, layout.w, 1.0f,
                 style.accent[0], style.accent[1], style.accent[2], 0.5f);
    tr.draw_rect(layout.x, layout.y + layout.resize_h - 1.0f, layout.w, 1.0f,
                 0.30f, 0.32f, 0.35f, 0.6f);

    std::string title_str = T("build_console", "Build Console");
    if (snapshot_.running_task_count > 0) title_str += "  LIVE";
    const char* title = title_str.c_str();
    tr.draw_text(layout.x + kHeaderPadX, layout.y + 4.0f, title,
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    const char* close_label = "\xe2\x9c\x95";
    float close_tw = tr.text_width(close_label);
    float close_pad = 4.0f;
    float close_x = layout.x + layout.w - close_tw - kHeaderPadX;
    float close_y = layout.y + layout.resize_h;
    float close_h = layout.header_h - layout.resize_h;
    close_btn_ = {close_x - close_pad, close_y, close_tw + close_pad * 2.0f, close_h};
    bool close_hov = mouse_x >= close_btn_.x && mouse_x <= close_btn_.x + close_btn_.w &&
                     mouse_y >= close_btn_.y && mouse_y <= close_btn_.y + close_btn_.h;
    tr.draw_text(close_x, layout.y + 4.0f, close_label,
                 close_hov ? style.bright_text[0] : style.dim_text[0],
                 close_hov ? style.bright_text[1] : style.dim_text[1],
                 close_hov ? style.bright_text[2] : style.dim_text[2]);

    std::string count = std::to_string(snapshot_.lines.size()) + " lines";
    float count_w = tr.text_width(count.c_str());
    tr.draw_text(close_x - count_w - kHeaderPadX, layout.y + 4.0f, count.c_str(),
                 style.dim_text[0], style.dim_text[1], style.dim_text[2]);

    tr.push_clip_rect(layout.content_x, layout.content_y, layout.content_w, layout.content_h);
    int sel_start = -1;
    int sel_end = -1;
    last_visible_first_line_ = -1;
    last_visible_last_line_ = -1;
    if (selection_anchor_line_ >= 0 && selection_current_line_ >= 0) {
        sel_start = std::min(selection_anchor_line_, selection_current_line_);
        sel_end = std::max(selection_anchor_line_, selection_current_line_);
    }

    float draw_y = layout.content_y - scroll_y_;
    for (size_t i = 0; i < snapshot_.lines.size(); ++i) {
        if (draw_y + line_h < layout.content_y) {
            draw_y += line_h;
            continue;
        }
        if (draw_y > layout.content_y + layout.content_h) break;
        if (last_visible_first_line_ < 0)
            last_visible_first_line_ = static_cast<int>(i);
        last_visible_last_line_ = static_cast<int>(i);

        const auto& line = snapshot_.lines[i];
        bool selected = sel_start >= 0 && static_cast<int>(i) >= sel_start && static_cast<int>(i) <= sel_end;
        if (selected) {
            tr.draw_rect(layout.x, draw_y, layout.w, line_h,
                         style.node_sel_bg[0], style.node_sel_bg[1], style.node_sel_bg[2], 0.45f);
        } else if (line.entry_kind != vivid::BuildConsoleEntryKind::Line) {
            tr.draw_rect(layout.x, draw_y, layout.w, line_h,
                         0.12f, 0.13f, 0.16f, 0.50f);
        }

        std::string gutter = format_gutter(line);
        tr.draw_text(layout.gutter_x, draw_y,
                     gutter.c_str(), style.dim_text[0], style.dim_text[1], style.dim_text[2]);

        std::string task_text = truncate_text(tr, line.task_label, layout.task_w, 0.9f);
        if (!task_text.empty()) {
            tr.push_clip_rect(layout.task_x, draw_y, layout.task_w, line_h);
            tr.draw_text(layout.task_x, draw_y, task_text.c_str(),
                         style.bright_text[0], style.bright_text[1], style.bright_text[2], 0.85f, 0.9f);
            tr.pop_clip_rect();
        }

        std::string message_text = truncate_text(tr, line.text, layout.message_w);
        float text_x = layout.message_x;
        float r = style.bright_text[0];
        float g = style.bright_text[1];
        float b = style.bright_text[2];
        if (line.stream_kind == vivid::BuildConsoleStreamKind::System) {
            r = style.accent[0]; g = style.accent[1]; b = style.accent[2];
        } else if (line.stream_kind == vivid::BuildConsoleStreamKind::Stderr) {
            r = 0.95f; g = 0.55f; b = 0.55f;
        }
        if (!message_text.empty()) {
            tr.push_clip_rect(layout.message_x, draw_y, layout.message_w, line_h);
            tr.draw_text(text_x, draw_y, message_text.c_str(), r, g, b);
            tr.pop_clip_rect();
        }
        draw_y += line_h;
    }
    tr.pop_clip_rect();
}

}  // namespace vivid::ui
