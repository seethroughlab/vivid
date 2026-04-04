// Team workflow regression: two repos (core + project package) with clean git diff boundaries.
//
// Case A: scaffold to project package — core CMakeLists unchanged, operator lands in pkg src/.
// Case B: clone to project package via command sink — same isolation + correct hot-reload target.
// Case C: fallback-to-core with no project package — warning emitted, operator lands in core.

#include "runtime/operators/operator_creator.h"
#include "runtime/operators/operator_destination_policy.h"
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
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include "test_helpers.h"

namespace fs = std::filesystem;

static std::string read_file(const fs::path& p) {
    std::ifstream ifs(p);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    // ==================================================================
    // Case A: scaffold to project package — core stays clean
    // ==================================================================
    std::fprintf(stderr, "\n--- Case A: scaffold to project package ---\n");
    {
        ScopedTempDir sandbox_a("team_reg_a");
        fs::path sandbox = sandbox_a.path;

        // Create core_root with operators/control/ tree and core CMakeLists.txt
        fs::path core_root = sandbox / "core_root";
        fs::create_directories(core_root / "operators" / "control");
        {
            std::ofstream ofs(core_root / "CMakeLists.txt");
            ofs << "# --- Control operators ---\n"
                   "add_vivid_operator(lfo operators/control/lfo/lfo.cpp)\n"
                   "# --- GPU operator plugins ---\n"
                   "# --- Audio operator plugins ---\n";
        }
        std::string core_cmake_before = read_file(core_root / "CMakeLists.txt");

        // Create proj_root as project package with src/ layout
        fs::path proj_root = sandbox / "proj_root";
        fs::create_directories(proj_root / "src");
        {
            std::ofstream ofs(proj_root / "vivid-package.json");
            ofs << "{\n  \"name\": \"team-proj\",\n  \"version\": \"0.0.1\",\n"
                   "  \"build\": \"cmake\",\n  \"operators\": []\n}\n";
        }
        {
            std::ofstream ofs(proj_root / "CMakeLists.txt");
            ofs << "cmake_minimum_required(VERSION 3.20)\n"
                   "project(team_proj)\n"
                   "set(CONTROL_OPS\n)\n";
        }

        // Scaffold team_op into project package (package_layout = true)
        VividCreateOperatorRequest req_a;
        req_a.name = "team_op";
        req_a.kind = VIVID_OP_CONTROL;
        auto result = vivid::OperatorCreator::create(req_a, proj_root.string(), /*package_layout=*/true);

        check(result.success, "Case A: scaffold to project package succeeds");
        check(fs::exists(proj_root / "src" / "team_op.cpp"),
              "Case A: operator landed in proj_root/src/");

        std::string proj_cmake = read_file(proj_root / "CMakeLists.txt");
        check(proj_cmake.find("team_op") != std::string::npos,
              "Case A: proj CMakeLists.txt contains team_op");

        std::string core_cmake_after = read_file(core_root / "CMakeLists.txt");
        check(core_cmake_before == core_cmake_after,
              "Case A: core CMakeLists.txt unchanged");
        check(!fs::exists(core_root / "operators" / "control" / "team_op"),
              "Case A: no team_op dir created in core operators");

        // ScopedTempDir handles cleanup
    }

    // ==================================================================
    // Case C: fallback-to-core with no project package — no cross-contamination
    // ==================================================================
    std::fprintf(stderr, "\n--- Case C: fallback-to-core, no project package ---\n");
    {
        ScopedTempDir sandbox_c("team_reg_c");
        fs::path sandbox = sandbox_c.path;

        fs::path core_root = sandbox / "core_root";
        fs::create_directories(core_root / "operators" / "control");
        {
            std::ofstream ofs(core_root / "CMakeLists.txt");
            ofs << "# --- Control operators ---\n"
                   "add_vivid_operator(lfo operators/control/lfo/lfo.cpp)\n"
                   "# --- GPU operator plugins ---\n"
                   "# --- Audio operator plugins ---\n";
        }

        // Resolve destination with auto + no packages → core fallback with warning
        std::vector<vivid::PackageInfo> no_packages;
        vivid::OperatorDestination dest;
        std::string error;
        bool ok = vivid::resolve_operator_destination(
            "auto", core_root.string(), no_packages, nullptr, dest, error);

        check(ok, "Case C: resolve_operator_destination succeeds");
        check(!dest.warning.empty(), "Case C: fallback-to-core emits non-empty warning");
        check(dest.root == core_root.string(), "Case C: destination resolves to core root");

        // Scaffold solo_op into the resolved core destination
        VividCreateOperatorRequest req_c;
        req_c.name = "solo_op";
        req_c.kind = VIVID_OP_CONTROL;
        auto result = vivid::OperatorCreator::create(req_c, dest.root);

        check(result.success, "Case C: scaffold into core succeeds");
        check(fs::exists(core_root / "operators" / "control" / "solo_op" / "solo_op.cpp"),
              "Case C: operator landed in core_root/operators/control/solo_op/");

        // ScopedTempDir handles cleanup
    }

    // ==================================================================
    // Case B: clone to project package via command sink — same isolation
    // Requires compiled test operator dylib (build_dir/test_op_v1.dylib)
    // ==================================================================
    std::fprintf(stderr, "\n--- Case B: clone to project package via command sink ---\n");
    {
        fs::path dylib = fs::path(build_dir) / "test_op_v1.dylib";
        if (!fs::exists(dylib)) {
            std::fprintf(stderr, "  SKIP: test_op_v1.dylib not found at %s\n",
                         dylib.c_str());
        } else {
            // Clean and recreate the packages discovery dir (cwd/packages = build_dir/packages)
            fs::path local_packages_root = fs::path(build_dir) / "packages";
            std::error_code clean_ec;
            fs::remove_all(local_packages_root, clean_ec);
            fs::create_directories(local_packages_root);

            // Stage compiled test operator for registry scan
            fs::path staging = fs::path(build_dir) / ".test_team_workflow_staging";
            fs::remove_all(staging);
            fs::create_directories(staging);
            fs::copy_file(dylib, staging / "test_op_v1.dylib",
                          fs::copy_options::overwrite_existing);

            vivid::OperatorRegistry registry;
            check(registry.scan(staging.c_str()), "Case B: registry.scan found operators");

            bool has_testop = (registry.find("TestOp") != nullptr);
            check(has_testop, "Case B: TestOp registered from compiled dylib");

            if (has_testop) {
                // Create core_root with TestOp source at the expected operator location
                fs::path core_root = fs::path(build_dir) / ".test_team_wf_core";
                fs::remove_all(core_root);
                fs::create_directories(core_root / "operators" / "control" / "testop");
                {
                    std::ofstream ofs(
                        core_root / "operators" / "control" / "testop" / "testop.cpp");
                    ofs << "#include \"operator_api/operator.h\"\n"
                           "struct TestOp : vivid::OperatorBase, vivid::FrameProcessable {\n"
                           "  static constexpr const char* kName = \"TestOp\";\n"
                           "  static constexpr bool kTimeDependent = false;\n"
                           "  vivid::Param<float> scale{\"scale\", 1.0f, 0.0f, 10.0f};\n"
                           "  void collect_params(std::vector<vivid::ParamBase*>& out) override "
                           "{ out.push_back(&scale); }\n"
                           "  void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
                           "    out.push_back({\"out\", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});\n"
                           "  }\n"
                           "  void process_frame(const VividFrameContext* ctx) override "
                           "{ ctx->output_values[0] = ctx->param_values[0] * 2.0f; }\n"
                           "};\n"
                           "VIVID_REGISTER(TestOp)\n";
                }
                {
                    std::ofstream ofs(core_root / "CMakeLists.txt");
                    ofs << "# --- Control operators ---\n"
                           "add_vivid_operator(testop operators/control/testop/testop.cpp)\n"
                           "# --- GPU operator plugins ---\n"
                           "# --- Audio operator plugins ---\n";
                }
                std::string core_cmake_before = read_file(core_root / "CMakeLists.txt");

                // Create project package source dir and link it into packages discovery root
                fs::path proj_src = fs::path(build_dir) / ".test_team_wf_proj_src";
                fs::remove_all(proj_src);
                fs::create_directories(proj_src / "src");
                {
                    std::ofstream ofs(proj_src / "vivid-package.json");
                    ofs << "{\n  \"name\": \"team-proj\",\n  \"version\": \"0.0.1\",\n"
                           "  \"build\": \"cmake\",\n  \"operators\": []\n}\n";
                }
                {
                    std::ofstream ofs(proj_src / "CMakeLists.txt");
                    ofs << "cmake_minimum_required(VERSION 3.20)\n"
                           "project(team_proj)\n"
                           "set(CONTROL_OPS\n)\n";
                }
                fs::path pkg_link = local_packages_root / "team-proj";
                std::error_code ec;
                fs::create_directory_symlink(fs::absolute(proj_src), pkg_link, ec);
                check(!ec, "Case B: created linked local project package symlink");

                // Set up runtime and command sink
                vivid::Graph graph;
                vivid::RuntimeCore runtime;
                vivid::AudioEngine audio_engine;
                vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);
                RuntimeCommandSink sink(api);
                sink.set_registry(&registry);
                sink.set_operators_dir((core_root / "operators").string());
                sink.set_build_dir(build_dir);
                OperatorInfoCache op_cache;
                sink.set_op_cache(&op_cache);

                vivid::PackageCompiler pkg_compiler(build_dir, build_dir);
                vivid::PackageManager pkg_manager(pkg_compiler, registry);
                sink.set_package_manager(&pkg_manager);

                vivid::Settings sink_settings;
                sink_settings.operator_clone_destination_mode = "project_default";
                sink.set_settings(&sink_settings);

                vivid::HotReloader hr;
                fs::path hr_build = fs::path(build_dir) / ".test_team_wf_hr_build";
                fs::path staged_dylib_path = hr_build / "fake_team_wf.dylib";
                fs::create_directories(hr_build);
                check(hr.start(hr_build.string()), "Case B: hot reloader started");
                hr.set_package_compiler([staged_dylib_path](const std::string& target) {
                    vivid::ReloadResult r;
                    r.target_name = target;
                    r.success = true;
                    r.staged_dylib_path = staged_dylib_path.string();
                    return r;
                });
                sink.set_hot_reloader(&hr);

                // Clone TestOp into project package
                sink.clone_and_edit("TestOp", "package:team-proj");

                // Assert: cloned .cpp exists in proj package src/
                check(fs::exists(proj_src / "src" / "testop_copy.cpp"),
                      "Case B: clone wrote source into proj package src/");

                // Assert: no testop_copy dir created under core operators
                check(!fs::exists(core_root / "operators" / "control" / "testop_copy"),
                      "Case B: no testop_copy dir in core operators");

                // Assert: core CMakeLists unchanged
                std::string core_cmake_after = read_file(core_root / "CMakeLists.txt");
                check(core_cmake_before == core_cmake_after,
                      "Case B: core CMakeLists.txt unchanged after clone");

                // Assert: hot-reload target is pkg:team-proj:testop_copy
                bool saw_pkg_reload = false;
                for (int i = 0; i < 40 && !saw_pkg_reload; ++i) {
                    auto ready = hr.poll_ready();
                    for (const auto& rr : ready) {
                        if (rr.target_name == "pkg:team-proj:testop_copy" && rr.success) {
                            saw_pkg_reload = true;
                            break;
                        }
                    }
                    if (!saw_pkg_reload)
                        std::this_thread::sleep_for(std::chrono::milliseconds(25));
                }
                check(saw_pkg_reload,
                      "Case B: clone queued package hot-reload target pkg:team-proj:testop_copy");

                hr.stop();
            }
        }
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
