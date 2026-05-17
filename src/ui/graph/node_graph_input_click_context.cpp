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


void NodeGraphUI::update_context_menu() {
    size_t cur_nodes = snap_.nodes.size();
    size_t cur_conns = snap_.connections.size();

    // Close param context menu on topology change
    if (inspector_.param_ctx_menu_open &&
        (cur_nodes != last_node_count_ || cur_conns != last_conn_count_)) {
        inspector_.param_ctx_menu_open = false;
    }

    // Param context menu click handling
    if (inspector_.param_ctx_menu_open && mouse_.left_clicked) {
        float mx = inspector_.param_ctx_menu_x;
        float my = inspector_.param_ctx_menu_y;
        float menu_h = kCtxMenuPadTop + kCtxMenuItemH + 2.0f;
        if (mouse_.x >= mx && mouse_.x <= mx + kCtxMenuW &&
            mouse_.y >= my && mouse_.y <= my + menu_h) {
            // "Reset to Default" clicked
            const auto* ns = snap_.find_node(inspector_.param_ctx_node_id);
            if (ns && ns->op_info) {
                for (uint32_t i = 0; i < ns->op_info->params.size(); ++i) {
                    if (ns->op_info->params[i].name == inspector_.param_ctx_param_name) {
                        const auto& pd = ns->op_info->params[i];
                        if (pd.type == VIVID_PARAM_FILE || pd.type == VIVID_PARAM_TEXT) {
                            commands_.set_string_param(ns->node_id, pd.name, pd.default_string);
                        } else {
                            commands_.set_param(ns->node_id, pd.name, pd.default_value);
                        }
                        break;
                    }
                }
            }
            mouse_.left_clicked = false;
            mouse_.left_released = false;
        }
        inspector_.param_ctx_menu_open = false;
    }

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
        // Solo + Reset All Params items for node context menus
        bool show_solo = !context_node_id_.empty() && !context_bg_menu_ && !is_sticky_ctx;
        if (show_solo) item_count += 2;
        // "Make many…" appears on drawable-pipeline emitter nodes.
        bool show_make_many = !context_node_id_.empty() && !context_bg_menu_ && !is_sticky_ctx
                              && is_drawable_emitter_type(context_node_type_);
        if (show_make_many) item_count += 1;
        // Bypass — only when the operator is bypass-eligible.
        const NodeSnapshot* ctx_ns_click = (!context_node_id_.empty() && !is_sticky_ctx)
            ? snap_.find_node(context_node_id_) : nullptr;
        bool show_bypass_click = ctx_ns_click && ctx_ns_click->bypassable;
        if (show_bypass_click) item_count += 1;

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
                    sticky_undo_seed();
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
                const bool show_make_many_click = is_drawable_emitter_type(context_node_type_);
                int delete_idx = 0;
                int clone_idx = context_node_has_shader_ ? 1 : -1;
                int solo_idx = context_node_has_shader_ ? 2 : 1;
                int bypass_idx = show_bypass_click ? (solo_idx + 1) : -1;
                int reset_idx = (bypass_idx >= 0 ? bypass_idx : solo_idx) + 1;
                int make_many_idx = show_make_many_click ? (reset_idx + 1) : -1;

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
                    open_clone_confirm_dialog(context_node_type_, context_node_id_);
                } else if (clicked_item == solo_idx) {
                    // "Solo" / "Unsolo"
                    bool is_soloed = (!snap_.solo_node_id.empty() && snap_.solo_node_id == context_node_id_);
                    commands_.set_solo(is_soloed ? "" : context_node_id_);
                } else if (bypass_idx >= 0 && clicked_item == bypass_idx) {
                    // "Bypass" / "Unbypass"
                    bool currently = ctx_ns_click && ctx_ns_click->bypassed;
                    commands_.set_node_bypassed(context_node_id_, !currently);
                } else if (clicked_item == reset_idx) {
                    // "Reset All Params"
                    const auto* ns = snap_.find_node(context_node_id_);
                    if (ns && ns->op_info) {
                        for (uint32_t i = 0; i < ns->op_info->params.size(); ++i) {
                            const auto& pd = ns->op_info->params[i];
                            if (pd.type == VIVID_PARAM_FILE || pd.type == VIVID_PARAM_TEXT) {
                                auto fit = ns->file_param_values.find(pd.name);
                                std::string current = (fit != ns->file_param_values.end()) ? fit->second : "";
                                if (current != pd.default_string)
                                    commands_.set_string_param(ns->node_id, pd.name, pd.default_string);
                            } else {
                                if (i < ns->param_values.size() && ns->param_values[i] != pd.default_value)
                                    commands_.set_param(ns->node_id, pd.name, pd.default_value);
                            }
                        }
                    }
                } else if (clicked_item == make_many_idx) {
                    // "Make many…" — insert Instancer2D + InstanceGrid2D downstream.
                    make_many_from_node(context_node_id_);
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
                        // Auto-select tab based on wire type
                        if (insert_wire_source_type_ == VIVID_PORT_TEXTURE)
                            chooser_tab_ = ChooserTab::GPU;
                        else if (insert_wire_source_type_ == VIVID_PORT_AUDIO_BUFFER)
                            chooser_tab_ = ChooserTab::Audio;
                        else
                            chooser_tab_ = ChooserTab::Control;
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
        float bottom_offset = session_grid_open_ ? session_strip_height() : 0.0f;
        if (build_console_panel_.contains(mouse_.x, mouse_.y, win_w_, win_h_, bottom_offset))
            return;
    }

    // Right-click on param label in inspector — open param context menu
    if (mouse_.x >= graph_right()) {
        for (int i = 0; i < static_cast<int>(inspector_.label_rects.size()); ++i) {
            const auto& r = inspector_.label_rects[i];
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                inspector_.param_ctx_menu_open = true;
                inspector_.param_ctx_menu_x = mouse_.x;
                inspector_.param_ctx_menu_y = mouse_.y;
                inspector_.param_ctx_node_id = r.node_id;
                inspector_.param_ctx_param_name = r.param_name;
                return;
            }
        }
        return;  // right-click in inspector but not on a label — ignore
    }

    // Session grid right-click — track header and scene row context menus
    if (session_grid_open_ && mouse_.y >= session_strip_top()) {
        // Track header right-click (action=0 = full header, check before "+" button)
        for (const auto& tr : session_track_rects_) {
            if (tr.action != 0) continue;
            if (mouse_.x >= tr.x && mouse_.x < tr.x + tr.w &&
                mouse_.y >= tr.y && mouse_.y < tr.y + tr.h) {
                session_ctx_menu_open_ = true;
                session_ctx_menu_idx_ = 1;           // track context
                session_edit_track_id_ = tr.track_id; // target id stored here
                session_ctx_menu_x_ = mouse_.x;
                session_ctx_menu_y_ = mouse_.y;
                return;
            }
        }
        // Scene row right-click
        for (const auto& sr : session_scene_rects_) {
            if (mouse_.x >= sr.x && mouse_.x < sr.x + sr.w &&
                mouse_.y >= sr.y && mouse_.y < sr.y + sr.h) {
                session_ctx_menu_open_ = true;
                session_ctx_menu_idx_ = 2;           // scene context
                session_edit_track_id_ = sr.scene_id; // reuse field for scene_id
                session_ctx_menu_x_ = mouse_.x;
                session_ctx_menu_y_ = mouse_.y;
                return;
            }
        }
        // Cell right-click
        for (const auto& cr : session_cell_rects_) {
            if (!(mouse_.x >= cr.x && mouse_.x < cr.x + cr.w &&
                  mouse_.y >= cr.y && mouse_.y < cr.y + cr.h)) continue;
            if (!cr.clip_id.empty()) {
                session_ctx_menu_open_ = true;
                session_ctx_menu_idx_ = 3;
                session_ctx_cell_scene_id_ = cr.scene_id;
                session_ctx_cell_track_id_ = cr.track_id;
                session_ctx_cell_clip_id_  = cr.clip_id;
                session_ctx_menu_x_ = mouse_.x;
                session_ctx_menu_y_ = mouse_.y;
            } else {
                const auto* ts = snap_.session.find_track(cr.track_id);
                if (ts && !ts->active_clip_id.empty()) {
                    session_ctx_menu_open_ = true;
                    session_ctx_menu_idx_ = 4;
                    session_ctx_cell_scene_id_ = cr.scene_id;
                    session_ctx_cell_track_id_ = cr.track_id;
                    session_ctx_cell_clip_id_  = "";
                    session_ctx_menu_x_ = mouse_.x;
                    session_ctx_menu_y_ = mouse_.y;
                }
            }
            mouse_.right_clicked = false;
            return;
        }
        session_ctx_menu_open_ = false;
        return;
    }

    inspector_.param_ctx_menu_open = false;  // close param menu when opening graph context menu
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
    if (async_add_active_ || async_graph_load_active_) return;
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
    chooser_tab_ = ChooserTab::All;
    chooser_error_.clear();
    rebuild_chooser_items();
    chooser_open_ = true;
}

} // namespace vivid::ui
