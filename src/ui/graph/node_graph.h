#pragma once

#include "ui/graph/node_graph_constants.h"
#include "ui/inspector/inspector_layout.h"
#include "ui/graph/graph_snapshot.h"
#include "ui/ui_command_sink.h"
#include "ui/style/ui_style.h"
#include "ui/style/theme_loader.h"
#include "ui/text_edit.h"
#include "ui/graph/node_graph_util.h"
#include "common/dialog_types.h"
#include "ui/dialogs/dialog_manager.h"
#include "ui/inspector/inspector_controller.h"
#include "ui/build_console_panel.h"
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

struct FileDropChooserAction {
    std::string label;
    std::string subtitle;
    std::string type_name;
    std::string file_param;
    std::string dropped_path;
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
    bool right_down = false;
    bool right_released = false; // true on the frame right button went up
    bool shift_down = false;
};

struct NodeRect {
    std::string node_id;
    std::string type_name;
    Cadence active_cadence = Cadence::Frame;
    bool is_gpu = false;
    uint8_t lane_behavior = 0;  // 0=Pointwise, 1=Structural, 2=Reduction, 3=Kernel
    float x = 0, y = 0, w = 0, h = 0;
    float target_h = 0;  // animated height target (h lerps toward this)
    struct PortPos { std::string name; float dy; bool is_param = false; };
    std::vector<PortPos> inputs, outputs;
    // Multi-output expand affordance
    bool     outputs_expandable  = false;
    uint32_t hidden_output_count = 0;
    bool     outputs_expanded    = false;
    float    affordance_dy       = 0; // Y offset from rect.y for affordance row
};

// Resolve absolute graph-space position of a port from its parent rect.
inline float port_gx(const NodeRect& r, bool is_output) {
    return is_output ? r.x + r.w : r.x;
}
inline float port_gy(const NodeRect& r, const NodeRect::PortPos& p) {
    return r.y + p.dy;
}

// Frame-rate-independent lerp: moves current toward target at the given speed.
inline float lerp_toward(float current, float target, float speed, float dt) {
    float t = 1.0f - std::exp(-speed * dt);
    return current + (target - current) * t;
}

class NodeGraphUI {
public:
    struct AsyncAddConnectionMutation {
        enum class Kind {
            Connect,
            Disconnect,
        };
        Kind kind = Kind::Connect;
        std::string from_addr;
        std::string to_addr;
    };

    struct AsyncAddOperatorRequest {
        std::string type_name;
        std::string node_id;
        std::string display_name;
        float graph_x = 0.0f;
        float graph_y = 0.0f;
        std::unordered_map<std::string, std::string> string_params;
        std::vector<AsyncAddConnectionMutation> connection_mutations;
    };

    enum class AsyncAddStage {
        Preparing,
        Compiling,
        Applying,
    };

    enum class AsyncGraphLoadStage {
        Loading,
        PreparingOperators,
        Compiling,
        Applying,
    };

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
        return dialogs_.wants_keyboard()
            || async_add_active_
            || async_graph_load_active_
            || chooser_open_
            || context_menu_open_
            || patch_ctx_open_
            || transport_bpm_editing_
            || session_editing_name_
            || session_ctx_menu_open_
            || record_dropdown_open_
            || editing_sticky_
            || sticky_color_menu_open_
            || build_console_panel_.wants_keyboard()
            || inspector_.wants_keyboard();
    }
    bool wire_inspector_visible() const;
    bool has_selection() const { return !selected_node_ids_.empty() || wire_inspector_visible(); }
    bool has_single_selection() const { return selected_node_ids_.size() == 1; }
    const std::string& single_selected_id() const { assert(!selected_node_ids_.empty()); return *selected_node_ids_.begin(); }

    void toggle_visible() { visible_ = !visible_; }
    void set_visible(bool visible) { visible_ = visible; }
    bool visible() const { return visible_; }

    // Graph actions (used by menu bar and bare-key shortcuts)
    void toggle_session_grid();
    void toggle_midi_map_mode();
    void delete_selected();
    void open_chooser();  // centers chooser in visible graph area
    bool graph_position_for_screen(float sx, float sy, float& gx, float& gy) const;
    void graph_center_position(float& gx, float& gy) const;
    void open_file_drop_chooser(std::vector<FileDropChooserAction> actions,
                                float graph_x, float graph_y);
    void confirm_chooser_selection(const std::string& type);
    using AsyncAddOperatorCallback =
        std::function<bool(const AsyncAddOperatorRequest&, std::string& error)>;
    void set_async_add_callback(AsyncAddOperatorCallback cb) {
        async_add_callback_ = std::move(cb);
    }
    bool async_add_active() const { return async_add_active_; }
    void set_async_add_stage(AsyncAddStage stage) { async_add_stage_ = stage; }
    void begin_async_graph_load(const std::string& display_name);
    bool async_graph_load_active() const { return async_graph_load_active_; }
    void set_async_graph_load_stage(AsyncGraphLoadStage stage) { async_graph_load_stage_ = stage; }
    void notify_async_graph_load_success();
    void notify_async_graph_load_failure(const std::string& summary);
    void update_modal_only();
    void notify_async_add_success(const std::string& node_id);
    void notify_async_add_failure(const std::string& summary);
    void clear_status_banner() { status_banner_error_.clear(); }

    // State queries (used by menu bar for checkmarks)
    bool session_grid_open() const { return session_grid_open_; }
    bool midi_map_mode() const { return midi_map_mode_; }
    bool build_console_open() const { return build_console_panel_.is_open(); }
    void toggle_build_console() { build_console_panel_.toggle_open(); }
    void set_build_console(std::shared_ptr<vivid::BuildConsole> console) {
        build_console_panel_.set_console(std::move(console));
    }

    // Called by main loop each frame with delta time
    void set_dt(float dt) {
        dt_ = dt;
        cursor_blink_time_ += dt;
        wire_flow_time_ += dt;
        if (editing_sticky_ && sticky_undo_dirty_) {
            sticky_undo_idle_time_ += dt;
            if (sticky_undo_idle_time_ > 0.4f) sticky_undo_commit();
        }
    }

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
        inspector_.set_custom_inspector_callback(std::move(cb));
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

    const std::string& pan_gesture() const { return pan_gesture_; }
    void set_pan_gesture(const std::string& g) { pan_gesture_ = g; }

    const UIStyle& style() const { return style_; }
    void set_style(const UIStyle& s) { style_ = s; }

    void toggle_preferences() { dialogs_.toggle_preferences(); }
    void toggle_package_browser() { dialogs_.toggle_package_browser(); }
    void set_package_browser_callbacks(PackageBrowserCallbacks callbacks) {
        dialogs_.set_package_browser_callbacks(std::move(callbacks));
    }
    void set_asset_browser_callbacks(AssetBrowserCallbacks callbacks) {
        dialogs_.set_asset_browser_callbacks(std::move(callbacks));
    }
    void notify_pkg_action_complete(bool success, const std::string& error) {
        dialogs_.notify_pkg_action_complete(success, error);
    }
    void toggle_example_browser() { dialogs_.toggle_example_browser(); }
    void set_examples(std::vector<ExampleEntry> examples) {
        dialogs_.set_examples(std::move(examples));
    }
    void set_example_open_callback(std::function<void(const std::string&)> cb) {
        dialogs_.set_example_open_callback(std::move(cb));
    }
    void set_example_package_checker(
        std::function<bool(const std::vector<std::string>&, std::string&)> cb) {
        dialogs_.set_example_package_checker(std::move(cb));
    }
    void open_graph_meta_editor(GraphMetaEditData data) {
        data.preview_options.clear();
        for (const auto& node : snap_.nodes) {
            if (!node.op_info) continue;
            PreviewControlOption opt;
            opt.node = node.node_id;
            for (const auto& param : node.op_info->params) {
                if (param.display_hint == VIVID_DISPLAY_HIDDEN) continue;
                opt.params.push_back(param.name);
            }
            if (!opt.params.empty())
                data.preview_options.push_back(std::move(opt));
        }
        dialogs_.open_graph_meta_editor(data, text_edit_);
    }
    void set_graph_meta_save_callback(
        std::function<bool(const GraphMetaEditData&, std::string&)> cb) {
        dialogs_.set_graph_meta_save_callback(std::move(cb));
    }
    void set_editor_options(std::vector<std::string> names, std::vector<std::string> ids,
                            int current_idx = 0, const std::string& custom_command = "") {
        dialogs_.set_editor_options(std::move(names), std::move(ids), current_idx, custom_command);
    }
    void set_style_options(std::vector<UIStyle> styles, int current_idx,
                            std::vector<ThemeInfo> themes = {}) {
        dialogs_.set_style_options(std::move(styles), current_idx, std::move(themes));
    }
    void set_audio_buffer_options(std::vector<uint32_t> sizes, int current_idx) {
        dialogs_.set_audio_buffer_options(std::move(sizes), current_idx);
    }
    void show_core_update_notice(const std::string& latest_version,
                                 const std::string& summary = "") {
        dialogs_.show_core_update_notice(latest_version, summary);
    }
    void clear_core_update_notice() { dialogs_.clear_core_update_notice(); }
    void set_core_update_notice_callbacks(std::function<void()> install_cb,
                                          std::function<void()> skip_cb,
                                          std::function<void()> later_cb) {
        dialogs_.set_core_update_notice_callbacks(std::move(install_cb),
                                                   std::move(skip_cb),
                                                   std::move(later_cb));
    }
    void open_about() { dialogs_.open_about(); }

    // Read-only UI snapshot accessors used by tests and seam verification.
    const std::vector<PackageBrowserEntry>& package_browser_entries() const { return dialogs_.package_browser_entries(); }
    const GraphMetaEditData& graph_meta_data() const { return dialogs_.graph_meta_data(); }
    std::vector<std::string> selected_node_ids_for_test() const {
        return std::vector<std::string>(selected_node_ids_.begin(), selected_node_ids_.end());
    }
    bool chooser_open_for_test() const { return chooser_open_; }
    bool file_drop_chooser_open_for_test() const {
        return chooser_open_ && chooser_mode_ == ChooserMode::FileDrop;
    }
    // Set the directory containing the MCP Python scripts (used in setup dialog)
    void set_mcp_dir(const std::string& dir) { dialogs_.set_mcp_dir(dir); }

    // Debug/signoff seam: force a single-node inspector selection by id.
    // Review/debug-only selection seam used for deterministic inspector capture.
    // This does not modify persisted graph state or undo history.
    bool select_single_node_for_review(const std::string& node_id);
    bool select_single_node_for_debug(const std::string& node_id) {
        return select_single_node_for_review(node_id);
    }

private:
    friend class InspectorController;
    // --- Layout ---
    void layout_nodes(bool force = false);
    void reposition_output_sinks();
    void place_new_nodes();
    void prune_node_rects();
    void recompute_ports(NodeRect& rect, const NodeSnapshot& ns);
    // refresh_package_browser_snapshot_if_ready moved to DialogManager

    // Count visible input/output ports for a node (signal ports + connected params/outputs)
    uint32_t count_visible_input_ports(const NodeSnapshot& ns, bool show_params = true) const;
    uint32_t count_visible_output_ports(const NodeSnapshot& ns, bool show_params = true) const;

    // --- Drawing (node_graph_draw.cpp) ---
    void draw_sticky_notes(Renderer2D& tr);
    void draw_graph(Renderer2D& tr);
    void draw_connections(Renderer2D& tr);
    void draw_inspector(Renderer2D& tr, uint32_t w, uint32_t h);
    void draw_inspector_header(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_params(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_one_inspector_param(Renderer2D& tr, const NodeSnapshot& node,
                                  InspectorLayout& layout,
                                  const ParamLayoutPlan& plan, uint32_t pi);
    void draw_inspector_knob(Renderer2D& tr, const NodeSnapshot& node,
                              InspectorLayout& layout,
                              const ParamLayoutPlan& plan, uint32_t pi);
    void draw_inspector_xy_pad(Renderer2D& tr, const NodeSnapshot& node,
                                InspectorLayout& layout, uint32_t pi_x, uint32_t pi_y);
    void draw_inspector_color_swatch(Renderer2D& tr, const NodeSnapshot& node,
                                      InspectorLayout& layout,
                                      uint32_t pi_r, uint32_t pi_g, uint32_t pi_b);
    void draw_inspector_adsr(Renderer2D& tr, const NodeSnapshot& node,
                              InspectorLayout& layout,
                              uint32_t pi_a, uint32_t pi_d,
                              uint32_t pi_s, uint32_t pi_r);
    void draw_inspector_lfo_preview(Renderer2D& tr, const NodeSnapshot& node,
                                     InspectorLayout& layout, uint32_t pi);
    void draw_inspector_step_seq(Renderer2D& tr, const NodeSnapshot& node,
                                  InspectorLayout& layout,
                                  uint32_t pi_start, uint32_t param_run_count);
    void draw_color_popup(Renderer2D& tr);
    void draw_inspector_group_header(Renderer2D& tr, InspectorLayout& layout,
                                      const std::string& type_name,
                                      const std::string& group_name, bool collapsed);
    void draw_one_inspector_param_simple(Renderer2D& tr, const NodeSnapshot& node,
                                         float px, float& py, uint32_t pi);
    void draw_section_separator(Renderer2D& tr, float px, float& py, float panel_w, const char* label);
    void draw_inspector_resolution(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_custom_inspector(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_outputs(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_state_presets(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_modulation(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_inspector_performance(Renderer2D& tr, const NodeSnapshot& node, float px, float& py);
    void draw_chooser(Renderer2D& tr);
    void draw_preview_wire(Renderer2D& tr);
    void draw_box_select(Renderer2D& tr);
    void draw_wire_tooltip(Renderer2D& tr);
    void draw_node_error_tooltip(Renderer2D& tr);
    void draw_param_tooltip(Renderer2D& tr);
    void draw_port_tooltip(Renderer2D& tr);
    void draw_description_popup(Renderer2D& tr, const std::string& desc);
    void draw_inspector_scrollbar(Renderer2D& tr);
    void draw_midi_map_banner(Renderer2D& tr);
    void draw_async_add_overlay(Renderer2D& tr);
    void draw_async_graph_load_overlay(Renderer2D& tr);
    void draw_status_banner(Renderer2D& tr);
    // draw_core_update_banner moved to DialogManager

    // --- Session grid ---
    void draw_session_grid(Renderer2D& tr);

    // --- Grid ---
    void draw_grid(Renderer2D& tr);

    // --- Workspace header ---
    void draw_workspace_header(Renderer2D& tr);
    void draw_perf_sparkline(Renderer2D& tr, const float* buf, uint32_t buf_len,
                             uint32_t write_idx, bool filled,
                             float x, float y, float w, float h,
                             float r, float g, float b, float a);
    void draw_diagnostics_panel(Renderer2D& tr);

    // --- Chooser ---
    void rebuild_chooser_items();
    bool build_async_add_request_for_selection(int idx, AsyncAddOperatorRequest& request);
    void stash_chooser_restore_state();
    void restore_chooser_after_async_failure();
    void confirm_chooser_selection_idx(int idx);
    void reset_chooser_state();

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
    void confirm_transport_bpm_edit();
    void cancel_transport_bpm_edit();
    void confirm_param_edit();
    void cancel_param_edit();
    void confirm_resolution_edit();
    void cancel_resolution_edit();
    void confirm_midi_range_edit();
    void cancel_midi_range_edit();
    void copy_selected_nodes();
    void paste_copied_nodes();
    std::string next_available_node_id(const std::string& base,
                                       const std::unordered_set<std::string>& reserved = {}) const;

    // --- Sorted port indices helper ---
    static std::vector<std::pair<uint32_t, std::string>> sorted_ports(
        const std::unordered_map<std::string, uint32_t>& port_indices);

    // --- Clone confirmation dialog (forwarded to DialogManager) ---
    void open_clone_confirm_dialog(const std::string& type_name,
                                   const std::string& node_id = {});

    // --- Save confirmation dialog (forwarded to DialogManager) ---
public:
    using SaveConfirmAction = vivid::SaveConfirmAction;
    void open_save_confirm_dialog(SaveConfirmAction action);
    // Callbacks forwarded to DialogManager
    std::function<void()>& on_save_confirm_save = dialogs_.on_save_confirm_save;
    std::function<void()>& on_save_confirm_dont_save = dialogs_.on_save_confirm_dont_save;
    std::function<void()>& on_save_confirm_cancel = dialogs_.on_save_confirm_cancel;
    bool save_confirm_open() const { return dialogs_.save_confirm_open(); }
    SaveConfirmAction save_confirm_action() const { return dialogs_.save_confirm_action(); }

    // --- Crash-recovery dialog (forwarded to DialogManager) ---
    void open_crash_recovery(const vivid::CrashRecord& rec, std::string report_path) {
        dialogs_.open_crash_recovery(rec, std::move(report_path));
    }
    std::function<void()>& on_crash_recovery_open_normally  = dialogs_.on_crash_recovery_open_normally;
    std::function<void()>& on_crash_recovery_open_safe_mode = dialogs_.on_crash_recovery_open_safe_mode;
    std::function<void()>& on_crash_recovery_reveal_report  = dialogs_.on_crash_recovery_reveal_report;

    // --- System-requirements dialog (forwarded to DialogManager) ---
    void open_system_requirements(bool auto_opened = false, std::string header_note = {}) {
        dialogs_.open_system_requirements(auto_opened, std::move(header_note));
    }
    bool system_requirements_open() const { return dialogs_.system_requirements_open(); }
private:

    // Create operator modal, preset name popup moved to DialogManager

    // Package browser / Example browser moved to DialogManager
    // draw_graph_meta_editor and update_graph_meta_editor moved to DialogManager

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
    void update_transport_bpm_drag();
    void update_modulation_drag();
    void update_xy_pad_drag();
    void update_rich_inspector_drag();
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
    ActiveTextField resolve_active_text_field();
    bool handle_transport_bpm_edit_key(int key);
    bool handle_sticky_edit_mode_key(int key, bool mod_key, bool shift);
    bool handle_session_mode_key(int key, int action, int mods, bool mod_key);
    bool handle_inspector_edit_mode_key(int key);
    bool handle_param_picker_mode_key(int key);
    bool handle_dropdown_mode_key(int key);
    bool handle_graph_global_key(int key, int action, int mods, bool mod_key);
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
    float session_strip_top() const;
    float inspector_x() const { return static_cast<float>(win_w_) - kInspectorW; }
    float chooser_x() const { return (graph_right() - kChooserW) * 0.5f; }
    float chooser_items_y() const {
        float base = kChooserY + kChooserHeaderH;
        if (chooser_mode_ == ChooserMode::Operators) base += kChooserTabH;
        return base;
    }

    UICommandSink& commands_;
    DialogManager dialogs_;
    GraphSnapshot snap_;
    bool snap_valid_ = false;
    MouseState mouse_;
    std::unordered_set<std::string> selected_node_ids_;
    int selected_wire_idx_ = -1;  // index into snap_.connections, or -1
    std::vector<NodeRect> node_rects_;
    struct ClipboardNode {
        NodeSnapshot node;
        float rel_x = 0.0f;
        float rel_y = 0.0f;
    };
    std::vector<ClipboardNode> clipboard_nodes_;
    std::vector<ConnectionSnapshot> clipboard_connections_;

    // Track topology version to re-layout on changes
    size_t last_node_count_ = 0;
    size_t last_conn_count_ = 0;
    bool first_layout_done_ = false;
    bool output_sink_positioned_ = false;

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

    // Shared text-edit cursor/selection state (only one field active at a time)
    TextEditState text_edit_;
    InspectorController inspector_;

    // MIDI map mode state
    bool midi_map_mode_ = false;
    bool midi_map_waiting_ = false;          // clicked param, waiting for CC
    std::string midi_map_node_id_;
    std::string midi_map_param_name_;

    // Preset name popup state: moved to DialogManager

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
    enum class ChooserMode {
        Operators,
        FileDrop,
    };
    enum class ChooserTab : uint8_t { All = 0, GPU, Audio, Control };
    bool chooser_open_ = false;
    ChooserMode chooser_mode_ = ChooserMode::Operators;
    ChooserTab chooser_tab_ = ChooserTab::All;
    std::string chooser_filter_;
    int chooser_sel_ = 0;
    float chooser_scroll_ = 0.0f;
    std::vector<std::string> chooser_items_;
    std::vector<std::string> chooser_subtitles_;
    std::vector<FileDropChooserAction> chooser_drop_actions_;
    float chooser_cursor_gx_ = 0, chooser_cursor_gy_ = 0;
    std::string chooser_error_;

    // Insert-on-wire state (chooser opened from wire context menu)
    bool chooser_insert_wire_ = false;
    ConnectionSnapshot chooser_insert_conn_;
    VividPortType insert_wire_source_type_ = VIVID_PORT_SCALAR;
    VividPortType insert_wire_dest_type_ = VIVID_PORT_SCALAR;

    // Connect-from-wire-drag state (chooser opened via Tab during wire drag)
    bool chooser_wire_connect_ = false;
    std::string wire_connect_node_id_;
    std::string wire_connect_port_;
    bool wire_connect_from_output_ = true;
    VividPortType wire_connect_type_ = VIVID_PORT_SCALAR;

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

    float port_hover_time_ = 0.0f;
    std::string port_hover_node_id_;
    std::string port_hover_name_;
    bool port_hover_is_output_ = false;

    // Multi-output expand state (keyed by node_id)
    std::unordered_set<std::string> outputs_expanded_;
    struct ExpandAffordanceRect { float x, y, w, h; std::string node_id; };
    std::vector<ExpandAffordanceRect> expand_affordance_rects_;

    // Group collapse state
    bool is_group_collapsed(const std::string& type_name, const std::string& group) const {
        auto it = inspector_.group_collapsed.find(type_name + "\t" + group);
        return it != inspector_.group_collapsed.end() && it->second;
    }
    void toggle_group_collapsed(const std::string& type_name, const std::string& group) {
        auto key = type_name + "\t" + group;
        inspector_.group_collapsed[key] = !inspector_.group_collapsed[key];
    }

    // Cached window dimensions (updated each frame in draw())
    uint32_t win_w_ = 1280, win_h_ = 720;

    // Double-click detection for shader editing
    double last_click_time_ = 0.0;
    std::string last_click_node_id_;

    // Clone confirm + Save confirm state: migrated to DialogManager

    // Create operator modal state: moved to DialogManager

    // Preferences state: moved to DialogManager::prefs

    // Package browser / Example browser state: moved to DialogManager

    // --- Graph meta editor (migrated to DialogManager) ---

    // --- Session grid (variation strip / exploration surface) ---
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
    // Selected card (separate from active — keyboard/visual focus)
    int session_selected_idx_ = -1;
    // Drag reorder state
    int session_drag_idx_ = -1;
    int session_drag_target_idx_ = -1;
    float session_drag_start_x_ = 0.0f;
    float session_drag_start_y_ = 0.0f;
    bool session_drag_active_ = false;
    // Context menu state
    bool session_ctx_menu_open_ = false;
    float session_ctx_menu_x_ = 0.0f;
    float session_ctx_menu_y_ = 0.0f;
    int session_ctx_menu_idx_ = -1;
    // Hit-test rects
    struct SessionCollapsedRect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; bool visible = false; };
    SessionCollapsedRect session_collapsed_rect_;
    struct VariationCellRect { float x, y, w, h; int idx; };
    std::vector<VariationCellRect> variation_cell_rects_;
    struct SessionButtonRect { float x, y, w, h; int action; bool enabled = true; }; // action: 0=+New, 1=Update, 2-5=quantize, 6=Branch, 7=Close
    std::vector<SessionButtonRect> session_button_rects_;
    struct SessionCtxMenuRect { float x, y, w, h; int action; }; // action: 0=Rename, 1=Duplicate, 2=Delete, 3=Branch From
    std::vector<SessionCtxMenuRect> session_ctx_menu_rects_;

    // Active UI style
    UIStyle style_;
    BuildConsolePanel build_console_panel_;
    AsyncAddOperatorCallback async_add_callback_;
    struct AsyncAddChooserRestoreState {
        bool valid = false;
        ChooserMode mode = ChooserMode::Operators;
        ChooserTab tab = ChooserTab::All;
        std::string filter;
        int sel = 0;
        float scroll = 0.0f;
        std::vector<std::string> items;
        std::vector<std::string> subtitles;
        std::vector<FileDropChooserAction> drop_actions;
        float cursor_gx = 0.0f;
        float cursor_gy = 0.0f;
        bool insert_wire = false;
        ConnectionSnapshot insert_conn;
        VividPortType insert_wire_source_type = VIVID_PORT_SCALAR;
        VividPortType insert_wire_dest_type = VIVID_PORT_SCALAR;
        bool wire_connect = false;
        std::string wire_connect_node_id;
        std::string wire_connect_port;
        bool wire_connect_from_output = true;
        VividPortType wire_connect_type = VIVID_PORT_SCALAR;
    };
    AsyncAddChooserRestoreState async_add_restore_;
    bool async_add_active_ = false;
    AsyncAddStage async_add_stage_ = AsyncAddStage::Preparing;
    std::string async_add_display_name_;
    bool async_graph_load_active_ = false;
    AsyncGraphLoadStage async_graph_load_stage_ = AsyncGraphLoadStage::Loading;
    std::string async_graph_load_display_name_;
    std::string status_banner_error_;

    // Wire rendering style toggle (B key)
    bool bezier_wires_ = false;

    // Param wire visibility toggle (P key)
    bool show_param_wires_ = false;
    bool last_show_param_wires_ = false;  // detect toggle for relayout

    // Pan gesture setting ("middle", "left", "right")
    std::string pan_gesture_ = "left";

    // Right-drag pan state (used when pan_gesture_ == "right")
    bool right_pending_ = false;
    float right_press_mx_ = 0, right_press_my_ = 0;

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
    PerfRingBuffer audio_load_history_;

    float dt_ = 0.0f;
    float cursor_blink_time_ = 0.0f;  // accumulated time for cursor blink
    float wire_flow_time_ = 0.0f;     // accumulated time for animated wire flow
    float smoothed_fps_ = 0.0f;
    float smoothed_ms_ = 0.0f;
    float display_fps_ = 0.0f;
    float display_ms_ = 0.0f;
    float smoothed_mem_mb_ = 0.0f;
    uint64_t perf_frame_counter_ = 0;

    bool perf_mem_hovered_ = false;
    float perf_mem_graph_x_ = 0.0f;
    float perf_mem_graph_y_ = 0.0f;

    bool perf_audio_hovered_ = false;
    float perf_audio_section_x_ = 0.0f;  // full audio section for hover
    float perf_audio_section_w_ = 0.0f;
    float perf_audio_graph_x_ = 0.0f;
    float perf_audio_graph_y_ = 0.0f;

    // --- Diagnostics/workspace header controls ---
    bool record_dropdown_open_ = false;
    float record_dropdown_x_ = 0.0f, record_dropdown_y_ = 0.0f;
    int record_codec_sel_ = 0;  // 0=H.264, 1=H.265, 2=ProRes 4444

    struct PerfButtonRect { float x, y, w, h; int action; bool enabled; };
    // action: 0=Record/Stop, 1=Snapshot, 2=Diagnostics, 5=Metronome toggle,
    // 6=Meter-, 7=Meter+
    std::vector<PerfButtonRect> perf_button_rects_;
    struct TransportValueRect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; bool visible = false; };
    TransportValueRect diagnostics_button_rect_;
    TransportValueRect lockfile_badge_rect_;  // Phase 6b: perf-bar lockfile status badge
    TransportValueRect transport_bpm_rect_;
    bool transport_bpm_dragging_ = false;
    float transport_bpm_drag_start_y_ = 0.0f;
    float transport_bpm_drag_start_bpm_ = 120.0f;
    bool transport_bpm_editing_ = false;
    std::string transport_bpm_edit_buffer_;
    double transport_bpm_last_click_time_ = -1.0;
    bool diagnostics_panel_open_ = false;
    struct DiagnosticsPanelRect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; bool visible = false; };
    DiagnosticsPanelRect diagnostics_panel_rect_;
    struct DiagnosticsMcpRect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; int idx = -1; };
    std::vector<DiagnosticsMcpRect> diagnostics_mcp_rects_;

    // --- Sticky notes ---
    struct StickyNoteRect { std::string id; float x, y, w, h; };
    std::vector<StickyNoteRect> sticky_note_rects_;
    struct StickyLinkRect { float x, y, w, h; std::string url; };
    std::vector<StickyLinkRect> sticky_link_rects_;
    std::string selected_sticky_id_;
    int dragging_sticky_idx_ = -1;
    float sticky_drag_offset_x_ = 0, sticky_drag_offset_y_ = 0;
    int resizing_sticky_idx_ = -1;
    int sticky_resize_edge_ = 0;  // bitmask: 1=left, 2=right, 4=top, 8=bottom
    float sticky_resize_start_x_ = 0, sticky_resize_start_y_ = 0;
    float sticky_resize_start_w_ = 0, sticky_resize_start_h_ = 0;
    float sticky_resize_start_gx_ = 0, sticky_resize_start_gy_ = 0;
    bool editing_sticky_ = false;
    std::string sticky_edit_buffer_;
    std::string sticky_edit_id_;
    int sticky_note_id_counter_ = 0;

    // Local text-edit undo for sticky editor (separate from graph undo).
    // Snapshots are committed on idle (~400ms) or on navigation/Enter.
    struct StickyUndoSnap { std::string buf; int cursor = 0; int sel_start = -1; };
    std::vector<StickyUndoSnap> sticky_undo_;
    std::vector<StickyUndoSnap> sticky_redo_;
    float sticky_undo_idle_time_ = 0.0f;
    bool sticky_undo_dirty_ = false;
    void sticky_undo_seed();
    void sticky_undo_mark_dirty();
    void sticky_undo_commit();
    void sticky_undo_apply(bool redo);
    void sticky_undo_clear();
    // Sticky note color picker context menu state
    bool sticky_color_menu_open_ = false;
    std::string sticky_color_menu_id_;
    float sticky_color_menu_x_ = 0, sticky_color_menu_y_ = 0;

    // Core update notice state: moved to DialogManager
};

} // namespace vivid::ui
