#pragma once

#include "ui/node_graph_constants.h"
#include "ui/inspector_layout.h"
#include "ui/graph_snapshot.h"
#include "ui/ui_command_sink.h"
#include "ui/ui_style.h"
#include "ui/theme_loader.h"
#include "ui/text_edit.h"
#include "operator_api/types.h"
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <cassert>

namespace vivid::ui {

struct ExampleEntry {
    std::string id;
    std::string title;
    std::string path;
    std::string summary;
    std::vector<std::string> tags;
    std::string difficulty;
    std::vector<std::string> domains;
    std::vector<std::string> requires_packages;
    int featured_rank = 1000;
    int estimated_minutes = 0;
};

struct GraphMetaEditData {
    std::string path;
    std::string id;
    std::string title;
    std::string description;
    std::string tags_csv;
    std::string difficulty;
    std::string domains_csv;
    std::string requires_packages_csv;
    std::string featured_rank;
};

enum class PackageBrowserFetchState {
    Idle,
    Fetching,
    Ready,
    Error
};

struct PackageBrowserEntry {
    std::string name;
    std::string description;
    std::string version;
    std::string author;
    std::string category;
    std::vector<std::string> tags;
    bool installed = false;
    bool linked = false;
};

struct PackageBrowserUpdateSummary {
    int installed_packages = 0;
    int updates_available = 0;
    int incompatible_updates = 0;
};

struct PackageBrowserCallbacks {
    std::function<void()> refresh;
    std::function<std::vector<PackageBrowserEntry>()> list_entries;
    std::function<PackageBrowserFetchState()> fetch_state;
    std::function<std::string()> fetch_error;
    std::function<PackageBrowserUpdateSummary()> update_summary;
    std::function<bool(const std::string&, std::string&)> install;
    std::function<bool(const std::string&, std::string&)> uninstall;
    std::function<bool(const std::string&, std::string&)> unlink;
    std::function<bool(const std::string&, std::string&)> link;
};

class Renderer2D;
class ThumbnailRenderer;
class ThumbnailCache;

struct MouseState {
    float x = 0, y = 0;
    float prev_x = 0, prev_y = 0;
    bool left_down = false;
    bool left_clicked = false;   // true on the frame the button went down
    bool left_released = false;  // true on the frame the button went up
    bool right_clicked = false;  // true on the frame right button went down
    bool shift_down = false;
};

struct NodeRect {
    std::string node_id;
    std::string type_name;
    VividDomain domain = VIVID_DOMAIN_CONTROL;
    float x = 0, y = 0, w = 0, h = 0;
    float target_h = 0;  // animated height target (h lerps toward this)
    struct PortPos { std::string name; float x, y; bool is_param = false; };
    std::vector<PortPos> inputs, outputs;
    // Multi-output expand affordance
    bool     outputs_expandable  = false;
    uint32_t hidden_output_count = 0;
    bool     outputs_expanded    = false;
    float    affordance_gy       = 0; // graph-space Y center of affordance row
};

// Frame-rate-independent lerp: moves current toward target at the given speed.
inline float lerp_toward(float current, float target, float speed, float dt) {
    float t = 1.0f - std::exp(-speed * dt);
    return current + (target - current) * t;
}

class NodeGraphUI {
public:
    NodeGraphUI(UICommandSink& commands);

    // GLFW callbacks
    void on_mouse_move(float x, float y);
    void on_mouse_button(int button, int action, int mods);
    void on_scroll(float x_offset, float y_offset, int mods);
    void on_key(int key, int action, int mods);
    void on_char(unsigned int codepoint);

    // Returns true during the "on" phase of a blinking cursor (0.5s half-period)
    bool cursor_blink_on() const {
        return static_cast<int>(cursor_blink_time_ * 2.0f) % 2 == 0;
    }

    // Returns true when a popup is open and wants keyboard focus
    bool wants_keyboard() const {
        return create_popup_open_
            || chooser_open_
            || editing_param_
            || editing_resolution_
            || editing_wire_remap_
            || dropdown_open_
            || context_menu_open_
            || editing_midi_range_
            || clone_confirm_open_
            || save_confirm_open_
            || prefs_open_
            || param_picker_open_
            || color_editing_hex_
            || color_editing_rgb_ >= 0
            || patch_ctx_open_
            || session_editing_name_
            || record_dropdown_open_
            || preset_name_popup_open_
            || pkg_browser_open_
            || example_browser_open_
            || graph_meta_editor_open_
            || about_open_
            || mcp_setup_open_
            || custom_inspector_wants_keyboard_;
    }
    bool wire_inspector_visible() const;
    bool has_selection() const { return !selected_node_ids_.empty() || wire_inspector_visible(); }
    bool has_single_selection() const { return selected_node_ids_.size() == 1; }
    const std::string& single_selected_id() const { assert(!selected_node_ids_.empty()); return *selected_node_ids_.begin(); }

    void toggle_visible() { visible_ = !visible_; }
    bool visible() const { return visible_; }

    // Graph actions (used by menu bar and bare-key shortcuts)
    void toggle_session_grid();
    void toggle_midi_map_mode();
    void delete_selected();
    void open_chooser();  // centers chooser in visible graph area

    // State queries (used by menu bar for checkmarks)
    bool session_grid_open() const { return session_grid_open_; }
    bool midi_map_mode() const { return midi_map_mode_; }

    // Called by main loop each frame with delta time
    void set_dt(float dt) { dt_ = dt; cursor_blink_time_ += dt; }

    // Per-frame
    void update(const GraphSnapshot& snapshot);
    void draw(Renderer2D& tr, uint32_t w, uint32_t h);
    void draw_overlays(Renderer2D& tr);

    // GPU thumbnail overlay (separate render pass after text)
    void draw_thumbnails(ThumbnailRenderer& tr, const ThumbnailCache& cache,
                         WGPUCommandEncoder encoder, WGPUTextureView surface,
                         uint32_t w, uint32_t h);

    const std::vector<NodeRect>& node_rects() const { return node_rects_; }

    // Custom inspector callback — set by main.cpp to invoke operator's draw_inspector
    using CustomInspectorCallback = std::function<void(
        const std::string& node_id, VividInspectorContext* ctx)>;
    void set_custom_inspector_callback(CustomInspectorCallback cb) {
        custom_inspector_cb_ = std::move(cb);
    }

    void set_custom_thumbnail_nodes(std::unordered_set<std::string> ids) {
        custom_thumb_nodes_ = std::move(ids);
    }

    void set_dpi_scale(float scale) { dpi_scale_ = scale; }

    float pan_x() const { return pan_x_; }
    float pan_y() const { return pan_y_; }
    float zoom() const { return zoom_; }
    void set_viewport(float px, float py, float z) {
        pan_x_ = px; pan_y_ = py; zoom_ = z;
        pan_target_x_ = px; pan_target_y_ = py; zoom_target_ = z;
    }

    bool bezier_wires() const { return bezier_wires_; }
    void set_bezier_wires(bool v) { bezier_wires_ = v; }

    bool show_param_wires() const { return show_param_wires_; }
    void set_show_param_wires(bool v) { show_param_wires_ = v; }

    const UIStyle& style() const { return style_; }
    void set_style(const UIStyle& s) { style_ = s; }

    void toggle_preferences();
    void toggle_package_browser();
    void set_package_browser_callbacks(PackageBrowserCallbacks callbacks);
    void notify_pkg_action_complete(bool success, const std::string& error);
    void toggle_example_browser();
    void set_examples(std::vector<ExampleEntry> examples);
    void set_example_open_callback(std::function<void(const std::string&)> cb);
    void set_example_package_checker(
        std::function<bool(const std::vector<std::string>&, std::string&)> cb);
    void open_graph_meta_editor(const GraphMetaEditData& data);
    void set_graph_meta_save_callback(
        std::function<bool(const GraphMetaEditData&, std::string&)> cb);
    void set_editor_options(std::vector<std::string> names, std::vector<std::string> ids,
                            int current_idx = 0, const std::string& custom_command = "");
    void set_style_options(std::vector<UIStyle> styles, int current_idx,
                            std::vector<ThemeInfo> themes = {});
    void show_core_update_notice(const std::string& latest_version,
                                 const std::string& summary = "");
    void clear_core_update_notice();
    void set_core_update_notice_callbacks(std::function<void()> install_cb,
                                          std::function<void()> skip_cb,
                                          std::function<void()> later_cb);
    void open_about() { about_open_ = true; about_scroll_ = 0.0f; }

    // Read-only UI snapshot accessors used by tests and seam verification.
    const std::vector<PackageBrowserEntry>& package_browser_entries() const { return pkg_browser_entries_; }
    const GraphMetaEditData& graph_meta_data() const { return graph_meta_data_; }

    // Set the directory containing the MCP Python scripts (used in setup dialog)
    void set_mcp_dir(const std::string& dir) { mcp_dir_ = dir; }

private:
    // --- Layout ---
    void layout_nodes(bool force = false);
    void place_new_nodes();
    void prune_node_rects();
    void recompute_ports(NodeRect& rect, const NodeSnapshot& ns);
    void refresh_package_browser_snapshot_if_ready();

    // Count visible input/output ports for a node (signal ports + connected params/outputs)
    uint32_t count_visible_input_ports(const NodeSnapshot& ns, bool show_params = true) const;
    uint32_t count_visible_output_ports(const NodeSnapshot& ns, bool show_params = true) const;

    // --- Drawing (node_graph_draw.cpp) ---
    void draw_graph(Renderer2D& tr);
    void draw_connections(Renderer2D& tr);
    void draw_inspector(Renderer2D& tr, uint32_t w, uint32_t h);
    void draw_inspector_header(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_params(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_one_inspector_param(Renderer2D& tr, const NodeSnapshot& node,
                                  InspectorLayout& layout, uint32_t pi);
    void draw_inspector_knob(Renderer2D& tr, const NodeSnapshot& node,
                              InspectorLayout& layout, uint32_t pi);
    void draw_inspector_xy_pad(Renderer2D& tr, const NodeSnapshot& node,
                                InspectorLayout& layout, uint32_t pi_x, uint32_t pi_y);
    void draw_inspector_color_swatch(Renderer2D& tr, const NodeSnapshot& node,
                                      InspectorLayout& layout,
                                      uint32_t pi_r, uint32_t pi_g, uint32_t pi_b);
    void draw_color_popup(Renderer2D& tr);
    void draw_inspector_group_header(Renderer2D& tr, InspectorLayout& layout,
                                      const std::string& type_name,
                                      const std::string& group_name, bool collapsed);
    void draw_one_inspector_param_simple(Renderer2D& tr, const NodeSnapshot& node,
                                         float px, float& py, uint32_t pi);
    void draw_inspector_resolution(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_custom_inspector(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_outputs(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_state_presets(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_chooser(Renderer2D& tr);
    void draw_preview_wire(Renderer2D& tr);
    void draw_box_select(Renderer2D& tr);
    void draw_wire_tooltip(Renderer2D& tr);
    void draw_node_error_tooltip(Renderer2D& tr);
    void draw_inspector_scrollbar(Renderer2D& tr);
    void draw_midi_map_banner(Renderer2D& tr);
    void draw_core_update_banner(Renderer2D& tr);

    // --- Session grid ---
    void draw_session_grid(Renderer2D& tr);

    // --- Grid ---
    void draw_grid(Renderer2D& tr);

    // --- Performance bar ---
    void draw_perf_bar(Renderer2D& tr);
    void draw_perf_sparkline(Renderer2D& tr, const float* buf, uint32_t buf_len,
                             uint32_t write_idx, bool filled,
                             float x, float y, float w, float h,
                             float r, float g, float b, float a);
    void draw_perf_expanded(Renderer2D& tr);
    void draw_mcp_setup_dialog(Renderer2D& tr);

    // --- Chooser ---
    void rebuild_chooser_items();
    void confirm_chooser_selection(const std::string& type);

    // --- Hit testing ---
    int hit_test_node(float mx, float my) const;

    struct PortHit {
        int node_idx = -1;
        std::string port_name;
        bool is_output = false;
        float gx = 0, gy = 0;
    };
    PortHit hit_test_port(float mx, float my) const;
    int hit_test_wire(float sx, float sy) const;

    // Generic AABB hit test for rect vectors (replaces 5 individual methods)
    template<typename RectT>
    static int hit_test_rect(const std::vector<RectT>& rects, float mx, float my);

    // --- Text editing ---
    void confirm_param_edit();
    void cancel_param_edit();
    void confirm_resolution_edit();
    void cancel_resolution_edit();
    void confirm_midi_range_edit();
    void cancel_midi_range_edit();

    // --- Sorted port indices helper ---
    static std::vector<std::pair<uint32_t, std::string>> sorted_ports(
        const std::unordered_map<std::string, uint32_t>& port_indices);

    // --- Clone confirmation dialog ---
    void open_clone_confirm_dialog(const std::string& type_name);
    void update_clone_confirm();
    void draw_clone_confirm(Renderer2D& tr);

    // --- Save confirmation dialog (dirty check before New/New Project) ---
public:
    enum class SaveConfirmAction { kNewGraph, kNewProject };
    void open_save_confirm_dialog(SaveConfirmAction action);
    // Callbacks set by main.cpp: invoked when the user resolves the save-confirm dialog
    std::function<void()> on_save_confirm_save;      // "Save" clicked
    std::function<void()> on_save_confirm_dont_save;  // "Don't Save" clicked (action)
    std::function<void()> on_save_confirm_cancel;      // "Cancel" clicked
    bool save_confirm_open() const { return save_confirm_open_; }
    SaveConfirmAction save_confirm_action() const { return save_confirm_action_; }
private:
    void update_save_confirm();
    void draw_save_confirm(Renderer2D& tr);

    // --- Create operator modal ---
    void update_create_popup();
    void draw_create_popup(Renderer2D& tr);
    void submit_create_operator(bool empty_variant);
    void reset_create_domain_defaults();

    // --- Preset name popup ---
    void draw_preset_name_popup(Renderer2D& tr);

    // --- Preferences panel ---
    void update_preferences();
    void draw_preferences(Renderer2D& tr);

    // --- Package browser ---
    void update_package_browser();
    void draw_package_browser(Renderer2D& tr);
    void rebuild_pkg_browser_items();
    void update_example_browser();
    void draw_example_browser(Renderer2D& tr);
    void rebuild_example_items();
    void update_graph_meta_editor();
    void draw_graph_meta_editor(Renderer2D& tr);
    void update_about();
    void draw_about(Renderer2D& tr);

    // --- Parameter picker popup ---
    void rebuild_param_picker_items();
    void update_param_picker();
    void draw_param_picker(Renderer2D& tr);

    // --- Patch panel (2-node connection view) ---
    void draw_patch_panel(Renderer2D& tr, const NodeSnapshot& node_a,
                          const NodeSnapshot& node_b, float px, float& py);
    bool handle_patch_click();
    void handle_patch_right_click();
    void update_patch_drag();

    // Resolve port type for a node+port (moved from file-local static)
    static VividPortType resolve_port_type(const GraphSnapshot& snap,
                                           const std::string& node_id,
                                           const std::string& port_name,
                                           bool is_output);

    // --- Decomposed update() sub-methods ---
    void check_relayout();
    void update_pan();
    void update_node_drag();
    void update_wire_drag();
    void update_slider_drag();
    void update_xy_pad_drag();
    void update_color_drag();
    void update_chooser_hover();
    void update_context_menu();
    void update_pan_release();
    void clear_frame_flags();
    void update_wire_hover();
    void update_node_hover();
    void update_sparklines();
    void update_scrollbar_drag();
    void update_box_select();

    // --- Input handling (node_graph_input.cpp) ---
    void handle_right_click();
    void handle_left_click();
    bool handle_chooser_click();
    bool handle_dropdown_click();
    bool handle_inspector_click();
    void handle_graph_click();

    // Graph space <-> screen space helpers
    float gx_to_sx(float gx) const { return gx * zoom_ + pan_x_; }
    float gy_to_sy(float gy) const { return gy * zoom_ + pan_y_; }
    float g_to_s(float gv) const { return gv * zoom_; }  // sizes
    float sx_to_gx(float sx) const { return (sx - pan_x_) / zoom_; }
    float sy_to_gy(float sy) const { return (sy - pan_y_) / zoom_; }

    // Right edge of interactive graph area (shrinks when inspector is visible)
    float graph_right() const;
    // Bottom edge of interactive graph area (shrinks when session grid is open)
    float graph_bottom() const;
    float inspector_x() const { return static_cast<float>(win_w_) - kInspectorW; }
    float chooser_x() const { return (graph_right() - kChooserW) * 0.5f; }

    UICommandSink& commands_;
    GraphSnapshot snap_;
    bool snap_valid_ = false;
    MouseState mouse_;
    std::unordered_set<std::string> selected_node_ids_;
    int selected_wire_idx_ = -1;  // index into snap_.connections, or -1
    std::vector<NodeRect> node_rects_;

    // Track topology version to re-layout on changes
    size_t last_node_count_ = 0;
    size_t last_conn_count_ = 0;
    bool first_layout_done_ = false;

    // Node drag state
    int dragging_node_idx_ = -1;
    float drag_offset_x_ = 0, drag_offset_y_ = 0;

    // Group drag state
    struct DragOffset { float dx, dy; };
    std::unordered_map<std::string, DragOffset> group_drag_offsets_;

    // Deferred deselection: solo-select on mouse-up if no drag occurred
    std::string pending_select_node_id_;
    bool did_drag_ = false;
    float drag_start_sx_ = 0, drag_start_sy_ = 0;

    // Box-select state
    bool box_selecting_ = false;
    float box_start_gx_ = 0, box_start_gy_ = 0;
    bool box_shift_held_ = false;

    // Wire drag state
    bool dragging_wire_ = false;
    bool wire_from_is_output_ = true;  // true = dragging from output, false = from body (pick output)
    std::string wire_from_node_id_;
    std::string wire_from_port_;
    float wire_from_gx_ = 0, wire_from_gy_ = 0;

    // Parameter picker popup state
    bool param_picker_open_ = false;
    bool param_picker_is_output_ = false;  // true = picking output port on source node
    float param_picker_x_ = 0, param_picker_y_ = 0;
    std::string param_picker_node_id_;     // node being picked on
    std::string param_picker_wire_from_node_;
    std::string param_picker_wire_from_port_;
    std::vector<std::string> param_picker_items_;
    std::vector<bool> param_picker_item_is_param_;  // parallel to param_picker_items_
    int param_picker_sel_ = 0;
    int param_picker_scroll_ = 0;

    // Zoom/pan state
    float zoom_ = 1.0f;
    float pan_x_ = 0.0f, pan_y_ = 0.0f;
    bool panning_ = false;
    float pan_start_mx_ = 0, pan_start_my_ = 0;
    float pan_start_px_ = 0, pan_start_py_ = 0;

    // Animated zoom/pan (lerp targets for scroll-zoom; direct pan bypasses easing)
    float zoom_target_ = 1.0f;
    float pan_target_x_ = 0.0f, pan_target_y_ = 0.0f;

    // Popup fade animation (0 = fully hidden, 1 = fully visible)
    float popup_opacity_ = 0.0f;
    // Which popup was open last frame (to detect open/close transitions)
    bool popup_was_open_ = false;

    // Node hover animation (smooth alpha transition)
    float node_hover_alpha_ = 0.0f;
    std::string node_hover_anim_id_;  // which node the animation tracks

    // Selection glow pulse
    float selection_glow_ = 0.0f;
    bool selection_glow_rising_ = true;

    // Slider drag state
    int active_slider_idx_ = -1;
    std::string active_slider_node_id_;
    std::string active_slider_param_name_;

    struct InspectorRect { float x, y, w, h; std::string node_id; std::string param_name; };
    std::vector<InspectorRect> slider_rects_;

    // Lock badge hit rects
    std::vector<InspectorRect> lock_badge_rects_;

    // XY pad state
    struct XYPadRect { float x, y, w, h; std::string node_id; std::string param_x, param_y; };
    std::vector<XYPadRect> xy_pad_rects_;
    int active_xy_pad_idx_ = -1;
    std::string active_xy_node_id_;
    std::string active_xy_param_x_, active_xy_param_y_;

    // Color swatch state
    struct ColorSwatchRect { float x, y, w, h; std::string node_id;
                             std::string param_r, param_g, param_b; };
    std::vector<ColorSwatchRect> color_swatch_rects_;

    // Color popup state
    bool color_popup_open_ = false;
    std::string color_popup_node_id_;
    std::string color_popup_param_r_, color_popup_param_g_, color_popup_param_b_;
    float color_popup_x_ = 0, color_popup_y_ = 0;
    float color_popup_h_ = 0, color_popup_s_ = 0, color_popup_v_ = 0;
    bool color_dragging_sv_ = false;
    bool color_dragging_hue_ = false;
    bool color_editing_hex_ = false;
    std::string color_hex_buffer_;
    int  color_editing_rgb_ = -1;      // -1 = none, 0 = R, 1 = G, 2 = B
    std::string color_rgb_buffer_;     // text buffer for active channel edit

    struct GroupHeaderRect { float x, y, w, h; std::string type_name; std::string group_name; };
    std::vector<GroupHeaderRect> group_header_rects_;

    // Shared text-edit cursor/selection state (only one field active at a time)
    TextEditState text_edit_;

    // Slider text-edit state (click value text to type a value)
    bool editing_param_ = false;
    std::string edit_node_id_;
    std::string edit_param_name_;
    std::string edit_buffer_;

    std::vector<InspectorRect> bool_rects_;
    std::vector<InspectorRect> value_text_rects_;
    std::vector<InspectorRect> dropdown_rects_;
    std::vector<InspectorRect> file_button_rects_;

    // Custom inspector state
    CustomInspectorCallback custom_inspector_cb_;
    bool custom_inspector_wants_keyboard_ = false;
    // Saved click/release events for custom inspector (persisted across update→draw boundary)
    bool insp_mouse_left_clicked_ = false;
    bool insp_mouse_left_released_ = false;
    bool insp_mouse_right_clicked_ = false;
    std::vector<VividInspectorKeyEvent> insp_key_events_;
    std::vector<uint32_t> insp_char_events_;

    struct ResolutionRect { float x, y, w, h; std::string node_id; bool is_width; };
    std::vector<ResolutionRect> resolution_rects_;

    // Wire remap inspector rects
    struct WireRemapRect { float x, y, w, h; int field; }; // field: 0=from_min,1=from_max,2=to_min,3=to_max
    std::vector<WireRemapRect> wire_remap_rects_;
    struct WireClampRect { float x, y, w, h; };
    std::vector<WireClampRect> wire_clamp_rects_;

    // Wire remap text editing state
    bool editing_wire_remap_ = false;
    int  edit_wire_remap_field_ = 0;  // 0..3 for the four float fields

    // Resolution editing state
    bool editing_resolution_ = false;
    std::string edit_res_node_id_;
    bool edit_res_is_width_ = true;

    // MIDI map mode state
    bool midi_map_mode_ = false;
    bool midi_map_waiting_ = false;          // clicked param, waiting for CC
    std::string midi_map_node_id_;
    std::string midi_map_param_name_;
    bool editing_midi_range_ = false;        // typing into a min/max field
    std::string midi_range_node_id_;
    std::string midi_range_param_name_;
    bool midi_range_editing_min_ = true;
    struct MidiRemoveRect { float x, y, w, h; std::string node_id; std::string param_name; };
    struct MidiRangeRect { float x, y, w, h; std::string node_id; std::string param_name; bool is_min; };
    std::vector<MidiRemoveRect> midi_remove_rects_;
    std::vector<MidiRangeRect> midi_range_rects_;

    // Dropdown popup state
    bool dropdown_open_ = false;
    bool dropdown_is_preset_ = false;  // true when showing preset choices (not param)
    bool dropdown_is_state_preset_ = false;  // true when showing state-preset choices
    std::string dropdown_node_id_;
    std::string dropdown_param_name_;
    int dropdown_sel_ = 0;
    float dropdown_x_ = 0, dropdown_y_ = 0, dropdown_w_ = 0;
    std::vector<std::string> dropdown_labels_;
    int dropdown_factory_count_ = 0;  // number of factory preset entries at start of labels

    // State-preset dropdown context
    int dropdown_state_idx_ = -1;
    std::string dropdown_sm_node_;
    std::string dropdown_target_node_;

    // Preset row hit-test rects
    std::vector<InspectorRect> preset_dropdown_rects_;
    std::vector<InspectorRect> preset_save_rects_;

    // State-preset hit-test rects
    struct StatePresetRect { float x, y, w, h; std::string sm_node; int state_idx; std::string target_node; };
    std::vector<StatePresetRect> state_preset_rects_;

    struct StateHeaderRect { float x, y, w, h; int state_idx; };
    std::vector<StateHeaderRect> state_header_rects_;

    // Preset name popup state (shown on Save when no active preset)
    bool preset_name_popup_open_ = false;
    std::string preset_name_buffer_;
    std::string preset_name_node_id_;

    // Patch panel state
    struct PatchJack {
        float x, y;
        std::string node_id;
        std::string port_name;
        VividPortType port_type;
        bool can_source;
        bool can_dest;
        bool is_param;
    };
    std::vector<PatchJack> patch_jacks_;

    struct PatchWire {
        float sx, sy, ex, ey;
        std::string from_node, from_port;
        std::string to_node, to_port;
        bool has_remap;
    };
    std::vector<PatchWire> patch_wires_;

    bool patch_dragging_ = false;
    int patch_drag_from_idx_ = -1;

    bool patch_ctx_open_ = false;
    float patch_ctx_x_ = 0, patch_ctx_y_ = 0;
    int patch_ctx_wire_idx_ = -1;

    // Sparkline ring buffers for control nodes
    static constexpr uint32_t kSparklineLen = 64;
    struct SparklineData {
        std::array<float, 64> values{};
        uint32_t write_idx = 0;
        bool filled = false;
    };
    std::unordered_map<std::string, SparklineData> sparklines_;

    // Nodes with custom draw_thumbnail (get full kGpuThumbH body height)
    std::unordered_set<std::string> custom_thumb_nodes_;

    float dpi_scale_ = 1.0f;

    // Operator chooser popup
    bool chooser_open_ = false;
    std::string chooser_filter_;
    int chooser_sel_ = 0;
    int chooser_scroll_ = 0;
    std::vector<std::string> chooser_items_;
    float chooser_cursor_gx_ = 0, chooser_cursor_gy_ = 0;

    // Insert-on-wire state (chooser opened from wire context menu)
    bool chooser_insert_wire_ = false;
    ConnectionSnapshot chooser_insert_conn_;
    VividPortType insert_wire_source_type_ = VIVID_PORT_FLOAT;
    VividPortType insert_wire_dest_type_ = VIVID_PORT_FLOAT;

    // Connect-from-wire-drag state (chooser opened via Tab during wire drag)
    bool chooser_wire_connect_ = false;
    std::string wire_connect_node_id_;
    std::string wire_connect_port_;
    bool wire_connect_from_output_ = true;
    VividPortType wire_connect_type_ = VIVID_PORT_FLOAT;

    // Right-click context menu state
    bool context_menu_open_ = false;
    float context_menu_x_ = 0, context_menu_y_ = 0;  // screen space
    std::string context_node_id_;   // non-empty if node menu
    std::string context_node_type_; // type of context node (for duplicate filter)
    bool context_node_has_shader_ = false;  // true if node is a shader-based filter
    int context_wire_idx_ = -1;     // >= 0 if wire menu
    bool context_bg_menu_ = false;  // true if background menu (no node/wire)
    int hovered_wire_idx_ = -1;
    std::string hovered_node_id_;

    // Port hover state (updated each frame)
    struct HoveredPort {
        std::string node_id;
        std::string port_name;
        bool is_output = false;
    };
    HoveredPort hovered_port_;

    // Inspector widget hover (index into the respective rect vector, -1 = none)
    int hovered_slider_idx_ = -1;
    int hovered_bool_idx_ = -1;
    int hovered_dropdown_idx_ = -1;

    // Multi-output expand state (keyed by node_id)
    std::unordered_set<std::string> outputs_expanded_;
    struct ExpandAffordanceRect { float x, y, w, h; std::string node_id; };
    std::vector<ExpandAffordanceRect> expand_affordance_rects_;

    // Group collapse state
    std::unordered_map<std::string, bool> group_collapsed_;

    bool is_group_collapsed(const std::string& type_name, const std::string& group) const {
        auto it = group_collapsed_.find(type_name + "\t" + group);
        return it != group_collapsed_.end() && it->second;
    }
    void toggle_group_collapsed(const std::string& type_name, const std::string& group) {
        auto key = type_name + "\t" + group;
        group_collapsed_[key] = !group_collapsed_[key];
    }

    // Inspector scroll state
    float insp_scroll_y_ = 0.0f;
    float insp_content_h_ = 0.0f;
    std::string insp_scroll_node_id_;    // reset scroll when selection changes
    bool insp_scrollbar_dragging_ = false;
    float insp_sb_drag_start_y_ = 0.0f;
    float insp_sb_drag_start_scroll_ = 0.0f;

    // Cached window dimensions (updated each frame in draw())
    uint32_t win_w_ = 1280, win_h_ = 720;

    // Double-click detection for shader editing
    double last_click_time_ = 0.0;
    std::string last_click_node_id_;

    // Clone confirmation dialog state
    bool clone_confirm_open_ = false;
    std::string clone_confirm_type_;
    bool clone_confirm_project_available_ = false;
    int clone_confirm_destination_ = 0; // 0=Project Package, 1=Core

    // Save confirmation dialog state
    bool save_confirm_open_ = false;
    SaveConfirmAction save_confirm_action_ = SaveConfirmAction::kNewGraph;

    // Create operator modal state
    bool create_popup_open_ = false;
    int create_domain_sel_ = 0;           // 0=control, 1=audio, 2=gpu
    std::string create_name_buf_;
    std::string create_error_;
    bool create_composite_ = false;        // variant checkbox, control-only
    int create_destination_ = 0;           // 0=auto, 1=project, 2=core

    // Active text field tracking: 0=name, 1..N=input port names,
    //   N+1..M=output port names, M+1..=param names
    int create_active_field_ = 0;

    struct CreatePortRow { std::string name; int type_sel = 0; };
    std::vector<CreatePortRow> create_inputs_;
    std::vector<CreatePortRow> create_outputs_;

    struct CreateParamRow {
        std::string name;
        int type_sel = 0;     // index into {float, int, bool, file, text}
        float default_val = 0.0f;
        float min_val = 0.0f;
        float max_val = 1.0f;
    };
    std::vector<CreateParamRow> create_params_;

    // Preferences panel state
    bool prefs_open_ = false;
    int prefs_editor_sel_ = 0;
    std::vector<std::string> prefs_editor_names_;
    std::vector<std::string> prefs_editor_ids_;
    std::string prefs_custom_command_;
    bool prefs_editing_custom_ = false;
    int prefs_style_sel_ = 0;
    std::vector<UIStyle> prefs_styles_;
    std::vector<ThemeInfo> prefs_themes_;
    int prefs_saved_style_sel_ = 0;   // to revert on cancel

    // --- Package browser ---
    bool pkg_browser_open_ = false;
    std::string pkg_browser_filter_;
    int pkg_browser_sel_ = 0;
    int pkg_browser_scroll_ = 0;
    int pkg_browser_category_ = 0;   // 0=All, 1=Audio, 2=GPU, 3=Control, 4=Utility, 5=Installed
    std::array<float, 6> pkg_browser_tab_widths_{};
    std::vector<PackageBrowserEntry> pkg_browser_entries_;   // filtered snapshot
    std::vector<PackageBrowserEntry> pkg_browser_all_;       // full snapshot
    PackageBrowserCallbacks pkg_browser_callbacks_{};
    bool pkg_action_pending_ = false;
    std::string pkg_action_name_;
    std::string pkg_action_error_;

    // --- Example browser ---
    bool example_browser_open_ = false;
    std::string example_browser_filter_;
    int example_browser_sel_ = 0;
    int example_browser_scroll_ = 0;
    int example_browser_domain_ = 0;      // 0=All 1=GPU 2=Audio 3=Control 4=IO
    int example_browser_difficulty_ = 0;  // 0=All 1=Beginner 2=Intermediate 3=Advanced
    int example_browser_sort_ = 0;        // 0=Featured 1=Alphabetical
    bool example_browser_core_only_ = true;
    bool example_browser_package_only_ = false;
    std::array<float, 5> example_domain_tab_widths_{};
    std::array<float, 4> example_diff_tab_widths_{};
    std::array<float, 2> example_sort_tab_widths_{};
    std::vector<ExampleEntry> example_entries_all_;
    std::vector<ExampleEntry> example_entries_;
    std::string example_action_error_;
    std::string example_warn_id_;
    std::function<void(const std::string&)> example_open_callback_;
    std::function<bool(const std::vector<std::string>&, std::string&)> example_package_checker_;

    // --- Graph meta editor ---
    bool graph_meta_editor_open_ = false;
    int graph_meta_active_field_ = 0;
    std::vector<std::string*> graph_meta_fields_;
    GraphMetaEditData graph_meta_data_;
    std::string graph_meta_error_;
    std::function<bool(const GraphMetaEditData&, std::string&)> graph_meta_save_callback_;

    // --- About modal ---
    bool about_open_ = false;
    float about_scroll_ = 0.0f;
    float about_max_scroll_ = 0.0f;

    // --- Session grid (variation strip) ---
    bool session_grid_open_ = false;
    float session_scroll_x_ = 0.0f;
    int session_hovered_col_ = -1;
    // Rename editing
    bool session_editing_name_ = false;
    int session_edit_idx_ = -1;
    std::string session_edit_buffer_;
    // Double-click detection for variation cell rename
    double last_variation_click_time_ = 0.0;
    int last_variation_click_idx_ = -1;
    // Quantize mode (persisted as UI state, synced via commands)
    int session_quantize_mode_ = 0;  // 0=Off, 1=Beat, 2=Bar, 3=4Bar
    // Hit-test rects
    struct VariationCellRect { float x, y, w, h; int idx; };
    std::vector<VariationCellRect> variation_cell_rects_;
    struct SessionButtonRect { float x, y, w, h; int action; }; // action: 0=+New, 1=Save, 2-5=quantize buttons
    std::vector<SessionButtonRect> session_button_rects_;

    // Active UI style
    UIStyle style_;

    // Wire rendering style toggle (B key)
    bool bezier_wires_ = false;

    // Param wire visibility toggle (P key)
    bool show_param_wires_ = false;
    bool last_show_param_wires_ = false;  // detect toggle for relayout

    // UI visibility toggle (tilde key)
    bool visible_ = true;

    // --- Performance stats ---
    struct PerfRingBuffer {
        float values[kPerfHistoryLen]{};
        uint32_t write_idx = 0;
        bool filled = false;

        void push(float v) {
            values[write_idx] = v;
            write_idx = (write_idx + 1) % kPerfHistoryLen;
            if (write_idx == 0) filled = true;
        }
        uint32_t count() const { return filled ? kPerfHistoryLen : write_idx; }
        float newest() const {
            uint32_t idx = (write_idx == 0) ? kPerfHistoryLen - 1 : write_idx - 1;
            return values[idx];
        }
    };

    PerfRingBuffer fps_history_;
    PerfRingBuffer frame_time_history_;
    PerfRingBuffer memory_history_;

    float dt_ = 0.0f;
    float cursor_blink_time_ = 0.0f;  // accumulated time for cursor blink
    float smoothed_fps_ = 0.0f;
    float smoothed_ms_ = 0.0f;
    float display_fps_ = 0.0f;
    float display_ms_ = 0.0f;
    float smoothed_mem_mb_ = 0.0f;
    uint64_t perf_frame_counter_ = 0;

    bool perf_mem_hovered_ = false;
    float perf_mem_graph_x_ = 0.0f;
    float perf_mem_graph_y_ = 0.0f;

    // --- MCP status dots in perf bar ---
    std::string mcp_dir_;  // directory containing vivid_mcp.py / vivid_opdev_mcp.py
    bool mcp_setup_open_ = false;
    struct McpDotRect { float x, y, w, h; int idx; };  // idx: 0=vivid, 1=opdev
    std::vector<McpDotRect> mcp_dot_rects_;
    struct McpDialogButtonRect { float x, y, w, h; int action; };  // action: 0=copy_vivid, 1=copy_opdev, 2=done, 3=close
    std::vector<McpDialogButtonRect> mcp_dialog_button_rects_;

    // --- Record/Snapshot buttons in perf bar ---
    bool record_dropdown_open_ = false;
    float record_dropdown_x_ = 0.0f, record_dropdown_y_ = 0.0f;
    int record_codec_sel_ = 0;  // 0=H.264, 1=H.265, 2=ProRes 4444

    struct PerfButtonRect { float x, y, w, h; int action; bool enabled; };
    // action: 0=Record/Stop, 1=Snapshot, 2=Undo, 3=Redo
    std::vector<PerfButtonRect> perf_button_rects_;

    // --- Core update notice ---
    bool core_update_notice_open_ = false;
    std::string core_update_notice_version_;
    std::string core_update_notice_summary_;
    struct CoreUpdateButtonRect { float x, y, w, h; int action; };
    // action: 0=Install, 1=Skip, 2=Later
    std::vector<CoreUpdateButtonRect> core_update_button_rects_;
    std::function<void()> on_core_update_install_;
    std::function<void()> on_core_update_skip_;
    std::function<void()> on_core_update_later_;
};

} // namespace vivid::ui
