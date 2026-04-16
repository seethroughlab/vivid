#define private public
#include "ui/graph/node_graph.h"
#undef private
#include "ui/graph/graph_snapshot.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include "test_helpers.h"

using namespace vivid::ui;

struct DummySink : UICommandSink {
    struct ParamCall { std::string node_id, param; float value; };
    struct StringParamCall { std::string node_id, param, value; };
    struct LayoutCall { std::string node_id; float x, y; };
    struct MetronomeCall { float bpm; int beats_per_bar; };

    std::vector<std::string> remove_calls;
    std::vector<std::pair<std::string, std::string>> connect_calls;
    std::vector<std::pair<std::string, std::string>> disconnect_calls;
    std::vector<std::pair<std::string, std::string>> add_calls;
    std::vector<ParamCall> set_param_calls;
    std::vector<StringParamCall> set_string_param_calls;
    std::vector<LayoutCall> layout_calls;
    std::vector<std::pair<std::string, int>> move_variation_calls;
    std::vector<std::string> save_variation_calls;
    std::vector<MetronomeCall> metronome_calls;
    std::vector<int> recall_variation_idx_calls;
    int undo_calls = 0;
    int redo_calls = 0;

    void set_param(const std::string& node_id, const std::string& param, float value) override {
        set_param_calls.push_back({node_id, param, value});
    }
    void add_node(const std::string& type, const std::string& id) override {
        add_calls.push_back({type, id});
    }
    bool try_add_node(const std::string& type, const std::string& id,
                      std::string* error = nullptr) override {
        add_calls.push_back({type, id});
        if (error) error->clear();
        return true;
    }
    void remove_node(const std::string& id) override {
        remove_calls.push_back(id);
    }
    void connect(const std::string& from, const std::string& to) override {
        connect_calls.push_back({from, to});
    }
    void disconnect(const std::string& from, const std::string& to) override {
        disconnect_calls.push_back({from, to});
    }
    void set_connection_remap(const std::string&, const std::string&, float, float, float, float, bool, uint8_t) override {}
    void set_node_layout(const std::string& node_id, float x, float y) override {
        layout_calls.push_back({node_id, x, y});
    }
    void set_resolution(const std::string&, uint32_t, uint32_t) override {}
    void add_midi_mapping(const std::string&, const std::string&, int, int, float, float) override {}
    void remove_midi_mapping(const std::string&, const std::string&) override {}
    void update_midi_mapping(const std::string&, const std::string&, float, float) override {}
    void set_string_param(const std::string& node_id, const std::string& param,
                          const std::string& value) override {
        set_string_param_calls.push_back({node_id, param, value});
    }
    void set_graph_metronome(float bpm, int beats_per_bar) override {
        metronome_calls.push_back({bpm, beats_per_bar});
    }
    void save_variation(const std::string& name) override {
        save_variation_calls.push_back(name);
    }
    void recall_variation_idx(int idx) override {
        recall_variation_idx_calls.push_back(idx);
    }
    void move_variation(const std::string& name, int to_index) override {
        move_variation_calls.push_back({name, to_index});
    }
    bool undo() override {
        ++undo_calls;
        return true;
    }
    bool redo() override {
        ++redo_calls;
        return true;
    }
};

static std::shared_ptr<OperatorInfo> make_op(
    const std::string& name,
    const std::vector<ParamInfo>& params,
    const std::vector<PortInfo>& ports) {
    auto op = std::make_shared<OperatorInfo>();
    op->name = name;
    op->is_gpu = false;
    op->params = params;
    op->ports = ports;
    return op;
}

static void add_node(GraphSnapshot& snap, const std::shared_ptr<OperatorInfo>& op,
                     const std::string& node_id, float x, float y,
                     const std::vector<float>& values,
                     const std::unordered_map<std::string, std::string>& string_values = {}) {
    NodeSnapshot node;
    node.node_id = node_id;
    node.type_name = op->name;
    node.active_cadence = vivid::Cadence::Frame;
    node.has_layout = true;
    node.layout_x = x;
    node.layout_y = y;
    node.op_info = op;
    node.param_values = values;
    node.param_lock_flags.resize(values.size(), 0);
    for (uint32_t i = 0; i < op->params.size(); ++i) {
        node.param_indices[op->params[i].name] = i;
    }
    uint32_t input_idx = 0;
    uint32_t output_idx = 0;
    for (const auto& port : op->ports) {
        if (port.direction == VIVID_PORT_INPUT) {
            node.input_port_indices[port.name] = input_idx++;
        } else {
            node.output_port_indices[port.name] = output_idx++;
        }
    }
    node.output_values.resize(output_idx, 0.0f);
    node.file_param_values = string_values;
    snap.node_index[node_id] = snap.nodes.size();
    snap.nodes.push_back(std::move(node));
}

static GraphSnapshot make_editor_snapshot() {
    GraphSnapshot snap;
    auto lfo = make_op(
        "LfoFr",
        {
            ParamInfo{"frequency", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 10.0f},
            ParamInfo{"amplitude", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f},
        },
        {
            PortInfo{"value", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT},
        });
    auto math = make_op(
        "Math",
        {
            ParamInfo{"mode", VIVID_PARAM_INT, 0.0f, 0.0f, 2.0f},
            ParamInfo{"label", VIVID_PARAM_TEXT, 0.0f, 0.0f, 0.0f},
        },
        {
            PortInfo{"a", VIVID_PORT_SCALAR, VIVID_PORT_INPUT},
            PortInfo{"b", VIVID_PORT_SCALAR, VIVID_PORT_INPUT},
            PortInfo{"value", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT},
        });

    add_node(snap, lfo, "lfo1", 100.0f, 120.0f, {0.5f, 0.25f});
    add_node(snap, lfo, "lfo2", 100.0f, 280.0f, {0.8f, 0.5f});
    add_node(snap, math, "math1", 360.0f, 200.0f, {1.0f, 0.0f}, {{"label", "sum"}});

    snap.connections.push_back({"lfo1", "value", "math1", "a"});
    snap.connections.push_back({"lfo2", "value", "math1", "b"});
    return snap;
}

static GraphSnapshot make_session_snapshot() {
    GraphSnapshot snap = make_editor_snapshot();
    snap.variations = {{"A"}, {"B"}, {"C"}};
    snap.active_variation = 0;
    snap.metronome_bpm = 120.0f;
    snap.metronome_beats_per_bar = 4;
    return snap;
}

static const NodeRect& find_rect(const NodeGraphUI& ui, const std::string& node_id) {
    for (const auto& rect : ui.node_rects_) {
        if (rect.node_id == node_id)
            return rect;
    }
    std::fprintf(stderr, "missing node rect for %s\n", node_id.c_str());
    std::abort();
}

static bool layout_call_for(const DummySink& sink, const std::string& node_id, DummySink::LayoutCall& out) {
    for (const auto& call : sink.layout_calls) {
        if (call.node_id == node_id) {
            out = call;
            return true;
        }
    }
    return false;
}

int main() {
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        ui.update(snap);
        ui.selected_node_ids_ = {"lfo1", "math1"};
        ui.on_key(GLFW_KEY_DELETE, GLFW_PRESS, 0);
        check(sink.remove_calls.size() == 2, "Delete dispatches one remove per selected node");
        check(std::find(sink.remove_calls.begin(), sink.remove_calls.end(), "lfo1") != sink.remove_calls.end(),
              "Delete includes first selected node");
        check(std::find(sink.remove_calls.begin(), sink.remove_calls.end(), "math1") != sink.remove_calls.end(),
              "Delete includes second selected node");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        ui.update(snap);
        const auto& lfo1 = find_rect(ui, "lfo1");
        const auto& lfo2 = find_rect(ui, "lfo2");
        const float old_dx = lfo2.x - lfo1.x;
        const float old_dy = lfo2.y - lfo1.y;
        ui.selected_node_ids_ = {"lfo1", "lfo2"};
        ui.on_mouse_move(lfo1.x + 20.0f, lfo1.y + 20.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(lfo1.x + 90.0f, lfo1.y + 65.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);

        DummySink::LayoutCall dragged1{}, dragged2{};
        check(layout_call_for(sink, "lfo1", dragged1), "Group drag emits layout for first node");
        check(layout_call_for(sink, "lfo2", dragged2), "Group drag emits layout for second node");
        check(std::fabs((dragged2.x - dragged1.x) - old_dx) < 0.01f &&
                  std::fabs((dragged2.y - dragged1.y) - old_dy) < 0.01f,
              "Group drag preserves relative node spacing");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        ui.update(snap);
        ui.on_mouse_move(80.0f, 90.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, GLFW_MOD_SHIFT);
        ui.update(snap);
        ui.on_mouse_move(260.0f, 420.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, GLFW_MOD_SHIFT);
        ui.update(snap);
        check(ui.selected_node_ids_.count("lfo1") == 1, "Box select includes first enclosed node");
        check(ui.selected_node_ids_.count("lfo2") == 1, "Box select includes second enclosed node");
        check(ui.selected_node_ids_.count("math1") == 0, "Box select excludes nodes outside marquee");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        snap.connections.clear();
        ui.update(snap);
        const auto& lfo1 = find_rect(ui, "lfo1");
        const auto& math1 = find_rect(ui, "math1");
        ui.on_mouse_move(port_gx(lfo1, true), port_gy(lfo1, lfo1.outputs[0]));
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(port_gx(math1, false), port_gy(math1, math1.inputs[0]));
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
        check(sink.connect_calls.size() == 1, "Wire drag connect dispatches one connection");
        check(sink.connect_calls[0].first == "lfo1/value" && sink.connect_calls[0].second == "math1/a",
              "Wire drag connects the dragged output to the hovered input");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        ui.update(snap);
        const auto& math1 = find_rect(ui, "math1");
        ui.on_mouse_move(port_gx(math1, false), port_gy(math1, math1.inputs[0]));
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(sink.disconnect_calls.size() == 1, "Input-port click disconnects the existing wire");
        check(sink.disconnect_calls[0].first == "lfo1/value" && sink.disconnect_calls[0].second == "math1/a",
              "Disconnect targets the matching incoming connection");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        snap.connections.clear();
        ui.update(snap);
        const auto& lfo1 = find_rect(ui, "lfo1");
        ui.on_mouse_move(port_gx(lfo1, true), port_gy(lfo1, lfo1.outputs[0]));
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(720.0f, 520.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
        check(sink.connect_calls.empty(), "Invalid wire drop does not dispatch a connection");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        ui.update(snap);
        ui.on_key(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_SUPER);
        ui.on_key(GLFW_KEY_Z, GLFW_PRESS, GLFW_MOD_SUPER | GLFW_MOD_SHIFT);
        check(sink.undo_calls == 1, "Cmd+Z dispatches undo");
        check(sink.redo_calls == 1, "Cmd+Shift+Z dispatches redo");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_editor_snapshot();
        ui.update(snap);
        ui.selected_node_ids_ = {"lfo1", "math1"};
        ui.on_key(GLFW_KEY_C, GLFW_PRESS, GLFW_MOD_SUPER);
        ui.on_mouse_move(620.0f, 180.0f);
        ui.on_key(GLFW_KEY_V, GLFW_PRESS, GLFW_MOD_SUPER);

        check(sink.add_calls.size() == 2, "Paste adds one node per copied selection");
        bool saw_lfo = false;
        bool saw_math = false;
        for (const auto& call : sink.add_calls) {
            saw_lfo = saw_lfo || call.first == "LfoFr";
            saw_math = saw_math || call.first == "Math";
        }
        check(saw_lfo && saw_math, "Paste preserves copied operator types");
        check(sink.layout_calls.size() == 2, "Paste restores layout for copied nodes");
        check(sink.connect_calls.size() == 1, "Paste recreates internal copied connections");
        check(sink.connect_calls[0].first == "lfo1_copy/value" && sink.connect_calls[0].second == "math1_copy/a",
              "Paste reconnects copied nodes using remapped ids");
        check(sink.set_string_param_calls.size() == 1 &&
                  sink.set_string_param_calls[0].node_id == "math1_copy" &&
                  sink.set_string_param_calls[0].value == "sum",
              "Paste restores copied string params");
        check(ui.selected_node_ids_.count("lfo1_copy") == 1 && ui.selected_node_ids_.count("math1_copy") == 1,
              "Paste selects the newly created nodes");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.session_grid_open_ = true;
        ui.variation_cell_rects_ = {
            {20.0f, 620.0f, 120.0f, 48.0f, 0},
            {160.0f, 620.0f, 120.0f, 48.0f, 1},
            {300.0f, 620.0f, 120.0f, 48.0f, 2},
        };
        ui.on_mouse_move(80.0f, 640.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        ui.on_mouse_move(410.0f, 640.0f);
        ui.update(snap);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        ui.update(snap);
        check(sink.recall_variation_idx_calls.size() == 1 && sink.recall_variation_idx_calls[0] == 0,
              "Session card click recalls the selected variation");
        check(sink.move_variation_calls.size() == 1 &&
                  sink.move_variation_calls[0].first == "A" &&
                  sink.move_variation_calls[0].second == 2,
              "Session drag reorder dispatches the moved card target index");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        snap.metronome_bpm = 111.0f;
        snap.metronome_beats_per_bar = 5;
        ui.update(snap);
        ui.perf_button_rects_ = {
            {0.0f, 0.0f, 20.0f, 20.0f, 2, true},
            {24.0f, 0.0f, 20.0f, 20.0f, 6, true},
            {48.0f, 0.0f, 20.0f, 20.0f, 7, true},
        };
        ui.diagnostics_button_rect_ = {0.0f, 0.0f, 20.0f, 20.0f, true};

        ui.mouse_ = {};
        ui.mouse_.x = 10.0f;
        ui.mouse_.y = 10.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(ui.diagnostics_panel_open_, "Diagnostics button opens the diagnostics panel");

        ui.mouse_ = {};
        ui.mouse_.x = 34.0f;
        ui.mouse_.y = 10.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();

        ui.mouse_ = {};
        ui.mouse_.x = 58.0f;
        ui.mouse_.y = 10.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();

        check(sink.metronome_calls.size() == 2, "Transport controls dispatch metronome mutations");
        if (sink.metronome_calls.size() == 2) {
            check(std::fabs(sink.metronome_calls[0].bpm - 111.0f) < 0.01f &&
                      sink.metronome_calls[0].beats_per_bar == 4,
                  "Meter- decrements beats per bar");
            check(sink.metronome_calls[1].beats_per_bar == 6,
                  "Meter+ increments beats per bar");
        }
        check(!ui.session_grid_open_, "Workspace header no longer toggles the session grid");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.diagnostics_panel_open_ = true;
        ui.diagnostics_panel_rect_ = {20.0f, 34.0f, 200.0f, 80.0f, true};
        ui.diagnostics_mcp_rects_ = {{24.0f, 40.0f, 180.0f, 24.0f, 0}};
        ui.mouse_ = {};
        ui.mouse_.x = 60.0f;
        ui.mouse_.y = 52.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(ui.dialogs_.mcp_setup.open, "Diagnostics MCP row opens MCP setup");
        check(ui.diagnostics_panel_open_, "Diagnostics panel stays open on panel interaction");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.diagnostics_panel_open_ = true;
        ui.diagnostics_panel_rect_ = {20.0f, 34.0f, 200.0f, 80.0f, true};
        ui.diagnostics_button_rect_ = {20.0f, 0.0f, 50.0f, 20.0f, true};
        ui.mouse_ = {};
        ui.mouse_.x = 260.0f;
        ui.mouse_.y = 160.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(!ui.diagnostics_panel_open_, "Clicking outside the diagnostics panel closes it");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        snap.metronome_bpm = 111.0f;
        snap.metronome_beats_per_bar = 5;
        ui.update(snap);
        ui.transport_bpm_rect_ = {10.0f, 10.0f, 80.0f, 20.0f, true};

        ui.mouse_ = {};
        ui.mouse_.x = 20.0f;
        ui.mouse_.y = 20.0f;
        ui.mouse_.left_clicked = true;
        ui.mouse_.left_down = true;
        ui.handle_left_click();

        ui.mouse_.prev_y = 20.0f;
        ui.mouse_.y = 12.0f;
        ui.update_transport_bpm_drag();
        check(!sink.metronome_calls.empty() &&
                  std::fabs(sink.metronome_calls.back().bpm - 112.0f) < 0.05f &&
                  sink.metronome_calls.back().beats_per_bar == 5,
              "BPM drag updates tempo live");

        const size_t coarse_calls = sink.metronome_calls.size();
        ui.mouse_.shift_down = true;
        ui.mouse_.y = 4.0f;
        ui.update_transport_bpm_drag();
        check(sink.metronome_calls.size() == coarse_calls + 1 &&
                  std::fabs(sink.metronome_calls.back().bpm - 112.1f) < 0.05f,
              "Shift-drag uses finer BPM increments");

        ui.mouse_.left_down = false;
        ui.mouse_.left_released = true;
        ui.update_transport_bpm_drag();
        check(!ui.transport_bpm_dragging_, "BPM drag stops on mouse release");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        snap.metronome_bpm = 111.0f;
        snap.metronome_beats_per_bar = 5;
        ui.update(snap);
        ui.transport_bpm_rect_ = {10.0f, 10.0f, 80.0f, 20.0f, true};

        ui.transport_bpm_last_click_time_ = glfwGetTime();
        ui.mouse_ = {};
        ui.mouse_.x = 20.0f;
        ui.mouse_.y = 20.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(ui.transport_bpm_editing_, "Double-click enters BPM inline edit mode");

        ui.transport_bpm_edit_buffer_ = "128.5";
        ui.on_key(GLFW_KEY_ENTER, GLFW_PRESS, 0);
        check(!sink.metronome_calls.empty() &&
                  std::fabs(sink.metronome_calls.back().bpm - 128.5f) < 0.01f &&
                  sink.metronome_calls.back().beats_per_bar == 5,
              "Enter commits typed BPM");
        check(!ui.transport_bpm_editing_, "BPM edit closes after Enter commit");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        snap.metronome_bpm = 111.0f;
        snap.metronome_beats_per_bar = 5;
        ui.update(snap);
        ui.transport_bpm_rect_ = {10.0f, 10.0f, 80.0f, 20.0f, true};
        ui.transport_bpm_editing_ = true;
        ui.transport_bpm_edit_buffer_ = "140";

        ui.mouse_ = {};
        ui.mouse_.x = 140.0f;
        ui.mouse_.y = 40.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(!sink.metronome_calls.empty() &&
                  std::fabs(sink.metronome_calls.back().bpm - 140.0f) < 0.01f,
              "Clicking away commits the BPM edit");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.transport_bpm_editing_ = true;
        ui.transport_bpm_edit_buffer_ = "145";
        ui.on_key(GLFW_KEY_ESCAPE, GLFW_PRESS, 0);
        check(!ui.transport_bpm_editing_, "Escape cancels BPM edit mode");
        check(sink.metronome_calls.empty(), "Escape does not dispatch a metronome change");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.transport_bpm_editing_ = true;
        ui.transport_bpm_edit_buffer_ = "abc";
        ui.confirm_transport_bpm_edit();
        check(sink.metronome_calls.empty(), "Invalid BPM input is discarded");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.session_grid_open_ = false;
        ui.session_collapsed_rect_ = {100.0f, 680.0f, 160.0f, 24.0f, true};
        ui.mouse_ = {};
        ui.mouse_.x = 120.0f;
        ui.mouse_.y = 690.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(ui.session_grid_open_, "Collapsed session affordance reopens the session strip");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.session_grid_open_ = true;
        ui.session_button_rects_ = {{220.0f, 650.0f, 18.0f, 18.0f, 7, true}};
        ui.mouse_ = {};
        ui.mouse_.x = 226.0f;
        ui.mouse_.y = 658.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(!ui.session_grid_open_, "Session strip close button collapses the strip");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.session_grid_open_ = true;
        ui.session_button_rects_ = {{20.0f, 650.0f, 40.0f, 20.0f, 3, false}};
        ui.mouse_ = {};
        ui.mouse_.x = 30.0f;
        ui.mouse_.y = 660.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(ui.session_quantize_mode_ == 0, "Disabled quantize button does not change session quantize mode");
        check(!ui.status_banner_error_.empty(), "Disabled quantize button explains missing metronome");
    }

    {
        DummySink sink;
        NodeGraphUI ui(sink);
        auto snap = make_session_snapshot();
        ui.update(snap);
        ui.session_grid_open_ = true;
        ui.session_button_rects_ = {{20.0f, 650.0f, 96.0f, 44.0f, 0, true}};
        ui.mouse_ = {};
        ui.mouse_.x = 96.0f;
        ui.mouse_.y = 672.0f;
        ui.mouse_.left_clicked = true;
        ui.handle_left_click();
        check(sink.save_variation_calls.size() == 1, "Save New button dispatches variation save through its resized hit rect");
        check(sink.save_variation_calls.front() == "Var 4", "Save New names the new variation from the next index");
    }

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
