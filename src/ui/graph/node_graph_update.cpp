#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "common/string_util.h"
#include <algorithm>
#include <unordered_map>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

using vivid::format_float;
using vivid::format_int;

void NodeGraphUI::update(const GraphSnapshot& snapshot) {
    snap_ = snapshot;
    snap_valid_ = true;
    build_console_panel_.sync_from_model();

    // Deselect a param wire that becomes hidden
    if (!show_param_wires_ && selected_wire_idx_ >= 0 &&
        selected_wire_idx_ < (int)snap_.connections.size() &&
        (snap_.connections[selected_wire_idx_].from_is_param ||
         snap_.connections[selected_wire_idx_].to_is_param)) {
        selected_wire_idx_ = -1;
    }

    // MIDI map mode: capture CC event when waiting for knob wiggle
    if (midi_map_waiting_ && !snap_.pending_cc_events.empty()) {
        const auto& ev = snap_.pending_cc_events[0];
        // Look up param descriptor for default range
        float range_min = 0.0f, range_max = 1.0f;
        const auto* ns = snap_.find_node(midi_map_node_id_);
        if (ns) {
            const ParamInfo* pd = ns->find_param(midi_map_param_name_);
            if (pd) {
                range_min = pd->min_value;
                range_max = pd->max_value;
            }
        }
        commands_.add_midi_mapping(midi_map_node_id_, midi_map_param_name_,
                                   ev.cc_number, ev.channel, range_min, range_max);
        midi_map_waiting_ = false;
    }

    {
        float bottom_offset = session_grid_open_ ? kSessionStripH : 0.0f;
        build_console_panel_.update_drag(mouse_.x, mouse_.y, mouse_.left_down,
                                         win_w_, win_h_, bottom_offset);
    }

    check_relayout();

    // Right-drag pan detection: promote pending right-press to panning
    if (pan_gesture_ == "right" && right_pending_) {
        float dx = mouse_.x - right_press_mx_;
        float dy = mouse_.y - right_press_my_;
        if (dx * dx + dy * dy > kRightClickDragThreshold * kRightClickDragThreshold) {
            right_pending_ = false;
            panning_ = true;
        }
    }

    update_pan();
    update_node_drag();

    // Sticky note drag
    if (dragging_sticky_idx_ >= 0) {
        if (mouse_.left_down) {
            float mgx = sx_to_gx(mouse_.x);
            float mgy = sy_to_gy(mouse_.y);
            float nx = mgx - sticky_drag_offset_x_;
            float ny = mgy - sticky_drag_offset_y_;
            // Update the screen-space rect for responsive feedback
            if (dragging_sticky_idx_ < static_cast<int>(sticky_note_rects_.size())) {
                auto& sr = sticky_note_rects_[dragging_sticky_idx_];
                sr.x = gx_to_sx(nx);
                sr.y = gy_to_sy(ny);
            }
        }
        if (mouse_.left_released) {
            float mgx = sx_to_gx(mouse_.x);
            float mgy = sy_to_gy(mouse_.y);
            float nx = mgx - sticky_drag_offset_x_;
            float ny = mgy - sticky_drag_offset_y_;
            if (dragging_sticky_idx_ < static_cast<int>(snap_.sticky_notes.size())) {
                auto& sn = snap_.sticky_notes[dragging_sticky_idx_];
                commands_.update_sticky_note(sn.id, sn.text, nx, ny,
                                             sn.width, sn.height, sn.color);
                // Patch snapshot so draw doesn't flash back to old position
                sn.x = nx;
                sn.y = ny;
            }
            dragging_sticky_idx_ = -1;
        }
    }

    // Sticky note resize
    if (resizing_sticky_idx_ >= 0) {
        if (mouse_.left_down) {
            float dgx = sx_to_gx(mouse_.x) - sticky_resize_start_gx_;
            float dgy = sy_to_gy(mouse_.y) - sticky_resize_start_gy_;
            float nx = sticky_resize_start_x_, ny = sticky_resize_start_y_;
            float nw = sticky_resize_start_w_, nh = sticky_resize_start_h_;
            if (sticky_resize_edge_ & 2) nw = std::max(kStickyMinW, sticky_resize_start_w_ + dgx);
            if (sticky_resize_edge_ & 1) { nw = std::max(kStickyMinW, sticky_resize_start_w_ - dgx); nx = sticky_resize_start_x_ + (sticky_resize_start_w_ - nw); }
            if (sticky_resize_edge_ & 8) nh = std::max(kStickyMinH, sticky_resize_start_h_ + dgy);
            if (sticky_resize_edge_ & 4) { nh = std::max(kStickyMinH, sticky_resize_start_h_ - dgy); ny = sticky_resize_start_y_ + (sticky_resize_start_h_ - nh); }
            // Update screen-space rect for responsive feedback
            if (resizing_sticky_idx_ < static_cast<int>(sticky_note_rects_.size())) {
                auto& sr = sticky_note_rects_[resizing_sticky_idx_];
                sr.x = gx_to_sx(nx); sr.y = gy_to_sy(ny);
                sr.w = g_to_s(nw); sr.h = g_to_s(nh);
            }
        }
        if (mouse_.left_released) {
            float dgx = sx_to_gx(mouse_.x) - sticky_resize_start_gx_;
            float dgy = sy_to_gy(mouse_.y) - sticky_resize_start_gy_;
            float nx = sticky_resize_start_x_, ny = sticky_resize_start_y_;
            float nw = sticky_resize_start_w_, nh = sticky_resize_start_h_;
            if (sticky_resize_edge_ & 2) nw = std::max(kStickyMinW, sticky_resize_start_w_ + dgx);
            if (sticky_resize_edge_ & 1) { nw = std::max(kStickyMinW, sticky_resize_start_w_ - dgx); nx = sticky_resize_start_x_ + (sticky_resize_start_w_ - nw); }
            if (sticky_resize_edge_ & 8) nh = std::max(kStickyMinH, sticky_resize_start_h_ + dgy);
            if (sticky_resize_edge_ & 4) { nh = std::max(kStickyMinH, sticky_resize_start_h_ - dgy); ny = sticky_resize_start_y_ + (sticky_resize_start_h_ - nh); }
            if (resizing_sticky_idx_ < static_cast<int>(snap_.sticky_notes.size())) {
                auto& sn = snap_.sticky_notes[resizing_sticky_idx_];
                commands_.update_sticky_note(sn.id, sn.text, nx, ny, nw, nh, sn.color);
                // Patch snapshot so draw doesn't flash back to old position/size
                sn.x = nx;
                sn.y = ny;
                sn.width = nw;
                sn.height = nh;
            }
            resizing_sticky_idx_ = -1;
        }
    }

    // Commit sticky note editing on click outside
    if (editing_sticky_ && mouse_.left_clicked) {
        bool clicked_inside = false;
        for (const auto& sr : sticky_note_rects_) {
            if (sr.id == sticky_edit_id_ &&
                mouse_.x >= sr.x && mouse_.x <= sr.x + sr.w &&
                mouse_.y >= sr.y && mouse_.y <= sr.y + sr.h) {
                clicked_inside = true;
                break;
            }
        }
        if (!clicked_inside) {
            // Commit edit
            for (const auto& sn : snap_.sticky_notes) {
                if (sn.id == sticky_edit_id_) {
                    commands_.update_sticky_note(sn.id, sticky_edit_buffer_,
                                                 sn.x, sn.y, sn.width, sn.height, sn.color);
                    break;
                }
            }
            editing_sticky_ = false;
            sticky_edit_id_.clear();
        }
    }

    // Sticky note color picker click
    if (sticky_color_menu_open_ && mouse_.left_clicked) {
        float cmx = sticky_color_menu_x_;
        float cmy = sticky_color_menu_y_;
        float swatch_size = 20.0f;
        float gap = 4.0f;
        bool hit = false;
        for (int i = 0; i < kStickyColorCount; ++i) {
            float sx2 = cmx + 4.0f + i * (swatch_size + gap);
            float sy2 = cmy + 4.0f;
            if (mouse_.x >= sx2 && mouse_.x <= sx2 + swatch_size &&
                mouse_.y >= sy2 && mouse_.y <= sy2 + swatch_size) {
                // Apply color change
                for (const auto& sn : snap_.sticky_notes) {
                    if (sn.id == sticky_color_menu_id_) {
                        commands_.update_sticky_note(sn.id, sn.text,
                                                     sn.x, sn.y, sn.width, sn.height, i);
                        break;
                    }
                }
                hit = true;
                break;
            }
        }
        sticky_color_menu_open_ = false;
        if (hit) {
            mouse_.left_clicked = false;
            mouse_.left_released = false;
        }
    }

    // Session card drag reorder
    if (session_drag_idx_ >= 0 && mouse_.left_down) {
        float dx = mouse_.x - session_drag_start_x_;
        float dy = mouse_.y - session_drag_start_y_;
        if (!session_drag_active_ &&
            (dx * dx + dy * dy) > kSessionDragThreshold * kSessionDragThreshold) {
            session_drag_active_ = true;
        }
        if (session_drag_active_) {
            // Determine insertion target from mouse X vs cell rects
            session_drag_target_idx_ = static_cast<int>(snap_.variations.size());
            for (const auto& cr : variation_cell_rects_) {
                if (mouse_.x < cr.x + cr.w * 0.5f) {
                    session_drag_target_idx_ = cr.idx;
                    break;
                }
            }
        }
    }
    if (session_drag_idx_ >= 0 && mouse_.left_released) {
        if (session_drag_active_ && session_drag_target_idx_ >= 0 &&
            session_drag_idx_ < static_cast<int>(snap_.variations.size())) {
            int target = session_drag_target_idx_;
            // Adjust target for the removal of the source
            if (target > session_drag_idx_) target--;
            if (target != session_drag_idx_) {
                commands_.move_variation(snap_.variations[session_drag_idx_].name, target);
            }
        }
        session_drag_idx_ = -1;
        session_drag_target_idx_ = -1;
        session_drag_active_ = false;
    }

    update_box_select();
    update_wire_drag();
    update_scrollbar_drag();
    update_slider_drag();
    update_transport_bpm_drag();
    update_modulation_drag();
    update_xy_pad_drag();
    update_color_drag();
    update_patch_drag();
    update_chooser_hover();
    update_param_picker();   // may consume left_clicked
    // update_package_browser, update_example_browser, update_graph_meta_editor moved to DialogManager
    dialogs_.update(mouse_, win_w_, win_h_);  // may consume left_clicked (includes pkg/example browsers, graph_meta, preferences)
    // update_create_popup moved to DialogManager (called via dialogs_.update())
    update_context_menu();   // may consume left_clicked
    handle_right_click();
    handle_left_click();     // dispatches to sub-handlers
    update_pan_release();
    // Preserve click events for custom inspector draw phase
    inspector_.insp_mouse_left_clicked = mouse_.left_clicked;
    inspector_.insp_mouse_left_released = mouse_.left_released;
    inspector_.insp_mouse_right_clicked = mouse_.right_clicked;
    clear_frame_flags();
    update_wire_hover();
    update_node_hover();

    // Port hover — find nearest port when not in a drag/popup state
    hovered_port_ = {};
    if (hovered_node_id_.empty() && !dragging_wire_ && !panning_ && !box_selecting_ &&
        dragging_node_idx_ < 0 && !context_menu_open_ && !chooser_open_ && !inspector_.dropdown_open) {
        PortHit ph = hit_test_port(mouse_.x, mouse_.y);
        if (ph.node_idx >= 0 && ph.node_idx < static_cast<int>(node_rects_.size())) {
            hovered_port_.node_id = node_rects_[ph.node_idx].node_id;
            hovered_port_.port_name = ph.port_name;
            hovered_port_.is_output = ph.is_output;
        }
    }

    // Inspector widget hover
    inspector_.hovered_slider_idx = -1;
    inspector_.hovered_bool_idx = -1;
    inspector_.hovered_dropdown_idx = -1;
    inspector_.hovered_label_idx = -1;
    if (mouse_.x >= graph_right() && has_selection() && !inspector_.editing_param) {
        for (int i = 0; i < static_cast<int>(inspector_.slider_rects.size()); ++i) {
            const auto& r = inspector_.slider_rects[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                inspector_.hovered_slider_idx = i;
                break;
            }
        }
        for (int i = 0; i < static_cast<int>(inspector_.bool_rects.size()); ++i) {
            const auto& r = inspector_.bool_rects[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                inspector_.hovered_bool_idx = i;
                break;
            }
        }
        for (int i = 0; i < static_cast<int>(inspector_.dropdown_rects.size()); ++i) {
            const auto& r = inspector_.dropdown_rects[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                inspector_.hovered_dropdown_idx = i;
                break;
            }
        }
        for (int i = 0; i < static_cast<int>(inspector_.label_rects.size()); ++i) {
            const auto& r = inspector_.label_rects[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                inspector_.hovered_label_idx = i;
                break;
            }
        }
    }

    // Flat dropdown popup hover tracking (non-preset param dropdowns)
    inspector_.dropdown_flat_hovered_idx = -1;
    if (inspector_.dropdown_open && !inspector_.dropdown_is_preset && !inspector_.dropdown_is_state_preset && !inspector_.dropdown_labels.empty()) {
        float item_h = kDropdownItemH;
        float popup_h = inspector_.dropdown_labels.size() * item_h + 4;
        if (mouse_.x >= inspector_.dropdown_x && mouse_.x <= inspector_.dropdown_x + inspector_.dropdown_w &&
            mouse_.y >= inspector_.dropdown_y && mouse_.y <= inspector_.dropdown_y + popup_h) {
            int idx = static_cast<int>((mouse_.y - inspector_.dropdown_y - 2) / item_h);
            if (idx >= 0 && idx < static_cast<int>(inspector_.dropdown_labels.size()))
                inspector_.dropdown_flat_hovered_idx = idx;
        }
    }

    // Param label tooltip hover timer
    if (inspector_.hovered_label_idx >= 0) {
        const auto& r = inspector_.label_rects[inspector_.hovered_label_idx];
        if (r.node_id == inspector_.label_hover_node_id && r.param_name == inspector_.label_hover_param_name) {
            inspector_.label_hover_time += dt_;
        } else {
            inspector_.label_hover_node_id = r.node_id;
            inspector_.label_hover_param_name = r.param_name;
            inspector_.label_hover_time = 0.0f;
        }
    } else {
        inspector_.label_hover_time = 0.0f;
        inspector_.label_hover_node_id.clear();
        inspector_.label_hover_param_name.clear();
    }

    update_sparklines();

    // --- Animation updates ---
    float dt = dt_;
    if (dt <= 0.0f) dt = 1.0f / 60.0f; // fallback

    // Smooth zoom/pan (only when not directly panning with middle mouse)
    if (!panning_) {
        zoom_ = lerp_toward(zoom_, zoom_target_, kZoomLerpSpeed, dt);
        pan_x_ = lerp_toward(pan_x_, pan_target_x_, kPanLerpSpeed, dt);
        pan_y_ = lerp_toward(pan_y_, pan_target_y_, kPanLerpSpeed, dt);
        // Snap when close enough to avoid perpetual sub-pixel drift
        if (std::fabs(zoom_ - zoom_target_) < 0.001f) zoom_ = zoom_target_;
        if (std::fabs(pan_x_ - pan_target_x_) < 0.5f) pan_x_ = pan_target_x_;
        if (std::fabs(pan_y_ - pan_target_y_) < 0.5f) pan_y_ = pan_target_y_;
    }

    // Popup fade
    bool any_popup = dialogs_.any_open() || chooser_open_ ||
                     inspector_.color_popup_open;
    float popup_target = any_popup ? 1.0f : 0.0f;
    popup_opacity_ = lerp_toward(popup_opacity_, popup_target, kPopupFadeSpeed, dt);
    if (popup_opacity_ < 0.01f) popup_opacity_ = 0.0f;
    if (popup_opacity_ > 0.99f) popup_opacity_ = 1.0f;

    // Node hover alpha
    float hover_target = hovered_node_id_.empty() ? 0.0f : 1.0f;
    if (!hovered_node_id_.empty() && hovered_node_id_ != node_hover_anim_id_) {
        node_hover_anim_id_ = hovered_node_id_;
        node_hover_alpha_ = 0.0f;
    } else if (hovered_node_id_.empty() && node_hover_alpha_ < 0.01f) {
        node_hover_anim_id_.clear();
    }
    node_hover_alpha_ = lerp_toward(node_hover_alpha_, hover_target, kHoverFadeSpeed, dt);

    // Selection glow pulse
    if (!selected_node_ids_.empty()) {
        float glow_target = selection_glow_rising_ ? 1.0f : 0.0f;
        selection_glow_ = lerp_toward(selection_glow_, glow_target, kSelectionGlowSpeed, dt);
        if (selection_glow_ > 0.95f) selection_glow_rising_ = false;
        if (selection_glow_ < 0.05f) selection_glow_rising_ = true;
    } else {
        selection_glow_ = 0.0f;
        selection_glow_rising_ = true;
    }

    // Animate node heights toward their targets
    for (auto& rect : node_rects_) {
        if (rect.target_h > 0.0f && std::fabs(rect.h - rect.target_h) > 0.5f) {
            rect.h = lerp_toward(rect.h, rect.target_h, kNodeHeightLerpSpeed, dt);
        } else if (rect.target_h > 0.0f) {
            rect.h = rect.target_h;
        }
    }

    // Preset submenu hover tracking
    if (inspector_.dropdown_open && (inspector_.dropdown_is_preset || inspector_.dropdown_is_state_preset)
        && !inspector_.dropdown_submenu_stack.empty()) {
        float item_h = kDropdownItemH;
        // Find deepest level the mouse is inside
        int hit_lvl = -1;
        int hit_idx = -1;
        for (int lvl = static_cast<int>(inspector_.dropdown_submenu_stack.size()) - 1; lvl >= 0; --lvl) {
            const auto& level = inspector_.dropdown_submenu_stack[lvl];
            if (!level.items || level.items->empty()) continue;
            int count = static_cast<int>(level.items->size());
            float popup_h = count * item_h + 4;
            if (mouse_.x >= level.x && mouse_.x <= level.x + level.w &&
                mouse_.y >= level.y && mouse_.y <= level.y + popup_h) {
                int idx = static_cast<int>((mouse_.y - level.y - 2) / item_h);
                if (idx >= 0 && idx < count) {
                    hit_lvl = lvl;
                    hit_idx = idx;
                }
                break;
            }
        }

        if (hit_lvl >= 0) {
            inspector_.dropdown_submenu_stack[hit_lvl].hovered_idx = hit_idx;
            const auto& node = (*inspector_.dropdown_submenu_stack[hit_lvl].items)[hit_idx];

            if (node.is_folder) {
                // Hover on folder: after delay, open its submenu
                int target_key = hit_lvl * 1000 + hit_idx;
                if (inspector_.dropdown_hover_target == target_key) {
                    inspector_.dropdown_hover_frames++;
                    if (inspector_.dropdown_hover_frames >= 10) {
                        // Open subfolder submenu
                        inspector_.dropdown_submenu_stack.resize(hit_lvl + 1);
                        const auto& level = inspector_.dropdown_submenu_stack[hit_lvl];
                        float sub_x = level.x + level.w - 2;
                        float sub_y = level.y + 2 + hit_idx * item_h;
                        float sub_w = level.w;
                        if (inspector_.dropdown_tr) {
                            for (const auto& child : node.children) {
                                float tw = inspector_.dropdown_tr->text_width(child.label.c_str()) + 24.0f;
                                if (child.is_folder) tw += 12.0f;
                                if (tw > sub_w) sub_w = tw;
                            }
                        }
                        float wf = static_cast<float>(win_w_);
                        if (sub_x + sub_w > wf) sub_x = level.x - sub_w + 2;
                        inspector_.dropdown_submenu_stack.push_back({&node.children, -1, sub_x, sub_y, sub_w});
                        inspector_.dropdown_hover_frames = 0;
                        inspector_.dropdown_hover_target = -1;
                    }
                } else {
                    inspector_.dropdown_hover_target = target_key;
                    inspector_.dropdown_hover_frames = 0;
                }
            } else {
                // Hovering a leaf: close deeper submenus
                inspector_.dropdown_submenu_stack.resize(hit_lvl + 1);
                inspector_.dropdown_hover_target = -1;
                inspector_.dropdown_hover_frames = 0;
            }
        }
    }
}

void NodeGraphUI::check_relayout() {
    // Detect full graph replacement (e.g. drag-and-drop new file).
    // Normal edits reuse existing node IDs; a new graph introduces unknown IDs.
    if (first_layout_done_ && !node_rects_.empty()) {
        std::unordered_set<std::string> rect_ids;
        rect_ids.reserve(node_rects_.size());
        for (const auto& r : node_rects_) rect_ids.insert(r.node_id);
        for (const auto& n : snap_.nodes) {
            if (!rect_ids.count(n.node_id)) {
                output_sink_positioned_ = false;
                layout_nodes();
                return;
            }
        }
    }

    size_t cur_nodes = snap_.nodes.size();
    size_t cur_conns = snap_.connections.size();
    if (cur_nodes > last_node_count_) {
        if (dragging_node_idx_ < 0 && !dragging_wire_) {
            if (!first_layout_done_)
                layout_nodes();       // first time: full Sugiyama
            else
                place_new_nodes();    // subsequent: incremental
        }
    } else if (cur_nodes < last_node_count_) {
        prune_node_rects();
    } else if (cur_conns != last_conn_count_ || show_param_wires_ != last_show_param_wires_) {
        // Connection changed or param wire visibility toggled —
        // recompute ports and heights for all nodes
        std::unordered_map<std::string, size_t> rect_by_id;
        for (size_t i = 0; i < node_rects_.size(); ++i)
            rect_by_id[node_rects_[i].node_id] = i;

        for (const auto& ns : snap_.nodes) {
            auto it = rect_by_id.find(ns.node_id);
            if (it == rect_by_id.end()) continue;
            auto& rect = node_rects_[it->second];
            rect.active_cadence = ns.active_cadence;
            rect.is_gpu = ns.is_gpu;
            rect.type_name = ns.type_name;
            bool has_ct = custom_thumb_nodes_.count(ns.node_id) > 0;
            float body_h = node_body_height(ns.is_gpu, ns.active_cadence, has_ct);
            uint32_t n_inputs = count_visible_input_ports(ns, show_param_wires_);
            uint32_t n_outputs = count_visible_output_ports(ns, show_param_wires_);
            uint32_t port_rows = std::max(n_inputs, n_outputs);
            rect.target_h = kAccentBarH + body_h + kNodePadY + kLineH * 2 + port_rows * kLineH + kNodePadY;
            recompute_ports(rect, ns);
        }
        last_node_count_ = cur_nodes;
        last_conn_count_ = cur_conns;
        last_show_param_wires_ = show_param_wires_;
    }
}

void NodeGraphUI::prune_node_rects() {
    std::unordered_map<std::string, NodeRect> old_rects;
    for (const auto& r : node_rects_)
        old_rects[r.node_id] = r;

    const auto& nodes = snap_.nodes;
    node_rects_.resize(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        auto it = old_rects.find(nodes[i].node_id);
        if (it != old_rects.end())
            node_rects_[i] = it->second;
    }

    if (nodes.empty())
        first_layout_done_ = false;

    // Prune stale selection IDs
    std::unordered_set<std::string> live_ids;
    for (const auto& n : nodes) live_ids.insert(n.node_id);
    for (auto it = selected_node_ids_.begin(); it != selected_node_ids_.end(); ) {
        if (!live_ids.count(*it))
            it = selected_node_ids_.erase(it);
        else
            ++it;
    }

    last_node_count_ = nodes.size();
    last_conn_count_ = snap_.connections.size();
}


void NodeGraphUI::clear_frame_flags() {
    mouse_.left_clicked = false;
    mouse_.left_released = false;
    mouse_.right_clicked = false;
    mouse_.right_released = false;
}


// -----------------------------------------------------------------------
// resolve_port_type — shared utility (was file-local static in input.cpp)
// -----------------------------------------------------------------------
VividPortType NodeGraphUI::resolve_port_type(const GraphSnapshot& snap,
                                              const std::string& node_id,
                                              const std::string& port_name,
                                              bool is_output) {
    const auto* ns = snap.find_node(node_id);
    if (!ns || !ns->op_info) return VIVID_PORT_SCALAR;
    for (const auto& p : ns->op_info->ports) {
        if (p.name == port_name &&
            ((is_output && p.direction == VIVID_PORT_OUTPUT) ||
             (!is_output && p.direction == VIVID_PORT_INPUT)))
            return p.type;
    }
    return VIVID_PORT_SCALAR;
}

} // namespace vivid::ui
