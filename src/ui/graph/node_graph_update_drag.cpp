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
            }
        } else {
            // Single drag
            auto& rect = node_rects_[dragging_node_idx_];
            rect.x = mgx - drag_offset_x_;
            rect.y = mgy - drag_offset_y_;
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

void NodeGraphUI::update_transport_bpm_drag() {
    if (!transport_bpm_dragging_) return;
    if (mouse_.left_down) {
        const float delta_y = transport_bpm_drag_start_y_ - mouse_.y;
        if (std::fabs(delta_y) > 0.0f) {
            const float sensitivity = mouse_.shift_down ? 0.1f : 1.0f;
            const float bpm = std::clamp(transport_bpm_drag_start_bpm_ + (delta_y / 8.0f) * sensitivity,
                                         1.0f, 300.0f);
            commands_.set_graph_metronome(bpm, std::max(1, snap_.metronome_beats_per_bar));
            transport_bpm_drag_start_y_ = mouse_.y;
            transport_bpm_drag_start_bpm_ = bpm;
        }
    }
    if (mouse_.left_released) {
        transport_bpm_dragging_ = false;
    }
}

void NodeGraphUI::update_modulation_drag() {
    if (!inspector_.modulation_amount_dragging || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down) {
        const auto* ns = snap_.find_node(inspector_.modulation_amount_node_id);
        if (ns) {
            auto rit = std::find_if(inspector_.mod_amount_rects.begin(), inspector_.mod_amount_rects.end(),
                                    [&](const InspectorController::ModAmountRect& r) {
                return r.node_id == inspector_.modulation_amount_node_id &&
                       r.source == inspector_.modulation_amount_source &&
                       r.destination == inspector_.modulation_amount_destination;
            });
            auto ait = std::find_if(ns->mod_assignments.begin(), ns->mod_assignments.end(),
                                    [&](const NodeSnapshot::ModAssignInfo& a) {
                return a.source == inspector_.modulation_amount_source &&
                       a.destination == inspector_.modulation_amount_destination;
            });
            if (rit != inspector_.mod_amount_rects.end() && ait != ns->mod_assignments.end()) {
                float t = std::clamp((mouse_.x - rit->x) / rit->w, 0.0f, 1.0f);
                float range = std::max(0.01f, inspector_.modulation_amount_range);
                float amount = -range + t * (range * 2.0f);
                std::string error;
                if (!commands_.try_update_mod_assignment(inspector_.modulation_amount_node_id,
                                                         inspector_.modulation_amount_source,
                                                         inspector_.modulation_amount_destination,
                                                         amount, ait->polarity, ait->curve, &error)) {
                    inspector_.modulation_error = error;
                } else {
                    inspector_.modulation_error.clear();
                }
            }
        }
    }
    if (mouse_.left_released) {
        inspector_.modulation_amount_dragging = false;
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

void NodeGraphUI::update_adsr_drag() {
    if (inspector_.active_adsr_idx < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down && inspector_.active_adsr_idx < static_cast<int>(inspector_.adsr_rects.size())) {
        const auto& ar = inspector_.adsr_rects[inspector_.active_adsr_idx];
        const auto* ns = snap_.find_node(inspector_.active_adsr_node_id);
        if (ns) {
            auto get_param = [&](const std::string& pname) -> const ParamInfo* {
                return ns->find_param(pname);
            };
            auto get_val = [&](const std::string& pname, float fallback) -> float {
                auto it = ns->param_indices.find(pname);
                return (it != ns->param_indices.end()) ? ns->param_values[it->second] : fallback;
            };

            float attack  = std::max(0.0001f, get_val(ar.param_a, 0.01f));
            float decay   = std::max(0.001f,  get_val(ar.param_d, 0.2f));
            float sustain = std::clamp(get_val(ar.param_s, 0.7f), 0.0f, 1.0f);
            float release = std::max(0.001f,  get_val(ar.param_r, 0.3f));
            float sustain_width = 0.3f * (attack + decay + release);
            float total_time = attack + decay + sustain_width + release;
            float pad = kADSRPad;
            float plot_w = ar.w - 2.0f * pad;

            // Inverse transforms: mouse position → time / env level
            auto x_to_time = [&](float mx) -> float {
                return ((mx - ar.x - pad) / plot_w) * total_time;
            };
            auto y_to_env = [&](float my) -> float {
                return 1.0f - ((my - ar.y - pad) / (ar.h - 2.0f * pad));
            };

            int pt = inspector_.active_adsr_point;

            if (pt == 0) {
                // Attack peak: drag X → attack time
                const ParamInfo* pd = get_param(ar.param_a);
                if (pd) {
                    float t = x_to_time(mouse_.x);
                    float val = std::clamp(t, pd->min_value, pd->max_value);
                    commands_.set_param(inspector_.active_adsr_node_id, ar.param_a, val);
                }
            } else if (pt == 1) {
                // Decay/Sustain junction: X → decay time, Y → sustain level
                const ParamInfo* pd_d = get_param(ar.param_d);
                const ParamInfo* pd_s = get_param(ar.param_s);
                if (pd_d) {
                    float t = x_to_time(mouse_.x);
                    float decay_val = std::clamp(t - attack, pd_d->min_value, pd_d->max_value);
                    commands_.set_param(inspector_.active_adsr_node_id, ar.param_d, decay_val);
                }
                if (pd_s) {
                    float env = y_to_env(mouse_.y);
                    float sus_val = std::clamp(env, pd_s->min_value, pd_s->max_value);
                    commands_.set_param(inspector_.active_adsr_node_id, ar.param_s, sus_val);
                }
            } else if (pt == 2) {
                // Release end: drag X → release time
                const ParamInfo* pd = get_param(ar.param_r);
                if (pd) {
                    float t = x_to_time(mouse_.x);
                    float release_start = attack + decay + sustain_width;
                    float release_val = std::clamp(t - release_start, pd->min_value, pd->max_value);
                    commands_.set_param(inspector_.active_adsr_node_id, ar.param_r, release_val);
                }
            }
        }
    }
    if (mouse_.left_released) {
        inspector_.active_adsr_idx = -1;
        inspector_.active_adsr_point = -1;
    }
}

void NodeGraphUI::update_step_seq_drag() {
    if (inspector_.active_step_seq_idx < 0 || dragging_node_idx_ >= 0) return;
    if (mouse_.left_down && inspector_.active_step_seq_idx < static_cast<int>(inspector_.step_seq_rects.size())) {
        const auto& sr = inspector_.step_seq_rects[inspector_.active_step_seq_idx];
        const auto* ns = snap_.find_node(sr.node_id);
        if (ns && inspector_.active_step_seq_step >= 0) {
            int num_steps = std::max(1, static_cast<int>(ns->param_values[sr.pi_count]));
            float pad = kStepSeqPad;
            float plot_x = sr.x + pad;
            float plot_y = sr.y + pad;
            float plot_w = sr.w - 2.0f * pad;
            float plot_h = sr.h - 2.0f * pad;
            float bar_w = plot_w / static_cast<float>(num_steps);

            // Allow dragging across bars
            int step = static_cast<int>((mouse_.x - plot_x) / bar_w);
            step = std::max(0, std::min(step, num_steps - 1));
            if (step < static_cast<int>(sr.value_count)) {
                inspector_.active_step_seq_step = step;
                uint32_t vi = sr.pi_values + static_cast<uint32_t>(step);
                if (vi < ns->op_info->params.size()) {
                    const auto& vpd = ns->op_info->params[vi];
                    float norm = 1.0f - (mouse_.y - plot_y) / plot_h;
                    norm = std::max(0.0f, std::min(1.0f, norm));
                    float val = vpd.min_value + norm * (vpd.max_value - vpd.min_value);
                    commands_.set_param(sr.node_id, vpd.name, val);
                }
            }
        }
    }
    if (mouse_.left_released) {
        inspector_.active_step_seq_idx = -1;
        inspector_.active_step_seq_step = -1;
    }
}

void NodeGraphUI::update_color_drag() {
    if (!inspector_.color_popup_open) return;
    if (!inspector_.color_dragging_sv && !inspector_.color_dragging_hue) return;
    if (mouse_.left_down) {
        float pad = kColorPopupPad;
        float sv_size = kColorPopupSVSize;
        float sv_x = inspector_.color_popup_x + pad;
        float sv_y = inspector_.color_popup_y + pad;
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


void NodeGraphUI::update_pan_release() {
    if (mouse_.left_released && panning_ && dragging_node_idx_ < 0) {
        panning_ = false;
    }
}


} // namespace vivid::ui
