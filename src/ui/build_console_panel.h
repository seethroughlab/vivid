#pragma once

#include "runtime/build_console.h"
#include "ui/ui_style.h"
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace vivid::ui {

class Renderer2D;

class BuildConsolePanel {
public:
    void set_console(std::shared_ptr<vivid::BuildConsole> console) { console_ = std::move(console); }

    void sync_from_model();
    void draw(Renderer2D& tr, const UIStyle& style, uint32_t win_w, uint32_t win_h, float bottom_offset);

    bool handle_scroll(float mouse_x, float mouse_y, float x_offset, float y_offset,
                       uint32_t win_w, uint32_t win_h, float bottom_offset);
    bool handle_left_press(float mouse_x, float mouse_y, uint32_t win_w, uint32_t win_h, float bottom_offset);
    bool handle_left_release(float mouse_x, float mouse_y, uint32_t win_w, uint32_t win_h, float bottom_offset);
    bool update_drag(float mouse_x, float mouse_y, bool left_down, uint32_t win_w, uint32_t win_h, float bottom_offset);
    bool handle_key(GLFWwindow* window, int key, int action, int mods);

    void toggle_open();
    void set_open(bool open);
    void blur() { focused_ = false; selecting_ = false; resizing_ = false; }
    bool is_open() const { return open_; }
    bool wants_keyboard() const { return open_ && focused_; }
    float panel_height() const { return open_ ? height_ : 0.0f; }
    bool contains(float mouse_x, float mouse_y, uint32_t win_w, uint32_t win_h, float bottom_offset) const;
    bool has_content() const { return !snapshot_.lines.empty(); }

private:
    struct Layout {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float header_h = 24.0f;
        float resize_h = 6.0f;
        float gutter_w = 136.0f;
        float content_x = 0.0f;
        float content_y = 0.0f;
        float content_w = 0.0f;
        float content_h = 0.0f;
    };

    Layout layout_for(uint32_t win_w, uint32_t win_h, float bottom_offset) const;
    int hit_line_index(const Layout& layout, float mouse_y, float line_h) const;
    void scroll_to_bottom(float line_h, const Layout& layout);
    void update_bottom_pin(float line_h, const Layout& layout);
    std::string selected_text() const;
    std::string format_gutter(const vivid::BuildConsoleLine& line) const;
    const char* tag_for(vivid::BuildTaskKind kind) const;

    std::shared_ptr<vivid::BuildConsole> console_;
    vivid::BuildConsoleSnapshot snapshot_;
    bool open_ = false;
    bool focused_ = false;
    bool pinned_to_bottom_ = true;
    bool resizing_ = false;
    bool selecting_ = false;
    bool selection_dragged_ = false;
    float height_ = 180.0f;
    float scroll_y_ = 0.0f;
    float scroll_x_ = 0.0f;
    int selection_anchor_line_ = -1;
    int selection_current_line_ = -1;
    uint64_t seen_auto_reveal_generation_ = 0;
    int last_visible_first_line_ = -1;
    int last_visible_last_line_ = -1;
};

}  // namespace vivid::ui
