#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/active_text_field.h"
#include "ui/rendering/overlay_layouts.h"
#include "ui/dialogs/file_dialog.h"
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


} // namespace vivid::ui
