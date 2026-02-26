#ifndef VIVID_UI_NODE_GRAPH_H
#define VIVID_UI_NODE_GRAPH_H

#include "ui/node_graph_constants.h"
#include "ui/graph_snapshot.h"
#include "ui/ui_command_sink.h"
#include "operator_api/types.h"
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace vivid::ui {

class Renderer2D;
class ThumbnailRenderer;
class ThumbnailCache;

struct MouseState {
    float x = 0, y = 0;
    bool left_down = false;
    bool left_clicked = false;   // true on the frame the button went down
    bool left_released = false;  // true on the frame the button went up
    bool right_clicked = false;  // true on the frame right button went down
};

struct NodeRect {
    std::string node_id;
    std::string type_name;
    VividDomain domain = VIVID_DOMAIN_CONTROL;
    float x = 0, y = 0, w = 0, h = 0;
    struct PortPos { std::string name; float x, y; };
    std::vector<PortPos> inputs, outputs;
};

class NodeGraphUI {
public:
    NodeGraphUI(UICommandSink& commands);

    // GLFW callbacks
    void on_mouse_move(float x, float y);
    void on_mouse_button(int button, int action);
    void on_scroll(float x_offset, float y_offset);
    void on_key(int key, int action, int mods);
    void on_char(unsigned int codepoint);

    // Returns true when a popup is open and wants keyboard focus
    bool wants_keyboard() const { return chooser_open_ || editing_param_ || editing_resolution_ || dropdown_open_ || context_menu_open_; }
    bool has_selection() const { return !selected_node_id_.empty(); }

    void toggle_visible() { visible_ = !visible_; }
    bool visible() const { return visible_; }

    // Called by main loop each frame with delta time
    void set_dt(float dt) { dt_ = dt; }

    // Per-frame
    void update(const GraphSnapshot& snapshot);
    void draw(Renderer2D& tr, uint32_t w, uint32_t h);

    // GPU thumbnail overlay (separate render pass after text)
    void draw_thumbnails(ThumbnailRenderer& tr, const ThumbnailCache& cache,
                         WGPUCommandEncoder encoder, WGPUTextureView surface,
                         uint32_t w, uint32_t h);

    const std::vector<NodeRect>& node_rects() const { return node_rects_; }

    void set_custom_thumbnail_nodes(std::unordered_set<std::string> ids) {
        custom_thumb_nodes_ = std::move(ids);
    }

    void set_dpi_scale(float scale) { dpi_scale_ = scale; }

private:
    // --- Layout ---
    void layout_nodes();
    void recompute_ports(NodeRect& rect, const NodeSnapshot& ns);

    // --- Drawing (node_graph_draw.cpp) ---
    void draw_graph(Renderer2D& tr);
    void draw_connections(Renderer2D& tr);
    void draw_inspector(Renderer2D& tr, uint32_t w, uint32_t h);
    void draw_chooser(Renderer2D& tr);
    void draw_preview_wire(Renderer2D& tr);
    void draw_wire_tooltip(Renderer2D& tr);

    // --- Performance bar ---
    void draw_perf_bar(Renderer2D& tr);
    void draw_perf_sparkline(Renderer2D& tr, const float* buf, uint32_t buf_len,
                             uint32_t write_idx, bool filled,
                             float x, float y, float w, float h,
                             float r, float g, float b, float a);
    void draw_perf_expanded(Renderer2D& tr);

    // --- Chooser ---
    void rebuild_chooser_items();

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

    // --- Sorted port indices helper ---
    static std::vector<std::pair<uint32_t, std::string>> sorted_ports(
        const std::unordered_map<std::string, uint32_t>& port_indices);

    // --- Decomposed update() sub-methods ---
    void check_relayout();
    void update_pan();
    void update_node_drag();
    void update_wire_drag();
    void update_slider_drag();
    void update_chooser_hover();
    void update_context_menu();
    void update_pan_release();
    void clear_frame_flags();
    void update_wire_hover();
    void update_sparklines();

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
    float inspector_x() const { return static_cast<float>(win_w_) - kInspectorW; }
    float chooser_x() const { return (graph_right() - kChooserW) * 0.5f; }

    UICommandSink& commands_;
    const GraphSnapshot* snap_ = nullptr;
    MouseState mouse_;
    std::string selected_node_id_;
    std::vector<NodeRect> node_rects_;

    // Track topology version to re-layout on changes
    size_t last_node_count_ = 0;
    size_t last_conn_count_ = 0;

    // Node drag state
    int dragging_node_idx_ = -1;
    float drag_offset_x_ = 0, drag_offset_y_ = 0;

    // Wire drag state
    bool dragging_wire_ = false;
    std::string wire_from_node_id_;
    std::string wire_from_port_;
    float wire_from_gx_ = 0, wire_from_gy_ = 0;

    // Zoom/pan state
    float zoom_ = 1.0f;
    float pan_x_ = 0.0f, pan_y_ = 0.0f;
    bool panning_ = false;
    float pan_start_mx_ = 0, pan_start_my_ = 0;
    float pan_start_px_ = 0, pan_start_py_ = 0;

    // Slider drag state
    int active_slider_idx_ = -1;
    std::string active_slider_node_id_;
    std::string active_slider_param_name_;

    struct InspectorRect { float x, y, w, h; std::string node_id; std::string param_name; };
    std::vector<InspectorRect> slider_rects_;

    // Slider text-edit state (click value text to type a value)
    bool editing_param_ = false;
    std::string edit_node_id_;
    std::string edit_param_name_;
    std::string edit_buffer_;

    std::vector<InspectorRect> bool_rects_;
    std::vector<InspectorRect> value_text_rects_;
    std::vector<InspectorRect> dropdown_rects_;

    struct ResolutionRect { float x, y, w, h; std::string node_id; bool is_width; };
    std::vector<ResolutionRect> resolution_rects_;

    // Resolution editing state
    bool editing_resolution_ = false;
    std::string edit_res_node_id_;
    bool edit_res_is_width_ = true;

    // Dropdown popup state
    bool dropdown_open_ = false;
    std::string dropdown_node_id_;
    std::string dropdown_param_name_;
    int dropdown_sel_ = 0;
    float dropdown_x_ = 0, dropdown_y_ = 0, dropdown_w_ = 0;
    std::vector<std::string> dropdown_labels_;

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

    // Right-click context menu state
    bool context_menu_open_ = false;
    float context_menu_x_ = 0, context_menu_y_ = 0;  // screen space
    std::string context_node_id_;   // non-empty if node menu
    int context_wire_idx_ = -1;     // >= 0 if wire menu
    int hovered_wire_idx_ = -1;

    // Cached window dimensions (updated each frame in draw())
    uint32_t win_w_ = 1280, win_h_ = 720;

    // Wire rendering style toggle (B key)
    bool bezier_wires_ = false;

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
    float smoothed_fps_ = 0.0f;
    float smoothed_ms_ = 0.0f;
    float smoothed_mem_mb_ = 0.0f;
    uint64_t perf_frame_counter_ = 0;

    bool perf_mem_hovered_ = false;
    float perf_mem_graph_x_ = 0.0f;
    float perf_mem_graph_y_ = 0.0f;
};

} // namespace vivid::ui

#endif // VIVID_UI_NODE_GRAPH_H
