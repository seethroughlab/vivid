#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/file_dialog.h"
#include "common/string_util.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cctype>

namespace vivid::ui {

using vivid::format_float;
using vivid::format_int;
using vivid::format_uint;

// -----------------------------------------------------------------------
// GLFW callbacks
// -----------------------------------------------------------------------
void NodeGraphUI::on_mouse_move(float x, float y) {
    mouse_.prev_x = mouse_.x;
    mouse_.prev_y = mouse_.y;
    mouse_.x = x;
    mouse_.y = y;
}

void NodeGraphUI::on_mouse_button(int button, int action, int mods) {
    mouse_.shift_down = (mods & GLFW_MOD_SHIFT) != 0;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            mouse_.left_down = true;
            mouse_.left_clicked = true;
        } else if (action == GLFW_RELEASE) {
            mouse_.left_down = false;
            mouse_.left_released = true;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            mouse_.right_clicked = true;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            panning_ = true;
            pan_start_mx_ = mouse_.x;
            pan_start_my_ = mouse_.y;
            pan_start_px_ = pan_x_;
            pan_start_py_ = pan_y_;
        } else if (action == GLFW_RELEASE) {
            panning_ = false;
        }
    }
}

void NodeGraphUI::on_scroll(float x_offset, float y_offset, int mods) {
    // Patchbay scroll
    if (patchbay_open_ &&
        mouse_.x >= patchbay_panel_x_ && mouse_.x <= patchbay_panel_x_ + patchbay_panel_w_ &&
        mouse_.y >= patchbay_panel_y_ && mouse_.y <= patchbay_panel_y_ + patchbay_panel_h_) {
        patchbay_scroll_y_ -= y_offset * kPatchbayScrollSpeed;
        if (mods & GLFW_MOD_SHIFT)
            patchbay_scroll_x_ -= y_offset * kPatchbayScrollSpeed;
        else
            patchbay_scroll_x_ -= x_offset * kPatchbayScrollSpeed;
        // Clamping happens in draw_patchbay()
        return;
    }

    // Param picker scroll
    if (param_picker_open_ && !param_picker_items_.empty()) {
        param_picker_scroll_ -= static_cast<int>(y_offset);
        int max_scroll = std::max(0, static_cast<int>(param_picker_items_.size()) - 12);
        param_picker_scroll_ = std::max(0, std::min(param_picker_scroll_, max_scroll));
        return;
    }

    // Chooser scroll when cursor is over the chooser panel
    if (chooser_open_) {
        float px = chooser_x();
        float panel_top = kChooserY;
        int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
        float panel_h = kChooserHeaderH + visible * kChooserItemH + 4;
        if (mouse_.x >= px && mouse_.x <= px + kChooserW &&
            mouse_.y >= panel_top && mouse_.y <= panel_top + panel_h) {
            chooser_scroll_ -= static_cast<int>(y_offset);
            int max_scroll = std::max(0, static_cast<int>(chooser_items_.size()) - kChooserMaxVisible);
            chooser_scroll_ = std::max(0, std::min(chooser_scroll_, max_scroll));
            return;
        }
    }

    // Inspector scroll when cursor is in inspector area
    if (mouse_.x >= graph_right() && has_selection()) {
        insp_scroll_y_ -= y_offset * kInspScrollSpeed;
        float viewport_h = static_cast<float>(win_h_) - kPerfBarH;
        float max_scroll = std::max(0.0f, insp_content_h_ - viewport_h);
        insp_scroll_y_ = std::max(0.0f, std::min(insp_scroll_y_, max_scroll));
        return;
    }

    if (mods & GLFW_MOD_SUPER) {
        // Cmd+scroll → pan
        constexpr float kPanSpeed = 3.0f;
        float speed = kPanSpeed / zoom_;
        pan_x_ += x_offset * speed;
        pan_y_ += y_offset * speed;
    } else {
        // Scroll → zoom (pivot around cursor)
        float factor = std::pow(1.12f, y_offset);
        float new_zoom = zoom_ * factor;
        new_zoom = std::max(0.4f, std::min(2.5f, new_zoom));

        float gx = sx_to_gx(mouse_.x);
        float gy = sy_to_gy(mouse_.y);
        zoom_ = new_zoom;
        pan_x_ = mouse_.x - gx * zoom_;
        pan_y_ = mouse_.y - gy * zoom_;
    }
}

// -----------------------------------------------------------------------
// Keyboard input
// -----------------------------------------------------------------------
void NodeGraphUI::on_key(int key, int action, int mods) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (prefs_open_) {
        if (key == GLFW_KEY_ESCAPE) {
            // Cancel: revert style
            if (prefs_saved_style_sel_ >= 0 &&
                prefs_saved_style_sel_ < static_cast<int>(prefs_styles_.size())) {
                style_ = prefs_styles_[prefs_saved_style_sel_];
                prefs_style_sel_ = prefs_saved_style_sel_;
            }
            prefs_open_ = false;
            prefs_editing_custom_ = false;
        } else if (prefs_editing_custom_) {
            if (key == GLFW_KEY_BACKSPACE && !prefs_custom_command_.empty())
                prefs_custom_command_.pop_back();
        }
        return;
    }

    if (editing_midi_range_) {
        if (key == GLFW_KEY_ENTER)       confirm_midi_range_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_midi_range_edit();
        else if (key == GLFW_KEY_TAB) {
            bool was_min = midi_range_editing_min_;
            std::string node_id = midi_range_node_id_;
            std::string param_name = midi_range_param_name_;
            confirm_midi_range_edit();
            if (was_min) {
                editing_midi_range_ = true;
                midi_range_node_id_ = node_id;
                midi_range_param_name_ = param_name;
                midi_range_editing_min_ = false;
                const auto* mm = snap_.find_midi_mapping(node_id, param_name);
                if (mm) edit_buffer_ = format_float(mm->range_max, 2);
            }
        }
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
        return;
    }

    if (editing_param_) {
        if (key == GLFW_KEY_ENTER)       confirm_param_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_param_edit();
        else if (key == GLFW_KEY_TAB) {
            std::string node_id = edit_node_id_;
            std::string param_name = edit_param_name_;
            confirm_param_edit();
            // Check for XY pad sibling
            const auto* ns = snap_.find_node(node_id);
            if (ns && ns->op_info) {
                const ParamInfo* pd = ns->find_param(param_name);
                if (pd && pd->display_hint == VIVID_DISPLAY_XY_PAD) {
                    auto pi_it = ns->param_indices.find(param_name);
                    if (pi_it != ns->param_indices.end()) {
                        uint32_t pi = pi_it->second;
                        // X is first of pair → advance to Y (pi+1)
                        // Y is second of pair → close
                        if (pi + 1 < ns->op_info->params.size() &&
                            ns->op_info->params[pi + 1].display_hint == VIVID_DISPLAY_XY_PAD) {
                            const auto& pd_y = ns->op_info->params[pi + 1];
                            editing_param_ = true;
                            edit_node_id_ = node_id;
                            edit_param_name_ = pd_y.name;
                            if (pd_y.type == VIVID_PARAM_INT)
                                edit_buffer_ = format_int(static_cast<int>(ns->param_values[pi + 1]));
                            else
                                edit_buffer_ = format_float(ns->param_values[pi + 1], 2);
                        }
                    }
                }
            }
        }
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
        return;
    }

    if (editing_resolution_) {
        if (key == GLFW_KEY_ENTER)       confirm_resolution_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_resolution_edit();
        else if (key == GLFW_KEY_TAB) {
            bool was_width = edit_res_is_width_;
            std::string node_id = edit_res_node_id_;
            confirm_resolution_edit();
            if (was_width) {
                editing_resolution_ = true;
                edit_res_node_id_ = node_id;
                edit_res_is_width_ = false;
                const auto* ns = snap_.find_node(node_id);
                if (ns) edit_buffer_ = format_uint(ns->gpu_tex_height);
            }
        }
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
        return;
    }

    if (color_editing_hex_) {
        bool mod_key = (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) != 0;
        if (key == GLFW_KEY_ENTER) {
            // Parse hex string -> RGB floats -> set params + update cached HSV
            std::string hex = color_hex_buffer_;
            if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
            if (hex.size() == 6) {
                unsigned int val = 0;
                bool valid = true;
                for (char c : hex) {
                    val <<= 4;
                    if (c >= '0' && c <= '9') val |= (c - '0');
                    else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
                    else { valid = false; break; }
                }
                if (valid) {
                    float r = ((val >> 16) & 0xFF) / 255.0f;
                    float g = ((val >> 8) & 0xFF) / 255.0f;
                    float b = (val & 0xFF) / 255.0f;
                    commands_.set_param(color_popup_node_id_, color_popup_param_r_, r);
                    commands_.set_param(color_popup_node_id_, color_popup_param_g_, g);
                    commands_.set_param(color_popup_node_id_, color_popup_param_b_, b);
                    rgb_to_hsv(r, g, b, color_popup_h_, color_popup_s_, color_popup_v_);
                }
            }
            color_editing_hex_ = false;
        } else if (key == GLFW_KEY_ESCAPE) {
            color_editing_hex_ = false;
        } else if (key == GLFW_KEY_BACKSPACE && !color_hex_buffer_.empty()) {
            color_hex_buffer_.pop_back();
        } else if (key == GLFW_KEY_C && mod_key) {
            // Copy current hex to clipboard
            glfwSetClipboardString(nullptr, color_hex_buffer_.c_str());
        } else if (key == GLFW_KEY_V && mod_key) {
            // Paste from clipboard
            const char* clip = glfwGetClipboardString(nullptr);
            if (clip) {
                std::string pasted = clip;
                // Validate and accept hex content
                color_hex_buffer_.clear();
                for (char c : pasted) {
                    if (c == '#' || std::isxdigit(static_cast<unsigned char>(c))) {
                        if (color_hex_buffer_.size() < 7)
                            color_hex_buffer_ += c;
                    }
                }
            }
        }
        return;
    }

    if (color_editing_rgb_ >= 0) {
        if (key == GLFW_KEY_ENTER) {
            // Parse buffer -> clamp 0-255 -> set param for active channel
            int val = color_rgb_buffer_.empty() ? 0 : std::atoi(color_rgb_buffer_.c_str());
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            float fval = val / 255.0f;
            const std::string* param_names[3] = {
                &color_popup_param_r_, &color_popup_param_g_, &color_popup_param_b_
            };
            commands_.set_param(color_popup_node_id_, *param_names[color_editing_rgb_], fval);
            // Re-read all three channels and update HSV
            const auto* ns = snap_.find_node(color_popup_node_id_);
            if (ns) {
                auto ri = ns->param_indices.find(color_popup_param_r_);
                auto gi = ns->param_indices.find(color_popup_param_g_);
                auto bi = ns->param_indices.find(color_popup_param_b_);
                if (ri != ns->param_indices.end() && gi != ns->param_indices.end() &&
                    bi != ns->param_indices.end()) {
                    float r = (color_editing_rgb_ == 0) ? fval : ns->param_values[ri->second];
                    float g = (color_editing_rgb_ == 1) ? fval : ns->param_values[gi->second];
                    float b = (color_editing_rgb_ == 2) ? fval : ns->param_values[bi->second];
                    rgb_to_hsv(r, g, b, color_popup_h_, color_popup_s_, color_popup_v_);
                }
            }
            color_editing_rgb_ = -1;
        } else if (key == GLFW_KEY_ESCAPE) {
            color_editing_rgb_ = -1;
        } else if (key == GLFW_KEY_BACKSPACE && !color_rgb_buffer_.empty()) {
            color_rgb_buffer_.pop_back();
        } else if (key == GLFW_KEY_TAB) {
            // Confirm current channel, advance to next
            int val = color_rgb_buffer_.empty() ? 0 : std::atoi(color_rgb_buffer_.c_str());
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            float fval = val / 255.0f;
            const std::string* param_names[3] = {
                &color_popup_param_r_, &color_popup_param_g_, &color_popup_param_b_
            };
            commands_.set_param(color_popup_node_id_, *param_names[color_editing_rgb_], fval);
            int next = color_editing_rgb_ + 1;
            if (next > 2) {
                // Update HSV and close
                const auto* ns = snap_.find_node(color_popup_node_id_);
                if (ns) {
                    auto ri = ns->param_indices.find(color_popup_param_r_);
                    auto gi = ns->param_indices.find(color_popup_param_g_);
                    auto bi = ns->param_indices.find(color_popup_param_b_);
                    if (ri != ns->param_indices.end() && gi != ns->param_indices.end() &&
                        bi != ns->param_indices.end()) {
                        float r = (color_editing_rgb_ == 0) ? fval : ns->param_values[ri->second];
                        float g = (color_editing_rgb_ == 1) ? fval : ns->param_values[gi->second];
                        float b = (color_editing_rgb_ == 2) ? fval : ns->param_values[bi->second];
                        rgb_to_hsv(r, g, b, color_popup_h_, color_popup_s_, color_popup_v_);
                    }
                }
                color_editing_rgb_ = -1;
            } else {
                // Update HSV for current channel, advance to next
                const auto* ns = snap_.find_node(color_popup_node_id_);
                if (ns) {
                    auto ri = ns->param_indices.find(color_popup_param_r_);
                    auto gi = ns->param_indices.find(color_popup_param_g_);
                    auto bi = ns->param_indices.find(color_popup_param_b_);
                    if (ri != ns->param_indices.end() && gi != ns->param_indices.end() &&
                        bi != ns->param_indices.end()) {
                        float r = (color_editing_rgb_ == 0) ? fval : ns->param_values[ri->second];
                        float g = (color_editing_rgb_ == 1) ? fval : ns->param_values[gi->second];
                        float b = (color_editing_rgb_ == 2) ? fval : ns->param_values[bi->second];
                        rgb_to_hsv(r, g, b, color_popup_h_, color_popup_s_, color_popup_v_);
                    }
                }
                color_editing_rgb_ = next;
                // Pre-fill buffer with current channel value
                const auto* ns2 = snap_.find_node(color_popup_node_id_);
                if (ns2) {
                    auto it = ns2->param_indices.find(*param_names[next]);
                    if (it != ns2->param_indices.end()) {
                        int v = static_cast<int>(ns2->param_values[it->second] * 255.0f + 0.5f);
                        color_rgb_buffer_ = std::to_string(v);
                    }
                }
            }
        }
        return;
    }

    if (patchbay_open_) {
        if (key == GLFW_KEY_ESCAPE) patchbay_open_ = false;
        return;  // swallow all other keys
    }

    if (create_popup_open_) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                create_popup_open_ = false;
                break;
            case GLFW_KEY_LEFT:
                if (create_domain_sel_ > 0) create_domain_sel_--;
                break;
            case GLFW_KEY_RIGHT:
                if (create_domain_sel_ < 2) create_domain_sel_++;
                break;
            case GLFW_KEY_BACKSPACE:
                if (!create_name_buf_.empty()) {
                    create_name_buf_.pop_back();
                    create_error_ = create_name_buf_.empty() ? "" :
                        commands_.validate_operator_name(create_name_buf_);
                }
                break;
            case GLFW_KEY_ENTER:
                if (!create_name_buf_.empty() && create_error_.empty()) {
                    if (commands_.create_operator(create_name_buf_, create_domain_sel_)) {
                        create_popup_open_ = false;
                    } else {
                        create_error_ = "creation failed";
                    }
                }
                break;
        }
        return;
    }

    if (clone_confirm_open_) {
        if (key == GLFW_KEY_ESCAPE) clone_confirm_open_ = false;
        return;
    }

    if (param_picker_open_) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                param_picker_open_ = false;
                break;
            case GLFW_KEY_UP:
                if (param_picker_sel_ > 0) {
                    param_picker_sel_--;
                    if (param_picker_sel_ < param_picker_scroll_)
                        param_picker_scroll_ = param_picker_sel_;
                }
                break;
            case GLFW_KEY_DOWN:
                if (param_picker_sel_ < static_cast<int>(param_picker_items_.size()) - 1) {
                    param_picker_sel_++;
                    if (param_picker_sel_ >= param_picker_scroll_ + 12)
                        param_picker_scroll_ = param_picker_sel_ - 12 + 1;
                }
                break;
            case GLFW_KEY_ENTER:
                if (!param_picker_items_.empty() && param_picker_sel_ >= 0 &&
                    param_picker_sel_ < static_cast<int>(param_picker_items_.size())) {
                    const std::string& selected = param_picker_items_[param_picker_sel_];
                    if (param_picker_is_output_) {
                        dragging_wire_ = true;
                        wire_from_node_id_ = param_picker_node_id_;
                        wire_from_port_ = selected;
                        wire_from_is_output_ = true;
                        for (const auto& r : node_rects_) {
                            if (r.node_id == param_picker_node_id_) {
                                wire_from_gx_ = r.x + r.w;
                                wire_from_gy_ = r.y + r.h * 0.5f;
                                for (const auto& p : r.outputs) {
                                    if (p.name == selected) {
                                        wire_from_gx_ = p.x;
                                        wire_from_gy_ = p.y;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    } else {
                        commands_.connect(param_picker_wire_from_node_ + "/" + param_picker_wire_from_port_,
                                     param_picker_node_id_ + "/" + selected);
                    }
                }
                param_picker_open_ = false;
                break;
        }
        return;
    }

    if (context_menu_open_) {
        if (key == GLFW_KEY_ESCAPE) context_menu_open_ = false;
        return;
    }

    if (dropdown_open_) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                dropdown_open_ = false;
                break;
            case GLFW_KEY_UP:
                if (dropdown_sel_ > 0) dropdown_sel_--;
                break;
            case GLFW_KEY_DOWN:
                if (dropdown_sel_ < static_cast<int>(dropdown_labels_.size()) - 1)
                    dropdown_sel_++;
                break;
            case GLFW_KEY_ENTER:
                commands_.set_param(dropdown_node_id_, dropdown_param_name_,
                               static_cast<float>(dropdown_sel_));
                dropdown_open_ = false;
                break;
        }
        return;
    }

    if (!chooser_open_) {
        // Tab opens the chooser (only if cursor is in graph area)
        if (key == GLFW_KEY_TAB && action == GLFW_PRESS &&
            snap_valid_ && !snap_.operator_types.empty()) {
            if (mouse_.x < graph_right()) {
                chooser_cursor_gx_ = sx_to_gx(mouse_.x);
                chooser_cursor_gy_ = sy_to_gy(mouse_.y);
                chooser_filter_.clear();
                chooser_sel_ = 0;
                chooser_scroll_ = 0;
                chooser_items_ = snap_.operator_types;
                chooser_open_ = true;
            }
        }
        // B toggles bezier wire rendering
        if (key == GLFW_KEY_B && action == GLFW_PRESS) {
            bezier_wires_ = !bezier_wires_;
        }
        // M: open patchbay if 3+ nodes selected, otherwise toggle MIDI map mode
        if (key == GLFW_KEY_M && action == GLFW_PRESS) {
            if (selected_node_ids_.size() >= 3 && !patchbay_open_) {
                open_patchbay();
            } else {
                midi_map_mode_ = !midi_map_mode_;
                if (!midi_map_mode_) {
                    midi_map_waiting_ = false;
                    editing_midi_range_ = false;
                }
            }
        }
        // Delete selected nodes (Delete or Backspace)
        if ((key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) && action == GLFW_PRESS) {
            if (!selected_node_ids_.empty()) {
                auto ids_copy = selected_node_ids_;
                for (const auto& id : ids_copy)
                    commands_.remove_node(id);
                selected_node_ids_.clear();
            }
        }
        return;
    }

    // Chooser is open
    switch (key) {
        case GLFW_KEY_ESCAPE:
            chooser_open_ = false;
            chooser_insert_wire_ = false;
            break;

        case GLFW_KEY_ENTER: {
            if (!chooser_items_.empty() && chooser_sel_ >= 0 &&
                chooser_sel_ < static_cast<int>(chooser_items_.size())) {
                confirm_chooser_selection(chooser_items_[chooser_sel_]);
            } else {
                chooser_insert_wire_ = false;
                chooser_open_ = false;
            }
            break;
        }

        case GLFW_KEY_UP:
            if (chooser_sel_ > 0) {
                chooser_sel_--;
                if (chooser_sel_ < chooser_scroll_)
                    chooser_scroll_ = chooser_sel_;
            }
            break;

        case GLFW_KEY_DOWN:
            if (chooser_sel_ < static_cast<int>(chooser_items_.size()) - 1) {
                chooser_sel_++;
                if (chooser_sel_ >= chooser_scroll_ + kChooserMaxVisible)
                    chooser_scroll_ = chooser_sel_ - kChooserMaxVisible + 1;
            }
            break;

        case GLFW_KEY_BACKSPACE:
            if (!chooser_filter_.empty()) {
                chooser_filter_.pop_back();
                rebuild_chooser_items();
            }
            break;

        default:
            break;
    }
}

void NodeGraphUI::on_char(unsigned int codepoint) {
    if (prefs_open_ && prefs_editing_custom_) {
        if (codepoint >= 32 && codepoint < 127)
            prefs_custom_command_ += static_cast<char>(codepoint);
        return;
    }
    if (editing_midi_range_) {
        char ch = static_cast<char>(codepoint);
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-')
            edit_buffer_ += ch;
        return;
    }
    if (editing_param_) {
        char ch = static_cast<char>(codepoint);
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-')
            edit_buffer_ += ch;
        return;
    }
    if (editing_resolution_) {
        char ch = static_cast<char>(codepoint);
        if (std::isdigit(static_cast<unsigned char>(ch)))
            edit_buffer_ += ch;
        return;
    }
    if (color_editing_hex_) {
        char ch = static_cast<char>(codepoint);
        if (std::isxdigit(static_cast<unsigned char>(ch)) || ch == '#') {
            if (color_hex_buffer_.size() < 7)
                color_hex_buffer_ += ch;
        }
        return;
    }
    if (color_editing_rgb_ >= 0) {
        char ch = static_cast<char>(codepoint);
        if (std::isdigit(static_cast<unsigned char>(ch)) && color_rgb_buffer_.size() < 3)
            color_rgb_buffer_ += ch;
        return;
    }
    if (create_popup_open_) {
        char ch = static_cast<char>(codepoint);
        // Accept lowercase, digits, underscores; auto-lowercase uppercase
        if (std::isupper(static_cast<unsigned char>(ch)))
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (std::islower(static_cast<unsigned char>(ch)) ||
            std::isdigit(static_cast<unsigned char>(ch)) || ch == '_') {
            create_name_buf_ += ch;
            create_error_ = commands_.validate_operator_name(create_name_buf_);
        }
        return;
    }
    if (!chooser_open_) return;
    if (codepoint >= 32 && codepoint < 127) {
        chooser_filter_ += static_cast<char>(codepoint);
        rebuild_chooser_items();
    }
}

// -----------------------------------------------------------------------
// Clone confirmation dialog interaction (called from update())
// -----------------------------------------------------------------------
void NodeGraphUI::update_clone_confirm() {
    if (!clone_confirm_open_ || !mouse_.left_clicked) return;

    // Dialog geometry (centered on screen)
    float dw = 280.0f, dh = 70.0f;
    float dx = (static_cast<float>(win_w_) - dw) * 0.5f;
    float dy = (static_cast<float>(win_h_) - dh) * 0.5f;

    float btn_w = 70.0f, btn_h = 22.0f;
    float btn_y = dy + dh - btn_h - 8.0f;
    float clone_x = dx + dw * 0.5f - btn_w - 6.0f;
    float cancel_x = dx + dw * 0.5f + 6.0f;

    if (mouse_.x >= clone_x && mouse_.x <= clone_x + btn_w &&
        mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h) {
        // Clone button clicked
        commands_.clone_and_edit(clone_confirm_type_);
        clone_confirm_open_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
    } else if (mouse_.x >= cancel_x && mouse_.x <= cancel_x + btn_w &&
               mouse_.y >= btn_y && mouse_.y <= btn_y + btn_h) {
        // Cancel button clicked
        clone_confirm_open_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
    } else if (mouse_.x < dx || mouse_.x > dx + dw ||
               mouse_.y < dy || mouse_.y > dy + dh) {
        // Clicked outside dialog
        clone_confirm_open_ = false;
    }
    // Click inside dialog but not on buttons — consume but do nothing
    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

// -----------------------------------------------------------------------
// Create operator popup interaction (called from update())
// -----------------------------------------------------------------------
void NodeGraphUI::update_create_popup() {
    if (!create_popup_open_ || !mouse_.left_clicked) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);
    float pw = kCreatePopupW, ph = kCreatePopupH;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    // Click outside popup → close
    if (mouse_.x < px || mouse_.x > px + pw ||
        mouse_.y < py || mouse_.y > py + ph) {
        create_popup_open_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    // Check domain button clicks
    float btn_gap = 8.0f;
    float total_btn_w = 3 * kCreateDomainBtnW + 2 * btn_gap;
    float bx = px + (pw - total_btn_w) * 0.5f;
    float btn_cy = py + 36.0f;  // matches draw: 12 + 24

    for (int i = 0; i < 3; ++i) {
        float btn_x = bx + i * (kCreateDomainBtnW + btn_gap);
        if (mouse_.x >= btn_x && mouse_.x <= btn_x + kCreateDomainBtnW &&
            mouse_.y >= btn_cy && mouse_.y <= btn_cy + kCreateDomainBtnH) {
            create_domain_sel_ = i;
            break;
        }
    }

    // Consume click inside popup
    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

// -----------------------------------------------------------------------
// Context menu interaction (called from update())
// -----------------------------------------------------------------------
void NodeGraphUI::update_context_menu() {
    size_t cur_nodes = snap_.nodes.size();
    size_t cur_conns = snap_.connections.size();

    // Close context menu on topology change
    if (context_menu_open_ &&
        (cur_nodes != last_node_count_ || cur_conns != last_conn_count_)) {
        context_menu_open_ = false;
    }

    // Context menu click
    if (context_menu_open_ && mouse_.left_clicked) {
        int item_count = 1;  // "Delete Node" or "Delete Wire"
        if (!context_node_id_.empty() && context_node_has_shader_)
            item_count = 2;  // + "Clone & Edit"
        if (context_wire_idx_ >= 0)
            item_count = 2;  // + "Insert Node"

        float menu_h = kCtxMenuPadTop + item_count * kCtxMenuItemH + 2.0f;
        if (mouse_.x >= context_menu_x_ && mouse_.x <= context_menu_x_ + kCtxMenuW &&
            mouse_.y >= context_menu_y_ && mouse_.y <= context_menu_y_ + menu_h) {
            // Which item was clicked?
            float rel_y = mouse_.y - context_menu_y_ - kCtxMenuPadTop;
            int clicked_item = static_cast<int>(rel_y / kCtxMenuItemH);
            if (clicked_item < 0) clicked_item = 0;
            if (clicked_item >= item_count) clicked_item = item_count - 1;

            if (context_bg_menu_) {
                if (clicked_item == 0) {
                    // "Re-layout All" — force ignores saved positions
                    layout_nodes(/*force=*/true);
                }
            } else if (!context_node_id_.empty()) {
                if (clicked_item == 0) {
                    // "Delete Node(s)"
                    if (selected_node_ids_.count(context_node_id_) && selected_node_ids_.size() > 1) {
                        auto ids_copy = selected_node_ids_;
                        for (const auto& id : ids_copy)
                            commands_.remove_node(id);
                        selected_node_ids_.clear();
                    } else {
                        commands_.remove_node(context_node_id_);
                        selected_node_ids_.erase(context_node_id_);
                    }
                } else if (clicked_item == 1 && context_node_has_shader_) {
                    // "Clone & Edit"
                    commands_.clone_and_edit(context_node_type_);
                }
            } else if (context_wire_idx_ >= 0) {
                const auto& conns = snap_.connections;
                if (context_wire_idx_ < static_cast<int>(conns.size())) {
                    const auto& c = conns[context_wire_idx_];
                    if (clicked_item == 0) {
                        // "Delete Wire"
                        commands_.disconnect(c.from_node + "/" + c.from_port,
                                        c.to_node + "/" + c.to_port);
                    } else if (clicked_item == 1) {
                        // "Insert Node" — open chooser filtered to compatible types
                        insert_wire_source_type_ = resolve_port_type(snap_, c.from_node, c.from_port, true);
                        insert_wire_dest_type_   = resolve_port_type(snap_, c.to_node, c.to_port, false);
                        chooser_insert_conn_ = c;
                        chooser_insert_wire_ = true;
                        chooser_cursor_gx_ = sx_to_gx(context_menu_x_);
                        chooser_cursor_gy_ = sy_to_gy(context_menu_y_);
                        chooser_filter_.clear();
                        rebuild_chooser_items();
                        chooser_open_ = true;
                    }
                }
            }
            context_menu_open_ = false;
            // Consume click so handle_left_click() won't fire
            mouse_.left_clicked = false;
            mouse_.left_released = false;
        } else {
            // Clicked outside — close
            context_menu_open_ = false;
        }
    }
}

// -----------------------------------------------------------------------
// Right-click — open context menu (called from update())
// -----------------------------------------------------------------------
void NodeGraphUI::handle_right_click() {
    if (!mouse_.right_clicked) return;

    context_menu_open_ = false;
    context_node_has_shader_ = false;
    context_node_type_.clear();
    context_bg_menu_ = false;
    int ni = hit_test_node(mouse_.x, mouse_.y);
    if (ni >= 0) {
        context_menu_open_ = true;
        context_node_id_ = node_rects_[ni].node_id;
        context_node_type_ = node_rects_[ni].type_name;
        context_wire_idx_ = -1;
        context_menu_x_ = mouse_.x;
        context_menu_y_ = mouse_.y;
        // Check if this node type has a shader (for "Duplicate Filter" option)
        auto cat_it = snap_.operator_catalog.find(context_node_type_);
        if (cat_it != snap_.operator_catalog.end() && cat_it->second)
            context_node_has_shader_ = cat_it->second->has_shader;
    } else {
        int wi = hit_test_wire(mouse_.x, mouse_.y);
        if (wi >= 0) {
            context_menu_open_ = true;
            context_node_id_.clear();
            context_wire_idx_ = wi;
            context_menu_x_ = mouse_.x;
            context_menu_y_ = mouse_.y;
        } else if (mouse_.x < graph_right()) {
            // Right-click on empty canvas — background menu
            context_menu_open_ = true;
            context_node_id_.clear();
            context_wire_idx_ = -1;
            context_bg_menu_ = true;
            context_menu_x_ = mouse_.x;
            context_menu_y_ = mouse_.y;
        }
    }
}

// -----------------------------------------------------------------------
// Click dispatch (replaces goto click_done pattern)
// -----------------------------------------------------------------------
void NodeGraphUI::handle_left_click() {
    if (!mouse_.left_clicked) return;
    if (handle_chooser_click()) return;
    if (handle_dropdown_click()) return;
    if (handle_inspector_click()) return;
    handle_graph_click();
}

bool NodeGraphUI::handle_chooser_click() {
    if (!chooser_open_) return false;

    int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
    if (visible == 0) visible = 1;
    float items_y = kChooserY + kChooserHeaderH;

    if (mouse_.x >= chooser_x() && mouse_.x <= chooser_x() + kChooserW &&
        mouse_.y >= items_y && mouse_.y <= items_y + visible * kChooserItemH &&
        !chooser_items_.empty()) {
        int idx = chooser_scroll_ + static_cast<int>((mouse_.y - items_y) / kChooserItemH);
        if (idx >= 0 && idx < static_cast<int>(chooser_items_.size())) {
            confirm_chooser_selection(chooser_items_[idx]);
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return true;
        }
    }
    chooser_insert_wire_ = false;
    chooser_open_ = false;
    mouse_.left_clicked = false;
    mouse_.left_released = false;
    return true;
}

bool NodeGraphUI::handle_dropdown_click() {
    if (!dropdown_open_ || dropdown_labels_.empty()) return false;

    float item_h = kDropdownItemH;
    float popup_h = dropdown_labels_.size() * item_h + 4;
    if (mouse_.x >= dropdown_x_ && mouse_.x <= dropdown_x_ + dropdown_w_ &&
        mouse_.y >= dropdown_y_ && mouse_.y <= dropdown_y_ + popup_h) {
        int idx = static_cast<int>((mouse_.y - dropdown_y_ - 2) / item_h);
        if (idx >= 0 && idx < static_cast<int>(dropdown_labels_.size())) {
            commands_.set_param(dropdown_node_id_, dropdown_param_name_,
                           static_cast<float>(idx));
        }
        dropdown_open_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return true;
    } else {
        dropdown_open_ = false;
        return false;
    }
}

bool NodeGraphUI::handle_inspector_click() {
    // --- Color popup click handling (overlays everything) ---
    if (color_popup_open_) {
        float pad = kColorPopupPad;
        float sv_size = kColorPopupSVSize;
        float hue_bar_w = kColorHueBarW;
        float gap = kColorPopupGap;
        float hex_h = kColorHexFieldH;
        float rgb_gap = kColorRGBGap;
        float rgb_h = kColorRGBFieldH;
        float popup_w = pad + sv_size + gap + hue_bar_w + pad;
        float popup_h = pad + sv_size + gap + hex_h + rgb_gap + rgb_h + pad;
        float px = color_popup_x_, py = color_popup_y_;
        float sv_x = px + pad, sv_y = py + pad;
        float hue_x = sv_x + sv_size + gap, hue_y = sv_y;
        float hex_field_y = sv_y + sv_size + gap;
        float hex_field_w = sv_size + gap + hue_bar_w;

        // Dismiss any active text edits when clicking elsewhere
        if (color_editing_hex_) color_editing_hex_ = false;
        if (color_editing_rgb_ >= 0) color_editing_rgb_ = -1;

        // Click in SV square
        if (mouse_.x >= sv_x && mouse_.x < sv_x + sv_size &&
            mouse_.y >= sv_y && mouse_.y < sv_y + sv_size) {
            color_dragging_sv_ = true;
            color_popup_s_ = std::max(0.0f, std::min(1.0f, (mouse_.x - sv_x) / sv_size));
            color_popup_v_ = std::max(0.0f, std::min(1.0f, 1.0f - (mouse_.y - sv_y) / sv_size));
            float r, g, b;
            hsv_to_rgb(color_popup_h_, color_popup_s_, color_popup_v_, r, g, b);
            commands_.set_param(color_popup_node_id_, color_popup_param_r_, r);
            commands_.set_param(color_popup_node_id_, color_popup_param_g_, g);
            commands_.set_param(color_popup_node_id_, color_popup_param_b_, b);
            return true;
        }

        // Click in hue bar
        if (mouse_.x >= hue_x && mouse_.x < hue_x + hue_bar_w &&
            mouse_.y >= hue_y && mouse_.y < hue_y + sv_size) {
            color_dragging_hue_ = true;
            color_popup_h_ = std::max(0.0f, std::min(360.0f,
                (mouse_.y - hue_y) / sv_size * 360.0f));
            float r, g, b;
            hsv_to_rgb(color_popup_h_, color_popup_s_, color_popup_v_, r, g, b);
            commands_.set_param(color_popup_node_id_, color_popup_param_r_, r);
            commands_.set_param(color_popup_node_id_, color_popup_param_g_, g);
            commands_.set_param(color_popup_node_id_, color_popup_param_b_, b);
            return true;
        }

        // Click in hex field
        if (mouse_.x >= sv_x && mouse_.x < sv_x + hex_field_w &&
            mouse_.y >= hex_field_y && mouse_.y < hex_field_y + hex_h) {
            color_editing_hex_ = true;
            // Pre-fill with current hex value
            float cr, cg, cb;
            hsv_to_rgb(color_popup_h_, color_popup_s_, color_popup_v_, cr, cg, cb);
            char hex[8];
            rgb_to_hex(cr, cg, cb, hex, sizeof(hex));
            color_hex_buffer_ = hex;
            return true;
        }

        // Click in RGB channel fields
        {
            float rgb_field_y = hex_field_y + hex_h + rgb_gap;
            float field_gap = 4.0f;
            float field_w = (hex_field_w - field_gap * 2.0f) / 3.0f;
            for (int ch = 0; ch < 3; ++ch) {
                float fx = sv_x + ch * (field_w + field_gap);
                if (mouse_.x >= fx && mouse_.x < fx + field_w &&
                    mouse_.y >= rgb_field_y && mouse_.y < rgb_field_y + rgb_h) {
                    color_editing_rgb_ = ch;
                    // Pre-fill with current channel value
                    const std::string* param_names[3] = {
                        &color_popup_param_r_, &color_popup_param_g_, &color_popup_param_b_
                    };
                    const auto* ns = snap_.find_node(color_popup_node_id_);
                    if (ns) {
                        auto it = ns->param_indices.find(*param_names[ch]);
                        if (it != ns->param_indices.end()) {
                            int v = static_cast<int>(ns->param_values[it->second] * 255.0f + 0.5f);
                            color_rgb_buffer_ = std::to_string(v);
                        }
                    }
                    return true;
                }
            }
        }

        // Click inside popup but not on a control — consume
        if (mouse_.x >= px && mouse_.x < px + popup_w &&
            mouse_.y >= py && mouse_.y < py + popup_h) {
            return true;
        }

        // Click outside popup — close it
        color_popup_open_ = false;
        color_dragging_sv_ = false;
        color_dragging_hue_ = false;
        color_editing_hex_ = false;
        color_editing_rgb_ = -1;
        return true;
    }

    if (mouse_.x < graph_right() || mouse_.y >= static_cast<float>(win_h_))
        return false;

    // --- Patchbay button click ---
    if (selected_node_ids_.size() >= 3 && !patchbay_open_) {
        const auto& br = patchbay_button_rect_;
        if (br.w > 0 && mouse_.x >= br.x && mouse_.x <= br.x + br.w &&
            mouse_.y >= br.y && mouse_.y <= br.y + br.h) {
            open_patchbay();
            mouse_.left_clicked = false;
            return true;
        }
    }

    // --- MIDI map mode click guard ---
    if (midi_map_mode_) {
        if (mouse_.y < kPerfBarH) return true;

        // Confirm any active midi range edit
        if (editing_midi_range_) confirm_midi_range_edit();

        // Hit-test remove rects
        int rmi = hit_test_rect(midi_remove_rects_, mouse_.x, mouse_.y);
        if (rmi >= 0) {
            const auto& rr = midi_remove_rects_[rmi];
            commands_.remove_midi_mapping(rr.node_id, rr.param_name);
            return true;
        }

        // Hit-test range rects (min/max edit fields)
        int rri = hit_test_rect(midi_range_rects_, mouse_.x, mouse_.y);
        if (rri >= 0) {
            const auto& mr = midi_range_rects_[rri];
            editing_midi_range_ = true;
            midi_range_node_id_ = mr.node_id;
            midi_range_param_name_ = mr.param_name;
            midi_range_editing_min_ = mr.is_min;
            // Pre-fill with current value
            const auto* mm = snap_.find_midi_mapping(mr.node_id, mr.param_name);
            if (mm) {
                edit_buffer_ = format_float(mr.is_min ? mm->range_min : mm->range_max, 2);
            } else {
                edit_buffer_.clear();
            }
            return true;
        }

        // Hit-test any slider/value_text/bool/dropdown rect -> set waiting target
        auto check_param_rect = [&](const std::vector<InspectorRect>& rects) -> bool {
            int idx = hit_test_rect(rects, mouse_.x, mouse_.y);
            if (idx >= 0) {
                midi_map_waiting_ = true;
                midi_map_node_id_ = rects[idx].node_id;
                midi_map_param_name_ = rects[idx].param_name;
                return true;
            }
            return false;
        };
        if (check_param_rect(slider_rects_)) return true;
        if (check_param_rect(value_text_rects_)) return true;
        if (check_param_rect(bool_rects_)) return true;
        if (check_param_rect(dropdown_rects_)) return true;
        if (check_param_rect(drum_grid_rects_)) return true;
        if (check_param_rect(drum_mod_a_rects_)) return true;
        if (check_param_rect(drum_mod_b_rects_)) return true;

        return true; // Consume all inspector clicks in MIDI map mode
    }

    // Scrollbar hit test — check the scrollbar track area
    if (insp_content_h_ > static_cast<float>(win_h_) - kPerfBarH) {
        float insp_x = inspector_x();
        float track_x = insp_x + kInspectorW - kInspScrollbarW - 2.0f;
        float viewport_top = kPerfBarH;
        float viewport_h = static_cast<float>(win_h_) - viewport_top;
        float track_y = viewport_top + 2.0f;
        float track_h = viewport_h - 4.0f;

        if (mouse_.x >= track_x && mouse_.x <= track_x + kInspScrollbarW + 2.0f &&
            mouse_.y >= track_y && mouse_.y <= track_y + track_h) {
            insp_scrollbar_dragging_ = true;
            insp_sb_drag_start_y_ = mouse_.y;
            insp_sb_drag_start_scroll_ = insp_scroll_y_;
            return true;
        }
    }

    // Group header collapse/expand
    for (const auto& gh : group_header_rects_) {
        if (mouse_.x >= gh.x && mouse_.x < gh.x + gh.w &&
            mouse_.y >= gh.y && mouse_.y < gh.y + gh.h) {
            toggle_group_collapsed(gh.type_name, gh.group_name);
            return true;
        }
    }

    // Reject clicks above perf bar (clipped-off content)
    if (mouse_.y < kPerfBarH) return true;

    // Confirm any active text edit when clicking in inspector
    if (editing_param_) confirm_param_edit();
    if (editing_resolution_) confirm_resolution_edit();

    // Check resolution rect click-to-edit
    int ri = hit_test_rect(resolution_rects_, mouse_.x, mouse_.y);
    if (ri >= 0) {
        const auto& rr = resolution_rects_[ri];
        editing_resolution_ = true;
        edit_res_node_id_ = rr.node_id;
        edit_res_is_width_ = rr.is_width;
        const auto* ns = snap_.find_node(rr.node_id);
        if (ns) {
            edit_buffer_ = format_uint(rr.is_width ? ns->gpu_tex_width : ns->gpu_tex_height);
        }
        return true;
    }

    // Check value text click-to-edit
    int vt = hit_test_rect(value_text_rects_, mouse_.x, mouse_.y);
    if (vt >= 0) {
        editing_param_ = true;
        edit_node_id_ = value_text_rects_[vt].node_id;
        edit_param_name_ = value_text_rects_[vt].param_name;
        const auto* ns = snap_.find_node(edit_node_id_);
        if (ns && ns->op_info) {
            auto it = ns->param_indices.find(edit_param_name_);
            if (it != ns->param_indices.end()) {
                for (const auto& pd : ns->op_info->params) {
                    if (pd.name != edit_param_name_) continue;
                    if (pd.type == VIVID_PARAM_INT) {
                        edit_buffer_ = format_int(static_cast<int>(ns->param_values[it->second]));
                    } else {
                        edit_buffer_ = format_float(ns->param_values[it->second], 2);
                    }
                    break;
                }
            }
        }
        return true;
    }

    // Check dropdown click
    int di = hit_test_rect(dropdown_rects_, mouse_.x, mouse_.y);
    if (di >= 0) {
        const auto& dr = dropdown_rects_[di];
        dropdown_node_id_ = dr.node_id;
        dropdown_param_name_ = dr.param_name;
        dropdown_x_ = dr.x;
        dropdown_y_ = dr.y + dr.h;
        dropdown_w_ = dr.w;
        dropdown_labels_.clear();
        const auto* ns = snap_.find_node(dr.node_id);
        if (ns && ns->op_info) {
            for (const auto& pd : ns->op_info->params) {
                if (pd.name != dr.param_name) continue;
                for (const auto& label : pd.choice_labels)
                    dropdown_labels_.push_back(label);
                auto it = ns->param_indices.find(dr.param_name);
                if (it != ns->param_indices.end())
                    dropdown_sel_ = static_cast<int>(ns->param_values[it->second]);
                break;
            }
        }
        dropdown_open_ = !dropdown_labels_.empty();
        return true;
    }

    // Check XY pad
    int xyi = hit_test_rect(xy_pad_rects_, mouse_.x, mouse_.y);
    if (xyi >= 0) {
        active_xy_pad_idx_ = xyi;
        active_xy_node_id_ = xy_pad_rects_[xyi].node_id;
        active_xy_param_x_ = xy_pad_rects_[xyi].param_x;
        active_xy_param_y_ = xy_pad_rects_[xyi].param_y;
        return true;
    }

    // Check color swatch
    int ci = hit_test_rect(color_swatch_rects_, mouse_.x, mouse_.y);
    if (ci >= 0) {
        const auto& cs = color_swatch_rects_[ci];
        // Toggle popup
        if (color_popup_open_ && color_popup_node_id_ == cs.node_id &&
            color_popup_param_r_ == cs.param_r) {
            color_popup_open_ = false;
            color_editing_rgb_ = -1;
        } else {
            color_popup_open_ = true;
            color_popup_node_id_ = cs.node_id;
            color_popup_param_r_ = cs.param_r;
            color_popup_param_g_ = cs.param_g;
            color_popup_param_b_ = cs.param_b;
            // Position popup adjacent to swatch
            color_popup_x_ = cs.x;
            color_popup_y_ = cs.y + cs.h + 4;
            // Convert current RGB to HSV
            const auto* ns = snap_.find_node(cs.node_id);
            if (ns) {
                auto r_it = ns->param_indices.find(cs.param_r);
                auto g_it = ns->param_indices.find(cs.param_g);
                auto b_it = ns->param_indices.find(cs.param_b);
                if (r_it != ns->param_indices.end() && g_it != ns->param_indices.end() &&
                    b_it != ns->param_indices.end()) {
                    float r = ns->param_values[r_it->second];
                    float g = ns->param_values[g_it->second];
                    float b = ns->param_values[b_it->second];
                    rgb_to_hsv(r, g, b, color_popup_h_, color_popup_s_, color_popup_v_);
                }
            }
        }
        return true;
    }

    // Check slider
    int si = hit_test_rect(slider_rects_, mouse_.x, mouse_.y);
    if (si >= 0) {
        active_slider_idx_ = si;
        active_slider_node_id_ = slider_rects_[si].node_id;
        active_slider_param_name_ = slider_rects_[si].param_name;
        return true;
    }

    // Check bool toggle
    int bi = hit_test_rect(bool_rects_, mouse_.x, mouse_.y);
    if (bi >= 0) {
        const auto& br = bool_rects_[bi];
        const auto* ns = snap_.find_node(br.node_id);
        if (ns) {
            auto it = ns->param_indices.find(br.param_name);
            if (it != ns->param_indices.end()) {
                float cur = ns->param_values[it->second];
                commands_.set_param(br.node_id, br.param_name,
                               cur > 0.5f ? 0.0f : 1.0f);
            }
        }
        return true;
    }

    // Check file button click
    int fi = hit_test_rect(file_button_rects_, mouse_.x, mouse_.y);
    if (fi >= 0) {
        const auto& fr = file_button_rects_[fi];
        std::string path = vivid::ui::open_file_dialog();
        if (!path.empty()) {
            commands_.set_string_param(fr.node_id, fr.param_name, path);
        }
        return true;
    }

    // Check drum tab click
    int dti = hit_test_rect(drum_tab_rects_, mouse_.x, mouse_.y);
    if (dti >= 0) {
        drum_grid_tab_ = dti;
        return true;
    }

    // Check drum mod cell drag start (Mod A / Mod B tabs)
    int dma = hit_test_rect(drum_mod_a_rects_, mouse_.x, mouse_.y);
    if (dma >= 0) {
        active_drum_mod_idx_ = dma;
        active_drum_mod_node_id_ = drum_mod_a_rects_[dma].node_id;
        active_drum_mod_param_name_ = drum_mod_a_rects_[dma].param_name;
        return true;
    }
    int dmb = hit_test_rect(drum_mod_b_rects_, mouse_.x, mouse_.y);
    if (dmb >= 0) {
        active_drum_mod_idx_ = dmb;
        active_drum_mod_node_id_ = drum_mod_b_rects_[dmb].node_id;
        active_drum_mod_param_name_ = drum_mod_b_rects_[dmb].param_name;
        return true;
    }

    // Check drum grid cell toggle (Pattern tab)
    int dgi = hit_test_rect(drum_grid_rects_, mouse_.x, mouse_.y);
    if (dgi >= 0) {
        const auto& dr = drum_grid_rects_[dgi];
        const auto* ns = snap_.find_node(dr.node_id);
        if (ns) {
            auto it = ns->param_indices.find(dr.param_name);
            if (it != ns->param_indices.end()) {
                float cur = ns->param_values[it->second];
                commands_.set_param(dr.node_id, dr.param_name,
                               cur > 0.5f ? 0.0f : 1.0f);
            }
        }
        return true;
    }

    // Check matrix cell click (connect/disconnect or start scale drag)
    if (handle_matrix_click()) return true;

    return true;  // Click was in inspector area, consume it
}

void NodeGraphUI::handle_graph_click() {
    if (mouse_.x >= graph_right() || mouse_.y >= static_cast<float>(win_h_))
        return;

    // Clicking in graph area confirms any active text edit
    if (editing_param_) confirm_param_edit();
    if (editing_resolution_) confirm_resolution_edit();

    // Port hit test first (ports are on node edges, inside node AABB)
    PortHit ph = hit_test_port(mouse_.x, mouse_.y);
    if (ph.node_idx >= 0) {
        if (ph.is_output) {
            // Start wire drag from output port
            dragging_wire_ = true;
            wire_from_node_id_ = node_rects_[ph.node_idx].node_id;
            wire_from_port_ = ph.port_name;
            wire_from_gx_ = ph.gx;
            wire_from_gy_ = ph.gy;
        } else {
            // Click on input port — disconnect existing wires to this input
            std::string to_node = node_rects_[ph.node_idx].node_id;
            const auto& conns = snap_.connections;
            for (const auto& c : conns) {
                if (c.to_node == to_node && c.to_port == ph.port_name) {
                    commands_.disconnect(c.from_node + "/" + c.from_port,
                                    to_node + "/" + ph.port_name);
                }
            }
        }
    } else {
        int ni = hit_test_node(mouse_.x, mouse_.y);
        if (ni >= 0) {
            std::string node_id = node_rects_[ni].node_id;

            // If click is on the right edge, open output/param picker to start a wire drag
            const auto* ns = snap_.find_node(node_id);
            if (ns) {
                float gx = sx_to_gx(mouse_.x);
                float right_zone = node_rects_[ni].x + node_rects_[ni].w - 15.0f;
                if (gx >= right_zone) {
                    param_picker_node_id_ = node_id;
                    param_picker_wire_from_node_.clear();
                    param_picker_wire_from_port_.clear();
                    param_picker_is_output_ = true;
                    param_picker_x_ = mouse_.x;
                    param_picker_y_ = mouse_.y;
                    param_picker_sel_ = 0;
                    param_picker_scroll_ = 0;
                    rebuild_param_picker_items();
                    if (!param_picker_items_.empty()) {
                        param_picker_open_ = true;
                        return;
                    }
                }
            }

            // Double-click detection: open/clone operator
            double now = glfwGetTime();
            if (node_id == last_click_node_id_ && (now - last_click_time_) < 0.3) {
                const std::string& type_name = node_rects_[ni].type_name;
                auto cat_it = snap_.operator_catalog.find(type_name);
                bool is_user = cat_it != snap_.operator_catalog.end() &&
                               cat_it->second && cat_it->second->is_user;
                if (is_user) {
                    commands_.open_shader(type_name);
                } else {
                    clone_confirm_type_ = type_name;
                    clone_confirm_open_ = true;
                }
                last_click_node_id_.clear();
            } else {
                last_click_node_id_ = node_id;
                last_click_time_ = now;
            }

            if (mouse_.shift_down) {
                // Shift-click: toggle node in/out of selection, no drag
                if (selected_node_ids_.count(node_id))
                    selected_node_ids_.erase(node_id);
                else
                    selected_node_ids_.insert(node_id);
                pending_select_node_id_.clear();
            } else if (selected_node_ids_.count(node_id)) {
                // Already selected: keep selection, begin group drag
                dragging_node_idx_ = ni;
                drag_offset_x_ = sx_to_gx(mouse_.x) - node_rects_[ni].x;
                drag_offset_y_ = sy_to_gy(mouse_.y) - node_rects_[ni].y;
                // Compute per-node offsets for group drag
                group_drag_offsets_.clear();
                float mgx = sx_to_gx(mouse_.x);
                float mgy = sy_to_gy(mouse_.y);
                for (const auto& sel_id : selected_node_ids_) {
                    for (const auto& r : node_rects_) {
                        if (r.node_id == sel_id) {
                            group_drag_offsets_[sel_id] = { mgx - r.x, mgy - r.y };
                            break;
                        }
                    }
                }
                pending_select_node_id_ = node_id;
                did_drag_ = false;
                drag_start_sx_ = mouse_.x;
                drag_start_sy_ = mouse_.y;
            } else {
                // Not selected: replace selection with this node, begin drag
                selected_node_ids_ = { node_id };
                dragging_node_idx_ = ni;
                drag_offset_x_ = sx_to_gx(mouse_.x) - node_rects_[ni].x;
                drag_offset_y_ = sy_to_gy(mouse_.y) - node_rects_[ni].y;
                group_drag_offsets_.clear();
                pending_select_node_id_.clear();
            }
        } else {
            // Empty canvas: start box-select
            if (!mouse_.shift_down)
                selected_node_ids_.clear();
            box_selecting_ = true;
            box_start_gx_ = sx_to_gx(mouse_.x);
            box_start_gy_ = sy_to_gy(mouse_.y);
            box_shift_held_ = mouse_.shift_down;
        }
    }
}

void NodeGraphUI::update_scrollbar_drag() {
    if (!insp_scrollbar_dragging_) return;

    if (mouse_.left_down) {
        float viewport_h = static_cast<float>(win_h_) - kPerfBarH;
        float track_h = viewport_h - 4.0f;
        float ratio = viewport_h / insp_content_h_;
        float thumb_h = std::max(kInspScrollbarMinThumb, track_h * ratio);
        float scrollable_track = track_h - thumb_h;

        if (scrollable_track > 0.0f) {
            float max_scroll = insp_content_h_ - viewport_h;
            float mouse_delta = mouse_.y - insp_sb_drag_start_y_;
            float scroll_delta = (mouse_delta / scrollable_track) * max_scroll;
            insp_scroll_y_ = std::max(0.0f, std::min(max_scroll,
                                      insp_sb_drag_start_scroll_ + scroll_delta));
        }
    }

    if (mouse_.left_released) {
        insp_scrollbar_dragging_ = false;
    }
}

// -----------------------------------------------------------------------
// Preferences panel click handling (called from update())
// -----------------------------------------------------------------------
void NodeGraphUI::update_preferences() {
    if (!prefs_open_ || !mouse_.left_clicked) return;

    float wf = static_cast<float>(win_w_);
    float hf = static_cast<float>(win_h_);

    int editor_count = static_cast<int>(prefs_editor_names_.size());
    int style_count = static_cast<int>(prefs_styles_.size());
    bool show_custom = (prefs_editor_sel_ >= 0 &&
                        prefs_editor_sel_ < static_cast<int>(prefs_editor_ids_.size()) &&
                        prefs_editor_ids_[prefs_editor_sel_] == "custom");

    float content_h = kPrefsPadY
        + kPrefsRowH + kPrefsSectionGap
        + kPrefsRowH + editor_count * kPrefsRowH
        + (show_custom ? kPrefsRowH + 4 : 0)
        + kPrefsSectionGap
        + kPrefsRowH + style_count * kPrefsRowH
        + kPrefsRowH + 4                          // "Open Themes Folder" link
        + kPrefsSectionGap + kPrefsBtnH + kPrefsPadY;

    float pw = kPrefsW;
    float ph = content_h;
    float px = (wf - pw) * 0.5f;
    float py = (hf - ph) * 0.5f;

    // Click outside panel → close (cancel)
    if (mouse_.x < px || mouse_.x > px + pw ||
        mouse_.y < py || mouse_.y > py + ph) {
        // Revert style
        if (prefs_saved_style_sel_ >= 0 &&
            prefs_saved_style_sel_ < static_cast<int>(prefs_styles_.size())) {
            style_ = prefs_styles_[prefs_saved_style_sel_];
            prefs_style_sel_ = prefs_saved_style_sel_;
        }
        prefs_open_ = false;
        prefs_editing_custom_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    float cx = px + kPrefsPadX;
    float inner_w = pw - 2 * kPrefsPadX;
    float cy = py + kPrefsPadY + kPrefsRowH + kPrefsSectionGap;

    // Skip section header
    cy += kPrefsRowH;

    // Editor radio items
    for (int i = 0; i < editor_count; ++i) {
        if (mouse_.x >= cx && mouse_.x <= cx + inner_w &&
            mouse_.y >= cy && mouse_.y <= cy + kPrefsRowH) {
            prefs_editor_sel_ = i;
            prefs_editing_custom_ = false;
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
        cy += kPrefsRowH;
    }

    // Custom command field click
    if (show_custom) {
        cy += 2;
        if (mouse_.x >= cx + 18 && mouse_.x <= cx + inner_w &&
            mouse_.y >= cy && mouse_.y <= cy + kPrefsRowH - 2) {
            prefs_editing_custom_ = true;
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
        cy += kPrefsRowH + 2;
    }

    cy += kPrefsSectionGap;

    // Skip STYLE section header
    cy += kPrefsRowH;

    // Style radio items
    for (int i = 0; i < style_count; ++i) {
        if (mouse_.x >= cx && mouse_.x <= cx + inner_w &&
            mouse_.y >= cy && mouse_.y <= cy + kPrefsRowH) {
            prefs_style_sel_ = i;
            // Live preview: apply style immediately
            if (i >= 0 && i < static_cast<int>(prefs_styles_.size())) {
                style_ = prefs_styles_[i];
            }
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return;
        }
        cy += kPrefsRowH;
    }

    // "Open Themes Folder" link
    cy += 4;
    if (mouse_.x >= cx + 18 && mouse_.x <= cx + inner_w &&
        mouse_.y >= cy && mouse_.y <= cy + kPrefsRowH) {
        open_themes_folder();
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }
    cy += kPrefsRowH;

    cy += kPrefsSectionGap;

    // Buttons
    float btn_total = 2 * kPrefsBtnW + 12;
    float save_x = px + (pw - btn_total) * 0.5f;
    float cancel_x = save_x + kPrefsBtnW + 12;

    if (mouse_.x >= save_x && mouse_.x <= save_x + kPrefsBtnW &&
        mouse_.y >= cy && mouse_.y <= cy + kPrefsBtnH) {
        // Save
        std::string editor_id;
        if (prefs_editor_sel_ >= 0 && prefs_editor_sel_ < static_cast<int>(prefs_editor_ids_.size()))
            editor_id = prefs_editor_ids_[prefs_editor_sel_];
        commands_.set_editor_preference(editor_id, prefs_custom_command_);

        if (prefs_style_sel_ >= 0 && prefs_style_sel_ < static_cast<int>(prefs_styles_.size())) {
            commands_.set_style_preference(prefs_styles_[prefs_style_sel_].id);
            prefs_saved_style_sel_ = prefs_style_sel_;
        }

        prefs_open_ = false;
        prefs_editing_custom_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    if (mouse_.x >= cancel_x && mouse_.x <= cancel_x + kPrefsBtnW &&
        mouse_.y >= cy && mouse_.y <= cy + kPrefsBtnH) {
        // Cancel: revert style
        if (prefs_saved_style_sel_ >= 0 &&
            prefs_saved_style_sel_ < static_cast<int>(prefs_styles_.size())) {
            style_ = prefs_styles_[prefs_saved_style_sel_];
            prefs_style_sel_ = prefs_saved_style_sel_;
        }
        prefs_open_ = false;
        prefs_editing_custom_ = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return;
    }

    // Consume click inside panel
    prefs_editing_custom_ = false;
    mouse_.left_clicked = false;
    mouse_.left_released = false;
}

// -----------------------------------------------------------------------
// Matrix cell click — toggle connection or start scale drag
// -----------------------------------------------------------------------
bool NodeGraphUI::handle_matrix_click() {
    int ci = hit_test_rect(matrix_cell_rects_, mouse_.x, mouse_.y);
    if (ci < 0) return false;

    const auto& cell = matrix_cell_rects_[ci];

    if (cell.connected) {
        // Start scale drag on connected cells
        matrix_scale_dragging_ = true;
        matrix_drag_cell_idx_ = ci;
        matrix_drag_start_y_ = mouse_.y;
        matrix_drag_start_scale_ = cell.scale;
    } else {
        // Connect: from_node/from_port -> to_node/to_port
        std::string from_addr = cell.from_node + "/" + cell.from_port;
        std::string to_addr = cell.to_node + "/" + cell.to_port;
        commands_.connect(from_addr, to_addr);
    }
    return true;
}

// -----------------------------------------------------------------------
// Patchbay update — click handling and auto-dismiss
// -----------------------------------------------------------------------
void NodeGraphUI::update_patchbay() {
    if (!patchbay_open_) return;

    // Auto-dismiss if selection changes or drops below 3
    if (selected_node_ids_.size() < 3 || selected_node_ids_ != patchbay_node_ids_) {
        patchbay_open_ = false;
        return;
    }

    // Click handling
    if (mouse_.left_clicked) {
        // Hit-test against panel rect
        if (mouse_.x < patchbay_panel_x_ || mouse_.x > patchbay_panel_x_ + patchbay_panel_w_ ||
            mouse_.y < patchbay_panel_y_ || mouse_.y > patchbay_panel_y_ + patchbay_panel_h_) {
            // Click outside panel — dismiss
            patchbay_open_ = false;
            mouse_.left_clicked = false;
            return;
        }

        // Click inside panel — delegate to matrix cell click handling
        if (handle_matrix_click()) {
            mouse_.left_clicked = false;
        }
    }
}

} // namespace vivid::ui
