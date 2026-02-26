#include "ui/node_graph.h"
#include "ui/node_graph_constants.h"
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
    mouse_.x = x;
    mouse_.y = y;
}

void NodeGraphUI::on_mouse_button(int button, int action) {
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

void NodeGraphUI::on_scroll(float /*x_offset*/, float y_offset) {
    // Inspector scroll when cursor is in inspector area
    if (mouse_.x >= graph_right() && has_selection()) {
        insp_scroll_y_ -= y_offset * kInspScrollSpeed;
        float viewport_h = static_cast<float>(win_h_) - kPerfBarH;
        float max_scroll = std::max(0.0f, insp_content_h_ - viewport_h);
        insp_scroll_y_ = std::max(0.0f, std::min(insp_scroll_y_, max_scroll));
        return;
    }

    float factor = std::pow(1.12f, y_offset);
    float new_zoom = zoom_ * factor;
    new_zoom = std::max(0.4f, std::min(2.5f, new_zoom));

    // Pivot around mouse cursor
    float gx = sx_to_gx(mouse_.x);
    float gy = sy_to_gy(mouse_.y);
    zoom_ = new_zoom;
    pan_x_ = mouse_.x - gx * zoom_;
    pan_y_ = mouse_.y - gy * zoom_;
}

// -----------------------------------------------------------------------
// Keyboard input
// -----------------------------------------------------------------------
void NodeGraphUI::on_key(int key, int action, int /*mods*/) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (editing_midi_range_) {
        if (key == GLFW_KEY_ENTER)       confirm_midi_range_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_midi_range_edit();
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
        return;
    }

    if (editing_param_) {
        if (key == GLFW_KEY_ENTER)       confirm_param_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_param_edit();
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
        return;
    }

    if (editing_resolution_) {
        if (key == GLFW_KEY_ENTER)       confirm_resolution_edit();
        else if (key == GLFW_KEY_ESCAPE) cancel_resolution_edit();
        else if (key == GLFW_KEY_BACKSPACE && !edit_buffer_.empty())
            edit_buffer_.pop_back();
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
            snap_ && !snap_->operator_types.empty()) {
            if (mouse_.x < graph_right()) {
                chooser_cursor_gx_ = sx_to_gx(mouse_.x);
                chooser_cursor_gy_ = sy_to_gy(mouse_.y);
                chooser_filter_.clear();
                chooser_sel_ = 0;
                chooser_scroll_ = 0;
                chooser_items_ = snap_->operator_types;
                chooser_open_ = true;
            }
        }
        // B toggles bezier wire rendering
        if (key == GLFW_KEY_B && action == GLFW_PRESS) {
            bezier_wires_ = !bezier_wires_;
        }
        // M toggles MIDI map mode
        if (key == GLFW_KEY_M && action == GLFW_PRESS) {
            midi_map_mode_ = !midi_map_mode_;
            if (!midi_map_mode_) {
                midi_map_waiting_ = false;
                editing_midi_range_ = false;
            }
        }
        // Delete selected node (Delete or Backspace)
        if ((key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) && action == GLFW_PRESS) {
            if (!selected_node_id_.empty()) {
                commands_.remove_node(selected_node_id_);
                selected_node_id_.clear();
            }
        }
        return;
    }

    // Chooser is open
    switch (key) {
        case GLFW_KEY_ESCAPE:
            chooser_open_ = false;
            break;

        case GLFW_KEY_ENTER: {
            if (!chooser_items_.empty() && chooser_sel_ >= 0 &&
                chooser_sel_ < static_cast<int>(chooser_items_.size())) {
                const std::string& type = chooser_items_[chooser_sel_];
                std::string id;
                for (int n = 1; ; ++n) {
                    id = type + std::to_string(n);
                    if (!snap_->has_node(id)) break;
                }
                commands_.add_node(type, id);
                commands_.set_node_layout(id, chooser_cursor_gx_, chooser_cursor_gy_);
                selected_node_id_ = id;
            }
            chooser_open_ = false;
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
    if (!chooser_open_) return;
    if (codepoint >= 32 && codepoint < 127) {
        chooser_filter_ += static_cast<char>(codepoint);
        rebuild_chooser_items();
    }
}

// -----------------------------------------------------------------------
// Context menu interaction (called from update())
// -----------------------------------------------------------------------
void NodeGraphUI::update_context_menu() {
    size_t cur_nodes = snap_->nodes.size();
    size_t cur_conns = snap_->connections.size();

    // Close context menu on topology change
    if (context_menu_open_ &&
        (cur_nodes != last_node_count_ || cur_conns != last_conn_count_)) {
        context_menu_open_ = false;
    }

    // Context menu click
    if (context_menu_open_ && mouse_.left_clicked) {
        float menu_h = kCtxMenuPadTop + kCtxMenuItemH + 2.0f;
        if (mouse_.x >= context_menu_x_ && mouse_.x <= context_menu_x_ + kCtxMenuW &&
            mouse_.y >= context_menu_y_ && mouse_.y <= context_menu_y_ + menu_h) {
            // Clicked inside menu — execute action
            if (!context_node_id_.empty()) {
                commands_.remove_node(context_node_id_);
                if (selected_node_id_ == context_node_id_) selected_node_id_.clear();
            } else if (context_wire_idx_ >= 0) {
                const auto& conns = snap_->connections;
                if (context_wire_idx_ < static_cast<int>(conns.size())) {
                    const auto& c = conns[context_wire_idx_];
                    commands_.disconnect(c.from_node + "/" + c.from_port,
                                    c.to_node + "/" + c.to_port);
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
    int ni = hit_test_node(mouse_.x, mouse_.y);
    if (ni >= 0) {
        context_menu_open_ = true;
        context_node_id_ = node_rects_[ni].node_id;
        context_wire_idx_ = -1;
        context_menu_x_ = mouse_.x;
        context_menu_y_ = mouse_.y;
    } else {
        int wi = hit_test_wire(mouse_.x, mouse_.y);
        if (wi >= 0) {
            context_menu_open_ = true;
            context_node_id_.clear();
            context_wire_idx_ = wi;
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
            const std::string& type = chooser_items_[idx];
            std::string id;
            for (int n = 1; ; ++n) {
                id = type + std::to_string(n);
                if (!snap_->has_node(id)) break;
            }
            commands_.add_node(type, id);
            commands_.set_node_layout(id, chooser_cursor_gx_, chooser_cursor_gy_);
            selected_node_id_ = id;
        }
    }
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
    if (mouse_.x < graph_right() || mouse_.y >= static_cast<float>(win_h_))
        return false;

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
            const auto* mm = snap_->find_midi_mapping(mr.node_id, mr.param_name);
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
        const auto* ns = snap_->find_node(rr.node_id);
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
        const auto* ns = snap_->find_node(edit_node_id_);
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
        const auto* ns = snap_->find_node(dr.node_id);
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
        const auto* ns = snap_->find_node(br.node_id);
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
            const auto& conns = snap_->connections;
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
            selected_node_id_ = node_rects_[ni].node_id;
            dragging_node_idx_ = ni;
            drag_offset_x_ = sx_to_gx(mouse_.x) - node_rects_[ni].x;
            drag_offset_y_ = sy_to_gy(mouse_.y) - node_rects_[ni].y;
        } else {
            selected_node_id_.clear();
            // Start left-drag pan on empty canvas
            panning_ = true;
            pan_start_mx_ = mouse_.x;
            pan_start_my_ = mouse_.y;
            pan_start_px_ = pan_x_;
            pan_start_py_ = pan_y_;
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

} // namespace vivid::ui
