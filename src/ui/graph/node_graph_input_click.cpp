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


void NodeGraphUI::handle_left_click() {
    if (!mouse_.left_clicked) return;
    if (handle_chooser_click()) return;
    if (handle_dropdown_click()) return;

    if (transport_bpm_editing_) {
        const bool inside_bpm_rect =
            transport_bpm_rect_.visible &&
            mouse_.x >= transport_bpm_rect_.x &&
            mouse_.x <= transport_bpm_rect_.x + transport_bpm_rect_.w &&
            mouse_.y >= transport_bpm_rect_.y &&
            mouse_.y <= transport_bpm_rect_.y + transport_bpm_rect_.h;
        if (!inside_bpm_rect) {
            confirm_transport_bpm_edit();
        } else {
            mouse_.left_clicked = false;
            return;
        }
    }

    const bool inside_diagnostics_button =
        diagnostics_button_rect_.visible &&
        mouse_.x >= diagnostics_button_rect_.x &&
        mouse_.x <= diagnostics_button_rect_.x + diagnostics_button_rect_.w &&
        mouse_.y >= diagnostics_button_rect_.y &&
        mouse_.y <= diagnostics_button_rect_.y + diagnostics_button_rect_.h;
    const bool inside_diagnostics_panel =
        diagnostics_panel_rect_.visible &&
        mouse_.x >= diagnostics_panel_rect_.x &&
        mouse_.x <= diagnostics_panel_rect_.x + diagnostics_panel_rect_.w &&
        mouse_.y >= diagnostics_panel_rect_.y &&
        mouse_.y <= diagnostics_panel_rect_.y + diagnostics_panel_rect_.h;

    if (diagnostics_panel_open_ && !inside_diagnostics_button && !inside_diagnostics_panel) {
        diagnostics_panel_open_ = false;
    }

    if (inside_diagnostics_panel) {
        for (const auto& dr : diagnostics_mcp_rects_) {
            if (mouse_.x >= dr.x && mouse_.x <= dr.x + dr.w &&
                mouse_.y >= dr.y && mouse_.y <= dr.y + dr.h) {
                dialogs_.open_mcp_setup();
                mouse_.left_clicked = false;
                return;
            }
        }
        mouse_.left_clicked = false;
        return;
    }

    // Phase 6b: lockfile badge click → open findings modal.
    if (lockfile_badge_rect_.visible &&
        mouse_.x >= lockfile_badge_rect_.x &&
        mouse_.x <= lockfile_badge_rect_.x + lockfile_badge_rect_.w &&
        mouse_.y >= lockfile_badge_rect_.y &&
        mouse_.y <= lockfile_badge_rect_.y + lockfile_badge_rect_.h) {
        dialogs_.open_lockfile_findings(snap_.lockfile_status);
        mouse_.left_clicked = false;
        return;
    }

    {
        float bottom_offset = session_grid_open_ ? session_strip_height() : 0.0f;
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

    if (transport_bpm_rect_.visible &&
        mouse_.x >= transport_bpm_rect_.x &&
        mouse_.x <= transport_bpm_rect_.x + transport_bpm_rect_.w &&
        mouse_.y >= transport_bpm_rect_.y &&
        mouse_.y <= transport_bpm_rect_.y + transport_bpm_rect_.h) {
        double now = glfwGetTime();
        if (transport_bpm_last_click_time_ >= 0.0 &&
            (now - transport_bpm_last_click_time_) < 0.4) {
            transport_bpm_dragging_ = false;
            transport_bpm_editing_ = true;
            transport_bpm_edit_buffer_ = format_float(std::max(1.0f, snap_.metronome_bpm), 1);
            text_edit_.select_all(static_cast<int>(transport_bpm_edit_buffer_.size()));
            transport_bpm_last_click_time_ = -1.0;
        } else {
            transport_bpm_dragging_ = true;
            transport_bpm_drag_start_y_ = mouse_.y;
            transport_bpm_drag_start_bpm_ =
                std::clamp(snap_.metronome_bpm > 0.0f ? snap_.metronome_bpm : 120.0f, 1.0f, 300.0f);
            transport_bpm_last_click_time_ = now;
            text_edit_.reset(0);
        }
        mouse_.left_clicked = false;
        return;
    }

    // Workspace header buttons
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
            } else if (btn.action == 2) {  // Diagnostics
                diagnostics_panel_open_ = !diagnostics_panel_open_;
            } else if (btn.action == 6) {  // Meter-
                const int beats_per_bar = std::max(1, snap_.metronome_beats_per_bar - 1);
                commands_.set_graph_metronome(snap_.metronome_bpm, beats_per_bar);
            } else if (btn.action == 7) {  // Meter+
                const int beats_per_bar = std::min(16, snap_.metronome_beats_per_bar + 1);
                commands_.set_graph_metronome(snap_.metronome_bpm, beats_per_bar);
            }
            mouse_.left_clicked = false;
            return;
        }
    }

    if (!session_grid_open_ && session_collapsed_rect_.visible &&
        mouse_.x >= session_collapsed_rect_.x &&
        mouse_.x <= session_collapsed_rect_.x + session_collapsed_rect_.w &&
        mouse_.y >= session_collapsed_rect_.y &&
        mouse_.y <= session_collapsed_rect_.y + session_collapsed_rect_.h) {
        toggle_session_grid();
        mouse_.left_clicked = false;
        return;
    }

    {
        float bottom_offset = session_grid_open_ ? session_strip_height() : 0.0f;
        if (build_console_panel_.handle_left_press(mouse_.x, mouse_.y, win_w_, win_h_, bottom_offset)) {
            mouse_.left_clicked = false;
            return;
        }
    }

    // Session grid click handling
    if (session_grid_open_ && mouse_.y >= session_strip_top()) {
        // Context menu dispatch
        if (session_ctx_menu_open_) {
            for (const auto& cr : session_ctx_menu_rects_) {
                if (mouse_.x >= cr.x && mouse_.x <= cr.x + cr.w &&
                    mouse_.y >= cr.y && mouse_.y <= cr.y + cr.h) {
                    if (session_ctx_menu_idx_ == 1) {
                        // Track context menu
                        if (cr.action == 0) {  // Rename
                            session_edit_type_ = 1;
                            session_edit_id_ = session_edit_track_id_;
                            session_edit_buffer_ = "";
                            // Find current name
                            for (const auto& t : snap_.session.tracks)
                                if (t.id == session_edit_id_) { session_edit_buffer_ = t.name; break; }
                            text_edit_.select_all(static_cast<int>(session_edit_buffer_.size()));
                            session_editing_name_ = true;
                        } else if (cr.action == 1) {  // Assign Selected
                            std::vector<std::string> ids(selected_node_ids_.begin(), selected_node_ids_.end());
                            commands_.session_assign_nodes(session_edit_track_id_, ids);
                        } else if (cr.action == 2) {  // Remove
                            commands_.session_remove_track(session_edit_track_id_);
                        }
                    } else if (session_ctx_menu_idx_ == 2) {
                        // Scene context menu
                        if (cr.action == 0) {  // Rename
                            session_edit_type_ = 3;
                            session_edit_id_ = session_edit_track_id_;  // reused as scene_id
                            session_edit_buffer_ = "";
                            for (const auto& s : snap_.session.scenes)
                                if (s.id == session_edit_id_) { session_edit_buffer_ = s.name; break; }
                            text_edit_.select_all(static_cast<int>(session_edit_buffer_.size()));
                            session_editing_name_ = true;
                        } else if (cr.action == 1) {  // Update
                            commands_.session_update_scene(session_edit_track_id_);
                        } else if (cr.action == 2) {  // Remove
                            commands_.session_remove_scene(session_edit_track_id_);
                        } else if (cr.action == 3) {  // Add to Cue
                            if (!snap_.session.cue_paths.empty())
                                commands_.session_add_cue_step(snap_.session.cue_paths.front().id,
                                                               session_edit_track_id_, -1);
                        }
                    } else if (session_ctx_menu_idx_ == 3) {
                        // Clip cell context menu
                        if (cr.action == 0) {  // Open Clip (show in inspector)
                            selected_clip_track_ = session_ctx_cell_track_id_;
                            selected_clip_id_    = session_ctx_cell_clip_id_;
                            selected_node_ids_.clear();  // clip inspector takes the panel
                        } else if (cr.action == 1) {  // Update Clip
                            commands_.session_update_clip(session_ctx_cell_track_id_,
                                                          session_ctx_cell_clip_id_);
                        } else if (cr.action == 2) {  // Rename Clip
                            session_edit_type_ = 2;
                            session_edit_id_       = session_ctx_cell_clip_id_;
                            session_edit_track_id_ = session_ctx_cell_track_id_;
                            session_edit_buffer_ = "";
                            const auto* ts = snap_.session.find_track(session_ctx_cell_track_id_);
                            if (ts) {
                                for (const auto& c : ts->clips)
                                    if (c.id == session_ctx_cell_clip_id_) { session_edit_buffer_ = c.name; break; }
                            }
                            text_edit_.select_all(static_cast<int>(session_edit_buffer_.size()));
                            session_editing_name_ = true;
                        } else if (cr.action == 3) {  // Remove Clip
                            commands_.session_remove_clip(session_ctx_cell_track_id_,
                                                          session_ctx_cell_clip_id_);
                        } else if (cr.action == 4) {  // Clear from Scene
                            commands_.session_clear_scene_assignment(session_ctx_cell_scene_id_,
                                                                      session_ctx_cell_track_id_);
                        }
                    } else if (session_ctx_menu_idx_ == 4) {
                        // Empty cell context menu
                        if (cr.action == 0) {  // Assign Active Clip
                            const auto* ts = snap_.session.find_track(session_ctx_cell_track_id_);
                            if (ts && !ts->active_clip_id.empty())
                                commands_.session_set_scene_assignment(session_ctx_cell_scene_id_,
                                                                        session_ctx_cell_track_id_,
                                                                        ts->active_clip_id);
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

        // Resize handle — start drag
        {
            const auto& rh = session_resize_handle_;
            if (mouse_.x >= rh.x && mouse_.x < rh.x + rh.w &&
                mouse_.y >= rh.y && mouse_.y < rh.y + rh.h) {
                session_resize_active_ = true;
                session_resize_start_y_ = mouse_.y;
                session_resize_start_h_ = session_panel_h_;
                mouse_.left_clicked = false;
                return;
            }
        }

        // Quantize and close buttons
        for (const auto& br : session_button_rects_) {
            if (mouse_.x >= br.x && mouse_.x <= br.x + br.w &&
                mouse_.y >= br.y && mouse_.y <= br.y + br.h) {
                if (br.action >= 2 && br.action <= 5) {
                    session_quantize_mode_ = br.action - 2;
                    clear_status_banner();
                } else if (br.action == 7) {
                    toggle_session_grid();
                }
                mouse_.left_clicked = false;
                return;
            }
        }

        // Track header clicks
        for (const auto& tr : session_track_rects_) {
            if (!(mouse_.x >= tr.x && mouse_.x < tr.x + tr.w &&
                  mouse_.y >= tr.y && mouse_.y < tr.y + tr.h)) continue;
            if (tr.action == 1) {
                // "+" save-clip button
                const auto* tsnap = snap_.session.find_track(tr.track_id);
                int n = tsnap ? static_cast<int>(tsnap->clips.size()) + 1 : 1;
                commands_.session_save_clip(tr.track_id, "Clip " + std::to_string(n));
            } else if (tr.action == 0) {
                // Full header left-click → select owned nodes in graph
                const auto* ts = snap_.session.find_track(tr.track_id);
                if (ts) {
                    selected_node_ids_.clear();
                    for (const auto& nid : ts->owned_node_ids)
                        selected_node_ids_.insert(nid);
                }
            }
            mouse_.left_clicked = false;
            return;
        }

        // "+ Add Track" button
        {
            const auto& b = session_add_track_btn_;
            if (b.w > 0 && mouse_.x >= b.x && mouse_.x < b.x + b.w &&
                mouse_.y >= b.y && mouse_.y < b.y + b.h) {
                int n = static_cast<int>(snap_.session.tracks.size()) + 1;
                commands_.session_create_track("Track " + std::to_string(n));
                mouse_.left_clicked = false;
                return;
            }
        }

        // Scene launch button (left side of scene row)
        for (const auto& sr : session_scene_rects_) {
            if (mouse_.x >= sr.x && mouse_.x < sr.x + sr.w &&
                mouse_.y >= sr.y && mouse_.y < sr.y + sr.h) {
                static const char* q_modes[] = {"instant", "beat", "bar", "4bar"};
                const char* q = q_modes[std::clamp(session_quantize_mode_, 0, 3)];
                commands_.session_queue_scene(sr.scene_id, q);
                mouse_.left_clicked = false;
                return;
            }
        }

        // Cue path controls
        {
            const auto& b = session_add_cue_path_btn_;
            if (b.w > 0 && mouse_.x >= b.x && mouse_.x < b.x + b.w &&
                mouse_.y >= b.y && mouse_.y < b.y + b.h) {
                int n = static_cast<int>(snap_.session.cue_paths.size()) + 1;
                commands_.session_create_cue_path("Cue Path " + std::to_string(n));
                mouse_.left_clicked = false;
                return;
            }
        }
        {
            const auto& b = session_cue_advance_btn_;
            if (b.w > 0 && mouse_.x >= b.x && mouse_.x < b.x + b.w &&
                mouse_.y >= b.y && mouse_.y < b.y + b.h &&
                !snap_.session.active_cue_path_id.empty()) {
                static const char* q_modes[] = {"instant", "beat", "bar", "4bar"};
                const char* q = q_modes[std::clamp(session_quantize_mode_, 0, 3)];
                commands_.session_advance_cue_path(snap_.session.active_cue_path_id, q);
                mouse_.left_clicked = false;
                return;
            }
        }
        {
            const auto& b = session_cue_stop_btn_;
            if (b.w > 0 && mouse_.x >= b.x && mouse_.x < b.x + b.w &&
                mouse_.y >= b.y && mouse_.y < b.y + b.h) {
                const std::string& path_id = !snap_.session.active_cue_path_id.empty()
                    ? snap_.session.active_cue_path_id : snap_.session.queued_cue_path_id;
                if (!path_id.empty())
                    commands_.session_stop_cue_path(path_id);
                mouse_.left_clicked = false;
                return;
            }
        }
        for (const auto& cr : session_cue_step_rects_) {
            if (mouse_.x >= cr.x && mouse_.x < cr.x + cr.w &&
                mouse_.y >= cr.y && mouse_.y < cr.y + cr.h) {
                static const char* q_modes[] = {"instant", "beat", "bar", "4bar"};
                const char* q = q_modes[std::clamp(session_quantize_mode_, 0, 3)];
                commands_.session_launch_cue_step(cr.path_id, cr.step_id, q);
                mouse_.left_clicked = false;
                return;
            }
        }

        // Grid cell click → launch clip for this track
        for (const auto& cr : session_cell_rects_) {
            if (cr.clip_id.empty()) continue;
            if (mouse_.x >= cr.x && mouse_.x < cr.x + cr.w &&
                mouse_.y >= cr.y && mouse_.y < cr.y + cr.h) {
                static const char* q_modes[] = {"instant", "beat", "bar", "4bar"};
                const char* q = q_modes[std::clamp(session_quantize_mode_, 0, 3)];
                commands_.session_queue_clip(cr.track_id, cr.clip_id, q);
                mouse_.left_clicked = false;
                return;
            }
        }

        // "+ Save Scene" button
        {
            const auto& b = session_add_scene_btn_;
            if (b.w > 0 && mouse_.x >= b.x && mouse_.x < b.x + b.w &&
                mouse_.y >= b.y && mouse_.y < b.y + b.h) {
                int n = static_cast<int>(snap_.session.scenes.size()) + 1;
                commands_.session_save_scene("Scene " + std::to_string(n));
                mouse_.left_clicked = false;
                return;
            }
        }

        // Clicked in session strip but not on any interactive element
        session_selected_idx_ = -1;
        mouse_.left_clicked = false;
        return;
    }

    if (handle_inspector_click()) return;
    handle_graph_click();
}

void NodeGraphUI::handle_graph_click() {
    if (mouse_.x >= graph_right() || mouse_.y >= static_cast<float>(win_h_))
        return;

    // Clicking in graph area confirms any active text edit
    if (inspector_.editing_param) confirm_param_edit();
    if (inspector_.editing_resolution) confirm_resolution_edit();
    if (inspector_.editing_node_id) cancel_node_id_edit();
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
                uint8_t ach = snap_.audio_channel_count(ar.node_id);
                float body_h = node_body_height(rect.is_gpu, rect.active_cadence, has_ct, ach);
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

            // Double-click detection: open the operator's editor if it has one.
            double now = glfwGetTime();
            if (node_id == last_click_node_id_ && (now - last_click_time_) < 0.3) {
                const std::string& type_name = node_rects_[ni].type_name;
                auto cat_it = snap_.operator_catalog.find(type_name);
                bool is_user = cat_it != snap_.operator_catalog.end() &&
                               cat_it->second && cat_it->second->is_user;
                bool is_module = cat_it != snap_.operator_catalog.end() &&
                                 cat_it->second && cat_it->second->is_module;
                const NodeSnapshot* ns = snap_.find_node(node_id);
                bool has_editor = ns && ns->op_info && ns->op_info->has_editor;
                if (has_editor) {
                    // Operators that define an editor window (e.g. MIDI Clip,
                    // Drum Sequencer) open it. Clone now lives in the context menu.
                    commands_.open_editor(node_id);
                } else if (is_user) {
                    commands_.open_shader(type_name);
                } else if (is_module) {
                    commands_.open_module_source(type_name);
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
                        sticky_undo_seed();
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


} // namespace vivid::ui
