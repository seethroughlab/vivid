#include "ui/node_graph.h"
#include "ui/graph_snapshot.h"
#include "ui/overlay_layouts.h"
#include <GLFW/glfw3.h>
#include <cstdio>

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
    void set_param(const std::string&, const std::string&, float) override {}
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
        OverlayRect open_btn = compute_example_open_button_rect(example_layout, 0);
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
        OverlayRect open_btn = compute_example_open_button_rect(example_layout, 0);
        ui.on_mouse_move(open_btn.x + open_btn.w * 0.5f, open_btn.y + open_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(!opened, "first missing-package Open click warns without opening");
        ui.on_mouse_move(open_btn.x + open_btn.w * 0.5f, open_btn.y + open_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(opened, "second missing-package Open click proceeds");
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
                    PackageBrowserEntry{"vivid-demo", "demo", "0.1.0", "dev", "utility", {}, false}
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
        OverlayRect pkg_btn = compute_package_action_button_rect(pkg_layout, 0);
        ui.on_mouse_move(pkg_btn.x + pkg_btn.w * 0.5f, pkg_btn.y + pkg_btn.h * 0.5f);
        ui.on_mouse_button(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        ui.update(snap);
        check(install_calls == 1, "Package install click invokes install callback");
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

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
