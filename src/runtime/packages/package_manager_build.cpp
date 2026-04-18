#include "runtime/packages/package_manager_internal.h"

#include "runtime/core/build_console.h"
#include "runtime/core/tool_discovery.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/platform/platform.h"
#include "runtime/platform/process_runner.h"

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <unordered_set>

namespace vivid {

static std::string abi_mismatch_error_for_package(const std::string& package_name,
                                                  const std::vector<AbiMismatchDiagnostic>& mismatches) {
    std::ostringstream oss;
    oss << "Plugin ABI mismatch for package '" << package_name << "'. "
        << "Rebuild vivid and rerun package rebuild.\n";
    for (const auto& m : mismatches) {
        oss << "  - " << m.plugin_path
            << " (ABI " << m.plugin_abi << ", expected " << m.runtime_abi << ")\n";
    }
    return oss.str();
}

bool PackageManager::compile_package(const std::string& pkg_dir, InstallResult& result,
                                     bool register_outputs) {
    std::error_code canonical_ec;
    std::string compile_pkg_dir = std::filesystem::canonical(pkg_dir, canonical_ec).string();
    if (canonical_ec || compile_pkg_dir.empty())
        compile_pkg_dir = pkg_dir;

    // build_dir is always inside pkg_dir; callers that remove pkg_dir on failure
    // implicitly clean up build_dir — no separate remove_all needed.
    std::string build_dir = compile_pkg_dir + "/build";

    if (result.info.build_type == "cmake") {
        std::string cmake_exe = find_tool("cmake");
        if (cmake_exe.empty()) {
            result.error_code = "missing_tool";
            result.error = missing_tool_error("cmake");
            if (build_console_) {
                auto task_id = build_console_->begin_task(BuildTaskKind::PackageConfigure, result.info.name);
                build_console_->append_system_line(task_id, result.error);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "missing cmake");
            }
            return false;
        }
        // cmake itself can be installed without a C++ compiler. Failing at the
        // configure step dumps cryptic cmake stderr; fail here with a friendly
        // hint instead.
        if (find_cxx_compiler().empty()) {
            result.error_code = "missing_tool";
            result.error = missing_tool_error("clang++");
            if (build_console_) {
                auto task_id = build_console_->begin_task(BuildTaskKind::PackageConfigure, result.info.name);
                build_console_->append_system_line(task_id, result.error);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "missing c++ compiler");
            }
            return false;
        }
        BuildTaskId configure_task = build_console_
            ? build_console_->begin_task(BuildTaskKind::PackageConfigure, result.info.name)
            : 0;
        // CMake-based package: configure + build
        std::filesystem::create_directories(build_dir);

        std::string src_dir = compiler_.src_dir();
        std::string vivid_build = compiler_.build_dir();

        // Configure
        ProcessRunOptions configure_opts;
        configure_opts.argv = {cmake_exe, "-B", build_dir, "-S", compile_pkg_dir,
                               "-DVIVID_SRC_DIR=" + src_dir,
                               "-DVIVID_BUILD_DIR=" + vivid_build,
                               "-DVIVID_PLUGIN_SUFFIX=" + std::string(kPluginSuffix)};

        // Pin the cmake-built package to the runtime's compile-time arch.
        // Same rationale as the clang -arch flag in PackageCompiler: cmake
        // inherits its host arch from its parent process, so a Rosetta-
        // translated x86_64 actions-runner produces x86_64 dylibs that the
        // arm64 vivid runtime can't dlopen.
#ifdef __APPLE__
#  if defined(__arm64__) || defined(__aarch64__)
        configure_opts.argv.push_back("-DCMAKE_OSX_ARCHITECTURES=arm64");
#  elif defined(__x86_64__)
        configure_opts.argv.push_back("-DCMAKE_OSX_ARCHITECTURES=x86_64");
#  endif
#endif

        {
            std::string dragonbox_include = PackageCompiler::managed_dragonbox_include_dir();
            std::string dragonbox_library = PackageCompiler::managed_dragonbox_library_path();
            if (!dragonbox_include.empty())
                configure_opts.argv.push_back("-DVIVID_DRAGONBOX_INCLUDE_DIR=" + dragonbox_include);
            if (!dragonbox_library.empty())
                configure_opts.argv.push_back("-DVIVID_DRAGONBOX_LIBRARY=" + dragonbox_library);
        }

#ifdef VIVID_HAS_HIGHWAY
        {
            std::string hwy_include = PackageCompiler::managed_highway_include_dir();
            std::string hwy_library = PackageCompiler::managed_highway_library_path();
            if (!hwy_include.empty())
                configure_opts.argv.push_back("-DVIVID_HIGHWAY_INCLUDE_DIR=" + hwy_include);
            if (!hwy_library.empty())
                configure_opts.argv.push_back("-DVIVID_HIGHWAY_LIBRARY=" + hwy_library);
        }
#endif
        std::fprintf(stderr, "[vivid] PackageManager: cmake configure %s\n", compile_pkg_dir.c_str());

        ProcessRunResult configure_result;
        if (build_console_) {
            configure_result = run_build_process(configure_opts, *build_console_, configure_task,
                                                 BuildConsoleStreamKind::Stdout);
        } else {
            configure_result = run_process(configure_opts);
        }

        if (!configure_result.launched) {
            result.error_code = "cmake_configure_failed";
            result.error = "Failed to execute cmake configure: " + configure_result.error;
            if (build_console_) {
                build_console_->append_system_line(configure_task, result.error);
                build_console_->finish_task(configure_task, BuildTaskState::Failed, "launch failed");
            }
            return false;
        }
        if (configure_result.exit_code != 0) {
            result.error_code = "cmake_configure_failed";
            result.error = "cmake configure failed:\n" + configure_result.output;
            if (build_console_)
                build_console_->finish_task(configure_task, BuildTaskState::Failed,
                                            "failed (exit " + std::to_string(configure_result.exit_code) + ")");
            return false;
        }
        if (build_console_)
            build_console_->finish_task(configure_task, BuildTaskState::Succeeded, "configured");

        // Build
        BuildTaskId build_task = build_console_
            ? build_console_->begin_task(BuildTaskKind::PackageBuild, result.info.name)
            : 0;
        ProcessRunOptions build_opts;
        build_opts.argv = {cmake_exe, "--build", build_dir};
        std::fprintf(stderr, "[vivid] PackageManager: cmake build %s\n", build_dir.c_str());

        ProcessRunResult build_result;
        if (build_console_) {
            build_result = run_build_process(build_opts, *build_console_, build_task,
                                             BuildConsoleStreamKind::Stdout);
        } else {
            build_result = run_process(build_opts);
        }

        if (!build_result.launched) {
            result.error_code = "cmake_build_failed";
            result.error = "Failed to execute cmake build: " + build_result.error;
            if (build_console_) {
                build_console_->append_system_line(build_task, result.error);
                build_console_->finish_task(build_task, BuildTaskState::Failed, "launch failed");
            }
            return false;
        }
        if (build_result.exit_code != 0) {
            result.error_code = "cmake_build_failed";
            result.error = "cmake build failed:\n" + build_result.output;
            if (build_console_)
                build_console_->finish_task(build_task, BuildTaskState::Failed,
                                            "failed (exit " + std::to_string(build_result.exit_code) + ")");
            return false;
        }
        if (build_console_)
            build_console_->finish_task(build_task, BuildTaskState::Succeeded, "built");

        // Synthesize compile results by scanning for dylibs in build dir
        for (auto& entry : std::filesystem::recursive_directory_iterator(build_dir)) {
            auto ext = entry.path().extension().string();
            if (ext == ".dylib" || ext == ".so" || ext == ".dll") {
                CompileResult cr;
                cr.success = true;
                cr.dylib_path = entry.path().string();
                cr.operator_name = entry.path().stem().string();
                result.compile_results.push_back(std::move(cr));
            }
        }
    } else {
        // Default: clang++ compilation via PackageCompiler
        std::string clang_exe = find_tool("clang++");
        if (clang_exe.empty()) {
            result.error_code = "missing_tool";
            result.error = missing_tool_error("clang++");
            if (build_console_) {
                auto task_id = build_console_->begin_task(BuildTaskKind::PackageBuild, result.info.name);
                build_console_->append_system_line(task_id, result.error);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "missing clang++");
            }
            return false;
        }

        std::vector<std::string> vendor_includes;
        for (const auto& vd : result.info.dependencies.vendor)
            vendor_includes.push_back(compile_pkg_dir + "/" + vd.include);
        result.compile_results = compiler_.compile_all(compile_pkg_dir,
            result.info.operators, result.info.gpu_operators, vendor_includes);

        bool all_ok = true;
        for (const auto& cr : result.compile_results) {
            if (!cr.success) {
                all_ok = false;
                break;
            }
        }

        if (!all_ok) {
            result.error_code = "compile_failed";
            std::string detail;
            for (const auto& cr : result.compile_results) {
                if (!cr.success) {
                    if (!detail.empty()) detail += "\n";
                    detail += cr.operator_name + ": " + cr.error_output;
                }
            }
            result.error = detail.empty() ? "Some operators failed to compile" : detail;
            return false;
        }
    }

    // Clean stale dylibs: remove any .dylib in build/ that doesn't correspond
    // to a declared operator. Prevents removed targets from being scanned.
    {
        std::unordered_set<std::string> declared;
        auto add_declared_target = [&](const std::string& op_path) {
            std::string target = op_path;
            auto slash = target.rfind('/');
            if (slash != std::string::npos)
                target = target.substr(slash + 1);
            declared.insert(target + kPluginSuffix);
        };
        for (const auto& op : result.info.operators)
            add_declared_target(op);
        for (const auto& op : result.info.gpu_operators)
            add_declared_target(op);
        std::error_code clean_ec;
        for (auto& entry : std::filesystem::directory_iterator(build_dir, clean_ec)) {
            if (clean_ec) break;
            auto ext = entry.path().extension().string();
            if (ext != ".dylib" && ext != ".so" && ext != ".dll") continue;
            if (declared.count(entry.path().filename().string())) continue;
            std::fprintf(stderr, "[vivid] PackageManager: removing stale dylib %s\n",
                         entry.path().c_str());
            std::filesystem::remove(entry.path(), clean_ec);
            clean_ec.clear();
        }
    }

    if (register_outputs) {
        // Scan compiled operators into registry
        registry_.clear_deferred_probe_handles_for_dir(build_dir);
        registry_.scan_deferred(build_dir.c_str());
        auto abi_mismatches = registry_.abi_mismatch_diagnostics_for_dir(build_dir);
        if (!abi_mismatches.empty()) {
            result.error_code = "abi_mismatch";
            result.error = abi_mismatch_error_for_package(result.info.name, abi_mismatches);
            return false;
        }

        // Track provenance
        registry_.register_package(result.info.name, build_dir);
    }

    return true;
}

} // namespace vivid
