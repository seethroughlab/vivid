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
