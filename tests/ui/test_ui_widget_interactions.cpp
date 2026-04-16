#define private public
#include "ui/graph/node_graph.h"
#undef private
#include "ui/graph/graph_snapshot.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include "test_helpers.h"

using namespace vivid::ui;

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
    void set_connection_remap(const std::string&, const std::string&, float, float, float, float, bool, uint8_t) override {}
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
        // name, type, default, min, max, default_string, choice_labels, choice_count, group, display_hint
        ParamInfo{"gain", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"pos_x", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f, "", {}, 0, "", VIVID_DISPLAY_XY_PAD},
        ParamInfo{"pos_y", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f, "", {}, 0, "", VIVID_DISPLAY_XY_PAD},
        ParamInfo{"mode", VIVID_PARAM_INT, 0.0f, 0.0f, 2.0f, "", {"Off", "On", "Auto"}, 3},
        ParamInfo{"enabled", VIVID_PARAM_INT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"r", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"g", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"b", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        ParamInfo{"percent", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 100.0f},
        ParamInfo{"attack", VIVID_PARAM_FLOAT, 0.1f, 0.001f, 2.0f, "", {}, 0, "", VIVID_DISPLAY_ADSR},
        ParamInfo{"decay", VIVID_PARAM_FLOAT, 0.2f, 0.001f, 2.0f, "", {}, 0, "", VIVID_DISPLAY_ADSR},
        ParamInfo{"sustain", VIVID_PARAM_FLOAT, 0.5f, 0.0f, 1.0f, "", {}, 0, "", VIVID_DISPLAY_ADSR},
        ParamInfo{"release", VIVID_PARAM_FLOAT, 0.3f, 0.001f, 2.0f, "", {}, 0, "", VIVID_DISPLAY_ADSR},
        ParamInfo{"num_steps", VIVID_PARAM_INT, 4.0f, 1.0f, 4.0f, "", {}, 0, "", VIVID_DISPLAY_STEP_SEQ},
        ParamInfo{"step_0", VIVID_PARAM_FLOAT, 0.1f, 0.0f, 1.0f, "", {}, 0, "", VIVID_DISPLAY_STEP_SEQ},
        ParamInfo{"step_1", VIVID_PARAM_FLOAT, 0.2f, 0.0f, 1.0f, "", {}, 0, "", VIVID_DISPLAY_STEP_SEQ},
        ParamInfo{"step_2", VIVID_PARAM_FLOAT, 0.3f, 0.0f, 1.0f, "", {}, 0, "", VIVID_DISPLAY_STEP_SEQ},
        ParamInfo{"step_3", VIVID_PARAM_FLOAT, 0.4f, 0.0f, 1.0f, "", {}, 0, "", VIVID_DISPLAY_STEP_SEQ},
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
    node.param_values = {
        0.25f, 0.2f, 0.8f, 0.0f, 0.0f, 0.1f, 0.2f, 0.3f, 25.0f,
        0.1f, 0.2f, 0.5f, 0.3f,
        4.0f, 0.1f, 0.2f, 0.3f, 0.4f,
    };
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
        ui.inspector_.slider_rects.push_back({920.0f, 120.0f, 160.0f, 16.0f, "widget1", "gain"});
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

    // Slider drag on a wide-range param (like Clock bpm 1–300)
    {
        auto clock_op = std::make_shared<OperatorInfo>();
        clock_op->name = "ClockFr";
        clock_op->params = {
            ParamInfo{"bpm", VIVID_PARAM_FLOAT, 120.0f, 1.0f, 300.0f},
            ParamInfo{"beats_per_bar", VIVID_PARAM_INT, 4.0f, 1.0f, 16.0f},
        };
        GraphSnapshot clock_snap;
        NodeSnapshot cn;
        cn.node_id = "clock1";
        cn.type_name = "ClockFr";
        cn.active_cadence = vivid::Cadence::Audio;
        cn.has_layout = true;
        cn.layout_x = 120.0f;
        cn.layout_y = 120.0f;
        cn.op_info = clock_op;
        cn.param_values = {120.0f, 4.0f};
        cn.param_lock_flags = {0, 0};
        cn.param_indices["bpm"] = 0;
        cn.param_indices["beats_per_bar"] = 1;
        clock_snap.node_index["clock1"] = 0;
        clock_snap.nodes.push_back(std::move(cn));

        DummySink sink;
        NodeGraphUI ui(sink);
        ui.selected_node_ids_ = {"clock1"};
        // Slider for bpm: drag to 50% position → should emit ~150.5
        ui.inspector_.slider_rects.push_back({920.0f, 120.0f, 160.0f, 16.0f, "clock1", "bpm"});
        ui.on_mouse_move(1000.0f, 128.0f);  // 50% of slider (920+80)
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(clock_snap);
        ui.on_mouse_move(1000.0f, 128.0f);  // hold at 50%
        ui.update(clock_snap);
        check(!sink.set_param_calls.empty(), "Wide-range slider drag emits a param update");
        float expected_bpm = 1.0f + ((1000.0f - 920.0f) / 160.0f) * (300.0f - 1.0f);
        check(last_param(sink).param == "bpm" &&
                  std::fabs(last_param(sink).value - expected_bpm) < 1.0f,
              "Wide-range slider maps mouse position to correct parameter value");
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(clock_snap);
    }

    // Slider drag on an int param (like Clock beats_per_bar 1–16)
    {
        auto clock_op = std::make_shared<OperatorInfo>();
        clock_op->name = "ClockFr";
        clock_op->params = {
            ParamInfo{"bpm", VIVID_PARAM_FLOAT, 120.0f, 1.0f, 300.0f},
            ParamInfo{"beats_per_bar", VIVID_PARAM_INT, 4.0f, 1.0f, 16.0f},
        };
        GraphSnapshot clock_snap;
        NodeSnapshot cn;
        cn.node_id = "clock1";
        cn.type_name = "ClockFr";
        cn.active_cadence = vivid::Cadence::Audio;
        cn.has_layout = true;
        cn.layout_x = 120.0f;
        cn.layout_y = 120.0f;
        cn.op_info = clock_op;
        cn.param_values = {120.0f, 4.0f};
        cn.param_lock_flags = {0, 0};
        cn.param_indices["bpm"] = 0;
        cn.param_indices["beats_per_bar"] = 1;
        clock_snap.node_index["clock1"] = 0;
        clock_snap.nodes.push_back(std::move(cn));

        DummySink sink;
        NodeGraphUI ui(sink);
        ui.selected_node_ids_ = {"clock1"};
        // Slider for beats_per_bar: drag to 75% → should round to int
        ui.inspector_.slider_rects.push_back({920.0f, 140.0f, 160.0f, 16.0f, "clock1", "beats_per_bar"});
        ui.on_mouse_move(1040.0f, 148.0f);  // 75% of slider
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(clock_snap);
        ui.on_mouse_move(1040.0f, 148.0f);
        ui.update(clock_snap);
        check(!sink.set_param_calls.empty(), "Int slider drag emits a param update");
        float raw = 1.0f + 0.75f * 15.0f;  // = 12.25 → rounds to 12
        check(last_param(sink).param == "beats_per_bar" &&
                  std::fabs(last_param(sink).value - std::round(raw)) < 0.001f,
              "Int slider drag rounds to nearest integer");
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(clock_snap);
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.inspector_.xy_pad_rects.push_back({920.0f, 160.0f, 120.0f, 120.0f, "widget1", "pos_x", "pos_y"});
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
        auto& surface = ui.inspector_.surface;
        surface.begin_frame();
        surface.add_adsr(920.0f, 120.0f, 200.0f, 100.0f,
                         "widget1", "attack", "decay", "sustain", "release");
        ui.on_mouse_move(950.0f, 126.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(970.0f, 126.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
        check(!sink.set_param_calls.empty(), "ADSR rich widget drag emits a param update");
        check(last_param(sink).param == "attack" && last_param(sink).value > 0.1f,
              "ADSR attack handle drag updates the attack param through the generic rich surface");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        auto& surface = ui.inspector_.surface;
        surface.begin_frame();
        surface.add_step_seq(920.0f, 120.0f, 200.0f, 100.0f,
                             "widget1", 13, 14, 4, 0, 0);
        ui.on_mouse_move(995.0f, 170.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(995.0f, 130.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
        check(!sink.set_param_calls.empty(), "Step-seq rich widget drag emits a param update");
        check(last_param(sink).param == "step_1" && last_param(sink).value > 0.8f,
              "Step-seq bar drag updates the selected step through the generic rich surface");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.inspector_.color_popup_open = true;
        ui.inspector_.color_popup_node_id = "widget1";
        ui.inspector_.color_popup_param_r = "r";
        ui.inspector_.color_popup_param_g = "g";
        ui.inspector_.color_popup_param_b = "b";
        ui.inspector_.color_popup_x = 920.0f;
        ui.inspector_.color_popup_y = 140.0f;
        ui.inspector_.color_dragging_sv = true;
        ui.mouse_.left_down = true;
        ui.mouse_.x = ui.inspector_.color_popup_x + kColorPopupPad + kColorPopupSVSize;
        ui.mouse_.y = ui.inspector_.color_popup_y + kColorPopupPad;
        ui.update(snap);
        check(sink.set_param_calls.size() == 3, "Color SV drag updates all RGB channels");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.inspector_.color_popup_open = true;
        ui.inspector_.color_popup_node_id = "widget1";
        ui.inspector_.color_popup_param_r = "r";
        ui.inspector_.color_popup_param_g = "g";
        ui.inspector_.color_popup_param_b = "b";
        ui.inspector_.color_popup_x = 920.0f;
        ui.inspector_.color_popup_y = 140.0f;
        ui.inspector_.color_dragging_hue = true;
        ui.mouse_.left_down = true;
        ui.mouse_.x = ui.inspector_.color_popup_x + kColorPopupPad + kColorPopupSVSize + kColorPopupGap + kColorHueBarW * 0.5f;
        ui.mouse_.y = ui.inspector_.color_popup_y + kColorPopupPad + kColorPopupSVSize;
        ui.update(snap);
        check(sink.set_param_calls.size() == 3, "Color hue drag updates all RGB channels");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.inspector_.color_popup_open = true;
        ui.inspector_.color_popup_node_id = "widget1";
        ui.inspector_.color_popup_param_r = "r";
        ui.inspector_.color_popup_param_g = "g";
        ui.inspector_.color_popup_param_b = "b";
        ui.inspector_.color_editing_hex = true;
        ui.inspector_.color_hex_buffer = "#336699";
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
        ui.inspector_.color_popup_open = true;
        ui.inspector_.color_popup_node_id = "widget1";
        ui.inspector_.color_popup_param_r = "r";
        ui.inspector_.color_popup_param_g = "g";
        ui.inspector_.color_popup_param_b = "b";
        ui.inspector_.color_editing_rgb = 0;
        ui.inspector_.color_rgb_buffer = "255";
        ui.on_key(GLFW_KEY_TAB, GLFW_PRESS, 0);
        check(sink.set_param_calls.size() == 1 &&
                  sink.set_param_calls[0].param == "r" &&
                  std::fabs(sink.set_param_calls[0].value - 1.0f) < 0.001f,
              "RGB text edit commits the active channel on Tab");
        check(ui.inspector_.color_editing_rgb == 1, "RGB text edit advances focus to the next channel");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_widget_snapshot();
        ui.selected_node_ids_ = {"widget1"};
        ui.inspector_.value_text_rects.push_back({920.0f, 120.0f, 80.0f, 18.0f, "widget1", "percent"});
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
        ui.inspector_.value_text_rects.push_back({920.0f, 120.0f, 80.0f, 18.0f, "widget1", "percent"});
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
        ui.inspector_.dropdown_rects.push_back({920.0f, 220.0f, 120.0f, 18.0f, "widget1", "mode"});
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
        ui.inspector_.bool_rects.push_back({920.0f, 260.0f, 14.0f, 14.0f, "widget1", "enabled"});
        ui.on_mouse_move(926.0f, 266.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(sink.set_param_calls.size() == 1 &&
                  sink.set_param_calls[0].param == "enabled" &&
                  std::fabs(sink.set_param_calls[0].value - 1.0f) < 0.001f,
              "Toggle click flips the bound boolean param");
    }

    // Knob vertical drag on a float param
    {
        auto op = std::make_shared<OperatorInfo>();
        op->name = "Chorus";
        op->params = {
            ParamInfo{"rate", VIVID_PARAM_FLOAT, 0.5f, 0.05f, 5.0f, "", {}, 0, "", VIVID_DISPLAY_KNOB},
        };
        GraphSnapshot snap;
        NodeSnapshot n;
        n.node_id = "chorus1";
        n.type_name = "Chorus";
        n.active_cadence = vivid::Cadence::Audio;
        n.has_layout = true;
        n.layout_x = 120.0f;
        n.layout_y = 120.0f;
        n.op_info = op;
        n.param_values = {0.5f};
        n.param_lock_flags = {0};
        n.param_indices["rate"] = 0;
        snap.node_index["chorus1"] = 0;
        snap.nodes.push_back(std::move(n));

        DummySink sink;
        NodeGraphUI ui(sink);
        ui.selected_node_ids_ = {"chorus1"};
        // Knob hit rect (kKnobDiameter + 4 = 44)
        ui.inspector_.slider_rects.push_back({920.0f, 120.0f, 44.0f, 44.0f, "chorus1", "rate"});
        ui.on_mouse_move(942.0f, 142.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        // Drag upward by 40px → should increase value
        ui.on_mouse_move(942.0f, 102.0f);
        ui.update(snap);
        check(!sink.set_param_calls.empty(), "Float knob vertical drag emits a param update");
        check(last_param(sink).param == "rate" && last_param(sink).value > 0.5f,
              "Float knob vertical drag upward increases the value");
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
    }

    // Knob vertical drag on an int param (Chorus voices)
    {
        auto op = std::make_shared<OperatorInfo>();
        op->name = "Chorus";
        op->params = {
            ParamInfo{"voices", VIVID_PARAM_INT, 3.0f, 1.0f, 6.0f, "", {}, 0, "", VIVID_DISPLAY_KNOB},
        };
        GraphSnapshot snap;
        NodeSnapshot n;
        n.node_id = "chorus1";
        n.type_name = "Chorus";
        n.active_cadence = vivid::Cadence::Audio;
        n.has_layout = true;
        n.layout_x = 120.0f;
        n.layout_y = 120.0f;
        n.op_info = op;
        n.param_values = {3.0f};
        n.param_lock_flags = {0};
        n.param_indices["voices"] = 0;
        snap.node_index["chorus1"] = 0;
        snap.nodes.push_back(std::move(n));

        DummySink sink;
        NodeGraphUI ui(sink);
        ui.selected_node_ids_ = {"chorus1"};
        // Knob hit rect (same dimensions as float knob)
        ui.inspector_.slider_rects.push_back({920.0f, 120.0f, 44.0f, 44.0f, "chorus1", "voices"});
        ui.on_mouse_move(942.0f, 142.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        // Drag upward by 80px → range=5, sensitivity=5/200=0.025, dy=80 → delta=2.0
        ui.on_mouse_move(942.0f, 62.0f);
        ui.update(snap);
        check(!sink.set_param_calls.empty(), "Int knob vertical drag emits a param update");
        check(last_param(sink).param == "voices" &&
                  std::fabs(last_param(sink).value - 5.0f) < 0.001f,
              "Int knob vertical drag rounds to nearest integer (3 + 2 = 5)");
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
    }

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
