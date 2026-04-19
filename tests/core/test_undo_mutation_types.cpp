#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/control/runtime_command_sink.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/packages/package_manager.h"
#include "runtime/core/hot_reload.h"
#include "runtime/operators/operator_info_cache.h"
#include "runtime/core/settings.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include "test_helpers.h"

namespace fs = std::filesystem;

static void settle_topology(vivid::RuntimeAPI& api, bool& has_gpu_ops, bool& has_audio) {
    if (api.has_pending()) {
        api.apply_pending(has_gpu_ops, has_audio);
    }
}

static bool has_connection(const vivid::Graph& g,
                           const char* fn, const char* fp,
                           const char* tn, const char* tp) {
    for (const auto& c : g.connections()) {
        if (c.from_node == fn && c.from_port == fp &&
            c.to_node == tn && c.to_port == tp) return true;
    }
    return false;
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "\n=== Test: Undo mutation coverage ===\n\n");

    // Stage test operator plugin so RuntimeAPI can add nodes by type.
    std::string staging = build_dir + "/.test_undo_mutation_staging";
    fs::create_directories(staging);
    fs::copy_file(build_dir + "/test_op_v1.dylib",
                  staging + "/test_op_v1.dylib",
                  fs::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load((build_dir + "/test_runtime_api.json").c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    vivid::AudioEngine audio_engine;
    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);
    bool has_gpu_ops = false;
    bool has_audio = false;

    RuntimeCommandSink sink(api);
    sink.set_graph(&graph);
    sink.set_runtime_flags(&has_gpu_ops, &has_audio);
    vivid::Settings sink_settings;
    sink_settings.editor = "custom";
    sink_settings.editor_command = "true";
    sink_settings.operator_clone_destination_mode = "project_default";
    sink.set_settings(&sink_settings);

    // 1) Change parameter value (covers slider/typed/color code paths via set_param).
    sink.set_param("a", "scale", 9.0f);
    check(graph.find_node("a")->params["scale"] == 9.0f, "set_param applied");
    check(sink.undo(), "undo param change");
    check(graph.find_node("a")->params["scale"] == 3.0f, "param restored after undo");
    check(sink.redo(), "redo param change");
    check(graph.find_node("a")->params["scale"] == 9.0f, "param restored after redo");

    // 2) Add/delete node.
    sink.add_node("TestOp", "u_add");
    settle_topology(api, has_gpu_ops, has_audio);
    check(graph.find_node("u_add") != nullptr, "add_node added node");
    check(sink.undo(), "undo add_node");
    check(graph.find_node("u_add") == nullptr, "node removed after undo add_node");
    check(sink.redo(), "redo add_node");
    check(graph.find_node("u_add") != nullptr, "node restored after redo add_node");

    sink.remove_node("u_add");
    settle_topology(api, has_gpu_ops, has_audio);
    check(graph.find_node("u_add") == nullptr, "remove_node removed node");
    check(sink.undo(), "undo remove_node");
    check(graph.find_node("u_add") != nullptr, "node restored after undo remove_node");
    check(sink.redo(), "redo remove_node");
    check(graph.find_node("u_add") == nullptr, "node removed after redo remove_node");

    // 3) Connect/disconnect wire.
    sink.add_node("TestOp", "u_wire");
    settle_topology(api, has_gpu_ops, has_audio);
    sink.connect("a/out", "u_wire/scale");
    settle_topology(api, has_gpu_ops, has_audio);
    check(has_connection(graph, "a", "out", "u_wire", "scale"), "connect created wire");
    check(sink.undo(), "undo connect");
    check(!has_connection(graph, "a", "out", "u_wire", "scale"), "wire removed after undo connect");
    check(sink.redo(), "redo connect");
    check(has_connection(graph, "a", "out", "u_wire", "scale"), "wire restored after redo connect");

    sink.disconnect("a/out", "u_wire/scale");
    settle_topology(api, has_gpu_ops, has_audio);
    check(!has_connection(graph, "a", "out", "u_wire", "scale"), "disconnect removed wire");
    check(sink.undo(), "undo disconnect");
    check(has_connection(graph, "a", "out", "u_wire", "scale"), "wire restored after undo disconnect");
    check(sink.redo(), "redo disconnect");
    check(!has_connection(graph, "a", "out", "u_wire", "scale"), "wire removed after redo disconnect");

    // 4) Move node position.
    sink.set_node_layout("a", 111.0f, 222.0f);
    check(graph.find_node("a")->has_layout(), "set_node_layout set layout");
    check(sink.undo(), "undo set_node_layout");
    check(!graph.find_node("a")->has_layout(), "layout cleared after undo");
    check(sink.redo(), "redo set_node_layout");
    check(graph.find_node("a")->has_layout() &&
          graph.find_node("a")->layout_x == 111.0f &&
          graph.find_node("a")->layout_y == 222.0f,
          "layout restored after redo");

    // 5) Copy/paste nodes (equivalent command sequence).
    sink.add_node("TestOp", "paste1");
    sink.set_param("paste1", "scale", 5.5f);
    sink.set_node_layout("paste1", 420.0f, 69.0f);
    sink.connect("a/out", "paste1/scale");
    settle_topology(api, has_gpu_ops, has_audio);
    check(graph.find_node("paste1") != nullptr &&
          has_connection(graph, "a", "out", "paste1", "scale"),
          "copy/paste-equivalent sequence applied");

    // Undo all paste steps.
    check(sink.undo(), "undo paste step 1");
    check(sink.undo(), "undo paste step 2");
    check(sink.undo(), "undo paste step 3");
    check(sink.undo(), "undo paste step 4");
    check(graph.find_node("paste1") == nullptr, "paste node removed after undo chain");

    // Redo all paste steps.
    check(sink.redo(), "redo paste step 1");
    check(sink.redo(), "redo paste step 2");
    check(sink.redo(), "redo paste step 3");
    check(sink.redo(), "redo paste step 4");
    check(graph.find_node("paste1") != nullptr &&
          has_connection(graph, "a", "out", "paste1", "scale"),
          "paste node restored after redo chain");

    // 6) Group delete (select multiple -> delete) as equivalent command sequence.
    sink.add_node("TestOp", "grp1");
    sink.add_node("TestOp", "grp2");
    settle_topology(api, has_gpu_ops, has_audio);
    check(graph.find_node("grp1") && graph.find_node("grp2"), "group nodes created");

    sink.remove_node("grp1");
    sink.remove_node("grp2");
    settle_topology(api, has_gpu_ops, has_audio);
    check(!graph.find_node("grp1") && !graph.find_node("grp2"), "group delete removed both nodes");

    check(sink.undo(), "undo group delete item 1");
    check(sink.undo(), "undo group delete item 2");
    check(graph.find_node("grp1") && graph.find_node("grp2"), "group delete undo restored both nodes");

    check(sink.redo(), "redo group delete item 1");
    check(sink.redo(), "redo group delete item 2");
    check(!graph.find_node("grp1") && !graph.find_node("grp2"), "group delete redo removed both nodes");

    // 7) Undo across file load: clear history and push loaded baseline.
    {
        std::string reload_path = build_dir + "/test_undo_reload_target.json";
        {
            std::ofstream ofs(reload_path);
            ofs << R"({
  "nodes": {
    "a": { "type": "TestOp", "params": { "scale": 4.0 } },
    "b": { "type": "TestOp", "params": { "scale": 1.0 } }
  },
  "connections": [
    { "from": "a/out", "to": "b/scale" }
  ]
})";
        }
        check(graph.load(reload_path.c_str()), "load new graph file for reload test");
        check(api.reload(has_gpu_ops, has_audio).ok, "runtime reload after new file load");
        sink.reset_undo_history();
        check(!sink.can_undo(), "undo history cleared on file load");

        sink.set_param("a", "scale", 10.0f);
        check(sink.can_undo(), "undo available after post-load mutation");
        check(sink.undo(), "undo post-load mutation");
        check(graph.find_node("a")->params["scale"] == 4.0f,
              "undo restored loaded graph baseline");
        fs::remove(reload_path);
    }

    // 8) Undo after operator set change: stale snapshot applies with placeholder.
    {
        auto* a_node = graph.find_node("a");
        auto* b_node = graph.find_node("b");
        check(a_node != nullptr && b_node != nullptr, "reload test graph still present");
        check(a_node->type == "TestOp", "node a starts with valid operator type");

        // Force one stale snapshot that references an unknown operator type.
        a_node->type = "NoSuchType";
        sink.set_param("b", "scale", 2.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));

        // Restore valid type and create a new top snapshot that should remain active on failed undo.
        a_node->type = "TestOp";
        sink.set_param("b", "scale", 3.0f);

        check(sink.undo(), "undo stale snapshot succeeds with placeholder");
        check(graph.find_node("a") != nullptr && graph.find_node("a")->type == "NoSuchType",
              "undo restored stale type as missing-operator placeholder");
        check(graph.find_node("b")->params["scale"] == 2.0f,
              "undo restored stale snapshot params");
        check(sink.redo(), "redo from placeholder snapshot succeeds");
        check(graph.find_node("a") != nullptr && graph.find_node("a")->type == "TestOp",
              "redo restored valid operator type");
        check(graph.find_node("b")->params["scale"] == 3.0f,
              "redo restored current graph params");
    }

    // 9) Undo after uninstall: missing operator uses placeholder and still applies.
    {
        check(graph.load_from_string(R"({
  "nodes": {
    "a": { "type": "TestOp", "params": { "scale": 4.0 } },
    "b": { "type": "TestOp", "params": { "scale": 1.0 } }
  },
  "connections": [
    { "from": "a/out", "to": "b/scale" }
  ]
})"), "load graph for uninstall undo test");
        check(runtime.build(graph, registry), "rebuild runtime for uninstall undo test");
        sink.reset_undo_history();
        sink.set_param("a", "scale", 12.0f);
        check(graph.find_node("a")->params["scale"] == 12.0f, "post-reset mutation applied");

        registry.unregister_package_operator("TestOp");
        check(sink.undo(), "undo succeeds after operator uninstall");
        check(graph.find_node("a")->params["scale"] == 4.0f,
              "undo restored baseline value with missing-operator placeholder");
    }

    // 10) Clone into project package routes to package src/ and package hot-reload target.
    {
        check(registry.scan(staging.c_str()), "re-scan registry for clone e2e");

        fs::path local_packages_root = fs::path(build_dir) / "packages";
        std::error_code clean_ec;
        fs::remove_all(local_packages_root, clean_ec);
        fs::create_directories(local_packages_root);

        fs::path pkg_src = fs::path(build_dir) / ".test_clone_project_pkg_src";
        fs::remove_all(pkg_src);
        fs::create_directories(pkg_src / "src");
        {
            std::ofstream ofs(pkg_src / "vivid-package.json");
            ofs << "{\n"
                   "  \"name\": \"vivid-clone-e2e\",\n"
                   "  \"version\": \"0.0.1\",\n"
                   "  \"build\": \"cmake\",\n"
                   "  \"operators\": []\n"
                   "}\n";
        }
        {
            std::ofstream ofs(pkg_src / "CMakeLists.txt");
            ofs << "cmake_minimum_required(VERSION 3.20)\n"
                   "project(vivid_clone_e2e)\n"
                   "set(CONTROL_OPS\n"
                   ")\n";
        }
        fs::path pkg_link = local_packages_root / "vivid-clone-e2e";
        std::error_code ec;
        fs::create_directory_symlink(fs::absolute(pkg_src), pkg_link, ec);
        check(!ec, "created linked local project package");

        fs::path core_ops = fs::path(build_dir) / ".test_clone_core_ops";
        fs::remove_all(core_ops);
        // Directory/filename stem must match the registry's target name
        // (derived from the loaded dylib: test_op_v1.dylib → "test_op_v1"),
        // since clone_cpp_operator resolves source via
        //   operators_dir_/<kind>/<stem>/<stem>.cpp
        fs::create_directories(core_ops / "control" / "test_op_v1");
        {
            std::ofstream ofs(core_ops / "control" / "test_op_v1" / "test_op_v1.cpp");
            ofs << "#include \"operator_api/operator.h\"\n"
                   "struct TestOp : vivid::OperatorBase, vivid::FrameProcessable {\n"
                   "  static constexpr const char* kName = \"TestOp\";\n"
                   "  static constexpr bool kTimeDependent = false;\n"
                   "  vivid::Param<float> scale{\"scale\", 1.0f, 0.0f, 10.0f};\n"
                   "  void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&scale); }\n"
                   "  void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
                   "    out.push_back({\"scale\", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});\n"
                   "    out.push_back({\"out\", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});\n"
                   "  }\n"
                   "  void process_frame(VividFrameContext* ctx) override { ctx->output_values[0] = ctx->input_values[0] * scale.value; }\n"
                   "};\n"
                   "VIVID_REGISTER(TestOp)\n";
        }

        vivid::PackageCompiler pkg_compiler(build_dir, build_dir);
        vivid::PackageManager pkg_manager(pkg_compiler, registry);
        sink.set_package_manager(&pkg_manager);
        {
            auto packages = pkg_manager.list();
            bool found_linked = false;
            for (const auto& p : packages) {
                if (p.name == "vivid-clone-e2e" && p.linked) {
                    found_linked = true;
                    break;
                }
            }
            check(found_linked, "package manager discovered linked project package");
        }

        vivid::HotReloader hr;
        fs::path hr_build = fs::path(build_dir) / ".test_clone_hr_build";
        fs::path staged_dylib_path = hr_build / "fake_clone_e2e.dylib";
        fs::create_directories(hr_build);
        check(hr.start(hr_build.string()), "hot reloader started for clone e2e");
        hr.set_package_compiler([staged_dylib_path](const std::string& target) {
            vivid::ReloadResult r;
            r.target_name = target;
            r.success = true;
            r.staged_dylib_path = staged_dylib_path.string();
            return r;
        });
        sink.set_hot_reloader(&hr);

        sink.set_operators_dir(core_ops.string());
        sink.set_build_dir(build_dir);
        sink.set_registry(&registry);
        OperatorInfoCache op_cache;
        sink.set_op_cache(&op_cache);

        // Pass empty custom_name so the new op is auto-named TestOp_copy.
        // Destination routing is already set via operator_clone_destination_mode
        // in sink_settings above.
        sink.clone_and_edit("TestOp", "");

        fs::path cloned_cpp = pkg_src / "src" / "testop_copy.cpp";
        check(fs::exists(cloned_cpp), "clone wrote source into linked package src/");

        std::string cmake_content;
        {
            std::ifstream ifs(pkg_src / "CMakeLists.txt");
            cmake_content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        }
        check(cmake_content.find("testop_copy") != std::string::npos,
              "clone patched package CMake ops list");

        bool saw_pkg_reload = false;
        for (int i = 0; i < 40 && !saw_pkg_reload; ++i) {
            auto ready = hr.poll_ready();
            for (const auto& rr : ready) {
                if (rr.target_name == "pkg:vivid-clone-e2e:testop_copy" && rr.success) {
                    saw_pkg_reload = true;
                    break;
                }
            }
            if (!saw_pkg_reload)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        check(saw_pkg_reload, "clone queued package hot-reload target that compiled");
        hr.stop();
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
