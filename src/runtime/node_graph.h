#ifndef VIVID_RUNTIME_NODE_GRAPH_H
#define VIVID_RUNTIME_NODE_GRAPH_H

#include <string>
#include <vector>

namespace vivid {

class RuntimeAPI;
class Graph;
class Scheduler;
class TextRenderer;

struct MouseState {
    float x = 0, y = 0;
    bool left_down = false;
    bool left_clicked = false;   // true on the frame the button went down
    bool left_released = false;  // true on the frame the button went up
};

struct NodeRect {
    std::string node_id;
    std::string type_name;
    float x, y, w, h;
    struct PortPos { std::string name; float x, y; };
    std::vector<PortPos> inputs, outputs;
};

class NodeGraphUI {
public:
    NodeGraphUI(RuntimeAPI& api, const Graph& graph, const Scheduler& scheduler);

    // GLFW callbacks
    void on_mouse_move(float x, float y);
    void on_mouse_button(int button, int action);

    // Per-frame
    void update();
    void draw(TextRenderer& tr, uint32_t w, uint32_t h);

private:
    void layout_nodes();
    void draw_graph(TextRenderer& tr);
    void draw_connections(TextRenderer& tr);
    void draw_inspector(TextRenderer& tr, uint32_t w);
    int hit_test_node(float mx, float my) const;
    int hit_test_slider(float mx, float my) const;
    int hit_test_bool(float mx, float my) const;

    RuntimeAPI& api_;
    const Graph& graph_;
    const Scheduler& scheduler_;
    MouseState mouse_;
    std::string selected_node_id_;
    std::vector<NodeRect> node_rects_;

    // Track topology version to re-layout on changes
    size_t last_node_count_ = 0;
    size_t last_conn_count_ = 0;

    // Slider drag state
    int active_slider_idx_ = -1;
    std::string active_slider_node_id_;
    std::string active_slider_param_name_;

    struct SliderRect { float x, y, w, h; std::string node_id; std::string param_name; };
    std::vector<SliderRect> slider_rects_;

    struct BoolRect { float x, y, w, h; std::string node_id; std::string param_name; };
    std::vector<BoolRect> bool_rects_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_NODE_GRAPH_H
