#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/active_text_field.h"
#include "ui/rendering/overlay_layouts.h"
#include "ui/style/i18n.h"
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

    // Reject clicks above the workspace header (clipped-off content)
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
                inspector_.dropdown_labels.push_back(T("preset_none", "(none)"));
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
                    ui::PresetMenuNode{T("preset_none", "(none)"), "", false, false, {}});
                inspector_.dropdown_submenu_stack.clear();
                inspector_.dropdown_submenu_stack.push_back({&inspector_.dropdown_menu_tree, -1,
                    inspector_.dropdown_x, inspector_.dropdown_y, inspector_.dropdown_w});
                inspector_.dropdown_hover_frames = 0;
                inspector_.dropdown_hover_target = -1;
            }
            return true;
        }
    }

    // Check modulation controls
    {
        auto cycle_assignment = [&](const InspectorController::ModAssignRect& r,
                                    const std::string& next_source,
                                    const std::string& next_destination,
                                    const std::string& next_polarity) {
            const auto* ns = snap_.find_node(r.node_id);
            if (!ns) return true;
            auto it = std::find_if(ns->mod_assignments.begin(), ns->mod_assignments.end(),
                                   [&](const NodeSnapshot::ModAssignInfo& a) {
                return a.source == r.source && a.destination == r.destination;
            });
            if (it == ns->mod_assignments.end()) return true;

            std::string error;
            if (next_source != r.source || next_destination != r.destination) {
                if (!commands_.try_add_mod_assignment(r.node_id, next_source, next_destination,
                                                      it->amount, next_polarity, it->curve, &error)) {
                    inspector_.modulation_error = error;
                    return true;
                }
                commands_.try_remove_mod_assignment(r.node_id, r.source, r.destination, nullptr);
                inspector_.modulation_error.clear();
                return true;
            }

            if (!commands_.try_update_mod_assignment(r.node_id, r.source, r.destination,
                                                     it->amount, next_polarity, it->curve, &error)) {
                inspector_.modulation_error = error;
            } else {
                inspector_.modulation_error.clear();
            }
            return true;
        };

        for (const auto& r : inspector_.mod_assign_rects) {
            if (mouse_.x < r.x || mouse_.x > r.x + r.w ||
                mouse_.y < r.y || mouse_.y > r.y + r.h) {
                continue;
            }

            const auto* ns = snap_.find_node(r.node_id);
            if (!ns) return true;

            if (r.action == 4) {
                if (!ns->mod_sources.empty() && !ns->mod_destinations.empty()) {
                    std::string error;
                    if (!commands_.try_add_mod_assignment(r.node_id, ns->mod_sources.front().name,
                                                          ns->mod_destinations.front().name,
                                                          1.0f, "unipolar", "linear", &error)) {
                        inspector_.modulation_error = error;
                    } else {
                        inspector_.modulation_error.clear();
                    }
                }
                return true;
            }

            auto ait = std::find_if(ns->mod_assignments.begin(), ns->mod_assignments.end(),
                                    [&](const NodeSnapshot::ModAssignInfo& a) {
                return a.source == r.source && a.destination == r.destination;
            });
            if (ait == ns->mod_assignments.end()) return true;

            if (r.action == 0 && !ns->mod_sources.empty()) {
                int idx = 0;
                for (int i = 0; i < static_cast<int>(ns->mod_sources.size()); ++i)
                    if (ns->mod_sources[i].name == r.source) { idx = i; break; }
                idx = (idx + 1) % static_cast<int>(ns->mod_sources.size());
                return cycle_assignment(r, ns->mod_sources[idx].name, r.destination, ait->polarity);
            }
            if (r.action == 1 && !ns->mod_destinations.empty()) {
                int idx = 0;
                for (int i = 0; i < static_cast<int>(ns->mod_destinations.size()); ++i)
                    if (ns->mod_destinations[i].name == r.destination) { idx = i; break; }
                idx = (idx + 1) % static_cast<int>(ns->mod_destinations.size());
                return cycle_assignment(r, r.source, ns->mod_destinations[idx].name, ait->polarity);
            }
            if (r.action == 2) {
                std::string next = (ait->polarity == "bipolar") ? "unipolar" : "bipolar";
                return cycle_assignment(r, r.source, r.destination, next);
            }
            if (r.action == 3) {
                std::string error;
                if (!commands_.try_remove_mod_assignment(r.node_id, r.source, r.destination, &error)) {
                    inspector_.modulation_error = error;
                } else {
                    inspector_.modulation_error.clear();
                }
                return true;
            }
        }

        for (const auto& r : inspector_.mod_amount_rects) {
            if (mouse_.x >= r.x && mouse_.x <= r.x + r.w &&
                mouse_.y >= r.y && mouse_.y <= r.y + r.h) {
                inspector_.modulation_amount_dragging = true;
                inspector_.modulation_amount_node_id = r.node_id;
                inspector_.modulation_amount_source = r.source;
                inspector_.modulation_amount_destination = r.destination;
                inspector_.modulation_amount_range = r.range;
                return true;
            }
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
        const auto* ns = snap_.find_node(fr.node_id);
        const ParamInfo* pd = ns ? ns->find_param(fr.param_name) : nullptr;
        if (pd && !pd->asset_kind.empty()) {
            std::string current_value;
            if (ns) {
                auto it = ns->file_param_values.find(fr.param_name);
                if (it != ns->file_param_values.end())
                    current_value = it->second;
            }
            dialogs_.open_asset_browser(fr.node_id, fr.param_name, pd->asset_kind, current_value);
        } else {
            std::string path = vivid::ui::open_file_dialog();
            if (!path.empty()) {
                commands_.set_string_param(fr.node_id, fr.param_name, path);
            }
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

} // namespace vivid::ui
