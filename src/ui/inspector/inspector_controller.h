#pragma once

#include "ui/graph/node_graph_util.h"
#include "ui/inspector/inspector_surface.h"
#include "operator_api/types.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vivid::ui {

struct MouseState;
class Renderer2D;

class InspectorController {
public:
    using CustomInspectorCallback = std::function<void(
        const std::string& node_id, VividInspectorContext* ctx)>;

    // True while any param-driving inspector gesture is in progress (slider,
    // modulation, xy-pad, custom surface, or color drag). Centralizes the
    // predicate that brackets a gesture into a single undo group, so adding a
    // new param-widget type only needs to extend this one place. (audit 08-F7)
    bool param_gesture_active() const {
        return active_slider_idx >= 0 ||
               modulation_amount_dragging ||
               active_xy_pad_idx >= 0 ||
               surface.has_active() ||
               color_dragging_sv || color_dragging_hue;
    }

    struct InspectorRect { float x, y, w, h; std::string node_id; std::string param_name; };
    struct XYPadRect { float x, y, w, h; std::string node_id; std::string param_x, param_y; };
    struct XYToggleRect { float x, y, w, h; std::string node_id; std::string param_x; };
    struct XYTabRect { float x, y, w, h; std::string node_id; std::string first_param; size_t tab_index; };
    struct ColorSwatchRect {
        float x, y, w, h;
        std::string node_id;
        std::string param_r, param_g, param_b;
    };
    struct GroupHeaderRect { float x, y, w, h; std::string type_name; std::string group_name; };
    struct ResolutionRect { float x, y, w, h; std::string node_id; bool is_width; };
    struct WireRemapRect { float x, y, w, h; int field; };
    struct WireClampRect { float x, y, w, h; };
    struct WireCurveRect { float x, y, w, h; };
    struct MidiRemoveRect { float x, y, w, h; std::string node_id; std::string param_name; };
    struct MidiRangeRect { float x, y, w, h; std::string node_id; std::string param_name; bool is_min; };
    struct SubmenuLevel {
        const std::vector<ui::PresetMenuNode>* items = nullptr;
        int hovered_idx = -1;
        float x = 0, y = 0, w = 0;
    };
    struct StatePresetRect {
        float x, y, w, h;
        std::string sm_node;
        int state_idx;
        std::string target_node;
    };
    struct StateHeaderRect { float x, y, w, h; int state_idx; };
    struct ModAssignRect {
        float x, y, w, h;
        std::string node_id;
        std::string source;
        std::string destination;
        int action = 0;  // 0=source, 1=destination, 2=polarity, 3=remove, 4=add
    };
    struct ModAmountRect {
        float x, y, w, h;
        std::string node_id;
        std::string source;
        std::string destination;
        float range = 1.0f;
    };

    bool wants_keyboard() const {
        return editing_param
            || editing_resolution
            || editing_wire_remap
            || editing_node_id
            || dropdown_open
            || editing_midi_range
            || param_picker_open
            || color_editing_hex
            || color_editing_rgb >= 0
            || custom_inspector_wants_keyboard;
    }

    void set_custom_inspector_callback(CustomInspectorCallback cb) {
        custom_inspector_cb = std::move(cb);
    }

    void reset_scroll_selection() {
        insp_scroll_y = 0.0f;
        insp_scroll_node_id.clear();
    }

    void capture_frame_events(const MouseState& mouse);

    // State remains public during the migration so NodeGraphUI can delegate
    // progressively without changing behavior.
    std::vector<InspectorRect> slider_rects;
    std::vector<InspectorRect> lock_badge_rects;
    std::vector<XYPadRect> xy_pad_rects;
    std::vector<XYToggleRect> xy_toggle_rects;
    std::vector<XYTabRect> xy_tab_rects;
    int active_xy_pad_idx = -1;
    std::string active_xy_node_id;
    std::string active_xy_param_x, active_xy_param_y;

    std::unordered_map<std::string, bool> xy_pad_expanded;
    std::unordered_map<std::string, size_t> xy_group_active_tab;

    InspectorSurface surface;

    std::vector<ColorSwatchRect> color_swatch_rects;
    bool color_popup_open = false;
    std::string color_popup_node_id;
    std::string color_popup_param_r, color_popup_param_g, color_popup_param_b;
    float color_popup_x = 0, color_popup_y = 0;
    float color_popup_h = 0, color_popup_s = 0, color_popup_v = 0;
    bool color_dragging_sv = false;
    bool color_dragging_hue = false;
    bool color_editing_hex = false;
    std::string color_hex_buffer;
    int color_editing_rgb = -1;
    std::string color_rgb_buffer;

    std::vector<GroupHeaderRect> group_header_rects;

    bool editing_param = false;
    std::string edit_node_id;
    std::string edit_param_name;
    std::string edit_buffer;

    // Inline node-id (rename) editing in the inspector header.
    bool editing_node_id = false;
    std::string node_id_edit_old;             // id being renamed
    std::string node_id_error;                // transient message (e.g. "id already in use")
    std::vector<InspectorRect> node_id_rects; // hit-test the header id label

    std::vector<InspectorRect> bool_rects;
    std::vector<InspectorRect> value_text_rects;
    std::vector<InspectorRect> dropdown_rects;
    std::vector<InspectorRect> file_button_rects;

    CustomInspectorCallback custom_inspector_cb;
    bool custom_inspector_wants_keyboard = false;
    bool insp_mouse_left_clicked = false;
    bool insp_mouse_left_released = false;
    bool insp_mouse_right_clicked = false;
    std::vector<VividInspectorKeyEvent> insp_key_events;
    std::vector<uint32_t> insp_char_events;

    std::vector<ResolutionRect> resolution_rects;
    std::vector<WireRemapRect> wire_remap_rects;
    std::vector<WireClampRect> wire_clamp_rects;
    std::vector<WireCurveRect> wire_curve_rects;
    bool editing_wire_remap = false;
    int edit_wire_remap_field = 0;

    bool editing_resolution = false;
    std::string edit_res_node_id;
    bool edit_res_is_width = true;

    bool editing_midi_range = false;
    std::string midi_range_node_id;
    std::string midi_range_param_name;
    bool midi_range_editing_min = true;
    std::vector<MidiRemoveRect> midi_remove_rects;
    std::vector<MidiRangeRect> midi_range_rects;

    bool dropdown_open = false;
    bool dropdown_is_preset = false;
    bool dropdown_is_state_preset = false;
    bool dropdown_is_wire_curve = false;
    std::string dropdown_node_id;
    std::string dropdown_param_name;
    int dropdown_sel = 0;
    float dropdown_x = 0, dropdown_y = 0, dropdown_w = 0;
    std::vector<std::string> dropdown_labels;
    int dropdown_factory_count = 0;
    Renderer2D* dropdown_tr = nullptr;
    std::vector<ui::PresetMenuNode> dropdown_menu_tree;
    std::vector<SubmenuLevel> dropdown_submenu_stack;
    int dropdown_hover_frames = 0;
    int dropdown_hover_target = -1;
    int dropdown_flat_hovered_idx = -1;
    int dropdown_state_idx = -1;
    std::string dropdown_sm_node;
    std::string dropdown_target_node;

    std::vector<InspectorRect> preset_dropdown_rects;
    std::vector<InspectorRect> preset_save_rects;
    // node_id is the selected node; param_name carries the operator type slug
    // used to build the docs URL.
    std::vector<InspectorRect> docs_link_rects;
    // Open Editor button — rendered only for operators whose op_info->has_editor.
    std::vector<InspectorRect> open_editor_rects;
    // Bypass toggle in the inspector header (rendered for bypass-eligible nodes).
    std::vector<InspectorRect> bypass_button_rects;
    std::vector<StatePresetRect> state_preset_rects;
    std::vector<StateHeaderRect> state_header_rects;
    std::vector<ModAssignRect> mod_assign_rects;
    std::vector<ModAmountRect> mod_amount_rects;
    std::string modulation_error;
    bool modulation_amount_dragging = false;
    std::string modulation_amount_node_id;
    std::string modulation_amount_source;
    std::string modulation_amount_destination;
    float modulation_amount_range = 1.0f;

    int hovered_slider_idx = -1;
    int hovered_bool_idx = -1;
    int hovered_dropdown_idx = -1;

    std::vector<InspectorRect> label_rects;
    int hovered_label_idx = -1;
    float label_hover_time = 0.0f;
    std::string label_hover_param_name;
    std::string label_hover_node_id;

    // Param right-click context menu
    bool param_ctx_menu_open = false;
    float param_ctx_menu_x = 0, param_ctx_menu_y = 0;
    std::string param_ctx_node_id;
    std::string param_ctx_param_name;

    int active_slider_idx = -1;
    std::string active_slider_node_id;
    std::string active_slider_param_name;

    float insp_scroll_y = 0.0f;
    float insp_content_h = 0.0f;
    std::string insp_scroll_node_id;
    bool insp_scrollbar_dragging = false;
    float insp_sb_drag_start_y = 0.0f;
    float insp_sb_drag_start_scroll = 0.0f;

    bool param_picker_open = false;
    bool param_picker_is_output = false;
    float param_picker_x = 0, param_picker_y = 0;
    std::string param_picker_node_id;
    std::string param_picker_wire_from_node;
    std::string param_picker_wire_from_port;
    std::vector<std::string> param_picker_items;
    std::vector<bool> param_picker_item_is_param;
    int param_picker_sel = 0;
    float param_picker_scroll = 0.0f;

    std::unordered_map<std::string, bool> group_collapsed;
};

} // namespace vivid::ui
