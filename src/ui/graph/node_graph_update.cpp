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

static const ParamInfo* find_param_semantic_for_endpoint(const OperatorInfo& op,
                                                         const std::string& endpoint_name) {
    for (const auto& p : op.params) {
        if (p.name == endpoint_name) return &p;
    }
    if (op.params.size() == 1) return &op.params[0];
    return nullptr;
}

static std::string semantic_tag_for_operator_endpoint(const OperatorInfo& op,
                                                      const std::string& endpoint_name) {
    const ParamInfo* p = find_param_semantic_for_endpoint(op, endpoint_name);
    return p ? p->semantic_tag : std::string{};
}

static std::string semantic_tag_for_snapshot_endpoint(const GraphSnapshot& snap,
                                                      const std::string& node_id,
                                                      const std::string& endpoint_name) {
    const auto* ns = snap.find_node(node_id);
    if (!ns || !ns->op_info) return {};
    return semantic_tag_for_operator_endpoint(*ns->op_info, endpoint_name);
}

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

void NodeGraphUI::update_pan() {
    if (panning_) {
        pan_x_ = pan_start_px_ + (mouse_.x - pan_start_mx_);
        pan_y_ = pan_start_py_ + (mouse_.y - pan_start_my_);
        // Keep targets synced during direct pan
        pan_target_x_ = pan_x_;
        pan_target_y_ = pan_y_;
        zoom_target_ = zoom_;
    }
}

void NodeGraphUI::update_node_drag() {
    if (dragging_node_idx_ < 0) return;
    if (mouse_.left_down) {
        // Detect real drag vs jittery click (2px threshold)
        if (!did_drag_ && !pending_select_node_id_.empty()) {
            float dx = mouse_.x - drag_start_sx_;
            float dy = mouse_.y - drag_start_sy_;
            if (dx * dx + dy * dy > 4.0f)
                did_drag_ = true;
        }

        float mgx = sx_to_gx(mouse_.x);
        float mgy = sy_to_gy(mouse_.y);

        if (group_drag_offsets_.size() > 1) {
            // Group drag: move all selected nodes
            for (auto& nr : node_rects_) {
                auto it = group_drag_offsets_.find(nr.node_id);
                if (it == group_drag_offsets_.end()) continue;
                nr.x = mgx - it->second.dx;
                nr.y = mgy - it->second.dy;
                const auto* ns = snap_.find_node(nr.node_id);
                if (ns) recompute_ports(nr, *ns);
            }
        } else {
            // Single drag
            auto& rect = node_rects_[dragging_node_idx_];
            rect.x = mgx - drag_offset_x_;
            rect.y = mgy - drag_offset_y_;
            const auto* ns = snap_.find_node(rect.node_id);
            if (ns) recompute_ports(rect, *ns);
        }
    }
    if (mouse_.left_released) {
        if (group_drag_offsets_.size() > 1) {
            for (const auto& nr : node_rects_) {
                if (group_drag_offsets_.count(nr.node_id))
                    commands_.set_node_layout(nr.node_id, nr.x, nr.y);
            }
            group_drag_offsets_.clear();
        } else {
            auto& rect = node_rects_[dragging_node_idx_];
            commands_.set_node_layout(rect.node_id, rect.x, rect.y);
        }
        dragging_node_idx_ = -1;

        // Deferred deselection: narrow to clicked node if no drag occurred
        if (!pending_select_node_id_.empty() && !did_drag_) {
            selected_node_ids_ = { pending_select_node_id_ };
        }
        pending_select_node_id_.clear();
    }
}

void NodeGraphUI::update_box_select() {
    if (!box_selecting_) return;
    if (mouse_.left_released) {
        // Compute graph-space AABB from anchor + current mouse
        float cur_gx = sx_to_gx(mouse_.x);
        float cur_gy = sy_to_gy(mouse_.y);
        float min_gx = std::min(box_start_gx_, cur_gx);
        float max_gx = std::max(box_start_gx_, cur_gx);
        float min_gy = std::min(box_start_gy_, cur_gy);
        float max_gy = std::max(box_start_gy_, cur_gy);

        // If not shift, start fresh
        if (!box_shift_held_)
            selected_node_ids_.clear();

        // Test intersection against all node_rects_
        for (const auto& r : node_rects_) {
            if (r.x + r.w >= min_gx && r.x <= max_gx &&
                r.y + r.h >= min_gy && r.y <= max_gy) {
                selected_node_ids_.insert(r.node_id);
            }
        }
        box_selecting_ = false;
    }
}

void NodeGraphUI::update_wire_drag() {
    if (mouse_.left_released && dragging_wire_) {
        PortHit ph = hit_test_port(mouse_.x, mouse_.y);
        if (ph.node_idx >= 0 && !ph.is_output) {
            // Dropped on an input port — connect directly
            std::string to_node = node_rects_[ph.node_idx].node_id;
            commands_.connect(wire_from_node_id_ + "/" + wire_from_port_,
                         to_node + "/" + ph.port_name);
        } else {
            // Check if dropped on a node body (not a port, not the source node)
            int ni = hit_test_node(mouse_.x, mouse_.y);
            if (ni >= 0 && node_rects_[ni].node_id != wire_from_node_id_) {
                // Open parameter picker for the target node's input params
                inspector_.param_picker_node_id = node_rects_[ni].node_id;
                inspector_.param_picker_wire_from_node = wire_from_node_id_;
                inspector_.param_picker_wire_from_port = wire_from_port_;
                inspector_.param_picker_is_output = false; // picking a destination param
                inspector_.param_picker_x = mouse_.x;
                inspector_.param_picker_y = mouse_.y;
                inspector_.param_picker_sel = 0;
                inspector_.param_picker_scroll = 0;
                rebuild_param_picker_items();
                if (!inspector_.param_picker_items.empty())
                    inspector_.param_picker_open = true;
            }
        }
        dragging_wire_ = false;
    }
}

void NodeGraphUI::update_slider_drag() {
    if (inspector_.active_slider_idx < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down) {
        if (inspector_.active_slider_idx >= static_cast<int>(inspector_.slider_rects.size())) {
            std::fprintf(stderr, "[UI DEBUG] slider drag: idx=%d out of range (size=%d), resetting\n",
                         inspector_.active_slider_idx, static_cast<int>(inspector_.slider_rects.size()));
            inspector_.active_slider_idx = -1;
            return;
        }
        const auto& s = inspector_.slider_rects[inspector_.active_slider_idx];
        const auto* ns = snap_.find_node(inspector_.active_slider_node_id);
        if (!ns) {
            std::fprintf(stderr, "[UI DEBUG] slider drag: node '%s' not found in snapshot\n",
                         inspector_.active_slider_node_id.c_str());
        }
        if (ns) {
            const ParamInfo* pd = ns->find_param(inspector_.active_slider_param_name);
            if (!pd) {
                std::fprintf(stderr, "[UI DEBUG] slider drag: param '%s' not found on node '%s'\n",
                             inspector_.active_slider_param_name.c_str(), inspector_.active_slider_node_id.c_str());
            }
            if (pd) {
                float val;
                if (pd->display_hint == VIVID_DISPLAY_KNOB) {
                    // Vertical drag: up = increase
                    float dy = mouse_.prev_y - mouse_.y;
                    float range = pd->max_value - pd->min_value;
                    float sensitivity = range / 200.0f;
                    auto pi_it = ns->param_indices.find(pd->name);
                    float cur = (pi_it != ns->param_indices.end())
                        ? ns->param_values[pi_it->second] : pd->min_value;
                    val = cur + dy * sensitivity;
                    val = std::max(pd->min_value, std::min(pd->max_value, val));
                } else {
                    float t = (mouse_.x - s.x) / s.w;
                    t = std::max(0.0f, std::min(1.0f, t));
                    val = pd->min_value + t * (pd->max_value - pd->min_value);
                }
                if (pd->type == VIVID_PARAM_INT) {
                    val = std::round(val);
                }
                commands_.set_param(inspector_.active_slider_node_id, inspector_.active_slider_param_name, val);
            }
        }
    }
    if (mouse_.left_released) {
        inspector_.active_slider_idx = -1;
    }
}

void NodeGraphUI::update_xy_pad_drag() {
    if (inspector_.active_xy_pad_idx < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down && inspector_.active_xy_pad_idx < static_cast<int>(inspector_.xy_pad_rects.size())) {
        const auto& pad = inspector_.xy_pad_rects[inspector_.active_xy_pad_idx];
        const auto* ns = snap_.find_node(inspector_.active_xy_node_id);
        if (ns) {
            const ParamInfo* pdx = ns->find_param(inspector_.active_xy_param_x);
            const ParamInfo* pdy = ns->find_param(inspector_.active_xy_param_y);
            if (pdx && pdy) {
                float tx = (mouse_.x - pad.x) / pad.w;
                float ty = (mouse_.y - pad.y) / pad.h;
                tx = std::max(0.0f, std::min(1.0f, tx));
                ty = std::max(0.0f, std::min(1.0f, ty));
                float val_x = pdx->min_value + tx * (pdx->max_value - pdx->min_value);
                float val_y = pdy->min_value + (1.0f - ty) * (pdy->max_value - pdy->min_value);
                commands_.set_param(inspector_.active_xy_node_id, inspector_.active_xy_param_x, val_x);
                commands_.set_param(inspector_.active_xy_node_id, inspector_.active_xy_param_y, val_y);
            }
        }
    }
    if (mouse_.left_released) {
        inspector_.active_xy_pad_idx = -1;
    }
}

void NodeGraphUI::update_color_drag() {
    if (!inspector_.color_popup_open) return;
    if (!inspector_.color_dragging_sv && !inspector_.color_dragging_hue) return;
    if (mouse_.left_down) {
        float pad = kColorPopupPad;
        float sv_size = kColorPopupSVSize;
        float hue_bar_w = kColorHueBarW;
        float gap = kColorPopupGap;
        float sv_x = inspector_.color_popup_x + pad;
        float sv_y = inspector_.color_popup_y + pad;
        float hue_x = sv_x + sv_size + gap;
        float hue_y = sv_y;

        if (inspector_.color_dragging_sv) {
            inspector_.color_popup_s = std::max(0.0f, std::min(1.0f, (mouse_.x - sv_x) / sv_size));
            inspector_.color_popup_v = std::max(0.0f, std::min(1.0f, 1.0f - (mouse_.y - sv_y) / sv_size));
        }
        if (inspector_.color_dragging_hue) {
            inspector_.color_popup_h = std::max(0.0f, std::min(360.0f,
                (mouse_.y - hue_y) / sv_size * 360.0f));
        }
        float r, g, b;
        hsv_to_rgb(inspector_.color_popup_h, inspector_.color_popup_s, inspector_.color_popup_v, r, g, b);
        commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_r, r);
        commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_g, g);
        commands_.set_param(inspector_.color_popup_node_id, inspector_.color_popup_param_b, b);
    }
    if (mouse_.left_released) {
        inspector_.color_dragging_sv = false;
        inspector_.color_dragging_hue = false;
    }
}

void NodeGraphUI::update_chooser_hover() {
    if (!chooser_open_) return;
    float items_y = kChooserY + kChooserHeaderH;
    int visible = std::min(static_cast<int>(chooser_items_.size()), kChooserMaxVisible);
    if (mouse_.x >= chooser_x() && mouse_.x <= chooser_x() + kChooserW &&
        mouse_.y >= items_y && mouse_.y < items_y + visible * kChooserItemH &&
        !chooser_items_.empty()) {
        int idx = static_cast<int>(std::floor((mouse_.y - items_y + chooser_scroll_) / kChooserItemH));
        if (idx >= 0 && idx < static_cast<int>(chooser_items_.size()))
            chooser_sel_ = idx;
    }
}

void NodeGraphUI::update_pan_release() {
    if (mouse_.left_released && panning_ && dragging_node_idx_ < 0) {
        panning_ = false;
    }
}

void NodeGraphUI::clear_frame_flags() {
    mouse_.left_clicked = false;
    mouse_.left_released = false;
    mouse_.right_clicked = false;
    mouse_.right_released = false;
}

void NodeGraphUI::update_node_hover() {
    hovered_node_id_.clear();
    if (dragging_wire_ || panning_ || box_selecting_ || dragging_node_idx_ >= 0 ||
        context_menu_open_ || chooser_open_ || inspector_.dropdown_open) return;
    float gx = sx_to_gx(mouse_.x);
    float gy = sy_to_gy(mouse_.y);
    for (const auto& nr : node_rects_) {
        if (gx >= nr.x && gx <= nr.x + nr.w && gy >= nr.y && gy <= nr.y + nr.h) {
            hovered_node_id_ = nr.node_id;
            break;
        }
    }
}

void NodeGraphUI::update_wire_hover() {
    if (!dragging_wire_ && !panning_ && !box_selecting_ && dragging_node_idx_ < 0 &&
        !context_menu_open_ && !chooser_open_ && !inspector_.dropdown_open) {
        hovered_wire_idx_ = hit_test_wire(mouse_.x, mouse_.y);
    } else {
        hovered_wire_idx_ = -1;
    }
}

void NodeGraphUI::update_sparklines() {
    for (const auto& ns : snap_.nodes) {
        if (ns.is_gpu || ns.active_cadence == Cadence::Audio) continue;

        auto sorted_outs = sorted_ports(ns.output_port_indices);
        if (sorted_outs.empty()) continue;
        if (sorted_outs[0].first >= ns.output_values.size()) continue;

        std::string key = ns.node_id + "/" + sorted_outs[0].second;
        float val = ns.output_values[sorted_outs[0].first];

        auto& sd = sparklines_[key];
        sd.values[sd.write_idx] = val;
        sd.write_idx = (sd.write_idx + 1) % kSparklineLen;
        if (sd.write_idx == 0) sd.filled = true;
    }
}

// toggle_preferences(), set_editor_options(), set_style_options() moved to DialogManager

// show_core_update_notice, clear_core_update_notice, set_core_update_notice_callbacks
// moved to DialogManager (inlined in node_graph.h)

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

// -----------------------------------------------------------------------
// wire_inspector_visible — true when a wire with a numeric dest type is selected
// -----------------------------------------------------------------------
bool NodeGraphUI::wire_inspector_visible() const {
    if (selected_wire_idx_ < 0 ||
        selected_wire_idx_ >= static_cast<int>(snap_.connections.size()))
        return false;
    const auto& c = snap_.connections[selected_wire_idx_];
    VividPortType t = resolve_port_type(snap_, c.to_node, c.to_port, false);
    return is_numeric_type(t);
}

// -----------------------------------------------------------------------
// Parameter picker popup
// -----------------------------------------------------------------------
void NodeGraphUI::rebuild_param_picker_items() {
    inspector_.param_picker_items.clear();
    inspector_.param_picker_item_is_param.clear();
    const auto* ns = snap_.find_node(inspector_.param_picker_node_id);
    if (!ns || !ns->op_info) return;

    if (inspector_.param_picker_is_output) {
        // Picking an output port on this node (source side)
        // Output ports first
        auto sorted_outs = sorted_ports(ns->output_port_indices);
        for (const auto& [idx, name] : sorted_outs) {
            inspector_.param_picker_items.push_back(name);
            inspector_.param_picker_item_is_param.push_back(false);
        }
        // Params (non-FILE, not already an output port name)
        std::vector<std::pair<uint32_t, std::string>> sorted_params;
        for (const auto& [name, idx] : ns->param_indices)
            if (!ns->output_port_indices.count(name)) sorted_params.push_back({idx, name});
        std::sort(sorted_params.begin(), sorted_params.end());
        for (const auto& [idx, name] : sorted_params) {
            const ParamInfo* pd = ns->find_param(name);
            if (pd && (pd->type == VIVID_PARAM_FILE || pd->type == VIVID_PARAM_TEXT)) continue;
            inspector_.param_picker_items.push_back(name);
            inspector_.param_picker_item_is_param.push_back(true);
        }
    } else {
        // Picking an input param on this node (destination side)
        // Determine source port type for compatibility filtering
        VividPortType src_type;
        std::string src_semantic_tag;
        if (!wire_from_is_output_)
            src_type = VIVID_PORT_SCALAR;  // param sources are always float
        else
            src_type = resolve_port_type(snap_, inspector_.param_picker_wire_from_node,
                                          inspector_.param_picker_wire_from_port, true);
        src_semantic_tag = semantic_tag_for_snapshot_endpoint(
            snap_, inspector_.param_picker_wire_from_node, inspector_.param_picker_wire_from_port);

        struct PickerCandidate {
            std::string name;
            bool is_param = false;
            int semantic_score = 0;
            size_t order = 0;
        };
        std::vector<PickerCandidate> candidates;
        size_t candidate_order = 0;

        // Add signal input ports first
        auto sorted_ins = sorted_ports(ns->input_port_indices);
        for (const auto& [idx, name] : sorted_ins) {
            // Check not already connected from this source
            bool already_connected = false;
            for (const auto& c : snap_.connections) {
                if (c.to_node == inspector_.param_picker_node_id && c.to_port == name) {
                    already_connected = true;
                    break;
                }
            }
            if (already_connected) continue;

            // Check type compatibility
            VividPortType dest_type = VIVID_PORT_SCALAR;
            for (const auto& p : ns->op_info->ports) {
                if (p.name == name && p.direction == VIVID_PORT_INPUT) {
                    dest_type = p.type;
                    break;
                }
            }
            if (!port_type_compatible(src_type, dest_type)) continue;
            int semantic_score = 0;
            if (!src_semantic_tag.empty()) {
                std::string candidate_tag =
                    semantic_tag_for_snapshot_endpoint(snap_, inspector_.param_picker_node_id, name);
                if (!candidate_tag.empty() && candidate_tag == src_semantic_tag)
                    semantic_score = 1;
            }
            candidates.push_back(PickerCandidate{name, false, semantic_score, candidate_order++});
        }

        // Add params (not already signal ports, not FILE type, not already connected)
        std::vector<std::pair<uint32_t, std::string>> sorted_params;
        for (const auto& [name, idx] : ns->param_indices)
            if (!ns->input_port_indices.count(name)) sorted_params.push_back({idx, name});
        std::sort(sorted_params.begin(), sorted_params.end());

        for (const auto& [idx, name] : sorted_params) {
            // Skip FILE params (can't wire to them)
            const ParamInfo* pd = ns->find_param(name);
            if (pd && (pd->type == VIVID_PARAM_FILE || pd->type == VIVID_PARAM_TEXT)) continue;

            // Skip if already connected
            bool already_connected = false;
            for (const auto& c : snap_.connections) {
                if (c.to_node == inspector_.param_picker_node_id && c.to_port == name) {
                    already_connected = true;
                    break;
                }
            }
            if (already_connected) continue;

            int semantic_score = 0;
            if (!src_semantic_tag.empty() && pd &&
                !pd->semantic_tag.empty() && pd->semantic_tag == src_semantic_tag) {
                semantic_score = 1;
            }
            candidates.push_back(PickerCandidate{name, true, semantic_score, candidate_order++});
        }

        std::stable_sort(candidates.begin(), candidates.end(),
            [](const PickerCandidate& a, const PickerCandidate& b) {
                if (a.semantic_score != b.semantic_score)
                    return a.semantic_score > b.semantic_score;
                return a.order < b.order;
            });
        for (const auto& c : candidates) {
            inspector_.param_picker_items.push_back(c.name);
            inspector_.param_picker_item_is_param.push_back(c.is_param);
        }
    }
}

void NodeGraphUI::update_param_picker() {
    if (!inspector_.param_picker_open) return;

    // Hover tracking
    int visible = std::min(static_cast<int>(inspector_.param_picker_items.size()), kPickerMaxVisible);
    float items_y = inspector_.param_picker_y;
    float items_h = visible * kPickerItemH;

    if (mouse_.x >= inspector_.param_picker_x && mouse_.x <= inspector_.param_picker_x + kPickerW &&
        mouse_.y >= items_y && mouse_.y < items_y + items_h) {
        int idx = static_cast<int>(std::floor((mouse_.y - items_y + inspector_.param_picker_scroll) / kPickerItemH));
        if (idx >= 0 && idx < static_cast<int>(inspector_.param_picker_items.size()))
            inspector_.param_picker_sel = idx;
    }

    // Click handling
    if (mouse_.left_clicked) {
        if (mouse_.x >= inspector_.param_picker_x && mouse_.x <= inspector_.param_picker_x + kPickerW &&
            mouse_.y >= items_y && mouse_.y < items_y + items_h &&
            !inspector_.param_picker_items.empty()) {
            int idx = static_cast<int>(std::floor((mouse_.y - items_y + inspector_.param_picker_scroll) / kPickerItemH));
            if (idx >= 0 && idx < static_cast<int>(inspector_.param_picker_items.size())) {
                const std::string& selected = inspector_.param_picker_items[idx];
                if (inspector_.param_picker_is_output) {
                    // Selected an output port or param — now start a wire drag from it
                    const auto* ns = snap_.find_node(inspector_.param_picker_node_id);
                    if (ns) {
                        bool is_param = (!inspector_.param_picker_item_is_param.empty() &&
                                         idx < static_cast<int>(inspector_.param_picker_item_is_param.size()) &&
                                         inspector_.param_picker_item_is_param[idx]);
                        dragging_wire_ = true;
                        wire_from_node_id_ = inspector_.param_picker_node_id;
                        wire_from_port_ = selected;
                        wire_from_is_output_ = !is_param;
                        // Find port position or use node center
                        for (const auto& r : node_rects_) {
                            if (r.node_id == inspector_.param_picker_node_id) {
                                wire_from_gx_ = r.x + r.w;
                                wire_from_gy_ = r.y + r.h * 0.5f;
                                if (!is_param) {
                                    for (const auto& p : r.outputs) {
                                        if (p.name == selected) {
                                            wire_from_gx_ = p.x;
                                            wire_from_gy_ = p.y;
                                            break;
                                        }
                                    }
                                }
                                break;
                            }
                        }
                    }
                } else {
                    // Selected a destination param — create connection
                    commands_.connect(inspector_.param_picker_wire_from_node + "/" + inspector_.param_picker_wire_from_port,
                                 inspector_.param_picker_node_id + "/" + selected);
                }
                inspector_.param_picker_open = false;
            }
        } else {
            // Clicked outside — close
            inspector_.param_picker_open = false;
        }
        mouse_.left_clicked = false;
        mouse_.left_released = false;
    }
}



} // namespace vivid::ui
