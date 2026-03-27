#define private public
#include "ui/node_graph.h"
#undef private
#include "ui/graph_snapshot.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>

using namespace vivid::ui;

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

struct DummySink : UICommandSink {
    struct ParamCall { std::string node_id, param; float value; };
    std::vector<ParamCall> set_param_calls;

    void set_param(const std::string& node_id, const std::string& param, float value) override {
        set_param_calls.push_back({node_id, param, value});
    }
    void add_node(const std::string&, const std::string&) override {}
    void remove_node(const std::string&) override {}
    void connect(const std::string&, const std::string&) override {}
    void disconnect(const std::string&, const std::string&) override {}
    void set_connection_remap(const std::string&, const std::string&, float, float, float, float, bool) override {}
    void set_node_layout(const std::string&, float, float) override {}
    void set_resolution(const std::string&, uint32_t, uint32_t) override {}
    void add_midi_mapping(const std::string&, const std::string&, int, int, float, float) override {}
    void remove_midi_mapping(const std::string&, const std::string&) override {}
    void update_midi_mapping(const std::string&, const std::string&, float, float) override {}
    void set_string_param(const std::string&, const std::string&, const std::string&) override {}
};

static std::shared_ptr<OperatorInfo> make_widget_op() {
    auto op = std::make_shared<OperatorInfo>();
    op->name = "WidgetTest";
    op->is_gpu = false;
    op->params = {
        ParamInfo{"gain", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"pos_x", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f, {}, 0, "", VIVID_DISPLAY_XY_PAD},
        ParamInfo{"pos_y", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f, {}, 0, "", VIVID_DISPLAY_XY_PAD},
        ParamInfo{"mode", VIVID_PARAM_INT, 0.0f, 0.0f, 2.0f, {"Off", "On", "Auto"}, 3},
        ParamInfo{"enabled", VIVID_PARAM_INT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"r", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"g", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"b", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"percent", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 100.0f},
    };
    return op;
}

static GraphSnapshot make_widget_snapshot() {
    GraphSnapshot snap;
    auto op = make_widget_op();
    NodeSnapshot node;
    node.node_id = "widget1";
    node.type_name = op->name;
    node.active_cadence = vivid::Cadence::Frame;
    node.has_layout = true;
    node.layout_x = 120.0f;
    node.layout_y = 120.0f;
    node.op_info = op;
    node.param_values = {0.25f, 0.2f, 0.8f, 0.0f, 0.0f, 0.1f, 0.2f, 0.3f, 25.0f};
    node.param_lock_flags.resize(node.param_values.size(), 0);
    for (uint32_t i = 0; i < op->params.size(); ++i) {
        node.param_indices[op->params[i].name] = i;
    }
    snap.node_index[node.node_id] = 0;
    snap.nodes.push_back(std::move(node));
    return snap;
}

static const DummySink::ParamCall& last_param(const DummySink& sink) {
    if (sink.set_param_calls.empty()) {
        std::fprintf(stderr, "missing set_param call\n");
        std::abort();
    }
    return sink.set_param_calls.back();
}

int main() {
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.slider_rects_.push_back({920.0f, 120.0f, 160.0f, 16.0f, "widget1", "gain"});
        ui.on_mouse_move(940.0f, 128.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(1200.0f, 128.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
        check(!sink.set_param_calls.empty(), "Slider drag emits a param update");
        check(last_param(sink).param == "gain" && std::fabs(last_param(sink).value - 1.0f) < 0.001f,
              "Slider drag clamps to the max bound");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.xy_pad_rects_.push_back({920.0f, 160.0f, 120.0f, 120.0f, "widget1", "pos_x", "pos_y"});
        ui.on_mouse_move(940.0f, 180.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(1040.0f, 280.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
        check(sink.set_param_calls.size() >= 2, "XY pad drag updates both bound parameters");
        check(sink.set_param_calls[sink.set_param_calls.size() - 2].param == "pos_x" &&
                  std::fabs(sink.set_param_calls[sink.set_param_calls.size() - 2].value - 1.0f) < 0.001f,
              "XY pad horizontal motion maps to the X parameter");
        check(sink.set_param_calls.back().param == "pos_y" &&
                  std::fabs(sink.set_param_calls.back().value - 0.0f) < 0.001f,
              "XY pad vertical motion maps to the Y parameter without axis swap");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.color_popup_open_ = true;
        ui.color_popup_node_id_ = "widget1";
        ui.color_popup_param_r_ = "r";
        ui.color_popup_param_g_ = "g";
        ui.color_popup_param_b_ = "b";
        ui.color_popup_x_ = 920.0f;
        ui.color_popup_y_ = 140.0f;
        ui.color_dragging_sv_ = true;
        ui.mouse_.left_down = true;
        ui.mouse_.x = ui.color_popup_x_ + kColorPopupPad + kColorPopupSVSize;
        ui.mouse_.y = ui.color_popup_y_ + kColorPopupPad;
        ui.update(snap);
        check(sink.set_param_calls.size() == 3, "Color SV drag updates all RGB channels");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.color_popup_open_ = true;
        ui.color_popup_node_id_ = "widget1";
        ui.color_popup_param_r_ = "r";
        ui.color_popup_param_g_ = "g";
        ui.color_popup_param_b_ = "b";
        ui.color_popup_x_ = 920.0f;
        ui.color_popup_y_ = 140.0f;
        ui.color_dragging_hue_ = true;
        ui.mouse_.left_down = true;
        ui.mouse_.x = ui.color_popup_x_ + kColorPopupPad + kColorPopupSVSize + kColorPopupGap + kColorHueBarW * 0.5f;
        ui.mouse_.y = ui.color_popup_y_ + kColorPopupPad + kColorPopupSVSize;
        ui.update(snap);
        check(sink.set_param_calls.size() == 3, "Color hue drag updates all RGB channels");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.color_popup_open_ = true;
        ui.color_popup_node_id_ = "widget1";
        ui.color_popup_param_r_ = "r";
        ui.color_popup_param_g_ = "g";
        ui.color_popup_param_b_ = "b";
        ui.color_editing_hex_ = true;
        ui.color_hex_buffer_ = "#336699";
        ui.on_key(GLFW_KEY_ENTER, GLFW_PRESS, 0);
        check(sink.set_param_calls.size() == 3, "Color hex commit updates all RGB channels");
        check(std::fabs(sink.set_param_calls[0].value - 0.2f) < 0.01f &&
                  std::fabs(sink.set_param_calls[1].value - 0.4f) < 0.01f &&
                  std::fabs(sink.set_param_calls[2].value - 0.6f) < 0.01f,
              "Color hex commit converts to the expected RGB values");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.color_popup_open_ = true;
        ui.color_popup_node_id_ = "widget1";
        ui.color_popup_param_r_ = "r";
        ui.color_popup_param_g_ = "g";
        ui.color_popup_param_b_ = "b";
        ui.color_editing_rgb_ = 0;
        ui.color_rgb_buffer_ = "255";
        ui.on_key(GLFW_KEY_TAB, GLFW_PRESS, 0);
        check(sink.set_param_calls.size() == 1 &&
                  sink.set_param_calls[0].param == "r" &&
                  std::fabs(sink.set_param_calls[0].value - 1.0f) < 0.001f,
              "RGB text edit commits the active channel on Tab");
        check(ui.color_editing_rgb_ == 1, "RGB text edit advances focus to the next channel");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.value_text_rects_.push_back({920.0f, 120.0f, 80.0f, 18.0f, "widget1", "percent"});
        ui.on_mouse_move(940.0f, 128.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_SUPER);
        ui.on_char('7');
        ui.on_char('5');
        ui.on_key(GLFW_KEY_ENTER, GLFW_PRESS, 0);
        check(sink.set_param_calls.size() == 1 &&
                  sink.set_param_calls[0].param == "percent" &&
                  std::fabs(sink.set_param_calls[0].value - 75.0f) < 0.001f,
              "Typed numeric edit commits the displayed value through set_param");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.value_text_rects_.push_back({920.0f, 120.0f, 80.0f, 18.0f, "widget1", "percent"});
        ui.on_mouse_move(940.0f, 128.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_key(GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_SUPER);
        ui.on_char('9');
        ui.on_char('9');
        ui.on_key(GLFW_KEY_ESCAPE, GLFW_PRESS, 0);
        check(sink.set_param_calls.empty(), "Cancelling a typed edit leaves runtime params untouched");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.dropdown_rects_.push_back({920.0f, 220.0f, 120.0f, 18.0f, "widget1", "mode"});
        ui.on_mouse_move(940.0f, 228.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_key(GLFW_KEY_DOWN, GLFW_PRESS, 0);
        ui.on_key(GLFW_KEY_DOWN, GLFW_PRESS, 0);
        ui.on_key(GLFW_KEY_ENTER, GLFW_PRESS, 0);
        check(sink.set_param_calls.size() == 1 &&
                  sink.set_param_calls[0].param == "mode" &&
                  std::fabs(sink.set_param_calls[0].value - 2.0f) < 0.001f,
              "Dropdown keyboard selection dispatches the chosen index");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.bool_rects_.push_back({920.0f, 260.0f, 14.0f, 14.0f, "widget1", "enabled"});
        ui.on_mouse_move(926.0f, 266.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(sink.set_param_calls.size() == 1 &&
                  sink.set_param_calls[0].param == "enabled" &&
                  std::fabs(sink.set_param_calls[0].value - 1.0f) < 0.001f,
              "Toggle click flips the bound boolean param");
    }

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
