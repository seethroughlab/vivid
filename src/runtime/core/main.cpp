#include "runtime/gpu/gpu_context.h"
#include "runtime/gpu/fullscreen_blit.h"
#include "runtime/debug/output_window.h"
#include "ui/rendering/thumbnail_cache.h"
#include "ui/rendering/thumbnail_renderer.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/core/file_watcher.h"
#include "runtime/core/hot_reload.h"
#include "runtime/control/runtime_api.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/graph/node_graph.h"
#include "ui/graph/graph_snapshot.h"
#include "ui/ui_command_sink.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/control/control_server.h"
#include "runtime/debug/capture_coordinator.h"
#include "runtime/audio/system_midi.h"
#include "runtime/core/settings.h"
#include "runtime/core/runtime_bootstrap.h"
#include "runtime/core/editor_detect.h"
#include "runtime/operators/operator_info_cache.h"
#include "runtime/operators/operator_preparation_service.h"
#include "runtime/control/runtime_command_sink.h"
#include "runtime/core/file_drop_registry.h"
#include "runtime/core/crash_guard.h"
#include "ui/style/ui_style.h"
#include "ui/style/theme_loader.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/data_driven_filter.h"
#include "operator_api/thumbnail.h"
#include "operator_api/types.h"
#include "operator_api/input_state.h"
#include "common/gpu_util.h"
#include "operator_api/gpu_common.h"
#include "export/export_pipeline.h"
#include "runtime/assets/asset_library.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_test_runner.h"
#include "runtime/core/build_console.h"
#include "runtime/packages/package_catalog.h"
#include "runtime/packages/package_scaffolder.h"
#include "runtime/platform/app_update_manager.h"
#include "runtime/platform/platform.h"
#include "runtime/operators/operator_creator.h"
#include "runtime/operators/operator_destination_policy.h"
#include "runtime/debug/ui_test_runner.h"
#include "ui/dialogs/file_dialog.h"
#include <fstream>
#include <sstream>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <GLFW/glfw3.h>
#include <stb_image_write.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <string>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <random>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#ifdef __APPLE__
#include "runtime/platform/macos_frame_timer.h"
#include "runtime/platform/macos_menu.h"
#include "runtime/platform/sparkle_bridge.h"
#endif
#include "runtime/control/control_server_internal.h"
#include "runtime/control/graph_file_io.h"
#include "runtime/core/workspace_manager.h"
#include "runtime/core/window_manager.h"
#include "runtime/graph/graph_snapshot_builder.h"
#include "runtime/core/main_helpers.h"
#include "runtime/core/main_internal.h"
#include "ui/style/i18n.h"  // must follow nlohmann/json includes (T macro conflicts)


// #16191D in sRGB → linear: pow(x/255, 2.2)
static constexpr double kClearLinear[4]  = { 0.00699, 0.00821, 0.01041, 1.0 };
// #16191D as raw unorm (no gamma conversion)
static constexpr double kClearRaw[4]     = { 0.0863, 0.0980, 0.1137, 1.0 };

// Thumbnail size: node width × 16:10 aspect
static constexpr uint32_t kThumbW = 140;
static constexpr uint32_t kThumbH = 88;

// Default GPU texture resolution for nodes without explicit size
static constexpr uint32_t kDefaultTexW = 800;
static constexpr uint32_t kDefaultTexH = 600;

using vivid::to_sv;
using namespace vivid;

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

namespace fs = std::filesystem;
namespace mi = vivid::main_internal;

int main(int argc, char* argv[]) {
    vivid::install_crash_handlers();

    // --- CLI argument parsing ---
    std::string graph_file;
    std::string screenshot_path;
    std::string screenshot_select_node;
    int screenshot_delay = 5;
    std::string test_drop_path;
    std::vector<int> test_drop_screen_pos;
    int test_drop_frame = 5;
    std::string test_ui_script_path;
    std::string test_dump_ui_state_path;
    bool headless = false;
    std::string src_dir;

    CLI::App app{"Vivid - Real-time audio-visual graph engine\n\n"
                 "Loads a JSON graph file and runs it in real-time.\n"
                 "Control server listens on http://127.0.0.1:9876 for live manipulation."};

    app.add_option("graph", graph_file, "Graph file to load")->type_name("FILE");
    app.add_option("--screenshot", screenshot_path, "Capture a screenshot to PNG and exit")->type_name("FILE");
    app.add_option("--screenshot-delay", screenshot_delay, "Frames to wait before capture (default: 5)");
    app.add_option("--select-node", screenshot_select_node,
                   "Select a node by id before drawing the inspector (useful with --screenshot)")
        ->type_name("NODE_ID");
    app.add_option("--test-drop-path", test_drop_path,
                   "Inject a synthetic drag-and-drop path after startup (testing seam)")
        ->type_name("FILE");
    app.add_option("--test-drop-screen-pos", test_drop_screen_pos,
                   "Synthetic drop location in window coordinates (x y)")
        ->type_name("X Y")
        ->expected(2);
    app.add_option("--test-drop-frame", test_drop_frame,
                   "Frame number at which to inject --test-drop-path (default: 5)");
    app.add_option("--test-ui-script", test_ui_script_path,
                   "Replay a scripted sequence of UI inputs after startup (testing seam)")
        ->type_name("FILE");
    app.add_option("--test-dump-ui-state", test_dump_ui_state_path,
                   "Write a machine-readable final UI/runtime state snapshot (testing seam)")
        ->type_name("FILE");
    app.add_flag("--headless", headless, "Run without displaying a window");
    app.add_option("--src-dir", src_dir, "Source directory for operator hot-reload")->type_name("PATH");

    // --- Export subcommand ---
    std::string export_graph_path;
    std::string export_output;
    std::string export_output_dir;
    bool export_headless = false;
    bool export_control_server = false;
    std::vector<std::string> export_extra_ops;

    auto* export_cmd = app.add_subcommand("export", "Export graph as a standalone binary");
    export_cmd->add_option("--graph", export_graph_path, "Graph file to export")
        ->required()->type_name("FILE");
    export_cmd->add_option("--output", export_output, "Output binary name")
        ->required()->type_name("NAME");
    export_cmd->add_option("--output-dir", export_output_dir, "Export build directory")->type_name("PATH");
    export_cmd->add_flag("--headless", export_headless, "Build headless (no window)");
    export_cmd->add_flag("--control-server", export_control_server, "Include HTTP control server");
    export_cmd->add_option("--extra-operators", export_extra_ops,
        "Additional operator types to include (comma-separated)")->delimiter(',');

    // --- Package management subcommands ---
    std::string install_url;
    std::string uninstall_name;

    auto* install_cmd = app.add_subcommand("install", "Install an operator package");
    install_cmd->add_option("url", install_url, "Git URL or local path")->required();

    auto* uninstall_cmd = app.add_subcommand("uninstall", "Uninstall an operator package");
    uninstall_cmd->add_option("name", uninstall_name, "Package name")->required();

    auto* list_pkg_cmd = app.add_subcommand("list-packages", "List installed operator packages");
    bool list_pkg_verbose = false;
    list_pkg_cmd->add_flag("--verbose", list_pkg_verbose,
                           "Show resolver diagnostics (scope/path/build metadata)");

    std::string link_path;
    auto* link_cmd = app.add_subcommand("link", "Link a local package for development");
    link_cmd->add_option("path", link_path, "Path to package directory")->required();

    std::string unlink_name;
    auto* unlink_cmd = app.add_subcommand("unlink", "Unlink a linked package");
    unlink_cmd->add_option("name", unlink_name, "Package name")->required();

    std::string rebuild_name;
    auto* rebuild_cmd = app.add_subcommand("rebuild", "Recompile operators for a package");
    rebuild_cmd->add_option("name", rebuild_name, "Package name")->required();

    std::string scaffold_pkg_name;
    std::string scaffold_pkg_template = "single";
    std::string scaffold_pkg_output_dir;
    std::string scaffold_pkg_template_root;
    bool scaffold_pkg_force = false;
    auto* scaffold_pkg_cmd = app.add_subcommand("scaffold-package",
        "Scaffold a package skeleton from template");
    scaffold_pkg_cmd->add_option("name", scaffold_pkg_name, "Package name")->required();
    scaffold_pkg_cmd->add_option("--template", scaffold_pkg_template,
                                 "Template variant: single|multi")
        ->check(CLI::IsMember({"single", "multi"}))
        ->default_val("single");
    scaffold_pkg_cmd->add_option("--output-dir", scaffold_pkg_output_dir,
                                 "Parent directory for generated package");
    scaffold_pkg_cmd->add_option("--template-root", scaffold_pkg_template_root,
                                 "Explicit template root (overrides auto-discovery)");
    scaffold_pkg_cmd->add_flag("--force", scaffold_pkg_force,
                               "Overwrite destination if it already exists");

    std::string scaffold_op_name;
    std::string scaffold_op_env = "control";
    std::string scaffold_op_variant;
    std::string scaffold_op_dest = "auto";
    auto* scaffold_op_cmd = app.add_subcommand("scaffold-operator",
        "Scaffold a starter operator source file");
    scaffold_op_cmd->add_option("name", scaffold_op_name, "Operator name (snake_case)")->required();
    scaffold_op_cmd->add_option("--env", scaffold_op_env,
                                "Execution environment: control|audio|gpu")
        ->check(CLI::IsMember({"control", "audio", "gpu"}))
        ->default_val("control");
    scaffold_op_cmd->add_option("--variant", scaffold_op_variant,
                                "Optional template variant (e.g. composite)");
    scaffold_op_cmd->add_option("--dest", scaffold_op_dest,
                                "Destination: auto|core|package:<name>|absolute path")
        ->default_val("auto");
    std::string update_core_version = VIVID_CORE_VERSION;
    bool update_include_all = false;
    auto* check_updates_cmd = app.add_subcommand("package-check-updates",
        "Check installed packages for available updates and compatibility");
    check_updates_cmd->add_option("--core-version", update_core_version,
                                  "Core version to evaluate against vivid_core constraints")
        ->default_val(VIVID_CORE_VERSION);
    check_updates_cmd->add_flag("--all", update_include_all,
                                "Include installed packages even when no update is available");

    bool check_core_force = false;
    auto* check_core_updates_cmd = app.add_subcommand("check-core-updates",
        "Check for available Vivid core application updates");
    check_core_updates_cmd->add_flag("--force", check_core_force,
                                     "Force immediate network refresh");

    std::string list_types_domain;
    auto* list_types_cmd = app.add_subcommand("list-types",
        "List available operator types as JSON");
    list_types_cmd->add_option("--domain", list_types_domain,
                               "Optional filter: control|audio|gpu");

    std::string operator_docs_name;
    std::string operator_docs_package;
    auto* operator_docs_cmd = app.add_subcommand("operator-docs",
        "Read one operator's merged docs/descriptor payload as JSON");
    operator_docs_cmd->add_option("name", operator_docs_name, "Operator type name")->required();
    operator_docs_cmd->add_option("--package", operator_docs_package,
                                  "Optional package name for package operator docs");

    std::string package_operator_docs_name;
    auto* package_operator_docs_cmd = app.add_subcommand("package-operator-docs",
        "Read merged docs/descriptor payloads for one package as JSON");
    package_operator_docs_cmd->add_option("name", package_operator_docs_name, "Package name")->required();

    auto* operator_map_cmd = app.add_subcommand("operator-map",
        "List the runtime operator map as JSON");

    auto* discovery_report_cmd = app.add_subcommand("discovery-report",
        "Show the package discovery report as JSON");

    std::string read_package_docs_name;
    auto* read_package_docs_cmd = app.add_subcommand("read-package-docs",
        "Read a package README as JSON");
    read_package_docs_cmd->add_option("name", read_package_docs_name, "Package name")->required();

    std::string list_package_examples_name;
    auto* list_package_examples_cmd = app.add_subcommand("list-package-examples",
        "List example graphs for a package as JSON");
    list_package_examples_cmd->add_option("name", list_package_examples_name, "Package name")->required();

    std::string read_package_example_name;
    std::string read_package_example_filename;
    auto* read_package_example_cmd = app.add_subcommand("read-package-example",
        "Read one package example graph as JSON");
    read_package_example_cmd->add_option("name", read_package_example_name, "Package name")->required();
    read_package_example_cmd->add_option("filename", read_package_example_filename, "Example filename")->required();

    auto* package_catalog_cmd = app.add_subcommand("package-catalog",
        "Fetch the package catalog as JSON");

    std::string test_package_name;
    auto* test_package_cmd = app.add_subcommand("test-package",
        "Run package tests and emit JSON results");
    test_package_cmd->add_option("name", test_package_name, "Package name")->required();

    bool cli_json = false;
    auto add_json_flag = [&](CLI::App* cmd) {
        cmd->add_flag("--json", cli_json, "Emit JSON output");
    };
    for (CLI::App* cmd : {
            install_cmd, uninstall_cmd, list_pkg_cmd, link_cmd, unlink_cmd, rebuild_cmd,
            scaffold_op_cmd, check_updates_cmd, check_core_updates_cmd,
            list_types_cmd, operator_docs_cmd, package_operator_docs_cmd,
            operator_map_cmd, discovery_report_cmd, read_package_docs_cmd,
            list_package_examples_cmd, read_package_example_cmd, package_catalog_cmd,
            test_package_cmd
        }) {
        add_json_flag(cmd);
    }

    app.require_subcommand(0, 1);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }
    if (test_drop_frame < 0) test_drop_frame = 0;
    vivid::UITestScript test_ui_script;
    if (!test_ui_script_path.empty()) {
        std::string script_error;
        if (!vivid::load_ui_test_script(test_ui_script_path, test_ui_script, script_error)) {
            std::fprintf(stderr, "[vivid] Failed to load --test-ui-script %s: %s\n",
                         test_ui_script_path.c_str(), script_error.c_str());
            return 1;
        }
    }
    if (!test_dump_ui_state_path.empty()) {
        std::filesystem::path dump_path(test_dump_ui_state_path);
        if (!dump_path.is_absolute())
            dump_path = std::filesystem::absolute(dump_path);
        test_dump_ui_state_path = dump_path.string();
    }

    auto runtime_paths = vivid::resolve_runtime_bootstrap_paths(argv[0], src_dir);
    auto exe_path = runtime_paths.exe_path;
    auto exe_dir = runtime_paths.exe_dir;
    auto resources_dir = runtime_paths.resources_dir;
    auto plugins_dir = runtime_paths.plugins_dir;

    // Resolve build/source directories once (used by export, packages, hot-reload)
    vivid::Settings settings = vivid::load_settings();

    // Load locale strings (auto-detect OS language, fall back to English)
    vivid::ui::I18n::instance().load_best((resources_dir / "locales").string());

    // --- Handle export subcommand (early exit, no GLFW) ---
    if (export_cmd->parsed()) {
        if (runtime_paths.source_dir.empty()) {
            std::fprintf(stderr, "[vivid] Cannot determine source directory. "
                         "Use --src-dir or run from a build directory.\n");
            return 1;
        }

        // Build registry to get type→target mappings
        vivid::OperatorRegistry registry;
        vivid::RegistryBootstrapOptions bootstrap_opts;
        bootstrap_opts.scan_packages = false;
        vivid::bootstrap_operator_registry(registry, nullptr, runtime_paths, bootstrap_opts);

        vivid::ExportOptions opts;
        opts.graph_path = export_graph_path;
        opts.output_name = export_output;
        opts.output_path = export_output;
        opts.output_dir = export_output_dir;
        opts.headless = export_headless;
        opts.control_server = export_control_server;
        opts.extra_operators = export_extra_ops;

        vivid::ExportPipeline pipeline(runtime_paths.source_dir, runtime_paths.build_dir);
        if (!pipeline.run(opts, registry)) {
            std::fprintf(stderr, "[vivid] Export failed\n");
            return 1;
        }
        return 0;
    }

    // --- Handle CLI query/admin subcommands (early exit, no GLFW) ---
    if (install_cmd->parsed() || uninstall_cmd->parsed() || list_pkg_cmd->parsed() ||
        link_cmd->parsed() || unlink_cmd->parsed() || rebuild_cmd->parsed() ||
        check_updates_cmd->parsed() || check_core_updates_cmd->parsed() ||
        scaffold_pkg_cmd->parsed() || scaffold_op_cmd->parsed() ||
        list_types_cmd->parsed() || operator_docs_cmd->parsed() ||
        package_operator_docs_cmd->parsed() || operator_map_cmd->parsed() ||
        discovery_report_cmd->parsed() || read_package_docs_cmd->parsed() ||
        list_package_examples_cmd->parsed() || read_package_example_cmd->parsed() ||
        package_catalog_cmd->parsed() || test_package_cmd->parsed()) {
        auto emit_json = [](const std::string& payload) -> int {
            std::printf("%s\n", payload.c_str());
            try {
                auto root = nlohmann::json::parse(payload);
                return root.value("ok", false) ? 0 : 1;
            } catch (...) {
                return 1;
            }
        };

        if (scaffold_pkg_cmd->parsed()) {
            vivid::PackageScaffoldOptions opts;
            opts.name = scaffold_pkg_name;
            opts.variant = scaffold_pkg_template;
            opts.output_dir = scaffold_pkg_output_dir;
            opts.template_root = scaffold_pkg_template_root;
            opts.source_dir = runtime_paths.source_dir;
            opts.force = scaffold_pkg_force;

            auto result = vivid::PackageScaffolder::scaffold(opts);
            if (!result.success) {
                std::fprintf(stderr, "Scaffold failed: %s\n", result.error.c_str());
                return 1;
            }

            std::printf("Scaffolded package: %s\n", result.package_dir.c_str());
            std::printf("Template used: %s\n", result.template_dir.c_str());
            std::printf("Next steps:\n");
            std::printf("  ./build/vivid link %s\n", result.package_dir.c_str());
            std::printf("  ./build/vivid rebuild %s\n", opts.name.c_str());
            return 0;
        }

        vivid::OperatorRegistry registry;
        vivid::PackageCompiler compiler(runtime_paths.source_dir, runtime_paths.build_dir);
        vivid::PackageManager pm(compiler, registry);
        vivid::SubgraphModuleRegistry subgraph_modules;
        vivid::OperatorSourceDocs source_docs;
        if (!runtime_paths.source_dir.empty())
            source_docs.set_core_source_root(runtime_paths.source_dir);

        const bool needs_package_scan =
            list_types_cmd->parsed() || operator_docs_cmd->parsed() ||
            package_operator_docs_cmd->parsed() || operator_map_cmd->parsed() ||
            discovery_report_cmd->parsed() || test_package_cmd->parsed();
        vivid::RegistryBootstrapOptions bootstrap_opts;
        bootstrap_opts.scan_packages = needs_package_scan;
        bootstrap_opts.subgraph_modules = needs_package_scan ? &subgraph_modules : nullptr;
        vivid::bootstrap_operator_registry(registry, &pm, runtime_paths, bootstrap_opts);

        if (list_types_cmd->parsed()) {
            nlohmann::json root = nlohmann::json::object();
            if (!list_types_domain.empty())
                root["domain"] = list_types_domain;
            return emit_json(handle_list_types(registry, &pm, source_docs, root, &subgraph_modules));
        } else if (operator_docs_cmd->parsed()) {
            nlohmann::json root = {{"name", operator_docs_name}};
            if (!operator_docs_package.empty())
                root["package"] = operator_docs_package;
            return emit_json(handle_operator_docs(registry, &pm, source_docs, root, &subgraph_modules));
        } else if (package_operator_docs_cmd->parsed()) {
            return emit_json(handle_package_operator_docs(
                registry, &pm, source_docs, nlohmann::json{{"name", package_operator_docs_name}}));
        } else if (operator_map_cmd->parsed()) {
            return emit_json(handle_operator_map(registry));
        } else if (discovery_report_cmd->parsed()) {
            return emit_json(handle_get_discovery_report(&pm));
        } else if (read_package_docs_cmd->parsed()) {
            return emit_json(handle_read_package_docs(&pm, nlohmann::json{{"name", read_package_docs_name}}));
        } else if (list_package_examples_cmd->parsed()) {
            return emit_json(handle_list_package_examples(&pm, nlohmann::json{{"name", list_package_examples_name}}));
        } else if (read_package_example_cmd->parsed()) {
            return emit_json(handle_read_package_example(
                &pm,
                nlohmann::json{{"name", read_package_example_name}, {"filename", read_package_example_filename}}));
        } else if (package_catalog_cmd->parsed()) {
            vivid::PackageCatalog catalog(pm);
            catalog.refresh();
            for (int i = 0; i < 200; ++i) {
                auto st = catalog.fetch_state();
                if (st != vivid::CatalogFetchState::Fetching) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (catalog.entries().empty() && catalog.fetch_state() == vivid::CatalogFetchState::Error)
                return emit_json(json_err(catalog.fetch_error()));
            return emit_json(handle_package_catalog(&catalog));
        } else if (test_package_cmd->parsed()) {
            auto tr = run_package_tests(test_package_name, pm, compiler, registry);
            if (!tr.error.empty())
                return emit_json(json_err(tr.error));

            nlohmann::json res = nlohmann::json::object();
            res["package"] = tr.package_name;
            res["summary"] = {{"total", tr.total}, {"passed", tr.passed}, {"failed", tr.failed}, {"skipped", tr.skipped}};
            if (!tr.notes.empty()) res["notes"] = tr.notes;
            nlohmann::json tests_arr = nlohmann::json::array();
            for (const auto& t : tr.tests) {
                nlohmann::json obj = nlohmann::json::object();
                obj["name"] = t.name;
                obj["type"] = t.type;
                obj["status"] = t.status;
                if (!t.code.empty()) obj["code"] = t.code;
                if (!t.reason.empty()) obj["reason"] = t.reason;
                if (!t.output.empty()) obj["output"] = t.output;
                tests_arr.push_back(std::move(obj));
            }
            res["tests"] = std::move(tests_arr);
            return emit_json(json_ok(std::move(res)));
        }

        if (scaffold_op_cmd->parsed()) {
            std::string validation_error = vivid::OperatorCreator::validate_name(scaffold_op_name, registry);
            if (!validation_error.empty()) {
                if (cli_json) return emit_json(json_err(validation_error));
                std::fprintf(stderr, "Scaffold failed: %s\n", validation_error.c_str());
                return 1;
            }

            VividOperatorKind kind = VIVID_OP_CONTROL;
            if (scaffold_op_env == "audio")
                kind = VIVID_OP_AUDIO;
            else if (scaffold_op_env == "gpu")
                kind = VIVID_OP_GPU;

            ScaffoldDestination destination;
            std::string dest_error;
            if (!resolve_scaffold_destination(scaffold_op_dest, runtime_paths.source_dir, pm,
                                              &settings,
                                              destination, dest_error)) {
                if (cli_json) return emit_json(json_err(dest_error));
                std::fprintf(stderr, "Scaffold failed: %s\n", dest_error.c_str());
                return 1;
            }
            if (!destination.warning.empty())
                std::fprintf(stderr, "[vivid] %s\n", destination.warning.c_str());

            VividCreateOperatorRequest req;
            req.name = scaffold_op_name;
            req.kind = kind;
            req.variant = scaffold_op_variant;
            auto result = vivid::OperatorCreator::create(req, destination.root,
                                                         destination.package_layout);
            if (!result.success) {
                if (cli_json) return emit_json(json_err(result.error));
                std::fprintf(stderr, "Scaffold failed: %s\n", result.error.c_str());
                return 1;
            }

            vivid::OperatorCreator::open_in_editor(result.cpp_path);
            if (cli_json) {
                nlohmann::json res = nlohmann::json::object();
                res["cpp_path"] = result.cpp_path;
                res["target_name"] = result.target_name;
                res["destination_root"] = destination.root;
                res["destination_is_package"] = destination.package_layout;
                if (!destination.package_name.empty())
                    res["destination_package"] = destination.package_name;
                if (!destination.warning.empty())
                    res["destination_warning"] = destination.warning;
                return emit_json(json_ok(std::move(res)));
            }

            std::printf("Scaffolded operator: %s\n", result.target_name.c_str());
            std::printf("Source file: %s\n", result.cpp_path.c_str());
            std::printf("Destination root: %s\n", destination.root.c_str());
            if (destination.package_layout) {
                if (!destination.package_name.empty())
                    std::printf("Destination package: %s\n", destination.package_name.c_str());
                std::printf("Next step: ./build/vivid rebuild %s\n",
                            destination.package_name.empty() ? "<package-name>" : destination.package_name.c_str());
            } else {
                std::printf("Next step: cmake --build %s --target %s\n",
                            runtime_paths.build_dir.c_str(), result.target_name.c_str());
            }
            std::printf("Hint: Use MCP opdev tools for advanced features (custom ports, params, inspectors)\n");
            return 0;
        }

        if (install_cmd->parsed()) {
            auto result = pm.install(install_url);
            if (cli_json) {
                if (!result.success) return emit_json(json_err(result.error));
                nlohmann::json res = {
                    {"name", result.info.name},
                    {"version", result.info.version},
                    {"operator_count", static_cast<int64_t>(result.info.operators.size() + result.info.gpu_operators.size())}
                };
                if (!result.info.vivid_core.empty())
                    res["vivid_core"] = result.info.vivid_core;
                return emit_json(json_ok(std::move(res)));
            }
            if (result.success) {
                std::fprintf(stderr, "Installed %s v%s (%zu operators)\n",
                             result.info.name.c_str(), result.info.version.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            }
            std::fprintf(stderr, "Install failed: %s\n", result.error.c_str());
            for (const auto& cr : result.compile_results) {
                if (!cr.success)
                    std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                 cr.error_output.c_str());
            }
            return 1;
        } else if (uninstall_cmd->parsed()) {
            const bool ok = pm.uninstall(uninstall_name);
            if (cli_json)
                return emit_json(ok ? json_ok_msg("uninstalled") : json_err("failed to uninstall package"));
            if (ok) {
                std::fprintf(stderr, "Uninstalled %s\n", uninstall_name.c_str());
                return 0;
            }
            std::fprintf(stderr, "Failed to uninstall %s\n", uninstall_name.c_str());
            return 1;
        } else if (list_pkg_cmd->parsed()) {
            if (cli_json)
                return emit_json(handle_list_packages(&pm));
            auto packages = pm.list();
            if (packages.empty()) {
                std::printf("No packages installed.\n");
            } else {
                for (const auto& pkg : packages) {
                    std::printf("%s v%s  (%zu operators)%s\n",
                                pkg.name.c_str(), pkg.version.c_str(),
                                pkg.operators.size() + pkg.gpu_operators.size(),
                                pkg.linked ? "  [linked]" : "");
                    if (list_pkg_verbose) {
                        std::printf("  scope: %s\n", pkg.source_scope.empty() ? "unknown" : pkg.source_scope.c_str());
                        std::printf("  path: %s\n", pkg.path.c_str());
                        if (!pkg.build_type.empty())
                            std::printf("  build: %s\n", pkg.build_type.c_str());
                    }
                    if (!pkg.vivid_core.empty())
                        std::printf("  vivid_core: %s\n", pkg.vivid_core.c_str());
                    if (!pkg.description.empty())
                        std::printf("  %s\n", pkg.description.c_str());
                    for (const auto& op : pkg.operators)
                        std::printf("    %s\n", op.c_str());
                    for (const auto& op : pkg.gpu_operators)
                        std::printf("    %s (gpu)\n", op.c_str());
                }
            }
            std::printf("Tip: run `vivid package-check-updates` to check for package updates.\n");
            return 0;
        } else if (link_cmd->parsed()) {
            auto result = pm.link(link_path);
            if (cli_json) {
                if (!result.success) return emit_json(json_err(result.error));
                nlohmann::json res = {
                    {"name", result.info.name},
                    {"version", result.info.version},
                    {"operator_count", static_cast<int64_t>(result.info.operators.size() + result.info.gpu_operators.size())},
                    {"linked", true}
                };
                if (!result.info.vivid_core.empty())
                    res["vivid_core"] = result.info.vivid_core;
                return emit_json(json_ok(std::move(res)));
            }
            if (result.success) {
                std::fprintf(stderr, "Linked %s v%s (%zu operators)\n",
                             result.info.name.c_str(), result.info.version.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            }
            std::fprintf(stderr, "Link failed: %s\n", result.error.c_str());
            for (const auto& cr : result.compile_results) {
                if (!cr.success)
                    std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                 cr.error_output.c_str());
            }
            return 1;
        } else if (unlink_cmd->parsed()) {
            const bool ok = pm.unlink(unlink_name);
            if (cli_json)
                return emit_json(ok ? json_ok_msg("unlinked") : json_err("failed to unlink package"));
            if (ok) {
                std::fprintf(stderr, "Unlinked %s\n", unlink_name.c_str());
                return 0;
            }
            std::fprintf(stderr, "Failed to unlink %s\n", unlink_name.c_str());
            return 1;
        } else if (rebuild_cmd->parsed()) {
            auto result = pm.rebuild(rebuild_name);
            if (cli_json) {
                if (!result.success) return emit_json(json_err(result.error));
                nlohmann::json res = {
                    {"name", result.info.name},
                    {"operator_count", static_cast<int64_t>(result.info.operators.size() + result.info.gpu_operators.size())},
                    {"linked", result.info.linked}
                };
                return emit_json(json_ok(std::move(res)));
            }
            if (result.success) {
                std::fprintf(stderr, "Rebuilt %s (%zu operators)\n",
                             result.info.name.c_str(),
                             result.info.operators.size() + result.info.gpu_operators.size());
                return 0;
            }
            std::fprintf(stderr, "Rebuild failed: %s\n", result.error.c_str());
            for (const auto& cr : result.compile_results) {
                if (!cr.success)
                    std::fprintf(stderr, "  %s: %s\n", cr.operator_name.c_str(),
                                 cr.error_output.c_str());
            }
            return 1;
        } else if (check_core_updates_cmd->parsed()) {
            vivid::AppUpdateManager updates(VIVID_CORE_VERSION);
            if (cli_json)
                return emit_json(handle_check_core_updates(&updates, nlohmann::json{{"force_refresh", check_core_force}}));

            if (check_core_force || updates.fetch_state() == vivid::AppUpdateFetchState::Idle)
                updates.refresh();
            for (int i = 0; i < 200; ++i) {
                auto st = updates.fetch_state();
                if (st != vivid::AppUpdateFetchState::Fetching) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto st = updates.fetch_state();
            auto info = updates.latest();
            if (st == vivid::AppUpdateFetchState::Error) {
                std::fprintf(stderr, "Core update check failed: %s\n",
                             updates.fetch_error().c_str());
                return 1;
            }

            std::printf("Core version: %s\n", VIVID_CORE_VERSION);
            std::printf("Appcast: %s\n", vivid::AppUpdateManager::appcast_url().c_str());
            if (info.latest_version.empty()) {
                std::printf("No update metadata available.\n");
            } else if (info.update_available) {
                std::printf("Update available: %s -> %s\n",
                            info.current_version.c_str(),
                            info.latest_version.c_str());
                if (!info.title.empty())
                    std::printf("Title: %s\n", info.title.c_str());
                if (!info.download_url.empty())
                    std::printf("Download: %s\n", info.download_url.c_str());
                if (!info.release_notes_url.empty())
                    std::printf("Release notes: %s\n", info.release_notes_url.c_str());
            } else {
                std::printf("Up to date (%s).\n",
                            info.current_version.empty() ? VIVID_CORE_VERSION : info.current_version.c_str());
            }
            return 0;
        } else if (check_updates_cmd->parsed()) {
            vivid::PackageCatalog catalog(pm);
            catalog.refresh();

            for (int i = 0; i < 200; ++i) {
                auto st = catalog.fetch_state();
                if (st != vivid::CatalogFetchState::Fetching) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            auto entries = catalog.entries();
            auto state = catalog.fetch_state();
            if (entries.empty() && state == vivid::CatalogFetchState::Error) {
                if (cli_json) return emit_json(json_err(catalog.fetch_error()));
                std::fprintf(stderr, "Update check failed: %s\n", catalog.fetch_error().c_str());
                return 1;
            }

            if (cli_json) {
                nlohmann::json root = {
                    {"core_version", update_core_version},
                    {"include_all_installed", update_include_all},
                };
                return emit_json(handle_check_package_updates(&catalog, &pm, root));
            }

            auto class_str = [](vivid::PackageUpdateClass c) -> const char* {
                switch (c) {
                    case vivid::PackageUpdateClass::UpToDate: return "up_to_date";
                    case vivid::PackageUpdateClass::CompatibleUpdate: return "compatible_update";
                    case vivid::PackageUpdateClass::IncompatibleUpdate: return "incompatible_update";
                    case vivid::PackageUpdateClass::RemoteOlderOrEqual: return "remote_older_or_equal";
                    case vivid::PackageUpdateClass::InvalidVersionData: return "invalid_version_data";
                    default: return "unknown";
                }
            };

            int installed_count = 0;
            int updates_available = 0;
            int incompatible_updates = 0;
            for (const auto& e : entries) {
                if (!e.installed) continue;
                installed_count++;

                vivid::PackageInfo installed;
                installed.name = e.name;
                installed.version = e.installed_version;
                auto a = vivid::PackageManager::assess_update(
                    installed, e.version, e.vivid_core, update_core_version);

                if (!update_include_all && !a.update_available) continue;

                std::printf("%s: installed=%s remote=%s class=%s compatible=%s\n",
                            a.package_name.c_str(),
                            a.installed_version.c_str(),
                            a.remote_version.c_str(),
                            class_str(a.classification),
                            a.compatible ? "yes" : "no");
                if (!a.remote_vivid_core.empty())
                    std::printf("  vivid_core: %s\n", a.remote_vivid_core.c_str());
                if (!a.message.empty())
                    std::printf("  %s\n", a.message.c_str());

                if (a.update_available) updates_available++;
                if (a.classification == vivid::PackageUpdateClass::IncompatibleUpdate)
                    incompatible_updates++;
            }

            if (installed_count == 0) {
                std::printf("No installed packages found in catalog.\n");
            } else if (!update_include_all && updates_available == 0) {
                std::printf("No package updates available.\n");
            }

            std::printf("Summary: installed=%d updates_available=%d incompatible_updates=%d core_version=%s\n",
                        installed_count, updates_available, incompatible_updates,
                        update_core_version.c_str());
            return 0;
        }
    }

    // --- GLFW ---
    if (!glfwInit()) {
        std::fprintf(stderr, "[vivid] Failed to init GLFW\n");
        return 1;
    }
    glfwSetMonitorCallback(monitor_callback);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    std::filesystem::path workspace_root;
    if (ensure_workspace_seeded(resources_dir, settings, workspace_root)) {
        vivid::save_settings(settings);
    }

    // Clamp saved window size to fit the primary monitor's work area
    {
        GLFWmonitor* primary = glfwGetPrimaryMonitor();
        if (primary) {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(primary, &mx, &my, &mw, &mh);
            if (settings.window_width > mw) settings.window_width = mw;
            if (settings.window_height > mh) settings.window_height = mh;
        }
    }

    GLFWwindow* window = glfwCreateWindow(settings.window_width, settings.window_height,
                                           "Vivid", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[vivid] Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    // Restore saved window position, validating it's on a visible monitor
    if (settings.window_x != -1 && settings.window_y != -1) {
        bool on_screen = false;
        int mon_count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&mon_count);
        for (int i = 0; i < mon_count; i++) {
            int mx, my, mw, mh;
            glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
            // Check that at least a 100x100 corner of the window is visible
            if (settings.window_x + 100 > mx && settings.window_x < mx + mw &&
                settings.window_y + 100 > my && settings.window_y < my + mh) {
                on_screen = true;
                break;
            }
        }
        if (on_screen) {
            glfwSetWindowPos(window, settings.window_x, settings.window_y);
        }
    }

    mi::DisplayState display_state;

    // --- Query physical framebuffer size and DPI scale ---
    int fb_width, fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    float xscale, yscale;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float dpi_scale = xscale; // on macOS, xscale == yscale
    std::fprintf(stderr, "[vivid] Framebuffer: %dx%d, DPI scale: %.1f\n",
                 fb_width, fb_height, dpi_scale);

    // --- GPU ---
    vivid::GpuContext gpu;
    if (!gpu.init(window, fb_width, fb_height)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    const double* clear = is_srgb_format(gpu.surface_format()) ? kClearLinear : kClearRaw;

    // --- Offscreen texture format (used by per-node GPU textures) ---
    static constexpr WGPUTextureFormat kOffscreenFormat = WGPUTextureFormat_RGBA16Float;

    // --- Fullscreen blit (per-node texture → surface) ---
    vivid::FullscreenBlit blit;
    if (!blit.init(gpu.device(), gpu.surface_format())) {
        std::fprintf(stderr, "[vivid] Failed to init FullscreenBlit\n");
        gpu.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // --- Output window (separate display for video_out) ---
    vivid::OutputWindow output_window;

    // --- Thumbnail cache + renderer ---
    // Scale thumbnail textures by DPI for crisp rendering on Retina displays.
    // Operators use logical coordinates (kThumbW x kThumbH); the Renderer2D
    // and ThumbnailRenderer handle the pixel scaling transparently.
    uint32_t thumb_tex_w = static_cast<uint32_t>(kThumbW * dpi_scale);
    uint32_t thumb_tex_h = static_cast<uint32_t>(kThumbH * dpi_scale);
    vivid::ui::ThumbnailCache thumb_cache;
    thumb_cache.init(gpu.device(), gpu.queue(), thumb_tex_w, thumb_tex_h);

    // Separate blit pipeline for offscreen→thumbnail (targets RGBA16Float, not surface format)
    vivid::FullscreenBlit thumb_blit;
    if (!thumb_blit.init(gpu.device(), kOffscreenFormat)) {
        std::fprintf(stderr, "[vivid] Failed to init thumbnail blit\n");
    }

    vivid::ui::ThumbnailRenderer thumb_renderer;
    bool thumb_renderer_ok = thumb_renderer.init(gpu.device(), gpu.queue(), gpu.surface_format());

    // --- Text renderer (initialized early for splash screen) ---
    vivid::ui::Renderer2D text_renderer;
    bool text_renderer_ok = false;
    {
        std::string font_path = (resources_dir / "JetBrainsMono-Regular.ttf").string();
        if (!std::filesystem::exists(font_path)) {
            auto alt = exe_dir.parent_path() / "fonts" / "JetBrainsMono-Regular.ttf";
            if (std::filesystem::exists(alt)) font_path = alt.string();
        }
        if (text_renderer.init(gpu.device(), gpu.surface_format(), font_path.c_str(), 16.0f, dpi_scale)) {
            text_renderer_ok = true;
        } else {
            std::fprintf(stderr, "[vivid] Text renderer disabled (font not found)\n");
        }
    }

    // --- Animated splash screen shader pipeline ---
    // Subtle dark animated background with slow-moving noise/gradient.
    // Random seed so the nebula pattern differs each launch.
    std::random_device rd;
    const float splash_seed = std::uniform_real_distribution<float>(0.0f, 100.0f)(rd);
    WGPURenderPipeline splash_pipeline = nullptr;
    WGPUBindGroup splash_bind_group = nullptr;
    WGPUBuffer splash_uniform_buf = nullptr;
    {
        // Fragment shader: animated nebula-like background
        static constexpr const char* kSplashFragSrc = R"(
@group(0) @binding(0) var<uniform> u: vec4f; // x=time, y=aspect, z=seed

// Logo V geometry: 5 nodes, 4 edges (normalized 0–1 coords)
const NODE0 = vec2f(0.300, 0.250);
const NODE1 = vec2f(0.380, 0.460);
const NODE2 = vec2f(0.500, 0.780);
const NODE3 = vec2f(0.620, 0.460);
const NODE4 = vec2f(0.700, 0.250);
const NODE_R = array<f32, 5>(0.028, 0.022, 0.030, 0.022, 0.028);

fn hash(p: vec2f) -> f32 {
    var h = dot(p, vec2f(127.1, 311.7));
    return fract(sin(h) * 43758.5453123);
}

fn noise(p: vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i + vec2f(0.0, 0.0)), hash(i + vec2f(1.0, 0.0)), u.x),
               mix(hash(i + vec2f(0.0, 1.0)), hash(i + vec2f(1.0, 1.0)), u.x), u.y);
}

fn fbm(p_in: vec2f) -> f32 {
    var p = p_in;
    var v = 0.0;
    var a = 0.5;
    let shift = vec2f(100.0);
    let rot = mat2x2f(cos(0.5), sin(0.5), -sin(0.5), cos(0.5));
    for (var i = 0; i < 5; i++) {
        v += a * noise(p);
        p = rot * p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// Distance from point p to segment a→b. Returns (distance, t along segment).
fn seg_dist(p: vec2f, a: vec2f, b: vec2f) -> vec2f {
    let ab = b - a;
    let t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
    let closest = a + ab * t;
    return vec2f(length(p - closest), t);
}

// Logo edge layer: flowing noise textured along each edge, masked by distance.
// Returns (brightness, min_dist_to_edge).
fn logo_edges(p: vec2f, time: f32) -> vec2f {
    let nodes = array<vec2f, 5>(NODE0, NODE1, NODE2, NODE3, NODE4);
    // Cumulative t offsets so flow is continuous across the whole V path
    let t_offsets = array<f32, 4>(0.0, 1.0, 2.0, 3.0);
    let edge_half_width = 0.028;
    let soft_edge = 0.040;

    var brightness = 0.0;
    var min_d = 1.0;

    for (var i = 0; i < 4; i++) {
        let a = nodes[i];
        let b = nodes[i + 1];
        let sd = seg_dist(p, a, b);
        let d = sd.x;
        let t = sd.y;
        min_d = min(min_d, d);

        // Soft mask from edge centerline
        let mask = smoothstep(edge_half_width + soft_edge, edge_half_width * 0.3, d);

        // Flow coordinate: position along edge scrolls with time
        let flow_coord = (t_offsets[i] + t) * 4.0 - time * 0.35;
        let cross_coord = d * 20.0;
        let flow_noise = fbm(vec2f(flow_coord, cross_coord) + vec2f(f32(i) * 7.3) + vec2f(u.z * 0.7));

        brightness = max(brightness, mask * flow_noise);
    }

    return vec2f(brightness, min_d);
}

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, true);
}

@fragment fn fs_main(in: FullscreenOutput) -> @location(0) vec4f {
    let t = u.x;
    let aspect = u.y;
    let seed = u.z;
    var uv = in.uv;
    uv.x *= aspect;

    // Seed-based offset so each launch looks different
    let seed_off = vec2f(seed, seed * 1.7 + 3.1);

    // --- Base nebula (original, untouched) ---
    let warp = vec2f(
        fbm(uv * 3.0 + vec2f(t * 0.08, t * 0.06) + seed_off),
        fbm(uv * 3.0 + vec2f(t * -0.05, t * 0.09) + vec2f(5.2, 1.3) + seed_off)
    );
    let n = fbm(uv * 2.0 + warp * 1.5 + vec2f(t * 0.02) + seed_off);

    let d = length(in.uv - vec2f(0.5));
    let vignette = 1.0 - smoothstep(0.1, 0.85, d);

    let deep    = vec3f(0.02, 0.02, 0.04);
    let blue    = vec3f(0.06, 0.10, 0.22);
    let purple  = vec3f(0.12, 0.06, 0.18);
    let teal    = vec3f(0.04, 0.14, 0.16);

    var color = deep;
    color = mix(color, blue,   smoothstep(0.25, 0.55, n) * vignette);
    color = mix(color, purple, smoothstep(0.45, 0.70, warp.x) * vignette * 0.6);
    color = mix(color, teal,   smoothstep(0.50, 0.75, warp.y) * vignette * 0.4);

    let wisp = smoothstep(0.62, 0.72, n) * vignette * vignette;
    color += vec3f(0.08, 0.10, 0.15) * wisp;

    // --- Logo V overlay (separate layer) ---
    let logo = logo_edges(in.uv, t);
    let edge_bright = logo.x;

    // Edge color: desaturated cyan-blue gradient along y
    let logo_cyan = vec3f(0.12, 0.40, 0.55);
    let logo_blue = vec3f(0.10, 0.30, 0.58);
    let edge_color = mix(logo_cyan, logo_blue, smoothstep(0.3, 0.7, in.uv.y));
    // Modulate edge brightness by background noise — nebula bleeds through
    let edge_modulated = edge_bright * mix(0.5, 1.0, n);
    color += edge_color * edge_modulated * vignette * 0.30;

    // Node glow spots — bright core + soft halo
    let node_positions = array<vec2f, 5>(NODE0, NODE1, NODE2, NODE3, NODE4);
    for (var i = 0; i < 5; i++) {
        let nd = length(in.uv - node_positions[i]);
        let r = NODE_R[i];
        let core = smoothstep(r, r * 0.2, nd);
        let halo = smoothstep(r * 4.0, r * 0.5, nd);
        let node_color = mix(logo_cyan, vec3f(0.20, 0.55, 0.68), core);
        color += node_color * (core * 0.20 + halo * 0.05) * vignette;
    }

    return vec4f(color, 1.0);
}
)";
        auto shader = vivid::gpu::create_shader(gpu.device(), kSplashFragSrc, "Splash Shader");
        if (shader) {
            // Uniform buffer: vec4f(time, aspect, 0, 0)
            splash_uniform_buf = vivid::gpu::create_uniform_buffer(gpu.device(), 16, "Splash Uniforms");

            // Bind group layout + pipeline layout
            WGPUBindGroupLayoutEntry bgl_entry{};
            bgl_entry.binding = 0;
            bgl_entry.visibility = WGPUShaderStage_Fragment;
            bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
            bgl_entry.buffer.minBindingSize = 16;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label = to_sv("Splash BGL");
            bgl_desc.entryCount = 1;
            bgl_desc.entries = &bgl_entry;
            auto bgl = wgpuDeviceCreateBindGroupLayout(gpu.device(), &bgl_desc);

            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label = to_sv("Splash PL");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts = &bgl;
            auto layout = wgpuDeviceCreatePipelineLayout(gpu.device(), &pl_desc);

            splash_pipeline = vivid::gpu::create_pipeline(
                gpu.device(), shader, layout, gpu.surface_format(), "Splash Pipeline");

            WGPUBindGroupEntry bg_entry{};
            bg_entry.binding = 0;
            bg_entry.buffer = splash_uniform_buf;
            bg_entry.size = 16;

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = to_sv("Splash BG");
            bg_desc.layout = bgl;
            bg_desc.entryCount = 1;
            bg_desc.entries = &bg_entry;
            splash_bind_group = wgpuDeviceCreateBindGroup(gpu.device(), &bg_desc);

            wgpuPipelineLayoutRelease(layout);
            wgpuBindGroupLayoutRelease(bgl);
            wgpuShaderModuleRelease(shader);
        }
    }

    // Splash screen state
    auto splash_start_time = std::chrono::steady_clock::now();

    // Render an animated splash frame with status text during blocking startup phases.
    auto render_splash_frame = [&](const char* status) {
        vivid::FrameState frame;
        if (!gpu.begin_frame(frame)) return;

        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        float wf = static_cast<float>(win_w);
        float hf = static_cast<float>(win_h);
        float aspect = (hf > 0.0f) ? wf / hf : 1.0f;

        // 1. Render animated background shader
        if (splash_pipeline && splash_bind_group) {
            float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - splash_start_time).count();
            float uniforms[4] = { elapsed, aspect, splash_seed, 0.0f };
            wgpuQueueWriteBuffer(gpu.queue(), splash_uniform_buf, 0, uniforms, 16);
            vivid::gpu::run_pass(frame.encoder, splash_pipeline, splash_bind_group,
                                 frame.view, "Splash BG");
        } else {
            emit_clear_pass(frame.encoder, frame.view, kClearLinear);
        }

        // 2. Overlay info panel and status text
        if (text_renderer_ok) {
            float cx = wf * 0.5f;
            float cy = hf * 0.5f;

            // Panel dimensions
            float panel_w = 320.0f;
            float panel_h = 160.0f;
            float panel_x = cx - panel_w * 0.5f;
            float panel_y = cy - panel_h * 0.5f;

            // Semi-transparent rounded panel
            text_renderer.draw_rounded_rect(panel_x, panel_y, panel_w, panel_h,
                                            8.0f, 0.0f, 0.0f, 0.0f, 0.55f);

            // Title
            float title_scale = 1.5f;
            const char* title = "Vivid";
            float tw = text_renderer.text_width(title, title_scale);
            text_renderer.draw_text(cx - tw * 0.5f, panel_y + 30.0f,
                                    title, 0.85f, 0.88f, 0.92f, 1.0f, title_scale);

            // Version
            float info_scale = 0.85f;
            float lh = text_renderer.line_height() * info_scale;
            char version_str[64];
            std::snprintf(version_str, sizeof(version_str), "v%s", VIVID_CORE_VERSION);
            float vw = text_renderer.text_width(version_str, info_scale);
            text_renderer.draw_text(cx - vw * 0.5f, panel_y + 62.0f,
                                    version_str, 0.5f, 0.53f, 0.58f, 0.8f, info_scale);

            // Copyright
            const char* copyright = "\xC2\xA9 2025-2026 Jeff Crouse";
            float cw = text_renderer.text_width(copyright, info_scale);
            text_renderer.draw_text(cx - cw * 0.5f, panel_y + 62.0f + lh + 4.0f,
                                    copyright, 0.4f, 0.42f, 0.45f, 0.6f, info_scale);

            // Subtle separator line
            float sep_y = panel_y + panel_h - 40.0f;
            text_renderer.draw_rect(panel_x + 20.0f, sep_y, panel_w - 40.0f, 1.0f,
                                    0.3f, 0.32f, 0.35f, 0.25f);

            // Status text (loading phase)
            float status_scale = 0.8f;
            float sw = text_renderer.text_width(status, status_scale);
            text_renderer.draw_text(cx - sw * 0.5f, sep_y + 10.0f,
                                    status, 0.45f, 0.48f, 0.52f, 0.7f, status_scale);

            text_renderer.flush(frame.encoder, frame.view,
                                static_cast<uint32_t>(win_w),
                                static_cast<uint32_t>(win_h));
        }

        gpu.end_frame(frame);
        glfwPollEvents();
    };

    // --- Load operator plugins ---
    // The progress callback renders a splash frame after each plugin probe,
    // keeping the animation smooth during the ~2s scan_deferred phase.
    vivid::OperatorRegistry registry;
    registry.set_progress_callback([&]() {
        render_splash_frame("Scanning operators...");
    });

    // --- Subgraph module registry ---
    vivid::SubgraphModuleRegistry subgraph_modules;
    {
        auto modules_dir = resources_dir / "modules";
        if (std::filesystem::is_directory(modules_dir))
            subgraph_modules.scan(modules_dir.string());
    }

    // --- Package management (needs to outlive main loop for catalog/install) ---
    vivid::PackageCompiler pkg_compiler(runtime_paths.source_dir, runtime_paths.build_dir);
    vivid::PackageManager pkg_manager(pkg_compiler, registry);
    auto build_console = std::make_shared<vivid::BuildConsole>();
    pkg_manager.set_build_console(build_console.get());

    // --- Asset library (import/index/cache for package and workspace assets) ---
    vivid::AssetLibrary asset_library;
    asset_library.set_workspace_root(workspace_root);
    asset_library.discover_workspace_assets(workspace_root);
    pkg_manager.set_asset_library(&asset_library);

    vivid::RegistryBootstrapOptions bootstrap_opts;
    bootstrap_opts.scan_factory_presets = true;
    bootstrap_opts.subgraph_modules = &subgraph_modules;
    {
        PhaseTimer t("bootstrap_operator_registry");
        vivid::bootstrap_operator_registry(registry, &pkg_manager, runtime_paths, bootstrap_opts);
    }
    // TEMP: hold splash screen until any key is pressed (for visual tuning)
    // if (screenshot_path.empty() && test_ui_script_path.empty() && test_dump_ui_state_path.empty()) {
    //     glfwSetKeyCallback(window, [](GLFWwindow* w, int, int, int action, int) {
    //         if (action == GLFW_PRESS) glfwSetWindowShouldClose(w, GLFW_TRUE);
    //     });
    //     while (!glfwWindowShouldClose(window)) {
    //         render_splash_frame("Press any key...");
    //     }
    //     glfwSetWindowShouldClose(window, GLFW_FALSE);
    //     glfwSetKeyCallback(window, nullptr);
    // }

    registry.set_progress_callback(nullptr);
    vivid::PackageCatalog pkg_catalog(pkg_manager);
    pkg_manager.set_resolver([&pkg_catalog](const std::string& name) -> std::string {
        for (const auto& e : pkg_catalog.entries())
            if (e.name == name) return e.url;
        return "";
    });
    // Non-blocking background fetch so update alerts can be shown without delaying startup.
    pkg_catalog.refresh();

    // --- Core app update checks (non-blocking appcast fetch) ---
    vivid::AppUpdateManager app_updates(VIVID_CORE_VERSION);
    app_updates.set_skipped_version(settings.core_update_skipped_version);
    if (settings.core_update_auto_check) {
        app_updates.refresh();
    }

    // --- Recursive graph discovery + graph-level meta ---
    std::filesystem::path bundle_graphs_root = resources_dir / "graphs";
    if (!std::filesystem::is_directory(bundle_graphs_root)) {
        // Compatibility fallback for older flat resource layout.
        bundle_graphs_root = resources_dir;
    }
    std::filesystem::path graphs_root = workspace_root / "graphs";
    if (!std::filesystem::is_directory(graphs_root)) {
        graphs_root = bundle_graphs_root;
    }
    std::vector<vivid::ExampleEntry> discovered_examples =
        discover_examples_with_packages(graphs_root, &pkg_manager);
    graph_file = resolve_graph_input_path(graph_file, graphs_root, discovered_examples);

    // --- Load graph ---
    vivid::Graph graph;
    vivid::RuntimeCore runtime;
    runtime.set_subgraph_modules(&subgraph_modules);
    runtime.frame_executor().set_analysis_enabled(settings.show_analysis);
    bool graph_loaded = false;

    mi::AsyncGraphLoadRequest initial_graph_request;
    bool have_initial_graph_request = false;
    if (mi::make_initial_graph_load_request(graph_file, resources_dir, initial_graph_request)) {
        have_initial_graph_request = true;
    } else if (graph_file.empty()) {
        std::fprintf(stderr, "[vivid] Error: could not load default graph template\n");
    }

    bool has_gpu_ops = graph_loaded && runtime.has_gpu_operators();

    // Allocate per-node GPU textures
    if (has_gpu_ops) {
        runtime.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
    }
    int video_out_idx = has_gpu_ops ? runtime.find_effective_gpu_sink() : -1;

    // --- Audio engine ---
    vivid::AudioEngine audio_engine;
    audio_engine.set_analysis_enabled(settings.show_analysis);
    bool has_audio = false;
    if (graph_loaded && runtime.has_audio_operators()) {
        PhaseTimer t("audio_engine build+start");
        if (audio_engine.build(runtime)) {
            if (audio_engine.start()) {
                has_audio = true;
            }
        }
    }

    // --- System MIDI listener (for MIDI mapping) ---
    vivid::SystemMidiListener system_midi;
    system_midi.open_all();  // listen on all available MIDI ports

    // --- RuntimeAPI ---
    vivid::RuntimeAPI runtime_api(graph, runtime, audio_engine, registry, &system_midi);
    runtime_api.set_resources_dir(resources_dir.string());
    runtime_api.set_subgraph_modules(&subgraph_modules);

    // --- Control server (MCP HTTP bridge) ---
    vivid::CaptureCoordinator capture_coordinator;
    if (has_audio) capture_coordinator.set_audio_engine(&audio_engine);
    capture_coordinator.set_runtime_api(&runtime_api);
    vivid::ControlServer control_server;
    control_server.set_capture_coordinator(&capture_coordinator);
    control_server.set_package_manager(&pkg_manager);
    control_server.set_package_compiler(&pkg_compiler);
    control_server.set_package_catalog(&pkg_catalog);
    control_server.set_app_update_manager(&app_updates);
    control_server.set_settings(&settings);
    control_server.set_audio_engine(&audio_engine);
    control_server.set_asset_library(&asset_library);
    control_server.set_build_console(build_console.get());
    control_server.set_bundled_source_dir((resources_dir / "source").string());
    if (!control_server.start(9876)) {
        std::fprintf(stderr, "[vivid] Control server unavailable (port 9876 in use?)\n");
    }
    if (!runtime_paths.source_dir.empty())
        control_server.set_src_dir(runtime_paths.source_dir);

    // (text_renderer was initialized earlier for the loading screen)

    // Thumbnail 2D renderer — targets RGBA16Float thumbnail textures.
    // Operators can use ctx->draw for simple 2D shapes instead of custom GPU pipelines.
    vivid::ui::Renderer2D thumb_draw_renderer;
    bool thumb_draw_renderer_ok = false;
    if (text_renderer_ok) {
        std::string font_path = (resources_dir / "JetBrainsMono-Regular.ttf").string();
        if (!std::filesystem::exists(font_path)) {
            auto alt = exe_dir.parent_path() / "fonts" / "JetBrainsMono-Regular.ttf";
            if (std::filesystem::exists(alt)) font_path = alt.string();
        }
        if (thumb_draw_renderer.init(gpu.device(), WGPUTextureFormat_RGBA16Float,
                                      font_path.c_str(), 16.0f, dpi_scale)) {
            thumb_draw_renderer_ok = true;
        }
    }

    RuntimeCommandSink command_sink(runtime_api);
    OperatorInfoCache op_info_cache;
    command_sink.set_registry(&registry);
    command_sink.set_graph(&graph);
    command_sink.set_op_cache(&op_info_cache);
    command_sink.set_settings(&settings);
    command_sink.set_capture_coordinator(&capture_coordinator);
    command_sink.set_runtime_flags(&has_gpu_ops, &has_audio);
    command_sink.set_package_manager(&pkg_manager);
    command_sink.set_subgraph_modules(&subgraph_modules);
    command_sink.set_build_console(build_console.get());
    vivid::ui::NodeGraphUI graph_ui(command_sink);
    graph_ui.set_build_console(build_console);
    graph_ui.set_dpi_scale(dpi_scale);
    graph_ui.set_bezier_wires(settings.bezier_wires);
    graph_ui.set_show_param_wires(settings.show_param_wires);
    graph_ui.set_pan_gesture(settings.pan_gesture);
    bool screenshot_select_applied = screenshot_select_node.empty();
    bool screenshot_select_warned = false;
    {
        // Resolve mcp/ directory: <bundle>/Contents/Resources/mcp or <exe_dir>/mcp
        auto mcp_dir = resources_dir / "mcp";
        graph_ui.set_mcp_dir(mcp_dir.string());
    }
    mi::MainAppContext app_ctx{
        graph,
        runtime,
        audio_engine,
        registry,
        runtime_api,
        command_sink,
        capture_coordinator,
        settings,
        graph_ui,
        thumb_cache,
        gpu,
        pkg_manager,
        pkg_catalog,
        app_updates,
        control_server,
        system_midi,
        subgraph_modules,
        build_console,
        resources_dir,
        exe_dir,
        graphs_root,
        discovered_examples,
        graph_loaded,
        has_gpu_ops,
        has_audio,
        video_out_idx,
    };

    mi::AsyncAddCoordinator async_add_coordinator;
    mi::AsyncGraphLoadCoordinator async_graph_load_coordinator;
    mi::PackageBrowserState package_browser_state;
    graph_ui.set_async_add_callback(
        [&](const vivid::ui::NodeGraphUI::AsyncAddOperatorRequest& request, std::string& error) {
            if (!graph_loaded || !runtime.compiled_graph()) {
                error = "runtime is not ready for adding operators";
                return false;
            }
            if (operator_preparation_service().has_graph_affecting_task()) {
                error = "another graph transaction is already running";
                return false;
            }
            return async_add_coordinator.begin(request, graph, runtime, registry, error);
        });
    auto queue_graph_load = [&](mi::AsyncGraphLoadRequest request, std::string* error = nullptr) -> bool {
        if (async_add_coordinator.active() || async_graph_load_coordinator.active() ||
            operator_preparation_service().has_graph_affecting_task()) {
            if (error) *error = "another graph transaction is already running";
            return false;
        }
        request.resolved_path =
            resolve_graph_input_path(request.requested_path, graphs_root, discovered_examples);
        if (request.display_name.empty()) {
            auto name = std::filesystem::path(request.resolved_path).filename().string();
            request.display_name = name.empty() ? request.resolved_path : name;
        }

        auto preserved_state =
            runtime_api.capture_preserved_runtime_state_for_path(request.clear_source_path ? "" : request.resolved_path);
        std::string begin_error;
        if (!async_graph_load_coordinator.begin(
                request, pkg_manager.list(), preserved_state, runtime, registry, begin_error)) {
            if (error) *error = begin_error;
            return false;
        }
        graph_ui.begin_async_graph_load(request.display_name);
        if (error) error->clear();
        return true;
    };
    mi::refresh_package_browser_entries_cache(app_ctx, package_browser_state);
    mi::configure_package_browser(app_ctx, package_browser_state);
    graph_ui.set_asset_browser_callbacks({
        [&asset_library]() {
            asset_library.refresh();
        },
        [&asset_library](const std::string& kind_name) {
            std::vector<vivid::AssetBrowserEntry> out;
            std::optional<vivid::AssetKind> kind;
            if (!kind_name.empty())
                kind = vivid::parse_asset_kind(kind_name);
            for (const auto& entry : asset_library.list(kind)) {
                vivid::AssetBrowserEntry item;
                item.asset_id = entry.asset_id;
                item.kind = vivid::asset_kind_str(entry.kind);
                item.display_name = entry.display_name;
                item.scope = vivid::asset_scope_str(entry.scope);
                item.package_name = entry.package_name;
                item.canonical_path = entry.canonical_path;
                item.relative_path = entry.relative_path;
                item.file_format = entry.file_format;
                out.push_back(std::move(item));
            }
            return out;
        },
        [&asset_library](const std::string& kind_name, const std::string& source_path,
                         vivid::AssetBrowserEntry& out_entry, std::string& error) {
            auto kind = vivid::parse_asset_kind(kind_name);
            if (!kind) {
                error = "Unsupported asset kind: " + kind_name;
                return false;
            }
            auto result = asset_library.import_asset(*kind, source_path);
            if (!result.ok) {
                error = result.error;
                return false;
            }
            out_entry.asset_id = result.entry.asset_id;
            out_entry.kind = vivid::asset_kind_str(result.entry.kind);
            out_entry.display_name = result.entry.display_name;
            out_entry.scope = vivid::asset_scope_str(result.entry.scope);
            out_entry.package_name = result.entry.package_name;
            out_entry.canonical_path = result.entry.canonical_path;
            out_entry.relative_path = result.entry.relative_path;
            out_entry.file_format = result.entry.file_format;
            error.clear();
            return true;
        }
    });
    graph_ui.set_examples(discovered_examples);
    graph_ui.set_example_package_checker(
        [&pkg_manager](const std::vector<std::string>& requires, std::string& missing) {
            for (const auto& pkg : requires) {
                if (!pkg.empty() && !pkg_manager.is_installed(pkg)) {
                    missing = pkg;
                    return false;
                }
            }
            missing.clear();
            return true;
        });
    graph_ui.set_core_update_notice_callbacks(
        [&]() {
#ifdef __APPLE__
            std::string err;
            if (!vivid::SparkleBridge::check_for_updates(&err)) {
                auto info = app_updates.latest();
                if (!info.download_url.empty()) {
                    if (!vivid::open_url(info.download_url, &err)) {
                        std::fprintf(stderr, "[vivid] Update install fallback failed: %s\n", err.c_str());
                    }
                } else {
                    std::fprintf(stderr, "[vivid] Sparkle unavailable: %s\n", err.c_str());
                }
            }
#else
            auto info = app_updates.latest();
            std::string err;
            if (!info.download_url.empty() && !vivid::open_url(info.download_url, &err)) {
                std::fprintf(stderr, "[vivid] Update install failed: %s\n", err.c_str());
            }
#endif
        },
        [&]() {
            auto info = app_updates.latest();
            settings.core_update_skipped_version = info.latest_version;
            app_updates.set_skipped_version(info.latest_version);
            vivid::save_settings(settings);
        },
        [&]() {});
    if (graph.has_viewport())
        graph_ui.set_viewport(graph.viewport_pan_x, graph.viewport_pan_y, graph.viewport_zoom);

    // Detect available text editors and set up style options
    {
        auto detected = vivid::detect_editors();
        std::vector<std::string> editor_names, editor_ids;
        int editor_sel = 0;
        for (size_t i = 0; i < detected.size(); ++i) {
            editor_names.push_back(detected[i].name);
            editor_ids.push_back(detected[i].app_id);
            if (detected[i].app_id == settings.editor)
                editor_sel = static_cast<int>(i);
        }
        // If editor is "custom", select that
        if (settings.editor == "custom") {
            for (size_t i = 0; i < editor_ids.size(); ++i) {
                if (editor_ids[i] == "custom") { editor_sel = static_cast<int>(i); break; }
            }
        }
        graph_ui.set_editor_options(std::move(editor_names), std::move(editor_ids),
                                    editor_sel, settings.editor_command);

        vivid::ui::ensure_default_themes();
        auto themes = vivid::ui::discover_themes();
        auto styles = vivid::ui::load_all_themes(themes);
        int style_sel = 0;
        for (size_t i = 0; i < styles.size(); ++i) {
            if (styles[i].id == settings.style_id)
                style_sel = static_cast<int>(i);
        }
        graph_ui.set_style_options(std::move(styles), style_sel, std::move(themes));
    }

    // Wire up custom inspector callback
    graph_ui.set_custom_inspector_callback(
        [&runtime](const std::string& node_id, VividInspectorContext* ctx) {
            const auto* cn = runtime.compiled_graph() ? runtime.compiled_graph()->find_node(node_id) : nullptr;
            if (!cn || !cn->loader || !cn->instance) return;
            cn->loader->draw_inspector(cn->instance, ctx);
        });

    // Set up GLFW input callbacks
    WindowUserData window_user_data;
    window_user_data.graph_ui = &graph_ui;
    window_user_data.runtime_api = &runtime_api;
    window_user_data.graph = &graph;
    window_user_data.settings = &settings;
    glfwSetWindowUserPointer(window, &window_user_data);
    glfwSetCharCallback(window, char_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetDropCallback(window, drop_callback);

    // --- Hot-reload ---
    vivid::FileWatcher file_watcher;
    vivid::HotReloader hot_reloader;
    bool hot_reload_enabled = false;
    auto next_package_watch_rescan_at = std::chrono::steady_clock::time_point{};
    auto refresh_package_watches = [&]() {
        int watched_pkg_files = 0;
        for (const auto& pkg : pkg_manager.list()) {
            watched_pkg_files += add_watch_for_resolved_package(file_watcher, pkg);
        }
        if (watched_pkg_files > 0) {
            std::fprintf(stderr, "[vivid] FileWatcher: watching %d package files (rescan)\n",
                         watched_pkg_files);
        }
    };
    {
        if (src_dir.empty()) {
            auto probe = exe_dir;
            for (int i = 0; i < 5 && probe.has_parent_path(); ++i) {
                probe = probe.parent_path();
                if (std::filesystem::exists(probe / "operators")) {
                    src_dir = probe.string();
                    break;
                }
            }
        }

        if (!src_dir.empty()) {
            std::string operators_dir = src_dir + "/operators";
            runtime.set_operators_src_dir(operators_dir);
            command_sink.set_operators_dir(operators_dir);
            command_sink.set_build_dir(runtime_paths.build_dir);
            op_info_cache.set_operators_dir(operators_dir);
            if (file_watcher.start(operators_dir) && hot_reloader.start(runtime_paths.build_dir)) {
                hot_reload_enabled = true;
                hot_reloader.set_build_console(build_console.get());
                control_server.set_hot_reloader(&hot_reloader);
                command_sink.set_hot_reloader(&hot_reloader);
                command_sink.set_shader_watch_callback(
                    [&file_watcher](const std::string& shader_path) {
                        file_watcher.add_watch(shader_path, "shader:" + shader_path);
                    });
                std::fprintf(stderr, "[vivid] Hot-reload enabled (watching %s)\n", operators_dir.c_str());

                file_watcher.add_shader_operator_watches(src_dir + "/filters");
                file_watcher.add_shader_operator_watches(mi::derive_project_shader_dir(graph));

                // Also watch package source files (both operators/ and src/ layouts).
                refresh_package_watches();
                next_package_watch_rescan_at = std::chrono::steady_clock::now() + std::chrono::seconds(1);

                // Set up package compile callback for hot-reloader
                std::string pkg_src_dir = src_dir;
                std::string pkg_build_dir = runtime_paths.build_dir;
                hot_reloader.set_package_compiler(
                    [&pkg_manager, pkg_src_dir, pkg_build_dir, build_console](const std::string& target) -> vivid::ReloadResult {
                        // Parse "pkg:<package_name>:<operator_name>"
                        vivid::ReloadResult result;
                        result.target_name = target;

                        auto first_colon = target.find(':');
                        auto second_colon = target.find(':', first_colon + 1);
                        if (first_colon == std::string::npos || second_colon == std::string::npos) {
                            result.success = false;
                            result.error_output = "Invalid package target: " + target;
                            return result;
                        }

                        std::string pkg_name = target.substr(first_colon + 1, second_colon - first_colon - 1);
                        std::string op_name = target.substr(second_colon + 1);
                        std::string pkg_dir = pkg_manager.resolve_package_path(pkg_name);
                        if (pkg_dir.empty()) {
                            result.success = false;
                            result.error_output = "Cannot resolve active package path for " + pkg_name;
                            return result;
                        }

                        // CMake src/ layout package (modern sibling package flow)
                        // supports hot-reload by building the package target directly.
                        std::filesystem::path src_cpp = std::filesystem::path(pkg_dir) / "src" / (op_name + ".cpp");
                        if (std::filesystem::exists(src_cpp)) {
                            auto quote = [](const std::string& s) { return "'" + s + "'"; };
                            std::string pkg_build = pkg_dir + "/build";
                            vivid::BuildTaskId configure_task = 0;

                            // Ensure build directory exists/configured.
                            if (!std::filesystem::exists(std::filesystem::path(pkg_build) / "CMakeCache.txt")) {
                                std::filesystem::create_directories(pkg_build);
                                configure_task = build_console->begin_task(
                                    vivid::BuildTaskKind::PackageConfigure,
                                    pkg_name + ":" + op_name);
                                std::string cfg_cmd = "cmake"
                                    " -B " + quote(pkg_build) +
                                    " -S " + quote(pkg_dir) +
                                    " -DVIVID_SRC_DIR=" + quote(pkg_src_dir) +
                                    " -DVIVID_BUILD_DIR=" + quote(pkg_build_dir) +
                                    " -DVIVID_PLUGIN_SUFFIX=" + std::string(vivid::kPluginSuffix) +
                                    " 2>&1";
                                std::string cfg_out;
                                FILE* cfg_pipe = popen(cfg_cmd.c_str(), "r");
                                if (!cfg_pipe) {
                                    result.success = false;
                                    result.error_output = "Failed to execute cmake configure for package target";
                                    build_console->append_system_line(configure_task, result.error_output);
                                    build_console->finish_task(configure_task, vivid::BuildTaskState::Failed, "launch failed");
                                    return result;
                                }
                                std::array<char, 256> cfg_buf;
                                while (fgets(cfg_buf.data(), cfg_buf.size(), cfg_pipe) != nullptr) {
                                    cfg_out += cfg_buf.data();
                                    build_console->append_line(configure_task, vivid::BuildConsoleStreamKind::Stdout, cfg_buf.data());
                                }
                                int cfg_status = pclose(cfg_pipe);
                                if (cfg_status != 0) {
                                    result.success = false;
                                    result.error_output = "cmake configure failed:\n" + cfg_out;
                                    build_console->finish_task(configure_task, vivid::BuildTaskState::Failed,
                                                               "failed (exit " + std::to_string(cfg_status) + ")");
                                    return result;
                                }
                                build_console->finish_task(configure_task, vivid::BuildTaskState::Succeeded, "configured");
                            }

                            vivid::BuildTaskId build_task = build_console->begin_task(
                                vivid::BuildTaskKind::PackageBuild,
                                pkg_name + ":" + op_name);
                            std::string build_cmd = "cmake --build " + quote(pkg_build) +
                                                    " --target " + quote(op_name) + " 2>&1";
                            std::string build_out;
                            FILE* pipe = popen(build_cmd.c_str(), "r");
                            if (!pipe) {
                                result.success = false;
                                result.error_output = "Failed to execute cmake build for package target";
                                build_console->append_system_line(build_task, result.error_output);
                                build_console->finish_task(build_task, vivid::BuildTaskState::Failed, "launch failed");
                                return result;
                            }
                            std::array<char, 256> buf;
                            while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
                                build_out += buf.data();
                                build_console->append_line(build_task, vivid::BuildConsoleStreamKind::Stdout, buf.data());
                            }
                            int status = pclose(pipe);
                            if (status != 0) {
                                result.success = false;
                                result.error_output = "cmake build failed:\n" + build_out;
                                build_console->finish_task(build_task, vivid::BuildTaskState::Failed,
                                                           "failed (exit " + std::to_string(status) + ")");
                                return result;
                            }
                            build_console->finish_task(build_task, vivid::BuildTaskState::Succeeded, "built");

                            result.success = true;
                            result.staged_dylib_path = pkg_build + "/" + op_name + vivid::kPluginSuffix;
                            return result;
                        }

                        // Legacy operators/<category>/<name>/ layout
                        vivid::PackageCompiler compiler(pkg_src_dir, pkg_build_dir);
                        std::string op_rel;
                        for (const auto& domain : {"audio", "control", "gpu"}) {
                            std::string candidate = pkg_dir + "/operators/" +
                                domain + "/" + op_name + "/" + op_name + ".cpp";
                            if (std::filesystem::exists(candidate)) {
                                op_rel = std::string(domain) + "/" + op_name;
                                break;
                            }
                        }
                        if (op_rel.empty()) {
                            result.success = false;
                            result.error_output = "Cannot find operator source for " + op_name + " in " + pkg_dir;
                            return result;
                        }

                        auto cr = compiler.compile_operator(pkg_dir, op_rel, false);
                        result.success = cr.success;
                        result.staged_dylib_path = cr.dylib_path;
                        result.error_output = cr.error_output;
                        return result;
                    });
            }
        } else {
            std::fprintf(stderr, "[vivid] Hot-reload disabled (operators/ not found; use --src-dir)\n");
        }
    }

    auto enter_fullscreen = [&](GLFWmonitor* preferred_monitor) {
        if (display_state.fullscreen) return;
        glfwGetWindowPos(window, &display_state.windowed_x, &display_state.windowed_y);
        glfwGetWindowSize(window, &display_state.windowed_w, &display_state.windowed_h);
        GLFWmonitor* monitor = preferred_monitor;
        if (!monitor_connected(monitor)) monitor = monitor_for_window(window);
        if (!monitor) monitor = glfwGetPrimaryMonitor();
        if (!monitor) return;
        int mx = 0, my = 0;
        glfwGetMonitorPos(monitor, &mx, &my);
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode) return;
        const int mw = mode->width;
        const int mh = mode->height;
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowPos(window, mx, my);
        glfwSetWindowSize(window, mw, mh);
#ifdef __APPLE__
        vivid::macos_set_presentation_fullscreen(true);
#endif
        display_state.fullscreen = true;
        display_state.fullscreen_monitor = monitor;
        display_state.surface_reconfigure_pending = true;
        display_state.surface_settle_frames = 2;
        std::fprintf(stderr, "[vivid] Fullscreen enabled (borderless %dx%d at %d,%d)\n",
                     mw, mh, mx, my);
    };

    auto exit_fullscreen = [&]() {
        if (!display_state.fullscreen) return;
        int x = display_state.windowed_x;
        int y = display_state.windowed_y;
        int w = display_state.windowed_w;
        int h = display_state.windowed_h;
        clamp_window_rect_to_monitor(glfwGetPrimaryMonitor(), &x, &y, &w, &h);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowPos(window, x, y);
        glfwSetWindowSize(window, w, h);
#ifdef __APPLE__
        vivid::macos_set_presentation_fullscreen(false);
#endif
        display_state.fullscreen = false;
        display_state.fullscreen_monitor = nullptr;
        display_state.surface_reconfigure_pending = true;
        display_state.surface_settle_frames = 2;
        std::fprintf(stderr, "[vivid] Fullscreen disabled (%dx%d at %d,%d)\n", w, h, x, y);
    };

    auto toggle_fullscreen = [&]() {
        if (display_state.fullscreen) {
            exit_fullscreen();
        } else {
            enter_fullscreen(monitor_for_window(window));
        }
    };

    auto request_graph_load = [&](mi::AsyncGraphLoadRequest request, const char* label) {
        std::string error;
        bool ok = queue_graph_load(std::move(request), &error);
        if (!ok) {
            std::fprintf(stderr, "[vivid] %s: %s\n", label, error.c_str());
            graph_ui.notify_async_graph_load_failure(error);
        }
        return ok;
    };

    auto new_graph_runtime = [&]() {
        auto result = runtime_api.new_graph(has_gpu_ops, has_audio);
        if (result.ok) {
            graph_loaded = true;
            command_sink.reset_undo_history();
        }
        std::fprintf(stderr, "[vivid] New Graph: %s\n", result.message.c_str());
        return result.ok;
    };

    vivid::FileDropRegistry file_drop_registry;
    auto refresh_file_drop_registry = [&]() {
        file_drop_registry.refresh(registry);
    };
    refresh_file_drop_registry();

    if (have_initial_graph_request) {
        std::string startup_error;
        if (!queue_graph_load(initial_graph_request, &startup_error)) {
            std::fprintf(stderr, "[vivid] Startup graph load: %s\n", startup_error.c_str());
            graph_ui.notify_async_graph_load_failure(startup_error);
        }
    }

    graph_ui.set_example_open_callback([&](const std::string& rel_path) {
        mi::AsyncGraphLoadRequest request;
        request.kind = mi::AsyncGraphLoadRequest::Kind::OpenExample;
        request.requested_path = rel_path;
        request.display_name = std::filesystem::path(rel_path).filename().string();
        request.update_recent_files = true;
        request_graph_load(std::move(request), "Open Example");
    });
    graph_ui.set_graph_meta_save_callback([&](const vivid::GraphMetaEditData& data,
                                              std::string& error) {
        if (!save_graph_meta_edit_data(data, error)) return false;
        mi::refresh_discovered_examples(app_ctx);
        return true;
    });
    mi::setup_save_confirm_callbacks(app_ctx);

    // --- macOS native menu bar ---
#ifdef __APPLE__
    mi::setup_macos_menu(app_ctx, runtime_paths, display_state, toggle_fullscreen,
                         request_graph_load, new_graph_runtime);
#endif

    double prev_time = glfwGetTime();
    uint64_t frame_count = 0;
    bool pkg_update_notice_done = false;
    bool core_update_notice_done = false;
    bool synthetic_drop_injected = false;
    vivid::UITestDumpState test_dump_state;
    bool test_dump_write_attempted = false;
#ifdef __APPLE__
    bool window_doc_edited = false;
#endif
    std::string window_title_graph_path;
    bool window_title_analysis = settings.show_analysis;
    vivid::reset_file_dialog_test_stats_runtime();

    // --- Main loop ---
    auto tick_frame = [&]() -> bool {
        // Guard against reentrancy from nested macOS run loops (e.g. drag-and-drop
        // spins a nested NSRunLoop that fires our CFRunLoopTimer again).
        struct ReentrancyGuard {
            bool& flag;
            bool was_reentrant;
            ReentrancyGuard(bool& f) : flag(f), was_reentrant(f) { flag = true; }
            ~ReentrancyGuard() { if (!was_reentrant) flag = false; }
        };
        static bool in_tick = false;
        ReentrancyGuard guard(in_tick);
        if (guard.was_reentrant) return true;

        // Close button may fire during macOS tracking (resize/menus).
        if (glfwWindowShouldClose(window)) return false;

        const std::string current_graph_path = graph.source_path();
        const bool current_analysis = runtime.frame_executor().analysis_enabled();
        if (current_graph_path != window_title_graph_path ||
            current_analysis != window_title_analysis) {
            refresh_window_title(window, current_graph_path, current_analysis);
            window_title_graph_path = current_graph_path;
            window_title_analysis = current_analysis;
        }

        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
        // Fullscreen state is managed by display_state (borderless fullscreen), not GLFW monitor mode.

        const uint64_t monitor_serial = g_monitor_topology_serial.load(std::memory_order_relaxed);
        if (display_state.seen_monitor_serial != monitor_serial) {
            display_state.seen_monitor_serial = monitor_serial;
            if (display_state.fullscreen) {
                if (!monitor_connected(display_state.fullscreen_monitor)) {
                    GLFWmonitor* fallback = glfwGetPrimaryMonitor();
                    if (!fallback) fallback = monitor_for_window(window);
                    if (fallback) {
                        int mx = 0, my = 0;
                        glfwGetMonitorPos(fallback, &mx, &my);
                        const GLFWvidmode* mode = glfwGetVideoMode(fallback);
                        if (!mode) return true;
                        const int mw = mode->width;
                        const int mh = mode->height;
                        glfwSetWindowPos(window, mx, my);
                        glfwSetWindowSize(window, mw, mh);
                        display_state.fullscreen_monitor = fallback;
                        display_state.surface_reconfigure_pending = true;
                        display_state.surface_settle_frames = 2;
                        std::fprintf(stderr, "[vivid] Rebound fullscreen to active monitor (%dx%d at %d,%d)\n",
                                     mw, mh, mx, my);
                    }
                }
            } else {
                int x = 0, y = 0, w = 0, h = 0;
                glfwGetWindowPos(window, &x, &y);
                glfwGetWindowSize(window, &w, &h);
                bool on_screen = false;
                int mon_count = 0;
                GLFWmonitor** monitors = glfwGetMonitors(&mon_count);
                for (int i = 0; i < mon_count; ++i) {
                    int mx = 0, my = 0, mw = 0, mh = 0;
                    glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
                    if (x + 100 > mx && x < mx + mw && y + 100 > my && y < my + mh) {
                        on_screen = true;
                        break;
                    }
                }
                if (!on_screen) {
                    clamp_window_rect_to_monitor(glfwGetPrimaryMonitor(), &x, &y, &w, &h);
                    glfwSetWindowPos(window, x, y);
                    glfwSetWindowSize(window, w, h);
                    display_state.surface_reconfigure_pending = true;
                    display_state.surface_settle_frames = 2;
                    std::fprintf(stderr, "[vivid] Repositioned window after display change (%dx%d at %d,%d)\n",
                                 w, h, x, y);
                }
            }
        }

        // Skip frame if minimized
        if (fb_w == 0 || fb_h == 0) return true;

        if (!test_drop_path.empty() &&
            !synthetic_drop_injected &&
            static_cast<int>(frame_count) >= test_drop_frame &&
            window_user_data.pending_drop_path.empty()) {
            std::filesystem::path drop_path(test_drop_path);
            if (!drop_path.is_absolute()) drop_path = std::filesystem::absolute(drop_path);
            if (!std::filesystem::exists(drop_path)) {
                std::fprintf(stderr,
                             "[vivid] Test drop fixture missing: %s\n",
                             drop_path.string().c_str());
            } else {
                float drop_x = static_cast<float>(win_w) * 0.5f;
                float drop_y = static_cast<float>(win_h) * 0.5f;
                if (test_drop_screen_pos.size() == 2) {
                    drop_x = static_cast<float>(test_drop_screen_pos[0]);
                    drop_y = static_cast<float>(test_drop_screen_pos[1]);
                }
                window_user_data.raw_mouse_x = drop_x;
                window_user_data.raw_mouse_y = drop_y;
                window_user_data.pending_drop_path = drop_path.string();
                std::fprintf(stderr,
                             "[vivid] Test drop injected: %s at (%.1f, %.1f) on frame %llu\n",
                             window_user_data.pending_drop_path.c_str(),
                             static_cast<double>(drop_x),
                             static_cast<double>(drop_y),
                             static_cast<unsigned long long>(frame_count));
            }
            synthetic_drop_injected = true;
        }

        // Handle drag-and-drop graph loading / file-to-operator creation
        if (!window_user_data.pending_drop_path.empty()) {
            std::string path = std::move(window_user_data.pending_drop_path);
            window_user_data.pending_drop_path.clear();
            std::string ext = std::filesystem::path(path).extension().string();
            for (auto& c : ext)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            if (ext == ".json") {
                mi::AsyncGraphLoadRequest request;
                request.kind = mi::AsyncGraphLoadRequest::Kind::DropGraph;
                request.requested_path = path;
                request.display_name = std::filesystem::path(path).filename().string();
                request.update_recent_files = true;
                request_graph_load(std::move(request), "Drop");
            } else {
                refresh_file_drop_registry();
                auto matches = file_drop_registry.matches_for_path(path);
                if (matches.empty()) {
                    std::fprintf(stderr, "[vivid] Drop: no operator registered for %s\n", path.c_str());
                } else {
                    float graph_x = 0.0f, graph_y = 0.0f;
                    if (!graph_ui.graph_position_for_screen(
                            static_cast<float>(window_user_data.raw_mouse_x),
                            static_cast<float>(window_user_data.raw_mouse_y),
                            graph_x, graph_y)) {
                        graph_ui.graph_center_position(graph_x, graph_y);
                    }

                    if (matches.size() == 1) {
                        mi::create_file_drop_node(graph_ui, matches.front(), path, graph_x, graph_y);
                    } else {
                        std::vector<vivid::ui::FileDropChooserAction> actions;
                        actions.reserve(matches.size());
                        for (const auto& match : matches) {
                            vivid::ui::FileDropChooserAction action;
                            action.label = match.label;
                            action.subtitle = match.package_name.empty()
                                ? match.type_name
                                : (match.type_name + "  [" + match.package_name + "]");
                            action.type_name = match.type_name;
                            action.file_param = match.file_param;
                            action.dropped_path = path;
                            actions.push_back(std::move(action));
                        }
                        graph_ui.open_file_drop_chooser(std::move(actions), graph_x, graph_y);
                    }
                }
            }
            // Suppress surface presentation for a few frames after a drop.
            // The drop callback fires during glfwPollEvents(), and macOS may
            // still be inside NSCoreDragReceiveMessageProc when our CFRunLoop
            // timer fires this tick.  The window surface can be transiently
            // invalid during drag-tracking runloops, so we avoid begin_frame /
            // end_frame (and the fatal wgpuQueueSubmit) until the surface
            // stabilises.  Reuses the same settle mechanism as resize/fullscreen.
            display_state.surface_settle_frames = std::max(display_state.surface_settle_frames, 3);
        }

        // Reconfigure GPU surface if framebuffer size changed.
        if (fb_w != fb_width || fb_h != fb_height) {
            fb_width = fb_w;
            fb_height = fb_h;
            gpu.resize(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
            display_state.surface_settle_frames = std::max(display_state.surface_settle_frames, 2);
            return true;
        }

        mi::AsyncAddPreparedResult async_add_result;
        if (async_add_coordinator.take_completed(async_add_result)) {
            if (async_add_result.success) {
                std::string added_node_id = async_add_result.node_id;
                graph_ui.set_async_add_stage(vivid::ui::NodeGraphUI::AsyncAddStage::Applying);
                mi::adopt_prepared_runtime_build(app_ctx, std::move(async_add_result));
                command_sink.capture_external_undo_snapshot();
                graph_ui.notify_async_add_success(added_node_id);
            } else {
                graph_ui.notify_async_add_failure(async_add_result.user_message);
            }
        } else if (async_add_coordinator.active()) {
            auto stage = async_add_coordinator.stage();
            graph_ui.set_async_add_stage(
                stage == mi::AsyncAddCoordinator::Stage::Compiling
                    ? vivid::ui::NodeGraphUI::AsyncAddStage::Compiling
                    : vivid::ui::NodeGraphUI::AsyncAddStage::Preparing);
        }

        mi::AsyncGraphLoadPreparedResult async_graph_load_result;
        if (async_graph_load_coordinator.take_completed(async_graph_load_result)) {
            if (async_graph_load_result.success) {
                const bool had_graph_before_commit = graph_loaded;
                graph_ui.set_async_graph_load_stage(
                    vivid::ui::NodeGraphUI::AsyncGraphLoadStage::Applying);
                mi::adopt_prepared_graph_load(app_ctx, std::move(async_graph_load_result));
                if (hot_reload_enabled)
                    file_watcher.add_shader_operator_watches(mi::derive_project_shader_dir(graph));
                if (!had_graph_before_commit && graph.has_viewport()) {
                    graph_ui.set_viewport(graph.viewport_pan_x, graph.viewport_pan_y, graph.viewport_zoom);
                }
                graph_ui.notify_async_graph_load_success();
            } else {
                graph_ui.notify_async_graph_load_failure(async_graph_load_result.user_message);
            }
        } else if (async_graph_load_coordinator.active()) {
            using GraphLoadStage = vivid::ui::NodeGraphUI::AsyncGraphLoadStage;
            auto stage = async_graph_load_coordinator.stage();
            GraphLoadStage ui_stage = GraphLoadStage::Loading;
            switch (stage) {
                case mi::AsyncGraphLoadCoordinator::Stage::Loading:
                    ui_stage = GraphLoadStage::Loading;
                    break;
                case mi::AsyncGraphLoadCoordinator::Stage::PreparingOperators:
                    ui_stage = GraphLoadStage::PreparingOperators;
                    break;
                case mi::AsyncGraphLoadCoordinator::Stage::Compiling:
                    ui_stage = GraphLoadStage::Compiling;
                    break;
                case mi::AsyncGraphLoadCoordinator::Stage::Idle:
                    ui_stage = GraphLoadStage::Loading;
                    break;
            }
            graph_ui.set_async_graph_load_stage(ui_stage);
        }

        const bool graph_transaction_active =
            async_add_coordinator.active() || async_graph_load_coordinator.active() ||
            operator_preparation_service().has_graph_affecting_task();

        // Drain control server requests (may set pending topology changes).
        // Keep the live graph stable while an async graph transaction is preparing.
        if (!graph_transaction_active) {
            control_server.process_requests(runtime_api, graph, runtime, registry,
                                            has_gpu_ops, has_audio);
        }
        static uint64_t last_reload_serial = 0;
        if (runtime_api.reload_serial() != last_reload_serial) {
            last_reload_serial = runtime_api.reload_serial();
            if (!runtime_api.consume_preserve_undo_history_reload()) {
                command_sink.reset_undo_history();
            }
        }

        if (!graph_transaction_active && runtime_api.has_pending()) {
            runtime_api.apply_pending(has_gpu_ops, has_audio);
            // Re-allocate per-node GPU textures after topology change
            if (has_gpu_ops) {
                runtime.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
            }
            video_out_idx = has_gpu_ops ? runtime.find_effective_gpu_sink() : -1;
            capture_coordinator.set_audio_engine(has_audio ? &audio_engine : nullptr);
            // Evict thumbnail cache entries for removed nodes
            if (auto* cg_evict = runtime.compiled_graph()) {
                std::unordered_set<std::string> active_ids;
                for (const auto& cn : cg_evict->nodes)
                    active_ids.insert(cn.node_id);
                thumb_cache.retain_only(active_ids);
            }
        }
        // Handle GPU realloc after reload command or operator-requested resize
        if (runtime_api.needs_gpu_realloc() || runtime.needs_gpu_realloc()) {
            runtime_api.clear_gpu_realloc();
            runtime.clear_gpu_realloc();
            runtime.allocate_gpu_textures(gpu.device(), kDefaultTexW, kDefaultTexH, kOffscreenFormat);
            video_out_idx = has_gpu_ops ? runtime.find_effective_gpu_sink() : -1;
        }
        if (!graph_loaded && runtime.compiled_graph() && !runtime.compiled_graph()->nodes.empty()) {
            graph_loaded = true;
        }

#ifdef __APPLE__
        bool now_dirty = runtime_api.graph_dirty();
        if (now_dirty != window_doc_edited) {
            vivid::macos_set_document_edited(now_dirty);
            window_doc_edited = now_dirty;
        }
#endif

        // Drive output window from video_out "launch" param.
        if (has_gpu_ops && video_out_idx >= 0 && runtime.compiled_graph() &&
            static_cast<size_t>(video_out_idx) < runtime.compiled_graph()->nodes.size()) {
            const auto& vo_cn = runtime.compiled_graph()->nodes[video_out_idx];
            auto launch_it = vo_cn.param_indices.find("launch");
            if (launch_it != vo_cn.param_indices.end() &&
                launch_it->second < vo_cn.param_values.size()) {
                const bool want_launch = vo_cn.param_values[launch_it->second] >= 0.5f;
                int target = 0; // Current monitor
                auto dt_it = vo_cn.param_indices.find("display_target");
                if (dt_it != vo_cn.param_indices.end() &&
                    dt_it->second < vo_cn.param_values.size()) {
                    target = static_cast<int>(vo_cn.param_values[dt_it->second]);
                    if (target < 0) target = 0;
                    if (target > 2) target = 2;
                }

                if (want_launch) {
                    GLFWmonitor* target_monitor = monitor_for_target(target, window);
                    if (!output_window.is_open()) {
                        output_window.open(gpu.instance(), gpu.adapter(), gpu.device(), gpu.queue(), target_monitor);
                    } else if (target != display_state.sink_target &&
                               monitor_connected(target_monitor)) {
                        output_window.move_to_monitor(target_monitor);
                    }
                } else if (output_window.is_open()) {
                    output_window.close();
                }

                // Handle ESC / window close on output window
                if (output_window.should_close()) {
                    output_window.close();
                    if (auto* cg = runtime.compiled_graph()) {
                        auto& cn = cg->nodes[video_out_idx];
                        cn.param_values[launch_it->second] = 0.0f;
                        cn.dirty = true;
                    }
                }

                display_state.sink_target = target;
            }
        }

        if (display_state.surface_reconfigure_pending) {
            glfwGetWindowSize(window, &win_w, &win_h);
            glfwGetFramebufferSize(window, &fb_w, &fb_h);
            if (fb_w > 0 && fb_h > 0) {
                fb_width = fb_w;
                fb_height = fb_h;
                gpu.resize(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
            }
            display_state.surface_reconfigure_pending = false;
        }
        bool suppress_surface_frame = false;
        if (display_state.surface_settle_frames > 0) {
            suppress_surface_frame = true;
            display_state.surface_settle_frames--;
        }

        // --- Compute dt unconditionally (before GPU work) ---
        double now = glfwGetTime();
        double dt = now - prev_time;
        prev_time = now;
        graph_ui.set_dt(static_cast<float>(dt));

        // Non-intrusive startup update alert (logs once, never blocks startup).
        if (!pkg_update_notice_done) {
            auto state = pkg_catalog.fetch_state();
            if (state == vivid::CatalogFetchState::Ready) {
                auto summary = pkg_catalog.summarize_updates(VIVID_CORE_VERSION);
                if (summary.updates_available > 0) {
                    std::fprintf(stderr,
                        "[vivid] Package updates available: %d (%d incompatible). "
                        "Run `vivid package-check-updates` for details.\n",
                        summary.updates_available, summary.incompatible_updates);
                }
                pkg_update_notice_done = true;
            } else if (state == vivid::CatalogFetchState::Error) {
                std::fprintf(stderr, "[vivid] Package update check unavailable (non-fatal): %s\n",
                             pkg_catalog.fetch_error().c_str());
                pkg_update_notice_done = true;
            }
        }

        // Non-intrusive startup core update alert.
        if (!core_update_notice_done && settings.core_update_auto_check) {
            auto st = app_updates.fetch_state();
            if (st == vivid::AppUpdateFetchState::Ready) {
                auto info = app_updates.latest();
                settings.core_update_last_checked_at = now_epoch_seconds_str();
                if (info.update_available && !app_updates.is_skipped(info.latest_version)) {
                    std::fprintf(stderr,
                        "[vivid] Core update available: %s -> %s. "
                        "Use File -> Check for Updates... for installer flow.\n",
                        info.current_version.c_str(), info.latest_version.c_str());
                    graph_ui.show_core_update_notice(info.latest_version, info.title);
                }
                core_update_notice_done = true;
            } else if (st == vivid::AppUpdateFetchState::Error) {
                settings.core_update_last_checked_at = now_epoch_seconds_str();
                std::fprintf(stderr, "[vivid] Core update check unavailable (non-fatal): %s\n",
                             app_updates.fetch_error().c_str());
                core_update_notice_done = true;
            }
        }

        if (async_graph_load_coordinator.startup_active()) {
            render_splash_frame(async_graph_load_coordinator.stage_text());
            return true;
        }

        // --- Apply MIDI mappings (before tick so wire wins on conflict) ---
        runtime_api.apply_midi_mappings();

        // --- Tick quantized variation switching ---
        runtime_api.tick_quantized_switch();

        // --- Try to acquire surface texture for presentation ---
        vivid::FrameState frame;
        bool have_surface = !suppress_surface_frame && gpu.begin_frame(frame);

        // If no surface (e.g. during resize), create a standalone encoder
        // so offscreen GPU work (runtime tick, thumbnails) still runs.
        WGPUCommandEncoder tick_encoder = nullptr;
        if (have_surface) {
            tick_encoder = frame.encoder;
        } else {
            WGPUCommandEncoderDescriptor enc_desc{};
            enc_desc.label = to_sv("Offscreen Tick Encoder");
            tick_encoder = wgpuDeviceCreateCommandEncoder(gpu.device(), &enc_desc);
        }

        // --- Tick graph (always runs, even without a surface) ---
        if (graph_loaded) {

            // Base GPU state (per-node textures are set by runtime)
            VividGpuContext gpu_state{};
            gpu_state.device          = gpu.device();
            gpu_state.queue           = gpu.queue();
            gpu_state.command_encoder = tick_encoder;
            gpu_state.output_format   = kOffscreenFormat;

            // --- Hot-reload polling ---
            if (hot_reload_enabled && !graph_transaction_active) {
                auto now_scan = std::chrono::steady_clock::now();
                if (now_scan >= next_package_watch_rescan_at) {
                    refresh_package_watches();
                    next_package_watch_rescan_at = now_scan + std::chrono::seconds(1);
                }
                poll_hot_reload(file_watcher, hot_reloader, runtime, registry, runtime_api,
                                audio_engine, has_gpu_ops, has_audio, &op_info_cache,
                                runtime.operators_src_dir());
            }

            if (has_audio)
                runtime.pre_tick_audio_sync(now);

            // --- Build input state for operators (when UI hidden) ---
            const VividInputState* input_ptr = nullptr;
            VividInputState input_state{};
            if (!window_user_data.pending_events.empty() ||
                (window_user_data.buttons_held && !(graph_ui.visible()))) {
                // Compute inverse blit_fit transform: window coords → [0,1] texture UV
                float scale_x = 1.0f, scale_y = 1.0f;
                float offset_x = 0.0f, offset_y = 0.0f;
                if (has_gpu_ops && video_out_idx >= 0 && fb_width > 0 && fb_height > 0) {
                    uint32_t src_w = 0, src_h = 0;
                    runtime.gpu_sink_source_size(video_out_idx, src_w, src_h);
                    if (src_w > 0 && src_h > 0) {
                        const auto& vo_cn = runtime.compiled_graph()->nodes[video_out_idx];
                        auto fit_mode = vivid::FitMode::Fit;
                        auto fm_it = vo_cn.param_indices.find("fit_mode");
                        if (fm_it != vo_cn.param_indices.end() && fm_it->second < vo_cn.param_values.size())
                            fit_mode = static_cast<vivid::FitMode>(
                                static_cast<int>(vo_cn.param_values[fm_it->second]));

                        float src_aspect = static_cast<float>(src_w) / static_cast<float>(src_h);
                        float dst_aspect = static_cast<float>(fb_width) / static_cast<float>(fb_height);

                        if (fit_mode == vivid::FitMode::Stretch) {
                            scale_x = 1.0f; scale_y = 1.0f;
                        } else if (fit_mode == vivid::FitMode::Fit) {
                            if (src_aspect > dst_aspect) {
                                scale_x = 1.0f; scale_y = dst_aspect / src_aspect;
                            } else {
                                scale_x = src_aspect / dst_aspect; scale_y = 1.0f;
                            }
                        } else { // Fill
                            if (src_aspect > dst_aspect) {
                                scale_x = src_aspect / dst_aspect; scale_y = 1.0f;
                            } else {
                                scale_x = 1.0f; scale_y = dst_aspect / src_aspect;
                            }
                        }
                        offset_x = (1.0f - scale_x) * 0.5f;
                        offset_y = (1.0f - scale_y) * 0.5f;
                    }
                }

                // Normalize mouse coords in all pending events: window px → [0,1] texture UV
                // ndc = cursor_pos / win_size;  tex_uv = (ndc - offset) / scale
                float inv_w = (win_w > 0) ? 1.0f / static_cast<float>(win_w) : 0.0f;
                float inv_h = (win_h > 0) ? 1.0f / static_cast<float>(win_h) : 0.0f;
                float inv_sx = (scale_x > 0.0f) ? 1.0f / scale_x : 0.0f;
                float inv_sy = (scale_y > 0.0f) ? 1.0f / scale_y : 0.0f;

                for (auto& ev : window_user_data.pending_events) {
                    float ndc_x = ev.mouse_x * inv_w;
                    float ndc_y = ev.mouse_y * inv_h;
                    ev.mouse_x = (ndc_x - offset_x) * inv_sx;
                    ev.mouse_y = (ndc_y - offset_y) * inv_sy;
                }

                float cur_ndc_x = static_cast<float>(window_user_data.raw_mouse_x) * inv_w;
                float cur_ndc_y = static_cast<float>(window_user_data.raw_mouse_y) * inv_h;

                input_state.events = window_user_data.pending_events.data();
                input_state.event_count = static_cast<uint32_t>(window_user_data.pending_events.size());
                input_state.mouse_x = (cur_ndc_x - offset_x) * inv_sx;
                input_state.mouse_y = (cur_ndc_y - offset_y) * inv_sy;
                input_state.buttons_held = window_user_data.buttons_held;
                input_state.modifiers = window_user_data.current_mods;
                input_ptr = &input_state;
            }

            // Tick with thumbnail capture callback for GPU nodes
            runtime.tick(now, dt, frame_count, &gpu_state,
                [&](uint32_t, const std::string& node_id, WGPUTextureView node_tex_view) {
                    // Blit per-node texture → thumbnail (uses RGBA16Float pipeline)
                    if (!node_tex_view) return;
                    auto* thumb_view = thumb_cache.get_or_create(node_id);
                    if (thumb_view) {
                        thumb_blit.blit(tick_encoder, node_tex_view, thumb_view);
                    }
                },
                input_ptr);

            // Clear consumed input events
            window_user_data.pending_events.clear();

            draw_custom_thumbnails(runtime, thumb_cache, graph_ui,
                                   thumb_draw_renderer_ok ? &thumb_draw_renderer : nullptr,
                                   gpu.device(), gpu.queue(), tick_encoder,
                                   now, dt, frame_count,
                                   thumb_tex_w, thumb_tex_h,
                                   kThumbW, kThumbH,
                                   kOffscreenFormat);

            // --- Tick state-preset mappings (after runtime tick, state outputs are fresh) ---
            runtime_api.tick_state_presets();

            // NOTE: post_tick_audio_sync (push_to_audio) is deferred to the end
            // of tick_frame so that UI set_param writes from graph_ui.update()
            // are captured before the audio snapshot is published.  Without this,
            // the audio executor overwrites cn.param_values with the stale
            // snapshot every callback, reverting slider drags on audio-cadence
            // nodes (e.g. Clock, DrumKick).

            // Process capture/recording/analysis requests (after tick, textures are fresh)
            if (capture_coordinator.has_pending() || capture_coordinator.is_recording() ||
                capture_coordinator.has_pending_analyses()) {
                WGPUTexture cap_tex = nullptr;
                uint32_t cap_w = 0, cap_h = 0;
                if (has_gpu_ops && video_out_idx >= 0) {
                    // Find the source node's texture (upstream of video_out)
                    cap_tex = runtime.gpu_sink_source_texture(video_out_idx);
                    runtime.gpu_sink_source_size(video_out_idx, cap_w, cap_h);
                }
                if (capture_coordinator.has_pending())
                    capture_coordinator.process_pending(
                        gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
                if (capture_coordinator.is_recording())
                    capture_coordinator.tick_recording(
                        gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
                capture_coordinator.tick_analysis(
                    gpu.device(), gpu.queue(), cap_tex, cap_w, cap_h);
            }

            if (frame_count % 60 == 0) {
                std::fprintf(stderr, "[vivid] frame=%llu",
                    static_cast<unsigned long long>(frame_count));
                if (auto* cg_dbg = runtime.compiled_graph()) {
                for (const auto& cn : cg_dbg->nodes) {
                    for (const auto& [port_name, port_idx] : cn.output_port_indices) {
                        std::fprintf(stderr, " | %s/%s=%.4f",
                            cn.node_id.c_str(), port_name.c_str(),
                            cn.output_values[port_idx]);
                    }
                }
                }
                std::fprintf(stderr, "\n");
            }
            frame_count++;
        }

        if (have_surface) {
            // Poll async package action completion (main thread only).
            mi::poll_package_browser_actions(app_ctx, package_browser_state,
                                             graph_transaction_active);

            // --- Surface presentation path ---
            if (has_gpu_ops && video_out_idx >= 0) {
                // Find video_out's input texture from its resolved_tex_inputs
                const auto& vo_cn = runtime.compiled_graph()->nodes[video_out_idx];
                WGPUTextureView display_tex = nullptr;
                uint32_t src_w = 0, src_h = 0;
                if (vo_cn.gpu && !vo_cn.gpu->resolved_tex_inputs.empty()) {
                    display_tex = vo_cn.gpu->resolved_tex_inputs[0];
                    runtime.gpu_sink_source_size(video_out_idx, src_w, src_h);
                }

                if (display_tex && src_w > 0 && src_h > 0) {
                    auto fit_mode = vivid::FitMode::Fit;
                    auto fm_it = vo_cn.param_indices.find("fit_mode");
                    if (fm_it != vo_cn.param_indices.end() && fm_it->second < vo_cn.param_values.size())
                        fit_mode = static_cast<vivid::FitMode>(static_cast<int>(vo_cn.param_values[fm_it->second]));
                    bool ui_vis = graph_ui.visible();
                    blit.blit_fit(frame.encoder, display_tex, frame.view,
                                  src_w, src_h,
                                  static_cast<uint32_t>(fb_width),
                                  static_cast<uint32_t>(fb_height),
                                  fit_mode, ui_vis);

                    // Present to output window (separate surface, own encoder)
                    if (output_window.is_open()) {
                        output_window.present(display_tex, src_w, src_h, fit_mode);
                    }
                } else {
                    emit_clear_pass(frame.encoder, frame.view, clear);
                }

            } else {
                emit_clear_pass(frame.encoder, frame.view, clear);
            }

            const bool interface_capture_requested =
                capture_coordinator.has_pending_interface_capture() ||
                capture_coordinator.has_active_interface_capture();
            if (interface_capture_requested && !gpu.surface_supports_copy_src()) {
                capture_coordinator.fail_pending_interface_captures(
                    "surface does not support interface capture");
            }
            if (interface_capture_requested && !text_renderer_ok) {
                capture_coordinator.fail_pending_interface_captures(
                    "UI text renderer is unavailable");
            }

            // --- Node graph UI overlay (2-pass rendering) ---
            if (text_renderer_ok && (graph_ui.visible() || interface_capture_requested)) {
                if (graph_transaction_active) {
                    graph_ui.update_modal_only();
                } else {
                    auto snapshot = build_graph_snapshot(
                        graph, runtime, has_audio ? &audio_engine : nullptr,
                        registry, op_info_cache, &system_midi, &runtime_api,
                        &capture_coordinator, &control_server, &subgraph_modules);

                    if (!test_ui_script.actions.empty()) {
                        run_ui_test_script_frame(test_ui_script, graph_ui, window_user_data,
                                                 screenshot_path, screenshot_delay, frame_count);
                    }
                    graph_ui.update(snapshot);
                    if (!test_dump_ui_state_path.empty()) {
                        test_dump_state.final_state =
                            vivid::capture_ui_test_observed_state(snapshot, graph_ui);
                        test_dump_state.has_final_state = true;
                        for (const auto& label : test_ui_script.pending_checkpoint_labels) {
                            test_dump_state.checkpoints.push_back(
                                vivid::UITestCheckpointState{
                                    label,
                                    vivid::capture_ui_test_observed_state(snapshot, graph_ui),
                                });
                        }
                        test_ui_script.pending_checkpoint_labels.clear();
                    }
                    if (interface_capture_requested) {
                        capture_coordinator.prepare_pending_interface_capture(graph_ui);
                    }
                    if (!screenshot_select_applied && !screenshot_select_node.empty()) {
                        if (graph_ui.select_single_node_for_review(screenshot_select_node)) {
                            screenshot_select_applied = true;
                        } else if (!screenshot_select_warned) {
                            std::fprintf(stderr,
                                         "[vivid] --select-node could not find node id '%s' in the current graph yet\n",
                                         screenshot_select_node.c_str());
                            screenshot_select_warned = true;
                        }
                    }
                }
                if (graph_ui.visible()) {
                    graph_ui.draw(text_renderer, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                    // Pass 1: text/rects
                    text_renderer.flush(frame.encoder, frame.view, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                    // Pass 2: thumbnails (GPU auto-captured + CPU custom, composited over text)
                    if (thumb_renderer_ok) {
                        graph_ui.draw_thumbnails(thumb_renderer, thumb_cache,
                                                 frame.encoder, frame.view,
                                                 static_cast<uint32_t>(fb_width),
                                                 static_cast<uint32_t>(fb_height));
                    }
                    // Pass 3: overlays (context menu, dropdown) on top of thumbnails
                    graph_ui.draw_overlays(text_renderer);
                    text_renderer.flush(frame.encoder, frame.view, static_cast<uint32_t>(win_w), static_cast<uint32_t>(win_h));
                }
            }

            bool frame_already_submitted = false;
            if (capture_coordinator.has_active_interface_capture()) {
                SurfaceCaptureResult interface_capture;
                std::string capture_error;
                if (capture_surface_png(gpu, frame, fb_width, fb_height,
                                        interface_capture, capture_error)) {
                    capture_coordinator.complete_active_interface_capture(
                        interface_capture.width, interface_capture.height, interface_capture.png_data);
                } else {
                    capture_coordinator.fail_active_interface_capture(capture_error);
                }
                frame_already_submitted = true;
            }

            // --- Screenshot capture ---
            if (!frame_already_submitted &&
                try_capture_screenshot(screenshot_path, gpu, frame, fb_width, fb_height,
                                       frame_count, screenshot_delay, window)) {
                return true; // frame already submitted inside try_capture_screenshot
            }

            int fb_now_w = 0, fb_now_h = 0;
            glfwGetFramebufferSize(window, &fb_now_w, &fb_now_h);
            if (fb_now_w != fb_width || fb_now_h != fb_height || fb_now_w == 0 || fb_now_h == 0) {
                if (fb_now_w > 0 && fb_now_h > 0) {
                    fb_width = fb_now_w;
                    fb_height = fb_now_h;
                    gpu.resize(static_cast<uint32_t>(fb_width), static_cast<uint32_t>(fb_height));
                }
                display_state.surface_settle_frames = std::max(display_state.surface_settle_frames, 2);
                if (!frame_already_submitted)
                    gpu.discard_frame(frame);
                return true;
            }

            if (!frame_already_submitted)
                gpu.end_frame(frame);
        } else {
            if (capture_coordinator.has_pending_interface_capture() ||
                capture_coordinator.has_active_interface_capture()) {
                capture_coordinator.fail_pending_interface_captures(
                    "no capturable surface available");
            }
            // No surface — submit offscreen GPU work (runtime tick, thumbnails)
            // and poll the device so audio/compute operators still advance.
            vivid::gpu_submit(gpu.device(), gpu.queue(), tick_encoder, "Offscreen Commands");
        }

        // Push param snapshot to audio thread AFTER UI update so that any
        // set_param writes from slider drags are included in the snapshot.
        if (has_audio)
            runtime.post_tick_audio_sync();

        // wgpu-native: poll the device to process async operations
        wgpuDevicePoll(gpu.device(), false, nullptr);
        return true;
    };

#ifdef __APPLE__
    auto poll_events = [&]() -> bool {
        glfwPollEvents();
        return !glfwWindowShouldClose(window);
    };
    vivid::macos_run_frame_loop(poll_events, tick_frame);
#else
    while (true) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) break;
        if (!tick_frame()) break;
    }
#endif

    if (!test_dump_ui_state_path.empty() && !test_dump_write_attempted) {
        std::filesystem::create_directories(
            std::filesystem::path(test_dump_ui_state_path).parent_path());
        std::string dump_error;
        if (vivid::write_ui_test_dump_file(test_dump_ui_state_path, test_dump_state, dump_error)) {
            std::fprintf(stderr, "[vivid] UI test dump saved: %s\n",
                         test_dump_ui_state_path.c_str());
        } else {
            std::fprintf(stderr, "[vivid] UI test dump FAILED: %s (%s)\n",
                         test_dump_ui_state_path.c_str(), dump_error.c_str());
        }
        test_dump_write_attempted = true;
    }

    // --- Shutdown ---
    system_midi.close();
    control_server.stop();
    if (hot_reload_enabled) {
        file_watcher.stop();
        hot_reloader.stop();
    }
    if (has_audio) {
        audio_engine.shutdown();
    }
    if (graph_loaded) {
        runtime.shutdown();
    }

    if (text_renderer_ok) {
        text_renderer.shutdown();
    }
    thumb_renderer.shutdown();
    thumb_cache.shutdown();
    thumb_blit.shutdown();
    blit.shutdown();
    output_window.close();
    gpu.shutdown();

    // Save window geometry for next launch
    {
        vivid::Settings s = settings;  // preserve editor/style prefs
        glfwGetWindowPos(window, &s.window_x, &s.window_y);
        glfwGetWindowSize(window, &s.window_width, &s.window_height);
        s.bezier_wires = graph_ui.bezier_wires();
        s.show_param_wires = graph_ui.show_param_wires();
        s.show_analysis = runtime.frame_executor().analysis_enabled();
        vivid::save_settings(s);
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    std::fprintf(stderr, "[vivid] Clean shutdown\n");
    return 0;
}
