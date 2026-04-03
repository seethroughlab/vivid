#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
#include "ui/node_graph_util.h"
#include "ui/active_text_field.h"
#include "ui/overlay_layouts.h"
#include "ui/file_dialog.h"
#include "runtime/platform/platform.h"
#include "common/string_util.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>

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
            float bottom_offset = session_grid_open_ ? kSessionStripH : 0.0f;
            build_console_panel_.handle_left_release(mouse_.x, mouse_.y, win_w_, win_h_, bottom_offset);
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            mouse_.right_down = true;
            if (pan_gesture_ == "right") {
                // Defer context menu: start pending right-drag pan
                right_pending_ = true;
                right_press_mx_ = mouse_.x;
                right_press_my_ = mouse_.y;
                pan_start_mx_ = mouse_.x;
                pan_start_my_ = mouse_.y;
                pan_start_px_ = pan_x_;
                pan_start_py_ = pan_y_;
            } else {
                mouse_.right_clicked = true;
            }
        } else if (action == GLFW_RELEASE) {
            mouse_.right_down = false;
            if (pan_gesture_ == "right") {
                if (right_pending_) {
                    // No drag occurred — treat as context menu click
                    right_pending_ = false;
                    mouse_.right_clicked = true;
                }
                panning_ = false;
            }
            mouse_.right_released = true;
        }
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (pan_gesture_ == "middle") {
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
}

void NodeGraphUI::on_scroll(float x_offset, float y_offset, int mods) {
    // Delegate to dialog manager first (handles pkg/example browser scroll, about scroll, etc.)
    if (dialogs_.on_scroll(y_offset)) return;

    // Param picker scroll
    if (inspector_.param_picker_open && !inspector_.param_picker_items.empty()) {
        inspector_.param_picker_scroll -= y_offset * kPickerItemH;
        float max_scroll = std::max(0.0f, (static_cast<int>(inspector_.param_picker_items.size()) - kPickerMaxVisible) * kPickerItemH);
        inspector_.param_picker_scroll = std::max(0.0f, std::min(inspector_.param_picker_scroll, max_scroll));
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
            chooser_scroll_ -= y_offset * kChooserItemH;
            float max_scroll = std::max(0.0f, (static_cast<int>(chooser_items_.size()) - kChooserMaxVisible) * kChooserItemH);
            chooser_scroll_ = std::max(0.0f, std::min(chooser_scroll_, max_scroll));
            return;
        }
    }

    float bottom_offset = session_grid_open_ ? kSessionStripH : 0.0f;
    if (build_console_panel_.handle_scroll(mouse_.x, mouse_.y, x_offset, y_offset,
                                           win_w_, win_h_, bottom_offset)) {
        return;
    }

    // Session grid horizontal scroll
    if (session_grid_open_ && mouse_.y >= session_strip_top()) {
        session_scroll_x_ -= y_offset * 30.0f;
        session_scroll_x_ = std::max(0.0f, session_scroll_x_);
        return;
    }

    // Inspector scroll when cursor is in inspector area
    if (mouse_.x >= graph_right() && has_selection()) {
        inspector_.insp_scroll_y -= y_offset * kInspScrollSpeed;
        float viewport_h = static_cast<float>(win_h_) - kPerfBarH;
        float max_scroll = std::max(0.0f, inspector_.insp_content_h - viewport_h);
        inspector_.insp_scroll_y = std::max(0.0f, std::min(inspector_.insp_scroll_y, max_scroll));
        return;
    }

    if (mods & GLFW_MOD_SUPER) {
        // Cmd+scroll → pan (set targets for easing)
        constexpr float kPanSpeed = 3.0f;
        float speed = kPanSpeed / zoom_target_;
        pan_target_x_ += x_offset * speed;
        pan_target_y_ += y_offset * speed;
    } else {
        // Scroll → zoom (pivot around cursor, set targets for easing)
        float factor = std::pow(1.12f, y_offset);
        float new_zoom = zoom_target_ * factor;
        new_zoom = std::max(0.4f, std::min(2.5f, new_zoom));

        // Compute pivot in graph space using current (not target) transform
        float gx = (mouse_.x - pan_x_) / zoom_;
        float gy = (mouse_.y - pan_y_) / zoom_;
        zoom_target_ = new_zoom;
        pan_target_x_ = mouse_.x - gx * new_zoom;
        pan_target_y_ = mouse_.y - gy * new_zoom;
    }
}

ActiveTextField NodeGraphUI::resolve_active_text_field() {
    {
        auto df = dialogs_.resolve_active_field();
        if (df.buf) return df;
    }
    if (session_editing_name_)
        return {&session_edit_buffer_, filter_printable};
    if (inspector_.editing_midi_range || inspector_.editing_wire_remap)
        return {&inspector_.edit_buffer, filter_numeric};
    if (inspector_.editing_param) {
        const auto* ns = snap_.find_node(inspector_.edit_node_id);
        const ParamInfo* pd = ns ? ns->find_param(inspector_.edit_param_name) : nullptr;
        if (pd && pd->type == VIVID_PARAM_TEXT)
            return {&inspector_.edit_buffer, filter_printable};
        return {&inspector_.edit_buffer, filter_numeric};
    }
    if (inspector_.editing_resolution)
        return {&inspector_.edit_buffer, filter_digits};
    if (inspector_.color_editing_hex)
        return {&inspector_.color_hex_buffer, filter_hex, 7};
    if (inspector_.color_editing_rgb >= 0)
        return {&inspector_.color_rgb_buffer, filter_rgb, 3};
    if (editing_sticky_)
        return {&sticky_edit_buffer_, filter_printable};
    if (chooser_open_)
        return {&chooser_filter_, filter_printable};
    return {};
}

bool NodeGraphUI::handle_sticky_edit_mode_key(int key, bool mod_key) {
    if (!editing_sticky_) return false;
    if (key == GLFW_KEY_ESCAPE) {
        editing_sticky_ = false;
        sticky_edit_id_.clear();
        return true;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        text_edit_insert(sticky_edit_buffer_, text_edit_, std::string(1, '\n'), nullptr);
        cursor_blink_time_ = 0.0f;
        return true;
    }
    if (key == GLFW_KEY_BACKSPACE && !mod_key) {
        text_edit_backspace(sticky_edit_buffer_, text_edit_);
        cursor_blink_time_ = 0.0f;
        return true;
    }
    return true;
}

bool NodeGraphUI::handle_session_mode_key(int key, int action, int mods, bool mod_key) {
    (void)mods;
    if (session_ctx_menu_open_) {
        if (key == GLFW_KEY_ESCAPE) {
            session_ctx_menu_open_ = false;
        }
        return true;
    }

    if (session_editing_name_) {
        if (key == GLFW_KEY_ENTER) {
            if (!session_edit_buffer_.empty() && session_edit_idx_ >= 0 &&
                session_edit_idx_ < static_cast<int>(snap_.variations.size())) {
                commands_.rename_variation(snap_.variations[session_edit_idx_].name,
                                           session_edit_buffer_);
            }
            session_editing_name_ = false;
        } else if (key == GLFW_KEY_ESCAPE) {
            session_editing_name_ = false;
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(session_edit_buffer_, text_edit_);
        }
        return true;
    }

    if (session_grid_open_ && session_selected_idx_ >= 0) {
        if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) {
            if (session_selected_idx_ < static_cast<int>(snap_.variations.size())) {
                commands_.remove_variation(snap_.variations[session_selected_idx_].name);
                session_selected_idx_ = -1;
            }
            return true;
        }
        if (key == GLFW_KEY_ESCAPE) {
            session_selected_idx_ = -1;
            return true;
        }
    }

    if (!chooser_open_ && action == GLFW_PRESS && mod_key) {
        if (key == GLFW_KEY_Z) {
            if (mods & GLFW_MOD_SHIFT) commands_.redo();
            else commands_.undo();
            return true;
        }
        if (key == GLFW_KEY_A) {
            selected_node_ids_.clear();
            for (const auto& r : node_rects_)
                selected_node_ids_.insert(r.node_id);
            return true;
        }
    }

    return false;
}

bool NodeGraphUI::handle_inspector_edit_mode_key(int key) {
    if (inspector_.editing_midi_range) {
        if (key == GLFW_KEY_ENTER)       confirm_midi_range_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_midi_range_edit();
        else if (key == GLFW_KEY_TAB) {
            bool was_min = inspector_.midi_range_editing_min;
            std::string node_id = inspector_.midi_range_node_id;
            std::string param_name = inspector_.midi_range_param_name;
            confirm_midi_range_edit();
            if (was_min) {
                inspector_.editing_midi_range = true;
                inspector_.midi_range_node_id = node_id;
                inspector_.midi_range_param_name = param_name;
                inspector_.midi_range_editing_min = false;
                const auto* mm = snap_.find_midi_mapping(node_id, param_name);
                if (mm) {
                    inspector_.edit_buffer = format_float(mm->range_max, 2);
                    text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
                }
            }
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(inspector_.edit_buffer, text_edit_);
        }
        return true;
    }

    if (inspector_.editing_param) {
        if (key == GLFW_KEY_ENTER)       confirm_param_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_param_edit();
        else if (key == GLFW_KEY_TAB) {
            std::string node_id = inspector_.edit_node_id;
            std::string param_name = inspector_.edit_param_name;
            const auto* ns = snap_.find_node(node_id);
            const ParamInfo* cur_pd = ns ? ns->find_param(param_name) : nullptr;
            if (cur_pd && cur_pd->type == VIVID_PARAM_TEXT) {
                confirm_param_edit();
                return true;
            }
            confirm_param_edit();
            if (ns && ns->op_info) {
                const ParamInfo* pd = ns->find_param(param_name);
                if (pd && pd->display_hint == VIVID_DISPLAY_XY_PAD) {
                    auto pi_it = ns->param_indices.find(param_name);
                    if (pi_it != ns->param_indices.end()) {
                        uint32_t pi = pi_it->second;
                        if (pi + 1 < ns->op_info->params.size() &&
                            ns->op_info->params[pi + 1].display_hint == VIVID_DISPLAY_XY_PAD) {
                            const auto& pd_y = ns->op_info->params[pi + 1];
                            inspector_.editing_param = true;
                            inspector_.edit_node_id = node_id;
                            inspector_.edit_param_name = pd_y.name;
                            if (pd_y.type == VIVID_PARAM_INT)
                                inspector_.edit_buffer = format_int(static_cast<int>(ns->param_values[pi + 1]));
                            else
                                inspector_.edit_buffer = format_float(ns->param_values[pi + 1], 2);
                            text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
                        }
                    }
                }
            }
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(inspector_.edit_buffer, text_edit_);
        }
        return true;
    }

    if (inspector_.editing_wire_remap) {
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_TAB) {
            if (selected_wire_idx_ >= 0 &&
                selected_wire_idx_ < static_cast<int>(snap_.connections.size())) {
                const auto& c = snap_.connections[selected_wire_idx_];
                float vals[4] = { c.from_min, c.from_max, c.to_min, c.to_max };
                vals[inspector_.edit_wire_remap_field] = static_cast<float>(std::atof(inspector_.edit_buffer.c_str()));
                std::string from_addr = c.from_node + "/" + c.from_port;
                std::string to_addr   = c.to_node   + "/" + c.to_port;
                commands_.set_connection_remap(from_addr, to_addr,
                    vals[0], vals[1], vals[2], vals[3], c.clamp);
            }
            if (key == GLFW_KEY_TAB && inspector_.edit_wire_remap_field < 3) {
                int next_field = inspector_.edit_wire_remap_field + 1;
                inspector_.editing_wire_remap = true;
                inspector_.edit_wire_remap_field = next_field;
                if (selected_wire_idx_ >= 0 &&
                    selected_wire_idx_ < static_cast<int>(snap_.connections.size())) {
                    const auto& c = snap_.connections[selected_wire_idx_];
                    float vals[4] = { c.from_min, c.from_max, c.to_min, c.to_max };
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "%.3g", vals[next_field]);
                    inspector_.edit_buffer = buf;
                    text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
                }
            } else {
                inspector_.editing_wire_remap = false;
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            inspector_.editing_wire_remap = false;
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(inspector_.edit_buffer, text_edit_);
        }
        return true;
    }

    if (inspector_.editing_resolution) {
        if (key == GLFW_KEY_ENTER)       confirm_resolution_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_resolution_edit();
        else if (key == GLFW_KEY_TAB) {
            bool was_width = inspector_.edit_res_is_width;
            std::string node_id = inspector_.edit_res_node_id;
            confirm_resolution_edit();
            if (was_width) {
                inspector_.editing_resolution = true;
                inspector_.edit_res_node_id = node_id;
                inspector_.edit_res_is_width = false;
                const auto* ns = snap_.find_node(node_id);
                if (ns) {
                    inspector_.edit_buffer = format_uint(ns->gpu_tex_height);
                    text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
                }
            }
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(inspector_.edit_buffer, text_edit_);
        }
        return true;
    }

    if (inspector_.color_editing_hex) {
        if (key == GLFW_KEY_ENTER) {
            std::string hex = inspector_.color_hex_buffer;
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
                    commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_r, r);
                    commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_g, g);
                    commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_b, b);
                    rgb_to_hsv(r, g, b, inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v);
                }
            }
            inspector_.color_editing_hex = false;
        } else if (key == GLFW_KEY_ESCAPE) {
            inspector_.color_editing_hex = false;
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(inspector_.color_hex_buffer, text_edit_);
        }
        return true;
    }

    if (inspector_.color_editing_rgb >= 0) {
        if (key == GLFW_KEY_ENTER || key == GLFW_KEY_TAB) {
            int val = inspector_.color_rgb_buffer.empty() ? 0 : std::atoi(inspector_.color_rgb_buffer.c_str());
            val = std::max(0, std::min(255, val));
            float fval = val / 255.0f;
            const std::string* param_names[3] = {
                &inspector_.color_popup_param_r, &inspector_.color_popup_param_g, &inspector_.color_popup_param_b
            };
            commands_.set_param(inspector_.color_popup_node_id, *param_names[inspector_.color_editing_rgb], fval);
            int next = inspector_.color_editing_rgb + ((key == GLFW_KEY_TAB) ? 1 : 99);
            const auto* ns = snap_.find_node(inspector_.color_popup_node_id);
            if (ns) {
                auto ri = ns->param_indices.find(inspector_.color_popup_param_r);
                auto gi = ns->param_indices.find(inspector_.color_popup_param_g);
                auto bi = ns->param_indices.find(inspector_.color_popup_param_b);
                if (ri != ns->param_indices.end() && gi != ns->param_indices.end() &&
                    bi != ns->param_indices.end()) {
                    float r = (inspector_.color_editing_rgb == 0) ? fval : ns->param_values[ri->second];
                    float g = (inspector_.color_editing_rgb == 1) ? fval : ns->param_values[gi->second];
                    float b = (inspector_.color_editing_rgb == 2) ? fval : ns->param_values[bi->second];
                    rgb_to_hsv(r, g, b, inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v);
                }
            }
            if (key == GLFW_KEY_TAB && next <= 2) {
                inspector_.color_editing_rgb = next;
                if (ns) {
                    auto it = ns->param_indices.find(*param_names[next]);
                    if (it != ns->param_indices.end()) {
                        int v = static_cast<int>(ns->param_values[it->second] * 255.0f + 0.5f);
                        inspector_.color_rgb_buffer = std::to_string(v);
                        text_edit_.reset(static_cast<int>(inspector_.color_rgb_buffer.size()));
                    }
                }
            } else {
                inspector_.color_editing_rgb = -1;
            }
        } else if (key == GLFW_KEY_ESCAPE) {
            inspector_.color_editing_rgb = -1;
        } else if (key == GLFW_KEY_BACKSPACE) {
            text_edit_backspace(inspector_.color_rgb_buffer, text_edit_);
        }
        return true;
    }

    return false;
}

bool NodeGraphUI::handle_param_picker_mode_key(int key) {
    if (!inspector_.param_picker_open) return false;
    switch (key) {
        case GLFW_KEY_ESCAPE:
            inspector_.param_picker_open = false;
            break;
        case GLFW_KEY_UP:
            if (inspector_.param_picker_sel > 0) {
                inspector_.param_picker_sel--;
                if (inspector_.param_picker_sel * kPickerItemH < inspector_.param_picker_scroll)
                    inspector_.param_picker_scroll = inspector_.param_picker_sel * kPickerItemH;
            }
            break;
        case GLFW_KEY_DOWN:
            if (inspector_.param_picker_sel < static_cast<int>(inspector_.param_picker_items.size()) - 1) {
                inspector_.param_picker_sel++;
                if ((inspector_.param_picker_sel + 1) * kPickerItemH >
                    inspector_.param_picker_scroll + kPickerMaxVisible * kPickerItemH) {
                    inspector_.param_picker_scroll = (inspector_.param_picker_sel - kPickerMaxVisible + 1) * kPickerItemH;
                }
            }
            break;
        case GLFW_KEY_ENTER:
            if (!inspector_.param_picker_items.empty() && inspector_.param_picker_sel >= 0 &&
                inspector_.param_picker_sel < static_cast<int>(inspector_.param_picker_items.size())) {
                const std::string& selected = inspector_.param_picker_items[inspector_.param_picker_sel];
                if (inspector_.param_picker_is_output) {
                    dragging_wire_ = true;
                    wire_from_node_id_ = inspector_.param_picker_node_id;
                    wire_from_port_ = selected;
                    wire_from_is_output_ = true;
                    for (const auto& r : node_rects_) {
                        if (r.node_id == inspector_.param_picker_node_id) {
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
                    commands_.connect(inspector_.param_picker_wire_from_node + "/" + inspector_.param_picker_wire_from_port,
                                      inspector_.param_picker_node_id + "/" + selected);
                }
            }
            inspector_.param_picker_open = false;
            break;
    }
    return true;
}

bool NodeGraphUI::handle_dropdown_mode_key(int key) {
    if (!inspector_.dropdown_open) return false;
    if ((inspector_.dropdown_is_preset || inspector_.dropdown_is_state_preset) &&
        !inspector_.dropdown_submenu_stack.empty()) {
        auto& cur = inspector_.dropdown_submenu_stack.back();
        int count = cur.items ? static_cast<int>(cur.items->size()) : 0;
        switch (key) {
            case GLFW_KEY_ESCAPE:
                inspector_.dropdown_open = false;
                inspector_.dropdown_is_preset = false;
                inspector_.dropdown_is_state_preset = false;
                break;
            case GLFW_KEY_UP:
                cur.hovered_idx = (cur.hovered_idx > 0) ? cur.hovered_idx - 1 : count - 1;
                break;
            case GLFW_KEY_DOWN:
                cur.hovered_idx = (cur.hovered_idx < count - 1) ? cur.hovered_idx + 1 : 0;
                break;
            case GLFW_KEY_RIGHT:
            case GLFW_KEY_ENTER: {
                if (cur.hovered_idx >= 0 && cur.hovered_idx < count) {
                    const auto& node = (*cur.items)[cur.hovered_idx];
                    if (node.is_folder) {
                        float item_h = kDropdownItemH;
                        float sub_x = cur.x + cur.w - 2;
                        float sub_y = cur.y + 2 + cur.hovered_idx * item_h;
                        float sub_w = cur.w;
                        if (inspector_.dropdown_tr) {
                            for (const auto& child : node.children) {
                                float tw = inspector_.dropdown_tr->text_width(child.label.c_str()) + 24.0f;
                                if (child.is_folder) tw += 12.0f;
                                if (tw > sub_w) sub_w = tw;
                            }
                        }
                        float wf = static_cast<float>(win_w_);
                        if (sub_x + sub_w > wf) sub_x = cur.x - sub_w + 2;
                        inspector_.dropdown_submenu_stack.push_back({&node.children, 0, sub_x, sub_y, sub_w});
                    } else if (key == GLFW_KEY_ENTER) {
                        if (inspector_.dropdown_is_state_preset) {
                            if (node.full_path.empty()) {
                                commands_.remove_state_preset(inspector_.dropdown_sm_node, inspector_.dropdown_state_idx,
                                                              inspector_.dropdown_target_node);
                            } else {
                                commands_.set_state_preset(inspector_.dropdown_sm_node, inspector_.dropdown_state_idx,
                                                           inspector_.dropdown_target_node, node.full_path);
                            }
                        } else {
                            commands_.recall_preset(inspector_.dropdown_node_id, node.full_path);
                        }
                        inspector_.dropdown_open = false;
                        inspector_.dropdown_is_preset = false;
                        inspector_.dropdown_is_state_preset = false;
                    }
                }
                break;
            }
            case GLFW_KEY_LEFT:
                if (inspector_.dropdown_submenu_stack.size() > 1) {
                    inspector_.dropdown_submenu_stack.pop_back();
                }
                break;
        }
    } else {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                inspector_.dropdown_open = false;
                inspector_.dropdown_is_preset = false;
                inspector_.dropdown_is_state_preset = false;
                break;
            case GLFW_KEY_UP:
                if (inspector_.dropdown_sel > 0) inspector_.dropdown_sel--;
                break;
            case GLFW_KEY_DOWN:
                if (inspector_.dropdown_sel < static_cast<int>(inspector_.dropdown_labels.size()) - 1)
                    inspector_.dropdown_sel++;
                break;
            case GLFW_KEY_ENTER:
                commands_.set_param(inspector_.dropdown_node_id, inspector_.dropdown_param_name,
                                    static_cast<float>(inspector_.dropdown_sel));
                inspector_.dropdown_open = false;
                inspector_.dropdown_is_preset = false;
                inspector_.dropdown_is_state_preset = false;
                break;
        }
    }
    return true;
}

bool NodeGraphUI::handle_graph_global_key(int key, int action, int mods, bool mod_key) {
    if (chooser_open_) return false;

    if (action == GLFW_PRESS && mod_key && key == GLFW_KEY_V) {
        const char* clip = glfwGetClipboardString(nullptr);
        ActiveTextField atf = resolve_active_text_field();
        if (atf.buf && clip) {
            std::string paste_text;
            for (const char* p = clip; *p; ++p) {
                char c = *p;
                if (atf.lowercase && std::isupper(static_cast<unsigned char>(c)))
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                paste_text += c;
            }
            text_edit_insert(*atf.buf, text_edit_, paste_text, atf.filter, atf.max_len);
            cursor_blink_time_ = 0.0f;
            dialogs_.on_char_post_insert();
            if (chooser_open_) rebuild_chooser_items();
            return true;
        }
    }

    if (action == GLFW_PRESS && mod_key) {
        if (key == GLFW_KEY_C) {
            copy_selected_nodes();
            return true;
        }
        if (key == GLFW_KEY_V) {
            paste_copied_nodes();
            return true;
        }
    }

    if (key == GLFW_KEY_TAB && action == GLFW_PRESS && snap_valid_ && !snap_.operator_types.empty()) {
        if (mouse_.x < graph_right()) {
            chooser_cursor_gx_ = sx_to_gx(mouse_.x);
            chooser_cursor_gy_ = sy_to_gy(mouse_.y);
            chooser_filter_.clear();
            text_edit_.reset(0);
            chooser_sel_ = 0;
            chooser_scroll_ = 0;
            if (dragging_wire_) {
                wire_connect_node_id_ = wire_from_node_id_;
                wire_connect_port_ = wire_from_port_;
                wire_connect_from_output_ = wire_from_is_output_;
                wire_connect_type_ = resolve_port_type(snap_, wire_from_node_id_,
                                                       wire_from_port_, wire_from_is_output_);
                chooser_wire_connect_ = true;
                dragging_wire_ = false;
            } else {
                chooser_wire_connect_ = false;
            }
            rebuild_chooser_items();
            chooser_open_ = true;
            return true;
        }
    }
    if (key == GLFW_KEY_B && action == GLFW_PRESS) {
        bezier_wires_ = !bezier_wires_;
        return true;
    }
    if (key == GLFW_KEY_P && action == GLFW_PRESS && mod_key && (mods & GLFW_MOD_SHIFT)) {
        toggle_package_browser();
        return true;
    }
    if (key == GLFW_KEY_P && action == GLFW_PRESS) {
        show_param_wires_ = !show_param_wires_;
        return true;
    }
    if (key == GLFW_KEY_V && action == GLFW_PRESS && !mod_key) {
        toggle_session_grid();
        return true;
    }
    if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        toggle_midi_map_mode();
        return true;
    }
    if (key == GLFW_KEY_S && action == GLFW_PRESS && !mod_key) {
        if (selected_node_ids_.size() == 1) {
            const auto& sel_id = *selected_node_ids_.begin();
            bool is_soloed = (!snap_.solo_node_id.empty() && snap_.solo_node_id == sel_id);
            commands_.set_solo(is_soloed ? "" : sel_id);
        }
        return true;
    }
    if ((key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) && action == GLFW_PRESS) {
        if (!selected_sticky_id_.empty() && !editing_sticky_) {
            commands_.remove_sticky_note(selected_sticky_id_);
            selected_sticky_id_.clear();
        } else {
            delete_selected();
        }
        return true;
    }

    return false;
}

// -----------------------------------------------------------------------
// Keyboard input
// -----------------------------------------------------------------------
void NodeGraphUI::on_key(int key, int action, int mods) {
    if (inspector_.custom_inspector_wants_keyboard) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT || action == GLFW_RELEASE) {
            inspector_.insp_key_events.push_back({key, action, mods});
        }
        return;
    }

    if (build_console_panel_.handle_key(nullptr, key, action, mods))
        return;

    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;
    const bool mod_key = (mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)) != 0;
    const bool shift = (mods & GLFW_MOD_SHIFT) != 0;

    ActiveTextField atf = resolve_active_text_field();
    if (atf.buf) {
        int len = static_cast<int>(atf.buf->size());
        text_edit_.clamp(len);

        if (key == GLFW_KEY_LEFT && !mod_key) {
            text_edit_move_left(text_edit_, shift);
            cursor_blink_time_ = 0.0f;
            return;
        }
        if (key == GLFW_KEY_RIGHT && !mod_key) {
            text_edit_move_right(text_edit_, len, shift);
            cursor_blink_time_ = 0.0f;
            return;
        }
        if ((key == GLFW_KEY_LEFT && mod_key) || key == GLFW_KEY_HOME) {
            text_edit_home(text_edit_, shift);
            cursor_blink_time_ = 0.0f;
            return;
        }
        if ((key == GLFW_KEY_RIGHT && mod_key) || key == GLFW_KEY_END) {
            text_edit_end(text_edit_, len, shift);
            cursor_blink_time_ = 0.0f;
            return;
        }
        if (key == GLFW_KEY_DELETE) {
            text_edit_delete_forward(*atf.buf, text_edit_);
            cursor_blink_time_ = 0.0f;
        }
        if (mod_key && key == GLFW_KEY_A) {
            text_edit_select_all(text_edit_, len);
            cursor_blink_time_ = 0.0f;
            return;
        }
        if (mod_key && key == GLFW_KEY_C) {
            std::string copied = text_edit_copy(*atf.buf, text_edit_);
            if (!copied.empty())
                glfwSetClipboardString(nullptr, copied.c_str());
            return;
        }
        if (mod_key && key == GLFW_KEY_X) {
            std::string cut = text_edit_cut(*atf.buf, text_edit_);
            if (!cut.empty())
                glfwSetClipboardString(nullptr, cut.c_str());
            cursor_blink_time_ = 0.0f;
        }
        if (action == GLFW_PRESS && mod_key && key == GLFW_KEY_V) {
            const char* clip = glfwGetClipboardString(nullptr);
            if (clip) {
                std::string paste_text;
                for (const char* p = clip; *p; ++p) {
                    char c = *p;
                    if (atf.lowercase && std::isupper(static_cast<unsigned char>(c)))
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    paste_text += c;
                }
                text_edit_insert(*atf.buf, text_edit_, paste_text, atf.filter, atf.max_len);
                cursor_blink_time_ = 0.0f;
                dialogs_.on_char_post_insert();
                if (chooser_open_) rebuild_chooser_items();
                return;
            }
        }
    }

    if (handle_sticky_edit_mode_key(key, mod_key)) return;
    if (editing_sticky_) return;
    if (dialogs_.on_key(key, action, mods, text_edit_, cursor_blink_time_)) return;
    if (dialogs_.prefs_open()) return;
    if (handle_session_mode_key(key, action, mods, mod_key)) return;
    if (handle_inspector_edit_mode_key(key)) return;
    if (handle_param_picker_mode_key(key)) return;

    if (record_dropdown_open_) {
        if (key == GLFW_KEY_ESCAPE) record_dropdown_open_ = false;
        return;
    }
    if (context_menu_open_) {
        if (key == GLFW_KEY_ESCAPE) context_menu_open_ = false;
        return;
    }
    if (handle_dropdown_mode_key(key)) return;
    if (handle_graph_global_key(key, action, mods, mod_key)) return;
    if (!chooser_open_) return;

    switch (key) {
        case GLFW_KEY_ESCAPE:
            chooser_open_ = false;
            reset_chooser_state();
            break;

        case GLFW_KEY_ENTER: {
            if (!chooser_items_.empty() && chooser_sel_ >= 0 &&
                chooser_sel_ < static_cast<int>(chooser_items_.size())) {
                confirm_chooser_selection_idx(chooser_sel_);
            } else {
                reset_chooser_state();
            }
            break;
        }

        case GLFW_KEY_UP:
            if (chooser_sel_ > 0) {
                chooser_sel_--;
                if (chooser_sel_ * kChooserItemH < chooser_scroll_)
                    chooser_scroll_ = chooser_sel_ * kChooserItemH;
            }
            break;

        case GLFW_KEY_DOWN:
            if (chooser_sel_ < static_cast<int>(chooser_items_.size()) - 1) {
                chooser_sel_++;
                if ((chooser_sel_ + 1) * kChooserItemH > chooser_scroll_ + kChooserMaxVisible * kChooserItemH)
                    chooser_scroll_ = (chooser_sel_ - kChooserMaxVisible + 1) * kChooserItemH;
            }
            break;

        case GLFW_KEY_BACKSPACE:
            text_edit_backspace(chooser_filter_, text_edit_);
            rebuild_chooser_items();
            break;

        default:
            break;
    }
}

void NodeGraphUI::on_char(unsigned int codepoint) {
    // Buffer char events for custom inspector when it wants keyboard focus
    if (inspector_.custom_inspector_wants_keyboard) {
        inspector_.insp_char_events.push_back(codepoint);
        return;
    }

    ActiveTextField atf = resolve_active_text_field();
    if (!atf.buf) return;

    // For identifier filter, lowercase the input
    char ch = static_cast<char>(codepoint);
    if (atf.lowercase && std::isupper(static_cast<unsigned char>(ch)))
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    text_edit_insert(*atf.buf, text_edit_, std::string(1, ch), atf.filter, atf.max_len);
    cursor_blink_time_ = 0.0f;

    // Per-field callbacks after character insert
    dialogs_.on_char_post_insert();
    if (chooser_open_) {
        rebuild_chooser_items();
    }
}

// submit_create_operator, reset_create_env_defaults, update_create_popup moved to DialogManager

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
        bool is_sticky_ctx = (context_node_id_ == "__sticky__");
        int item_count = 1;  // "Delete Node" or "Delete Wire"
        if (context_bg_menu_)
            item_count = 2;  // "Re-layout All" + "Add Sticky Note"
        else if (is_sticky_ctx)
            item_count = 2;  // "Delete Note" + "Change Color"
        else if (!context_node_id_.empty() && context_node_has_shader_)
            item_count = 2;  // + "Clone & Edit"
        if (!context_bg_menu_ && !is_sticky_ctx && context_wire_idx_ >= 0 && context_node_id_.empty())
            item_count = 2;  // + "Insert Node"
        // Solo item for node context menus
        bool show_solo = !context_node_id_.empty() && !context_bg_menu_ && !is_sticky_ctx;
        if (show_solo) item_count++;

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
                } else if (clicked_item == 1) {
                    // "Add Sticky Note"
                    std::string id = "sticky_" + std::to_string(++sticky_note_id_counter_);
                    float gx = sx_to_gx(context_menu_x_);
                    float gy = sy_to_gy(context_menu_y_);
                    commands_.add_sticky_note(id, "", gx, gy, 200.0f, 120.0f, 0);
                    selected_sticky_id_ = id;
                    selected_node_ids_.clear();
                    selected_wire_idx_ = -1;
                    // Enter edit mode
                    editing_sticky_ = true;
                    sticky_edit_id_ = id;
                    sticky_edit_buffer_.clear();
                    text_edit_.reset(0);
                    cursor_blink_time_ = 0.0f;
                }
            } else if (context_node_id_ == "__sticky__") {
                if (clicked_item == 0) {
                    // "Delete Note"
                    commands_.remove_sticky_note(selected_sticky_id_);
                    selected_sticky_id_.clear();
                } else if (clicked_item == 1) {
                    // "Change Color" — open color picker
                    sticky_color_menu_open_ = true;
                    sticky_color_menu_id_ = selected_sticky_id_;
                    sticky_color_menu_x_ = context_menu_x_;
                    sticky_color_menu_y_ = context_menu_y_;
                }
            } else if (!context_node_id_.empty()) {
                // Build the item index map to match draw order
                int delete_idx = 0;
                int clone_idx = context_node_has_shader_ ? 1 : -1;
                int solo_idx = context_node_has_shader_ ? 2 : 1;

                if (clicked_item == delete_idx) {
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
                } else if (clicked_item == clone_idx && context_node_has_shader_) {
                    // "Clone & Edit"
                    open_clone_confirm_dialog(context_node_type_);
                } else if (clicked_item == solo_idx) {
                    // "Solo" / "Unsolo"
                    bool is_soloed = (!snap_.solo_node_id.empty() && snap_.solo_node_id == context_node_id_);
                    commands_.set_solo(is_soloed ? "" : context_node_id_);
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
                        text_edit_.reset(0);
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

    {
        float bottom_offset = session_grid_open_ ? kSessionStripH : 0.0f;
        if (build_console_panel_.contains(mouse_.x, mouse_.y, win_w_, win_h_, bottom_offset))
            return;
    }

    // Session grid right-click — context menu on card
    if (session_grid_open_ && mouse_.y >= session_strip_top()) {
        session_ctx_menu_open_ = false;
        for (const auto& cr : variation_cell_rects_) {
            if (mouse_.x >= cr.x && mouse_.x <= cr.x + cr.w &&
                mouse_.y >= cr.y && mouse_.y <= cr.y + cr.h) {
                session_ctx_menu_open_ = true;
                session_ctx_menu_x_ = mouse_.x;
                session_ctx_menu_y_ = mouse_.y;
                session_ctx_menu_idx_ = cr.idx;
                session_selected_idx_ = cr.idx;
                break;
            }
        }
        return;
    }

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
        // Check sticky notes first
        int sticky_hit = -1;
        for (int si = 0; si < static_cast<int>(sticky_note_rects_.size()); ++si) {
            const auto& sr = sticky_note_rects_[si];
            if (mouse_.x >= sr.x && mouse_.x <= sr.x + sr.w &&
                mouse_.y >= sr.y && mouse_.y <= sr.y + sr.h) {
                sticky_hit = si;
                break;
            }
        }
        if (sticky_hit >= 0) {
            // Right-click on sticky note — show Delete Note / Change Color
            context_menu_open_ = true;
            context_node_id_ = "__sticky__";  // sentinel to identify sticky context
            context_wire_idx_ = sticky_hit;    // reuse for sticky index
            context_bg_menu_ = false;
            context_menu_x_ = mouse_.x;
            context_menu_y_ = mouse_.y;
            selected_sticky_id_ = sticky_note_rects_[sticky_hit].id;
            return;
        }
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

    // Check patch panel right-click (inspector area)
    if (!context_menu_open_ && mouse_.x >= graph_right())
        handle_patch_right_click();
}

// -----------------------------------------------------------------------
// Click dispatch (replaces goto click_done pattern)
// -----------------------------------------------------------------------
void NodeGraphUI::handle_left_click() {
    if (!mouse_.left_clicked) return;
    if (handle_chooser_click()) return;
    if (handle_dropdown_click()) return;

    {
        float bottom_offset = session_grid_open_ ? kSessionStripH : 0.0f;
        if (!build_console_panel_.contains(mouse_.x, mouse_.y, win_w_, win_h_, bottom_offset))
            build_console_panel_.blur();
    }

    // Record codec dropdown click handling
    if (record_dropdown_open_) {
        static const char* codec_ids[] = { "h264", "h265", "prores4444" };
        constexpr int codec_count = 3;
        float item_h = kDropdownItemH;
        float dx = record_dropdown_x_;
        float dy = record_dropdown_y_;
        float popup_w = kPerfCodecDropW;
        for (int i = 0; i < codec_count; ++i) {
            float iy = dy + 2 + i * item_h;
            if (mouse_.x >= dx && mouse_.x <= dx + popup_w &&
                mouse_.y >= iy && mouse_.y <= iy + item_h) {
                record_codec_sel_ = i;
                record_dropdown_open_ = false;
                commands_.start_recording("", codec_ids[i], 60.0);
                mouse_.left_clicked = false;
                return;
            }
        }
        // Clicked outside dropdown — close it
        record_dropdown_open_ = false;
        mouse_.left_clicked = false;
        return;
    }

    // preset_name_popup and core_update_button clicks moved to DialogManager

    // Perf bar buttons (Record/Stop, Snapshot)
    for (const auto& btn : perf_button_rects_) {
        if (mouse_.x >= btn.x && mouse_.x <= btn.x + btn.w &&
            mouse_.y >= btn.y && mouse_.y <= btn.y + btn.h) {
            if (!btn.enabled) {
                mouse_.left_clicked = false;
                return;
            }
            if (btn.action == 0) {  // Record/Stop
                if (snap_.is_recording) {
                    commands_.stop_recording();
                } else {
                    record_dropdown_open_ = !record_dropdown_open_;
                    record_dropdown_x_ = btn.x;
                    record_dropdown_y_ = btn.y + btn.h;
                }
            } else if (btn.action == 1) {  // Snapshot
                commands_.capture_snapshot();
            } else if (btn.action == 2) {  // Undo
                commands_.undo();
            } else if (btn.action == 3) {  // Redo
                commands_.redo();
            } else if (btn.action == 4) {  // Build Console
                build_console_panel_.toggle_open();
            }
            mouse_.left_clicked = false;
            return;
        }
    }

    // MCP dot clicks (in perf bar) — open setup dialog
    for (const auto& dr : mcp_dot_rects_) {
        if (mouse_.x >= dr.x && mouse_.x <= dr.x + dr.w &&
            mouse_.y >= dr.y && mouse_.y <= dr.y + dr.h) {
            dialogs_.open_mcp_setup();
            mouse_.left_clicked = false;
            return;
        }
    }

    {
        float bottom_offset = session_grid_open_ ? kSessionStripH : 0.0f;
        if (build_console_panel_.handle_left_press(mouse_.x, mouse_.y, win_w_, win_h_, bottom_offset)) {
            mouse_.left_clicked = false;
            return;
        }
    }

    // Session grid click handling
    if (session_grid_open_ && mouse_.y >= session_strip_top()) {
        // Close context menu if clicking elsewhere
        if (session_ctx_menu_open_) {
            // Check context menu item clicks
            for (const auto& cr : session_ctx_menu_rects_) {
                if (mouse_.x >= cr.x && mouse_.x <= cr.x + cr.w &&
                    mouse_.y >= cr.y && mouse_.y <= cr.y + cr.h) {
                    int target = session_ctx_menu_idx_;
                    if (target >= 0 && target < static_cast<int>(snap_.variations.size())) {
                        const auto& vname = snap_.variations[target].name;
                        if (cr.action == 0) {
                            // Rename
                            session_editing_name_ = true;
                            session_edit_idx_ = target;
                            session_edit_buffer_ = vname;
                            text_edit_.select_all(static_cast<int>(session_edit_buffer_.size()));
                        } else if (cr.action == 1) {
                            // Duplicate
                            std::string new_name = vname + " copy";
                            commands_.duplicate_variation(vname, new_name);
                        } else if (cr.action == 2) {
                            // Delete
                            commands_.remove_variation(vname);
                            if (session_selected_idx_ == target)
                                session_selected_idx_ = -1;
                        } else if (cr.action == 3) {
                            // Branch From
                            std::string branch_name = vname + " branch";
                            commands_.duplicate_variation(vname, branch_name);
                        }
                    }
                    session_ctx_menu_open_ = false;
                    mouse_.left_clicked = false;
                    return;
                }
            }
            session_ctx_menu_open_ = false;
            mouse_.left_clicked = false;
            return;
        }

        // Check variation cells
        for (const auto& cr : variation_cell_rects_) {
            if (mouse_.x >= cr.x && mouse_.x <= cr.x + cr.w &&
                mouse_.y >= cr.y && mouse_.y <= cr.y + cr.h) {
                // Double-click to rename
                double now = glfwGetTime();
                if (last_variation_click_idx_ == cr.idx &&
                    (now - last_variation_click_time_) < 0.4) {
                    // Double-click — enter rename mode
                    session_editing_name_ = true;
                    session_edit_idx_ = cr.idx;
                    session_edit_buffer_ = snap_.variations[cr.idx].name;
                    text_edit_.select_all(static_cast<int>(session_edit_buffer_.size()));
                    last_variation_click_idx_ = -1;
                } else {
                    // Single click — select and recall/queue
                    session_selected_idx_ = cr.idx;
                    if (session_quantize_mode_ > 0) {
                        static const char* q_modes[] = { "instant", "beat", "bar", "4bar" };
                        commands_.queue_variation(snap_.variations[cr.idx].name,
                                                  q_modes[session_quantize_mode_]);
                    } else {
                        commands_.recall_variation_idx(cr.idx);
                    }
                    // Begin potential drag
                    session_drag_idx_ = cr.idx;
                    session_drag_start_x_ = mouse_.x;
                    session_drag_start_y_ = mouse_.y;
                    session_drag_active_ = false;

                    last_variation_click_idx_ = cr.idx;
                    last_variation_click_time_ = now;
                }
                mouse_.left_clicked = false;
                return;
            }
        }
        // Check buttons
        for (const auto& br : session_button_rects_) {
            if (mouse_.x >= br.x && mouse_.x <= br.x + br.w &&
                mouse_.y >= br.y && mouse_.y <= br.y + br.h) {
                if (br.action == 0) {
                    // + Save New
                    std::string name = "Var " + std::to_string(snap_.variations.size() + 1);
                    commands_.save_variation(name);
                } else if (br.action == 1) {
                    // Update (overwrite active variation)
                    if (snap_.active_variation >= 0 &&
                        snap_.active_variation < static_cast<int>(snap_.variations.size())) {
                        commands_.update_variation(
                            snap_.variations[snap_.active_variation].name);
                    }
                } else if (br.action >= 2 && br.action <= 5) {
                    // Quantize mode buttons
                    session_quantize_mode_ = br.action - 2;
                } else if (br.action == 6) {
                    // Branch (duplicate active variation then recall the copy)
                    if (snap_.active_variation >= 0 &&
                        snap_.active_variation < static_cast<int>(snap_.variations.size())) {
                        const auto& active_name = snap_.variations[snap_.active_variation].name;
                        std::string branch_name = active_name + " branch";
                        commands_.duplicate_variation(active_name, branch_name);
                        commands_.recall_variation(branch_name);
                    }
                }
                mouse_.left_clicked = false;
                return;
            }
        }
        // Clicked in session strip but not on a cell/button — deselect
        session_selected_idx_ = -1;
        mouse_.left_clicked = false;
        return;
    }

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
        int idx = static_cast<int>(std::floor((mouse_.y - items_y + chooser_scroll_) / kChooserItemH));
        if (idx >= 0 && idx < static_cast<int>(chooser_items_.size())) {
            confirm_chooser_selection_idx(idx);
            mouse_.left_clicked = false;
            mouse_.left_released = false;
            return true;
        }
    }
    reset_chooser_state();
    mouse_.left_clicked = false;
    mouse_.left_released = false;
    return true;
}

bool NodeGraphUI::handle_dropdown_click() {
    if (!inspector_.dropdown_open || inspector_.dropdown_labels.empty()) return false;

    float item_h = kDropdownItemH;

    // Preset dropdowns: use hierarchical submenu hit-testing
    if ((inspector_.dropdown_is_preset || inspector_.dropdown_is_state_preset) && !inspector_.dropdown_submenu_stack.empty()) {
        // Hit-test levels deepest-first (deepest is drawn on top)
        for (int lvl = static_cast<int>(inspector_.dropdown_submenu_stack.size()) - 1; lvl >= 0; --lvl) {
            const auto& level = inspector_.dropdown_submenu_stack[lvl];
            if (!level.items || level.items->empty()) continue;
            int count = static_cast<int>(level.items->size());
            float popup_h = count * item_h + 4;
            if (mouse_.x >= level.x && mouse_.x <= level.x + level.w &&
                mouse_.y >= level.y && mouse_.y <= level.y + popup_h) {
                int idx = static_cast<int>((mouse_.y - level.y - 2) / item_h);
                if (idx >= 0 && idx < count) {
                    const auto& node = (*level.items)[idx];
                    if (node.is_folder) {
                        // Open this folder's submenu
                        inspector_.dropdown_submenu_stack.resize(lvl + 1);
                        float sub_x = level.x + level.w - 2;
                        float sub_y = level.y + 2 + idx * item_h;
                        // Compute width from longest child label
                        float sub_w = level.w;
                        for (const auto& child : node.children) {
                            float tw = inspector_.dropdown_tr ? inspector_.dropdown_tr->text_width(child.label.c_str()) + 24.0f : level.w;
                            if (child.is_folder) tw += 12.0f;
                            if (tw > sub_w) sub_w = tw;
                        }
                        // Flip to left if off-screen
                        float wf = static_cast<float>(win_w_);
                        if (sub_x + sub_w > wf) sub_x = level.x - sub_w + 2;
                        inspector_.dropdown_submenu_stack.push_back({&node.children, -1, sub_x, sub_y, sub_w});
                        inspector_.dropdown_submenu_stack[lvl].hovered_idx = idx;
                    } else {
                        // Leaf: select preset
                        if (inspector_.dropdown_is_state_preset) {
                            if (node.full_path.empty()) {
                                commands_.remove_state_preset(inspector_.dropdown_sm_node, inspector_.dropdown_state_idx,
                                                              inspector_.dropdown_target_node);
                            } else {
                                commands_.set_state_preset(inspector_.dropdown_sm_node, inspector_.dropdown_state_idx,
                                                          inspector_.dropdown_target_node, node.full_path);
                            }
                        } else {
                            commands_.recall_preset(inspector_.dropdown_node_id, node.full_path);
                        }
                        inspector_.dropdown_is_preset = false;
                        inspector_.dropdown_is_state_preset = false;
                        inspector_.dropdown_open = false;
                    }
                }
                mouse_.left_clicked = false;
                mouse_.left_released = false;
                return true;
            }
        }
        // Click outside all levels: close
        inspector_.dropdown_open = false;
        inspector_.dropdown_is_preset = false;
        inspector_.dropdown_is_state_preset = false;
        return false;
    }

    // Non-preset flat dropdown (param selectors, etc.)
    float popup_h = inspector_.dropdown_labels.size() * item_h + 4;
    if (mouse_.x >= inspector_.dropdown_x && mouse_.x <= inspector_.dropdown_x + inspector_.dropdown_w &&
        mouse_.y >= inspector_.dropdown_y && mouse_.y <= inspector_.dropdown_y + popup_h) {
        int idx = static_cast<int>((mouse_.y - inspector_.dropdown_y - 2) / item_h);
        if (idx >= 0 && idx < static_cast<int>(inspector_.dropdown_labels.size())) {
            commands_.set_param(inspector_.dropdown_node_id, inspector_.dropdown_param_name,
                           static_cast<float>(idx));
        }
        inspector_.dropdown_is_preset = false;
        inspector_.dropdown_is_state_preset = false;
        inspector_.dropdown_open = false;
        mouse_.left_clicked = false;
        mouse_.left_released = false;
        return true;
    } else {
        inspector_.dropdown_open = false;
        inspector_.dropdown_is_preset = false;
        inspector_.dropdown_is_state_preset = false;
        return false;
    }
}

bool NodeGraphUI::handle_inspector_click() {
    // --- Color popup click handling (overlays everything) ---
    if (inspector_.color_popup_open) {
        float pad = kColorPopupPad;
        float sv_size = kColorPopupSVSize;
        float hue_bar_w = kColorHueBarW;
        float gap = kColorPopupGap;
        float hex_h = kColorHexFieldH;
        float rgb_gap = kColorRGBGap;
        float rgb_h = kColorRGBFieldH;
        float popup_w = pad + sv_size + gap + hue_bar_w + pad;
        float popup_h = pad + sv_size + gap + hex_h + rgb_gap + rgb_h + pad;
        float px = inspector_.color_popup_x, py = inspector_.color_popup_y;
        float sv_x = px + pad, sv_y = py + pad;
        float hue_x = sv_x + sv_size + gap, hue_y = sv_y;
        float hex_field_y = sv_y + sv_size + gap;
        float hex_field_w = sv_size + gap + hue_bar_w;

        // Dismiss any active text edits when clicking elsewhere
        if (inspector_.color_editing_hex) inspector_.color_editing_hex = false;
        if (inspector_.color_editing_rgb >= 0) inspector_.color_editing_rgb = -1;

        // Click in SV square
        if (mouse_.x >= sv_x && mouse_.x < sv_x + sv_size &&
            mouse_.y >= sv_y && mouse_.y < sv_y + sv_size) {
            inspector_.color_dragging_sv = true;
            inspector_.color_popup_s = std::max(0.0f, std::min(1.0f, (mouse_.x - sv_x) / sv_size));
            inspector_.color_popup_v = std::max(0.0f, std::min(1.0f, 1.0f - (mouse_.y - sv_y) / sv_size));
            float r, g, b;
            hsv_to_rgb(inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v, r, g, b);
            commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_r, r);
            commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_g, g);
            commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_b, b);
            return true;
        }

        // Click in hue bar
        if (mouse_.x >= hue_x && mouse_.x < hue_x + hue_bar_w &&
            mouse_.y >= hue_y && mouse_.y < hue_y + sv_size) {
            inspector_.color_dragging_hue = true;
            inspector_.color_popup_h = std::max(0.0f, std::min(360.0f,
                (mouse_.y - hue_y) / sv_size * 360.0f));
            float r, g, b;
            hsv_to_rgb(inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v, r, g, b);
            commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_r, r);
            commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_g, g);
            commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_b, b);
            return true;
        }

        // Click in hex field
        if (mouse_.x >= sv_x && mouse_.x < sv_x + hex_field_w &&
            mouse_.y >= hex_field_y && mouse_.y < hex_field_y + hex_h) {
            inspector_.color_editing_hex = true;
            // Pre-fill with current hex value
            float cr, cg, cb;
            hsv_to_rgb(inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v, cr, cg, cb);
            char hex[8];
            rgb_to_hex(cr, cg, cb, hex, sizeof(hex));
            inspector_.color_hex_buffer = hex;
            text_edit_.reset(static_cast<int>(inspector_.color_hex_buffer.size()));
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
                    inspector_.color_editing_rgb = ch;
                    // Pre-fill with current channel value
                    const std::string* param_names[3] = {
                        &inspector_.color_popup_param_r, &inspector_.color_popup_param_g, &inspector_.color_popup_param_b
                    };
                    const auto* ns = snap_.find_node(inspector_.color_popup_node_id);
                    if (ns) {
                        auto it = ns->param_indices.find(*param_names[ch]);
                        if (it != ns->param_indices.end()) {
                            int v = static_cast<int>(ns->param_values[it->second] * 255.0f + 0.5f);
                            inspector_.color_rgb_buffer = std::to_string(v);
                            text_edit_.reset(static_cast<int>(inspector_.color_rgb_buffer.size()));
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
        inspector_.color_popup_open = false;
        inspector_.color_dragging_sv = false;
        inspector_.color_dragging_hue = false;
        inspector_.color_editing_hex = false;
        inspector_.color_editing_rgb = -1;
        return true;
    }

    if (mouse_.x < graph_right() || mouse_.y >= static_cast<float>(win_h_)) {
        std::fprintf(stderr, "[UI DEBUG] inspector click rejected: mx=%.0f graph_right=%.0f my=%.0f win_h=%d sliders=%d bools=%d\n",
                     mouse_.x, graph_right(), mouse_.y, win_h_,
                     static_cast<int>(inspector_.slider_rects.size()), static_cast<int>(inspector_.bool_rects.size()));
        return false;
    }

    // --- MIDI map mode click guard ---
    if (midi_map_mode_) {
        if (mouse_.y < kPerfBarH) return true;

        // Confirm any active midi range edit
        if (inspector_.editing_midi_range) confirm_midi_range_edit();

        // Hit-test remove rects
        int rmi = hit_test_rect(inspector_.midi_remove_rects, mouse_.x, mouse_.y);
        if (rmi >= 0) {
            const auto& rr = inspector_.midi_remove_rects[rmi];
            commands_.remove_midi_mapping(rr.node_id, rr.param_name);
            return true;
        }

        // Hit-test range rects (min/max edit fields)
        int rri = hit_test_rect(inspector_.midi_range_rects, mouse_.x, mouse_.y);
        if (rri >= 0) {
            const auto& mr = inspector_.midi_range_rects[rri];
            inspector_.editing_midi_range = true;
            inspector_.midi_range_node_id = mr.node_id;
            inspector_.midi_range_param_name = mr.param_name;
            inspector_.midi_range_editing_min = mr.is_min;
            // Pre-fill with current value
            const auto* mm = snap_.find_midi_mapping(mr.node_id, mr.param_name);
            if (mm) {
                inspector_.edit_buffer = format_float(mr.is_min ? mm->range_min : mm->range_max, 2);
                text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
            } else {
                inspector_.edit_buffer.clear();
                text_edit_.reset(0);
            }
            return true;
        }

        // Hit-test any slider/value_text/bool/dropdown rect -> set waiting target
        auto check_param_rect = [&](const std::vector<InspectorController::InspectorRect>& rects) -> bool {
            int idx = hit_test_rect(rects, mouse_.x, mouse_.y);
            if (idx >= 0) {
                midi_map_waiting_ = true;
                midi_map_node_id_ = rects[idx].node_id;
                midi_map_param_name_ = rects[idx].param_name;
                return true;
            }
            return false;
        };
        if (check_param_rect(inspector_.slider_rects)) return true;
        if (check_param_rect(inspector_.value_text_rects)) return true;
        if (check_param_rect(inspector_.bool_rects)) return true;
        if (check_param_rect(inspector_.dropdown_rects)) return true;
        return true; // Consume all inspector clicks in MIDI map mode
    }

    // Scrollbar hit test — check the scrollbar track area
    if (inspector_.insp_content_h > static_cast<float>(win_h_) - kPerfBarH) {
        float insp_x = inspector_x();
        float track_x = insp_x + kInspectorW - kInspScrollbarW - 2.0f;
        float viewport_top = kPerfBarH;
        float viewport_h = static_cast<float>(win_h_) - viewport_top;
        float track_y = viewport_top + 2.0f;
        float track_h = viewport_h - 4.0f;

        if (mouse_.x >= track_x && mouse_.x <= track_x + kInspScrollbarW + 2.0f &&
            mouse_.y >= track_y && mouse_.y <= track_y + track_h) {
            inspector_.insp_scrollbar_dragging = true;
            inspector_.insp_sb_drag_start_y = mouse_.y;
            inspector_.insp_sb_drag_start_scroll = inspector_.insp_scroll_y;
            return true;
        }
    }

    // Group header collapse/expand
    for (const auto& gh : inspector_.group_header_rects) {
        if (mouse_.x >= gh.x && mouse_.x < gh.x + gh.w &&
            mouse_.y >= gh.y && mouse_.y < gh.y + gh.h) {
            toggle_group_collapsed(gh.type_name, gh.group_name);
            return true;
        }
    }

    // Reject clicks above perf bar (clipped-off content)
    if (mouse_.y < kPerfBarH) return true;

    // Lock badge click: cycle (none) → W → P → WP → (none)
    {
        int li = hit_test_rect(inspector_.lock_badge_rects, mouse_.x, mouse_.y);
        if (li >= 0) {
            const auto& lr = inspector_.lock_badge_rects[li];
            const auto* ns = snap_.find_node(lr.node_id);
            if (ns) {
                auto pi_it = ns->param_indices.find(lr.param_name);
                if (pi_it != ns->param_indices.end()) {
                    uint8_t cur = (pi_it->second < ns->param_lock_flags.size())
                                  ? ns->param_lock_flags[pi_it->second] : 0;
                    uint8_t next = (cur + 1) & 0x03;  // cycle 0→1→2→3→0
                    commands_.set_param_lock(lr.node_id, lr.param_name, next);
                }
            }
            return true;
        }
    }

    // Confirm any active text edit when clicking in inspector
    if (inspector_.editing_param) confirm_param_edit();
    if (inspector_.editing_resolution) confirm_resolution_edit();

    // Check preset dropdown click
    {
        int pi = hit_test_rect(inspector_.preset_dropdown_rects, mouse_.x, mouse_.y);
        if (pi >= 0) {
            const auto& r = inspector_.preset_dropdown_rects[pi];
            inspector_.dropdown_node_id = r.node_id;
            inspector_.dropdown_param_name.clear();
            inspector_.dropdown_x = r.x;
            inspector_.dropdown_y = r.y + r.h;
            inspector_.dropdown_w = r.w;
            inspector_.dropdown_labels.clear();
            inspector_.dropdown_factory_count = 0;
            const auto* ns = snap_.find_node(r.node_id);
            if (ns) {
                // Factory presets first (read-only)
                for (const auto& name : ns->factory_preset_names)
                    inspector_.dropdown_labels.push_back(name);
                inspector_.dropdown_factory_count = static_cast<int>(ns->factory_preset_names.size());

                // Then user presets
                for (const auto& name : ns->preset_names)
                    inspector_.dropdown_labels.push_back(name);

                // Find active preset selection
                inspector_.dropdown_sel = -1;
                for (int i = 0; i < static_cast<int>(inspector_.dropdown_labels.size()); i++) {
                    if (inspector_.dropdown_labels[i] == ns->active_preset) { inspector_.dropdown_sel = i; break; }
                }

                // Build hierarchical menu tree for submenu rendering
                inspector_.dropdown_menu_tree = ui::build_preset_menu_tree(
                    ns->factory_preset_names, ns->preset_names);
                inspector_.dropdown_submenu_stack.clear();
                inspector_.dropdown_submenu_stack.push_back({&inspector_.dropdown_menu_tree, -1,
                    inspector_.dropdown_x, inspector_.dropdown_y, inspector_.dropdown_w});
                inspector_.dropdown_hover_frames = 0;
                inspector_.dropdown_hover_target = -1;
            }
            inspector_.dropdown_is_preset = true;
            inspector_.dropdown_is_state_preset = false;
            inspector_.dropdown_open = !inspector_.dropdown_labels.empty();
            return true;
        }
    }

    // Check preset Save button click
    {
        int si = hit_test_rect(inspector_.preset_save_rects, mouse_.x, mouse_.y);
        if (si >= 0) {
            const auto& r = inspector_.preset_save_rects[si];
            const auto* ns = snap_.find_node(r.node_id);
            if (ns && !ns->active_preset.empty()) {
                commands_.save_preset(r.node_id, ns->active_preset);
            } else if (ns) {
                dialogs_.open_preset_name_popup(r.node_id);
                text_edit_.reset(0);
            }
            return true;
        }
    }

    // Check state-preset header click (collapse toggle)
    {
        int shi = hit_test_rect(inspector_.state_header_rects, mouse_.x, mouse_.y);
        if (shi >= 0) {
            auto key = "__state_preset\t" + std::to_string(inspector_.state_header_rects[shi].state_idx);
            inspector_.group_collapsed[key] = !inspector_.group_collapsed[key];
            return true;
        }
    }

    // Check state-preset dropdown click
    {
        int spi = hit_test_rect(inspector_.state_preset_rects, mouse_.x, mouse_.y);
        if (spi >= 0) {
            const auto& r = inspector_.state_preset_rects[spi];
            const auto* target = snap_.find_node(r.target_node);
            if (target && (!target->preset_names.empty() || !target->factory_preset_names.empty())) {
                inspector_.dropdown_labels.clear();
                inspector_.dropdown_factory_count = 0;
                inspector_.dropdown_labels.push_back("(none)");
                // Factory presets first
                for (const auto& pn : target->factory_preset_names)
                    inspector_.dropdown_labels.push_back(pn);
                inspector_.dropdown_factory_count = static_cast<int>(target->factory_preset_names.size());
                // Then user presets
                for (const auto& pn : target->preset_names)
                    inspector_.dropdown_labels.push_back(pn);
                // Find current mapping to set selection
                inspector_.dropdown_sel = 0;
                const auto* sm_node = snap_.find_node(r.sm_node);
                if (sm_node && r.state_idx < static_cast<int>(sm_node->state_preset_map.size())) {
                    auto mit = sm_node->state_preset_map[r.state_idx].find(r.target_node);
                    if (mit != sm_node->state_preset_map[r.state_idx].end()) {
                        for (int i = 1; i < static_cast<int>(inspector_.dropdown_labels.size()); i++) {
                            if (inspector_.dropdown_labels[i] == mit->second) { inspector_.dropdown_sel = i; break; }
                        }
                    }
                }
                inspector_.dropdown_x = r.x;
                inspector_.dropdown_y = r.y + r.h;
                inspector_.dropdown_w = r.w;
                inspector_.dropdown_open = true;
                inspector_.dropdown_is_preset = false;
                inspector_.dropdown_is_state_preset = true;
                inspector_.dropdown_sm_node = r.sm_node;
                inspector_.dropdown_state_idx = r.state_idx;
                inspector_.dropdown_target_node = r.target_node;

                // Build hierarchical menu tree for submenu rendering
                inspector_.dropdown_menu_tree = ui::build_preset_menu_tree(
                    target->factory_preset_names, target->preset_names);
                // Insert "(none)" as first entry for state-preset clearing
                inspector_.dropdown_menu_tree.insert(inspector_.dropdown_menu_tree.begin(),
                    ui::PresetMenuNode{"(none)", "", false, false, {}});
                inspector_.dropdown_submenu_stack.clear();
                inspector_.dropdown_submenu_stack.push_back({&inspector_.dropdown_menu_tree, -1,
                    inspector_.dropdown_x, inspector_.dropdown_y, inspector_.dropdown_w});
                inspector_.dropdown_hover_frames = 0;
                inspector_.dropdown_hover_target = -1;
            }
            return true;
        }
    }

    // Check resolution rect click-to-edit
    int ri = hit_test_rect(inspector_.resolution_rects, mouse_.x, mouse_.y);
    if (ri >= 0) {
        const auto& rr = inspector_.resolution_rects[ri];
        inspector_.editing_resolution = true;
        inspector_.edit_res_node_id = rr.node_id;
        inspector_.edit_res_is_width = rr.is_width;
        const auto* ns = snap_.find_node(rr.node_id);
        if (ns) {
            inspector_.edit_buffer = format_uint(rr.is_width ? ns->gpu_tex_width : ns->gpu_tex_height);
            text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
        }
        return true;
    }

    // Check value text click-to-edit
    int vt = hit_test_rect(inspector_.value_text_rects, mouse_.x, mouse_.y);
    if (vt >= 0) {
        inspector_.editing_param = true;
        inspector_.edit_node_id = inspector_.value_text_rects[vt].node_id;
        inspector_.edit_param_name = inspector_.value_text_rects[vt].param_name;
        const auto* ns = snap_.find_node(inspector_.edit_node_id);
        if (ns && ns->op_info) {
            auto it = ns->param_indices.find(inspector_.edit_param_name);
            if (it != ns->param_indices.end()) {
                for (const auto& pd : ns->op_info->params) {
                    if (pd.name != inspector_.edit_param_name) continue;
                    if (pd.type == VIVID_PARAM_TEXT) {
                        auto fit = ns->file_param_values.find(inspector_.edit_param_name);
                        inspector_.edit_buffer = (fit != ns->file_param_values.end()) ? fit->second : "";
                    } else if (pd.type == VIVID_PARAM_INT) {
                        inspector_.edit_buffer = format_int(static_cast<int>(ns->param_values[it->second]));
                    } else {
                        inspector_.edit_buffer = format_float(ns->param_values[it->second], 2);
                    }
                    text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
                    break;
                }
            }
        }
        return true;
    }

    // Check dropdown click
    int di = hit_test_rect(inspector_.dropdown_rects, mouse_.x, mouse_.y);
    if (di >= 0) {
        const auto& dr = inspector_.dropdown_rects[di];
        inspector_.dropdown_node_id = dr.node_id;
        inspector_.dropdown_param_name = dr.param_name;
        inspector_.dropdown_x = dr.x;
        inspector_.dropdown_y = dr.y + dr.h;
        inspector_.dropdown_w = dr.w;
        inspector_.dropdown_labels.clear();
        inspector_.dropdown_factory_count = 0;
        const auto* ns = snap_.find_node(dr.node_id);
        if (ns && ns->op_info) {
            for (const auto& pd : ns->op_info->params) {
                if (pd.name != dr.param_name) continue;
                for (const auto& label : pd.choice_labels)
                    inspector_.dropdown_labels.push_back(label);
                auto it = ns->param_indices.find(dr.param_name);
                if (it != ns->param_indices.end())
                    inspector_.dropdown_sel = static_cast<int>(ns->param_values[it->second]);
                break;
            }
        }
        inspector_.dropdown_is_preset = false;
        inspector_.dropdown_is_state_preset = false;
        inspector_.dropdown_open = !inspector_.dropdown_labels.empty();
        return true;
    }

    // Check XY pad
    int xyi = hit_test_rect(inspector_.xy_pad_rects, mouse_.x, mouse_.y);
    if (xyi >= 0) {
        inspector_.active_xy_pad_idx = xyi;
        inspector_.active_xy_node_id = inspector_.xy_pad_rects[xyi].node_id;
        inspector_.active_xy_param_x = inspector_.xy_pad_rects[xyi].param_x;
        inspector_.active_xy_param_y = inspector_.xy_pad_rects[xyi].param_y;
        return true;
    }

    // Check color swatch
    int ci = hit_test_rect(inspector_.color_swatch_rects, mouse_.x, mouse_.y);
    if (ci >= 0) {
        const auto& cs = inspector_.color_swatch_rects[ci];
        // Toggle popup
        if (inspector_.color_popup_open && inspector_.color_popup_node_id == cs.node_id &&
            inspector_.color_popup_param_r == cs.param_r) {
            inspector_.color_popup_open = false;
            inspector_.color_editing_rgb = -1;
        } else {
            inspector_.color_popup_open = true;
            inspector_.color_popup_node_id = cs.node_id;
            inspector_.color_popup_param_r = cs.param_r;
            inspector_.color_popup_param_g = cs.param_g;
            inspector_.color_popup_param_b = cs.param_b;
            // Position popup adjacent to swatch
            inspector_.color_popup_x = cs.x;
            inspector_.color_popup_y = cs.y + cs.h + 4;
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
                    rgb_to_hsv(r, g, b, inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v);
                }
            }
        }
        return true;
    }

    // Check slider
    int si = hit_test_rect(inspector_.slider_rects, mouse_.x, mouse_.y);
    if (si >= 0) {
        inspector_.active_slider_idx = si;
        inspector_.active_slider_node_id = inspector_.slider_rects[si].node_id;
        inspector_.active_slider_param_name = inspector_.slider_rects[si].param_name;
        std::fprintf(stderr, "[UI DEBUG] slider click: idx=%d node=%s param=%s\n",
                     si, inspector_.active_slider_node_id.c_str(), inspector_.active_slider_param_name.c_str());
        return true;
    }

    // Check bool toggle
    int bi = hit_test_rect(inspector_.bool_rects, mouse_.x, mouse_.y);
    if (bi >= 0) {
        const auto& br = inspector_.bool_rects[bi];
        std::fprintf(stderr, "[UI DEBUG] bool click: node=%s param=%s\n",
                     br.node_id.c_str(), br.param_name.c_str());
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
    int fi = hit_test_rect(inspector_.file_button_rects, mouse_.x, mouse_.y);
    if (fi >= 0) {
        const auto& fr = inspector_.file_button_rects[fi];
        std::string path = vivid::ui::open_file_dialog();
        if (!path.empty()) {
            commands_.set_string_param(fr.node_id, fr.param_name, path);
        }
        return true;
    }

    // Check wire remap text field click
    for (size_t i = 0; i < inspector_.wire_remap_rects.size(); ++i) {
        const auto& wr = inspector_.wire_remap_rects[i];
        if (mouse_.x >= wr.x && mouse_.x <= wr.x + wr.w &&
            mouse_.y >= wr.y && mouse_.y <= wr.y + wr.h) {
            inspector_.editing_wire_remap = true;
            inspector_.edit_wire_remap_field = wr.field;
            // Pre-fill buffer with current value
            if (selected_wire_idx_ >= 0 &&
                selected_wire_idx_ < static_cast<int>(snap_.connections.size())) {
                const auto& c = snap_.connections[selected_wire_idx_];
                float vals[4] = { c.from_min, c.from_max, c.to_min, c.to_max };
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.3g", vals[wr.field]);
                inspector_.edit_buffer = buf;
            } else {
                inspector_.edit_buffer = "0";
            }
            text_edit_.reset(static_cast<int>(inspector_.edit_buffer.size()));
            return true;
        }
    }

    // Check wire clamp checkbox click
    for (const auto& cr : inspector_.wire_clamp_rects) {
        if (mouse_.x >= cr.x && mouse_.x <= cr.x + cr.w &&
            mouse_.y >= cr.y && mouse_.y <= cr.y + cr.h) {
            if (selected_wire_idx_ >= 0 &&
                selected_wire_idx_ < static_cast<int>(snap_.connections.size())) {
                const auto& c = snap_.connections[selected_wire_idx_];
                std::string from_addr = c.from_node + "/" + c.from_port;
                std::string to_addr   = c.to_node   + "/" + c.to_port;
                commands_.set_connection_remap(from_addr, to_addr,
                    c.from_min, c.from_max, c.to_min, c.to_max, !c.clamp);
            }
            return true;
        }
    }

    // Check patch panel jack click (start wire drag)
    if (handle_patch_click()) return true;

    return true;  // Click was in inspector area, consume it
}

void NodeGraphUI::handle_graph_click() {
    if (mouse_.x >= graph_right() || mouse_.y >= static_cast<float>(win_h_))
        return;

    // Clicking in graph area confirms any active text edit
    if (inspector_.editing_param) confirm_param_edit();
    if (inspector_.editing_resolution) confirm_resolution_edit();
    inspector_.editing_wire_remap = false;

    // Check expand/collapse affordance rows before port hit testing
    for (const auto& ar : expand_affordance_rects_) {
        if (mouse_.x >= ar.x && mouse_.x <= ar.x + ar.w &&
            mouse_.y >= ar.y && mouse_.y <= ar.y + ar.h) {
            if (outputs_expanded_.count(ar.node_id))
                outputs_expanded_.erase(ar.node_id);
            else
                outputs_expanded_.insert(ar.node_id);
            // Recompute this node's height and port positions immediately
            for (auto& rect : node_rects_) {
                if (rect.node_id != ar.node_id) continue;
                const auto* ns = snap_.find_node(ar.node_id);
                if (!ns) break;
                bool has_ct = custom_thumb_nodes_.count(ar.node_id) > 0;
                float body_h = node_body_height(rect.is_gpu, rect.active_cadence, has_ct);
                uint32_t n_inputs  = count_visible_input_ports(*ns, show_param_wires_);
                uint32_t n_outputs = count_visible_output_ports(*ns, show_param_wires_);
                uint32_t port_rows = std::max(n_inputs, n_outputs);
                rect.target_h = kAccentBarH + body_h + kNodePadY + kLineH * 2
                                + port_rows * kLineH + kNodePadY;
                recompute_ports(rect, *ns);
                break;
            }
            return;
        }
    }

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
                    inspector_.param_picker_node_id = node_id;
                    inspector_.param_picker_wire_from_node.clear();
                    inspector_.param_picker_wire_from_port.clear();
                    inspector_.param_picker_is_output = true;
                    inspector_.param_picker_x = mouse_.x;
                    inspector_.param_picker_y = mouse_.y;
                    inspector_.param_picker_sel = 0;
                    inspector_.param_picker_scroll = 0;
                    rebuild_param_picker_items();
                    if (!inspector_.param_picker_items.empty()) {
                        inspector_.param_picker_open = true;
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
                    open_clone_confirm_dialog(type_name);
                }
                last_click_node_id_.clear();
            } else {
                last_click_node_id_ = node_id;
                last_click_time_ = now;
            }

            selected_wire_idx_ = -1;  // clicking a node clears wire selection

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
            // Hit-test sticky notes before wires
            int sticky_hit = -1;
            for (int si = 0; si < static_cast<int>(sticky_note_rects_.size()); ++si) {
                const auto& sr = sticky_note_rects_[si];
                if (mouse_.x >= sr.x && mouse_.x <= sr.x + sr.w &&
                    mouse_.y >= sr.y && mouse_.y <= sr.y + sr.h) {
                    sticky_hit = si;
                    break;
                }
            }
            if (sticky_hit >= 0) {
                const auto& sr = sticky_note_rects_[sticky_hit];
                selected_sticky_id_ = sr.id;
                selected_node_ids_.clear();
                selected_wire_idx_ = -1;

                // Check if click is on a hyperlink — open in browser and consume the click
                for (const auto& lr : sticky_link_rects_) {
                    if (mouse_.x >= lr.x && mouse_.x <= lr.x + lr.w &&
                        mouse_.y >= lr.y && mouse_.y <= lr.y + lr.h) {
                        open_url(lr.url);
                        return;
                    }
                }

                // Check for resize grab handles (bottom-right, bottom-left, top-right)
                float gs = kStickyResizeGrab;
                float mx = mouse_.x, my = mouse_.y;
                int edge = 0;
                // bottom-right
                if (mx >= sr.x + sr.w - gs && my >= sr.y + sr.h - gs) edge = 2 | 8;
                // bottom-left
                else if (mx <= sr.x + gs && my >= sr.y + sr.h - gs) edge = 1 | 8;
                // top-right
                else if (mx >= sr.x + sr.w - gs && my <= sr.y + gs) edge = 2 | 4;

                if (edge != 0) {
                    resizing_sticky_idx_ = sticky_hit;
                    sticky_resize_edge_ = edge;
                    // Find graph-space note to store starting geometry
                    if (sticky_hit < static_cast<int>(snap_.sticky_notes.size())) {
                        const auto& sn = snap_.sticky_notes[sticky_hit];
                        sticky_resize_start_x_ = sn.x;
                        sticky_resize_start_y_ = sn.y;
                        sticky_resize_start_w_ = sn.width;
                        sticky_resize_start_h_ = sn.height;
                        sticky_resize_start_gx_ = sx_to_gx(mouse_.x);
                        sticky_resize_start_gy_ = sy_to_gy(mouse_.y);
                    }
                } else {
                    // Double-click detection for inline editing
                    static double last_sticky_click_time = 0.0;
                    static std::string last_sticky_click_id;
                    double now = glfwGetTime();
                    if (sr.id == last_sticky_click_id && (now - last_sticky_click_time) < 0.3) {
                        // Enter edit mode
                        editing_sticky_ = true;
                        sticky_edit_id_ = sr.id;
                        // Find the note text
                        for (const auto& sn : snap_.sticky_notes) {
                            if (sn.id == sr.id) {
                                sticky_edit_buffer_ = sn.text;
                                break;
                            }
                        }
                        text_edit_.reset(static_cast<int>(sticky_edit_buffer_.size()));
                        cursor_blink_time_ = 0.0f;
                        last_sticky_click_id.clear();
                    } else {
                        last_sticky_click_id = sr.id;
                        last_sticky_click_time = now;

                        // Start drag
                        dragging_sticky_idx_ = sticky_hit;
                        sticky_drag_offset_x_ = sx_to_gx(mouse_.x) - snap_.sticky_notes[sticky_hit].x;
                        sticky_drag_offset_y_ = sy_to_gy(mouse_.y) - snap_.sticky_notes[sticky_hit].y;
                    }
                }
            } else {
            // No node or sticky hit — try wire selection
            int wi = hit_test_wire(mouse_.x, mouse_.y);
            if (wi >= 0) {
                selected_wire_idx_ = wi;
                selected_node_ids_.clear();
                selected_sticky_id_.clear();
            } else {
                // Empty canvas: clear all selection
                selected_wire_idx_ = -1;
                selected_sticky_id_.clear();
                if (pan_gesture_ == "left" && !mouse_.shift_down) {
                    // Left-drag pans; shift+left-drag box-selects
                    selected_node_ids_.clear();
                    panning_ = true;
                    pan_start_mx_ = mouse_.x;
                    pan_start_my_ = mouse_.y;
                    pan_start_px_ = pan_x_;
                    pan_start_py_ = pan_y_;
                } else {
                    if (!mouse_.shift_down)
                        selected_node_ids_.clear();
                    box_selecting_ = true;
                    box_start_gx_ = sx_to_gx(mouse_.x);
                    box_start_gy_ = sy_to_gy(mouse_.y);
                    box_shift_held_ = mouse_.shift_down;
                }
            }
            } // end sticky_hit else (no sticky hit)
        }
    }
}

void NodeGraphUI::update_scrollbar_drag() {
    if (!inspector_.insp_scrollbar_dragging) return;

    if (mouse_.left_down) {
        float viewport_h = static_cast<float>(win_h_) - kPerfBarH;
        float track_h = viewport_h - 4.0f;
        float ratio = viewport_h / inspector_.insp_content_h;
        float thumb_h = std::max(kInspScrollbarMinThumb, track_h * ratio);
        float scrollable_track = track_h - thumb_h;

        if (scrollable_track > 0.0f) {
            float max_scroll = inspector_.insp_content_h - viewport_h;
            float mouse_delta = mouse_.y - inspector_.insp_sb_drag_start_y;
            float scroll_delta = (mouse_delta / scrollable_track) * max_scroll;
            inspector_.insp_scroll_y = std::max(0.0f, std::min(max_scroll,
                                      inspector_.insp_sb_drag_start_scroll + scroll_delta));
        }
    }

    if (mouse_.left_released) {
        inspector_.insp_scrollbar_dragging = false;
    }
}

// update_preferences moved to DialogManager

// -----------------------------------------------------------------------
// Package browser interaction
// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// Patch panel — jack click (start wire drag)
// -----------------------------------------------------------------------
bool NodeGraphUI::handle_patch_click() {
    for (int i = 0; i < static_cast<int>(patch_jacks_.size()); ++i) {
        const auto& j = patch_jacks_[i];
        float dx = mouse_.x - j.x;
        float dy = mouse_.y - j.y;
        if (dx * dx + dy * dy > kPatchJackHitRadius * kPatchJackHitRadius) continue;
        if (!j.can_source) continue;
        patch_dragging_ = true;
        patch_drag_from_idx_ = i;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// Patch panel — right-click on wire or jack
// -----------------------------------------------------------------------
void NodeGraphUI::handle_patch_right_click() {
    if (!mouse_.right_clicked) return;

    // Hit-test wires (point-to-bezier distance)
    for (int i = 0; i < static_cast<int>(patch_wires_.size()); ++i) {
        const auto& w = patch_wires_[i];
        // Draw left-to-right like in draw_patch_panel
        float wx0, wy0, wx1, wy1;
        if (w.sx <= w.ex) { wx0 = w.sx; wy0 = w.sy; wx1 = w.ex; wy1 = w.ey; }
        else              { wx0 = w.ex; wy0 = w.ey; wx1 = w.sx; wy1 = w.sy; }

        float min_dist2 = 1e9f;
        traverse_wire(wx0, wy0, wx1, wy1, true, [&](float x0, float y0, float x1, float y1) {
            float d2 = point_seg_dist2(mouse_.x, mouse_.y, x0, y0, x1, y1);
            if (d2 < min_dist2) min_dist2 = d2;
        });
        if (min_dist2 < 6.0f * 6.0f) {
            patch_ctx_open_ = true;
            patch_ctx_x_ = mouse_.x;
            patch_ctx_y_ = mouse_.y;
            patch_ctx_wire_idx_ = i;
            return;
        }
    }

    // Hit-test jacks — find a wire connected to this jack
    for (int ji = 0; ji < static_cast<int>(patch_jacks_.size()); ++ji) {
        const auto& j = patch_jacks_[ji];
        float dx = mouse_.x - j.x;
        float dy = mouse_.y - j.y;
        if (dx * dx + dy * dy > kPatchJackHitRadius * kPatchJackHitRadius) continue;

        // Find first wire connected to this jack
        for (int wi = 0; wi < static_cast<int>(patch_wires_.size()); ++wi) {
            const auto& w = patch_wires_[wi];
            if ((w.from_node == j.node_id && w.from_port == j.port_name) ||
                (w.to_node == j.node_id && w.to_port == j.port_name)) {
                patch_ctx_open_ = true;
                patch_ctx_x_ = mouse_.x;
                patch_ctx_y_ = mouse_.y;
                patch_ctx_wire_idx_ = wi;
                return;
            }
        }
        break;  // Found a jack but no wire on it
    }
}

// -----------------------------------------------------------------------
// Patch panel — drag update (connect on release)
// -----------------------------------------------------------------------
void NodeGraphUI::update_patch_drag() {
    if (!patch_dragging_) return;

    // Handle context menu click
    if (patch_ctx_open_ && mouse_.left_clicked) {
        if (patch_ctx_wire_idx_ >= 0 &&
            patch_ctx_wire_idx_ < static_cast<int>(patch_wires_.size())) {
            const auto& w = patch_wires_[patch_ctx_wire_idx_];
            // Check if "Disconnect" item was clicked
            float menu_w = 160.0f;
            float item_y = patch_ctx_y_ + kCtxMenuPadTop + kCtxMenuItemH;
            if (mouse_.x >= patch_ctx_x_ && mouse_.x <= patch_ctx_x_ + menu_w &&
                mouse_.y >= item_y && mouse_.y <= item_y + kCtxMenuItemH) {
                std::string from_addr = w.from_node + "/" + w.from_port;
                std::string to_addr = w.to_node + "/" + w.to_port;
                commands_.disconnect(from_addr, to_addr);
            }
        }
        patch_ctx_open_ = false;
        patch_ctx_wire_idx_ = -1;
        mouse_.left_clicked = false;
        return;
    }

    if (!mouse_.left_down) {
        // Released — check if we hit a compatible jack on the other node
        if (patch_drag_from_idx_ >= 0 &&
            patch_drag_from_idx_ < static_cast<int>(patch_jacks_.size())) {
            const auto& src = patch_jacks_[patch_drag_from_idx_];
            for (const auto& j : patch_jacks_) {
                if (j.node_id == src.node_id) continue;  // must be on other node
                if (!j.can_dest) continue;
                float dx = mouse_.x - j.x;
                float dy = mouse_.y - j.y;
                if (dx * dx + dy * dy > kPatchJackHitRadius * kPatchJackHitRadius) continue;
                if (!port_type_compatible(src.port_type, j.port_type)) continue;

                std::string from_addr = src.node_id + "/" + src.port_name;
                std::string to_addr = j.node_id + "/" + j.port_name;
                commands_.connect(from_addr, to_addr);
                break;
            }
        }
        patch_dragging_ = false;
        patch_drag_from_idx_ = -1;
    }
}

// -----------------------------------------------------------------------
// Public action methods (used by menu bar callbacks and bare-key shortcuts)
// -----------------------------------------------------------------------
void NodeGraphUI::toggle_session_grid() {
    session_grid_open_ = !session_grid_open_;
    if (!session_grid_open_) {
        session_editing_name_ = false;
        session_ctx_menu_open_ = false;
        session_selected_idx_ = -1;
        session_drag_idx_ = -1;
        session_drag_active_ = false;
    }
}

void NodeGraphUI::toggle_midi_map_mode() {
    midi_map_mode_ = !midi_map_mode_;
    if (!midi_map_mode_) {
        midi_map_waiting_ = false;
        inspector_.editing_midi_range = false;
    }
}

void NodeGraphUI::delete_selected() {
    if (selected_node_ids_.empty()) return;
    auto ids_copy = selected_node_ids_;
    for (const auto& id : ids_copy)
        commands_.remove_node(id);
    selected_node_ids_.clear();
}

void NodeGraphUI::open_chooser() {
    if (!snap_valid_ || snap_.operator_types.empty()) return;
    // Center chooser in visible graph area
    float center_sx = graph_right() * 0.5f;
    float center_sy = static_cast<float>(win_h_) * 0.5f;
    chooser_cursor_gx_ = sx_to_gx(center_sx);
    chooser_cursor_gy_ = sy_to_gy(center_sy);
    chooser_filter_.clear();
    text_edit_.reset(0);
    chooser_sel_ = 0;
    chooser_scroll_ = 0;
    rebuild_chooser_items();
    chooser_open_ = true;
}

} // namespace vivid::ui
