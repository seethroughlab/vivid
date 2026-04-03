#define private public
#include "ui/graph/node_graph.h"
#undef private
#include "ui/graph/graph_snapshot.h"
#include "ui/rendering/overlay_layouts.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <tuple>
#include "test_helpers.h"

using namespace vivid::ui;

struct DummySink : UICommandSink {
    std::vector<std::pair<std::string, std::string>> connect_calls;
    std::vector<std::pair<std::string, std::string>> disconnect_calls;
    std::vector<std::pair<std::string, std::string>> rollback_disconnect_calls;
    std::vector<std::pair<std::string, std::string>> add_calls;
    std::vector<std::tuple<std::string, float, float>> layout_calls;
    std::vector<std::tuple<std::string, std::string, std::string>> string_param_calls;
    std::string fail_connect_from;
    std::string fail_connect_to;

    void set_param(const std::string&, const std::string&, float) override {}
    void add_node(const std::string& type, const std::string& id) override {
        add_calls.push_back({type, id});
    }
    bool try_add_node(const std::string& type, const std::string& id,
                      std::string* error = nullptr) override {
        add_calls.push_back({type, id});
        if (error) error->clear();
        return true;
    }
    void remove_node(const std::string&) override {}
    void connect(const std::string& from, const std::string& to) override {
        connect_calls.push_back({from, to});
    }
    bool try_connect(const std::string& from, const std::string& to,
                     std::string* error = nullptr) override {
        connect_calls.push_back({from, to});
        if (from == fail_connect_from && to == fail_connect_to) {
            if (error) *error = "simulated connect failure";
            return false;
        }
        if (error) error->clear();
        return true;
    }
    void disconnect(const std::string& from, const std::string& to) override {
        disconnect_calls.push_back({from, to});
    }
    bool try_disconnect(const std::string& from, const std::string& to,
                        std::string* error = nullptr) override {
        disconnect_calls.push_back({from, to});
        if (error) error->clear();
        return true;
    }
    void set_connection_remap(const std::string&, const std::string&, float, float, float, float, bool) override {}
    void set_node_layout(const std::string& node_id, float x, float y) override {
        layout_calls.emplace_back(node_id, x, y);
    }
    void set_resolution(const std::string&, uint32_t, uint32_t) override {}
    void add_midi_mapping(const std::string&, const std::string&, int, int, float, float) override {}
    void remove_midi_mapping(const std::string&, const std::string&) override {}
    void update_midi_mapping(const std::string&, const std::string&, float, float) override {}
    void set_string_param(const std::string& node_id, const std::string& param,
                          const std::string& value) override {
        string_param_calls.emplace_back(node_id, param, value);
    }
};

int main() {
    GraphSnapshot snap;
    ExampleEntry ex;
    ex.id = "demo";
    ex.title = "Demo";
    ex.path = "intro/demo.json";
    ex.summary = "demo graph";
    ex.featured_rank = 1;

    // Example browser: Open click dispatches callback
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        ui.set_examples({ex});
        bool opened = false;
        std::string opened_path;
        ui.set_example_open_callback([&](const std::string& p) {
            opened = true;
            opened_path = p;
        });
        ui.toggle_example_browser();
        OverlayPanelLayout example_layout = compute_example_browser_layout(1280, 720, 1);
        OverlayRect open_btn = compute_example_open_button_rect(example_layout, example_layout.list_top);
        ui.on_mouse_move(open_btn.x + open_btn.w * 0.5f, open_btn.y + open_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(opened, "Open Example click invokes callback");
        check(opened_path == "intro/demo.json", "Open Example callback path matches selected graph");
    }

    // Missing package warning then second confirm
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        ui.set_examples({ex});
        bool opened = false;
        ui.set_example_open_callback([&](const std::string&) { opened = true; });
        ui.set_example_package_checker([](const std::vector<std::string>&, std::string& missing) {
            missing = "vivid-drums";
            return false;
        });
        ui.toggle_example_browser();
        OverlayPanelLayout example_layout = compute_example_browser_layout(1280, 720, 1);
        OverlayRect open_btn = compute_example_open_button_rect(example_layout, example_layout.list_top);
        ui.on_mouse_move(open_btn.x + open_btn.w * 0.5f, open_btn.y + open_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(!opened, "first missing-package Open click warns without opening");
        ui.on_mouse_move(open_btn.x + open_btn.w * 0.5f, open_btn.y + open_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(opened, "second missing-package Open click proceeds");
    }

    // Async operator add: chooser submits a prepared-build request and restores on failure
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        NodeGraphUI::AsyncAddOperatorRequest submitted;
        int submit_calls = 0;
        ui.set_async_add_callback(
            [&](const NodeGraphUI::AsyncAddOperatorRequest& request, std::string& error) {
                ++submit_calls;
                submitted = request;
                error.clear();
                return true;
            });

        ui.chooser_open_ = true;
        ui.chooser_mode_ = NodeGraphUI::ChooserMode::Operators;
        ui.chooser_items_ = {"Math"};
        ui.chooser_filter_ = "ma";
        ui.chooser_sel_ = 0;
        ui.chooser_scroll_ = 12.0f;
        ui.chooser_cursor_gx_ = 42.0f;
        ui.chooser_cursor_gy_ = 84.0f;

        ui.confirm_chooser_selection_idx(0);
        check(submit_calls == 1, "Chooser confirm submits async add request");
        check(ui.async_add_active_, "Async add becomes active after submission");
        check(!ui.chooser_open_, "Chooser closes while async add is running");
        check(submitted.type_name == "Math", "Async add request keeps selected type");
        check(submitted.node_id == "Math1", "Async add request generates unique node id");
        check_float(submitted.graph_x, 42.0f, "Async add request keeps graph x");
        check_float(submitted.graph_y, 84.0f, "Async add request keeps graph y");

        ui.notify_async_add_failure("failed to compile graph after adding Math");
        check(!ui.async_add_active_, "Async add clears active state on failure");
        check(ui.chooser_open_, "Chooser reopens after async add failure");
        check(ui.chooser_filter_ == "ma", "Chooser filter is restored after async add failure");
        check(ui.chooser_error_ == "failed to compile graph after adding Math",
              "Chooser shows compact async add failure summary");
    }

    // Package browser: install action dispatch
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        int install_calls = 0;
        ui.set_package_browser_callbacks(PackageBrowserCallbacks{
            []() {},
            []() {
                return std::vector<PackageBrowserEntry>{
                    PackageBrowserEntry{"vivid-demo", "demo", "0.1.0", "dev", "", {}, false}
                };
            },
            []() { return PackageBrowserFetchState::Ready; },
            []() { return std::string(); },
            []() { return PackageBrowserUpdateSummary{}; },
            [&](const std::string& name, std::string&) {
                if (name == "vivid-demo") ++install_calls;
                return true;
            },
            [](const std::string&, std::string&) { return true; },
        });
        ui.toggle_package_browser();
        OverlayPanelLayout pkg_layout = compute_package_browser_layout(1280, 720, 1);
        OverlayRect pkg_btn = compute_package_action_button_rect(pkg_layout, pkg_layout.list_top);
        ui.on_mouse_move(pkg_btn.x + pkg_btn.w * 0.5f, pkg_btn.y + pkg_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(install_calls == 1, "Package install click invokes install callback");
    }

    // Package browser: ready-state refresh updates cached entries even when count stays the same
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        int refresh_calls = 0;
        int list_calls = 0;
        PackageBrowserFetchState state = PackageBrowserFetchState::Idle;
        std::vector<PackageBrowserEntry> entries{
            PackageBrowserEntry{"vivid-demo", "demo v1", "0.1.0", "dev", "", {}, false}
        };
        ui.set_package_browser_callbacks(PackageBrowserCallbacks{
            [&]() { ++refresh_calls; },
            [&]() {
                ++list_calls;
                return entries;
            },
            [&]() { return state; },
            []() { return std::string(); },
            []() { return PackageBrowserUpdateSummary{}; },
            [](const std::string&, std::string&) { return true; },
            [](const std::string&, std::string&) { return true; },
        });
        ui.toggle_package_browser();
        check(refresh_calls == 1, "Package browser requests refresh when opened from idle");
        check(ui.package_browser_entries().size() == 1, "Package browser caches initial entry snapshot");
        check(ui.package_browser_entries()[0].description == "demo v1", "initial package browser description cached");

        entries[0].description = "demo v2";
        state = PackageBrowserFetchState::Ready;
        ui.update(snap);
        check(list_calls >= 2, "Package browser re-polls list when ready");
        check(ui.package_browser_entries()[0].description == "demo v2",
              "Package browser refreshes cached metadata even when entry count is unchanged");
    }

    // Package browser: compile-related failures collapse to a single-line console-first footer
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        int install_calls = 0;
        int open_console_calls = 0;
        ui.set_package_browser_callbacks(PackageBrowserCallbacks{
            []() {},
            []() {
                return std::vector<PackageBrowserEntry>{
                    PackageBrowserEntry{"vivid-demo", "demo", "0.1.0", "dev", "", {}, false}
                };
            },
            []() { return PackageBrowserFetchState::Ready; },
            []() { return std::string(); },
            []() { return PackageBrowserUpdateSummary{}; },
            [&](const std::string& name, std::string& err) {
                if (name == "vivid-demo") ++install_calls;
                err = "cmake build failed:\nvery long compiler output that should not be rendered inline";
                return false;
            },
            [](const std::string&, std::string&) { return true; },
            [](const std::string&, std::string&) { return true; },
            [](const std::string&, std::string&) { return true; },
            [](const std::string&, std::string&) { return true; },
            [&]() { ++open_console_calls; },
        });
        ui.toggle_package_browser();
        OverlayPanelLayout pkg_layout = compute_package_browser_layout(1280, 720, 1);
        OverlayRect pkg_btn = compute_package_action_button_rect(pkg_layout, pkg_layout.list_top);
        ui.on_mouse_move(pkg_btn.x + pkg_btn.w * 0.5f, pkg_btn.y + pkg_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(install_calls == 1, "Package install failure still invokes install callback");
        check(ui.dialogs_.pkg_browser.action_error_display == "Build failed — see Build Console",
              "Compile-related package failure collapses to compact console-first summary");
        check(ui.dialogs_.pkg_browser.action_error_console_backed,
              "Compile-related package failure is marked console-backed");

        ui.dialogs_.pkg_browser.footer_action_btn = {pkg_layout.cx + pkg_layout.inner_w - 96.0f,
                                                     pkg_layout.status_y - 1.0f, 96.0f, 20.0f};
        const auto& footer_btn = ui.dialogs_.pkg_browser.footer_action_btn;
        ui.on_mouse_move(footer_btn.x + footer_btn.w * 0.5f, footer_btn.y + footer_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(open_console_calls == 1, "Package failure footer opens the Build Console");
    }

    // Package browser: non-build failures stay inline and do not expose the console footer action
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        int uninstall_calls = 0;
        int open_console_calls = 0;
        ui.set_package_browser_callbacks(PackageBrowserCallbacks{
            []() {},
            []() {
                return std::vector<PackageBrowserEntry>{
                    PackageBrowserEntry{"vivid-demo", "demo", "0.1.0", "dev", "", {}, true}
                };
            },
            []() { return PackageBrowserFetchState::Ready; },
            []() { return std::string(); },
            []() { return PackageBrowserUpdateSummary{}; },
            [](const std::string&, std::string&) { return true; },
            [&](const std::string& name, std::string& err) {
                if (name == "vivid-demo") ++uninstall_calls;
                err = "Failed to uninstall vivid-demo because the package directory is locked";
                return false;
            },
            [](const std::string&, std::string&) { return true; },
            [](const std::string&, std::string&) { return true; },
            [](const std::string&, std::string&) { return true; },
            [&]() { ++open_console_calls; },
        });
        ui.toggle_package_browser();
        OverlayPanelLayout pkg_layout = compute_package_browser_layout(1280, 720, 1);
        OverlayRect pkg_btn = compute_package_action_button_rect(pkg_layout, pkg_layout.list_top);
        ui.on_mouse_move(pkg_btn.x + pkg_btn.w * 0.5f, pkg_btn.y + pkg_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(uninstall_calls == 1, "Package uninstall failure still invokes uninstall callback");
        check(ui.dialogs_.pkg_browser.action_error_display ==
                  "Failed to uninstall vivid-demo because the package directory is locked",
              "Non-build package failures keep their inline summary");
        check(!ui.dialogs_.pkg_browser.action_error_console_backed,
              "Non-build package failures do not become console-backed");
        check(ui.dialogs_.pkg_browser.footer_action_btn.w == 0.0f,
              "Non-build package failures do not expose a footer action button before draw");

        ui.dialogs_.pkg_browser.footer_action_btn = {pkg_layout.cx + pkg_layout.inner_w - 96.0f,
                                                     pkg_layout.status_y - 1.0f, 96.0f, 20.0f};
        const auto& footer_btn = ui.dialogs_.pkg_browser.footer_action_btn;
        ui.on_mouse_move(footer_btn.x + footer_btn.w * 0.5f, footer_btn.y + footer_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(open_console_calls == 0, "Non-build package failures do not open the Build Console");
    }

    // Meta editor: save click dispatch
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        int save_calls = 0;
        ui.set_graph_meta_save_callback([&](const GraphMetaEditData&, std::string&) {
            ++save_calls;
            return true;
        });
        GraphMetaEditData meta;
        meta.path = "/tmp/demo.json";
        meta.id = "demo";
        ui.open_graph_meta_editor(meta);
        OverlayPanelLayout meta_layout = compute_graph_meta_editor_layout(1280, 720);
        float by = meta_layout.py + meta_layout.ph - 42.0f;
        float save_x = meta_layout.px + meta_layout.pw - 16.0f - 80.0f - 8.0f - 90.0f;
        ui.on_mouse_move(save_x + 10.0f, by + 10.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(save_calls == 1, "Meta editor Save click invokes save callback");
    }

    // Graph meta editor: text-edit focus and save use the active field state
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        GraphMetaEditData saved;
        int save_calls = 0;
        ui.set_graph_meta_save_callback([&](const GraphMetaEditData& data, std::string&) {
            ++save_calls;
            saved = data;
            return true;
        });
        GraphMetaEditData meta;
        meta.path = "/tmp/demo.json";
        meta.id = "demo";
        meta.title = "Title";
        ui.open_graph_meta_editor(meta);

        ui.on_char('X');                 // id field
        check(ui.graph_meta_data().id == "demoX", "Meta editor updates live id field on char input");
        ui.on_key(GLFW_KEY_TAB, GLFW_PRESS, 0);   // title field
        ui.on_char('Y');
        check(ui.graph_meta_data().title == "TitleY", "Meta editor updates live title field after tab focus");

        OverlayPanelLayout meta_layout = compute_graph_meta_editor_layout(1280, 720);
        float by = meta_layout.py + meta_layout.ph - 42.0f;
        float save_x = meta_layout.px + meta_layout.pw - 16.0f - 80.0f - 8.0f - 90.0f;
        ui.on_mouse_move(save_x + 10.0f, by + 10.0f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);

        check(save_calls == 1, "Meta editor keyboard edits are saved");
        check(saved.id == "demoX", "Meta editor appends characters to the active id field");
        check(saved.title == "TitleY", "Meta editor tab focus advances text editing to the next field");
    }

    // File-drop chooser: selected action creates node and assigns dropped path
    {
        DummySink sink;
        NodeGraphUI ui(sink);
        ui.open_file_drop_chooser({
            FileDropChooserAction{
                "Create Test Asset Node",
                "FileDropTestOp  [tests]",
                "FileDropTestOp",
                "file",
                "/tmp/example.dropx"
            }
        }, 123.0f, 456.0f);

        check(ui.chooser_open_, "file-drop chooser opens");
        ui.confirm_chooser_selection_idx(0);
        check(!sink.add_calls.empty(), "file-drop chooser adds node");
        if (!sink.add_calls.empty())
            check(sink.add_calls[0].first == "FileDropTestOp", "file-drop chooser adds expected type");
        check(!sink.layout_calls.empty(), "file-drop chooser sets layout");
        if (!sink.layout_calls.empty())
            check(std::get<1>(sink.layout_calls[0]) == 123.0f &&
                  std::get<2>(sink.layout_calls[0]) == 456.0f,
                  "file-drop chooser uses supplied graph position");
        check(!sink.string_param_calls.empty(), "file-drop chooser sets dropped path");
        if (!sink.string_param_calls.empty())
            check(std::get<1>(sink.string_param_calls[0]) == "file" &&
                  std::get<2>(sink.string_param_calls[0]) == "/tmp/example.dropx",
                  "file-drop chooser sets declared file param");
    }

    // Chooser insert-on-wire: preserve the original wire if the splice cannot complete
    {
        DummySink sink;
        NodeGraphUI ui(sink);

        auto make_op = [](std::initializer_list<PortInfo> ports) {
            auto op = std::make_shared<OperatorInfo>();
            op->ports.assign(ports.begin(), ports.end());
            return op;
        };

        GraphSnapshot chooser_snap;
        chooser_snap.operator_catalog["insert"] = make_op({
            PortInfo{"in", VIVID_PORT_SCALAR, VIVID_PORT_INPUT},
            PortInfo{"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT},
        });

        NodeSnapshot src;
        src.node_id = "src";
        src.op_info = make_op({PortInfo{"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT}});
        chooser_snap.node_index[src.node_id] = chooser_snap.nodes.size();
        chooser_snap.nodes.push_back(src);

        NodeSnapshot dst;
        dst.node_id = "dst";
        dst.op_info = make_op({PortInfo{"in", VIVID_PORT_SCALAR, VIVID_PORT_INPUT}});
        chooser_snap.node_index[dst.node_id] = chooser_snap.nodes.size();
        chooser_snap.nodes.push_back(dst);

        ConnectionSnapshot conn;
        conn.from_node = "src";
        conn.from_port = "out";
        conn.to_node = "dst";
        conn.to_port = "in";
        chooser_snap.connections.push_back(conn);

        ui.update(chooser_snap);
        ui.chooser_insert_wire_ = true;
        ui.chooser_insert_conn_ = conn;
        ui.insert_wire_source_type_ = VIVID_PORT_SCALAR;
        ui.insert_wire_dest_type_ = VIVID_PORT_SCALAR;
        ui.chooser_cursor_gx_ = 10.0f;
        ui.chooser_cursor_gy_ = 20.0f;

        sink.fail_connect_from = "insert1/out";
        sink.fail_connect_to = "dst/in";

        ui.confirm_chooser_selection("insert");

        check(sink.add_calls.size() == 1, "Chooser insert adds the replacement node");
        check(sink.connect_calls.size() == 2, "Chooser insert attempts both replacement connects before touching the original wire");
        check(sink.disconnect_calls.size() == 1, "Chooser insert rolls back the partial replacement connection");
        check(sink.disconnect_calls[0].first == "src/out" && sink.disconnect_calls[0].second == "insert1/in",
              "Chooser insert rollback only removes the partial new connection");
        check(std::none_of(sink.disconnect_calls.begin(), sink.disconnect_calls.end(),
                           [](const auto& call) { return call.first == "src/out" && call.second == "dst/in"; }),
              "Chooser insert preserves the original wire when the replacement splice fails");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
