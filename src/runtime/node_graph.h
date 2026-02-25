#ifndef VIVID_RUNTIME_NODE_GRAPH_H
#define VIVID_RUNTIME_NODE_GRAPH_H

#include "operator_api/types.h"
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>

namespace vivid {

class RuntimeAPI;
class Graph;
class Scheduler;
struct NodeState;
class TextRenderer;
class AudioEngine;
class ThumbnailRenderer;
class ThumbnailCache;

struct MouseState {
    float x = 0, y = 0;
    bool left_down = false;
    bool left_clicked = false;   // true on the frame the button went down
    bool left_released = false;  // true on the frame the button went up
};

struct NodeRect {
    std::string node_id;
    std::string type_name;
    VividDomain domain = VIVID_DOMAIN_CONTROL;
    float x, y, w, h;
    struct PortPos { std::string name; float x, y; };
    std::vector<PortPos> inputs, outputs;
};

class NodeGraphUI {
public:
    NodeGraphUI(RuntimeAPI& api, const Graph& graph, const Scheduler& scheduler,
                AudioEngine* audio_engine = nullptr);

    // GLFW callbacks
    void on_mouse_move(float x, float y);
    void on_mouse_button(int button, int action);

    // Per-frame
    void update();
    void draw(TextRenderer& tr, uint32_t w, uint32_t h);

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
    void layout_nodes();
    void recompute_ports(NodeRect& rect, const NodeState& ns);
    void draw_graph(TextRenderer& tr);
    void draw_connections(TextRenderer& tr);
    void draw_inspector(TextRenderer& tr, uint32_t w);
    int hit_test_node(float mx, float my) const;
    int hit_test_slider(float mx, float my) const;
    int hit_test_bool(float mx, float my) const;

    RuntimeAPI& api_;
    const Graph& graph_;
    const Scheduler& scheduler_;
    AudioEngine* audio_engine_ = nullptr;
    MouseState mouse_;
    std::string selected_node_id_;
    std::vector<NodeRect> node_rects_;

    // Track topology version to re-layout on changes
    size_t last_node_count_ = 0;
    size_t last_conn_count_ = 0;

    // Node drag state
    int dragging_node_idx_ = -1;
    float drag_offset_x_ = 0, drag_offset_y_ = 0;

    // Slider drag state
    int active_slider_idx_ = -1;
    std::string active_slider_node_id_;
    std::string active_slider_param_name_;

    struct SliderRect { float x, y, w, h; std::string node_id; std::string param_name; };
    std::vector<SliderRect> slider_rects_;

    struct BoolRect { float x, y, w, h; std::string node_id; std::string param_name; };
    std::vector<BoolRect> bool_rects_;

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
};

} // namespace vivid

#endif // VIVID_RUNTIME_NODE_GRAPH_H
