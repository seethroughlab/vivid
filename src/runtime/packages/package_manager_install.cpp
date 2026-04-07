#include "runtime/packages/package_manager_internal.h"

#include "runtime/core/build_console.h"
#include "runtime/core/tool_discovery.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/platform/platform.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sstream>

namespace vivid {

using package_manager_internal::diagnose_non_package_dir;
using package_manager_internal::quote;

namespace {
std::string abi_mismatch_error_for_package(const std::string& package_name,
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
} // namespace

InstallResult PackageManager::install(const std::string& url) {
    std::set<std::string> chain;
    std::vector<std::string> installed_deps;
    auto result = install_with_chain(url, chain, installed_deps);
    result.installed_deps = std::move(installed_deps);
    return result;
}

InstallResult PackageManager::install_with_chain(const std::string& url,
                                                  std::set<std::string>& installing_chain,
                                                  std::vector<std::string>& installed_deps) {
    InstallResult result;

    // Create packages directory
    std::filesystem::create_directories(packages_dir());

    // Use a temporary staging dir, then rename after parsing the manifest
    // to get the canonical package name.
    std::string staging_name = ".staging_" + std::to_string(
        std::hash<std::string>{}(url) % 999999);
    std::string staging_dir = packages_dir() + "/" + staging_name;

    // Clean up any leftover staging dir
    std::filesystem::remove_all(staging_dir);

    // Normalize GitHub URLs (skip if path exists on disk)
    std::string normalized_url = url;
    if (!std::filesystem::exists(url))
        normalized_url = normalize_github_url(url);

    // Check if URL is a local path
    bool is_local = std::filesystem::exists(normalized_url);

    if (is_local) {
        // Copy local directory
        std::error_code ec;
        std::filesystem::copy(normalized_url, staging_dir,
            std::filesystem::copy_options::recursive, ec);
        if (ec) {
            result.error_code = "copy_failed";
            result.error = "Failed to copy local package: " + ec.message();
            return result;
        }
    } else {
        BuildTaskId task_id = build_console_
            ? build_console_->begin_task(BuildTaskKind::GitClone, normalized_url)
            : 0;
        std::string git_exe = find_tool("git");
        if (git_exe.empty()) {
            result.error_code = "missing_tool";
            result.error = missing_tool_error("git");
            if (build_console_) {
                build_console_->append_system_line(task_id, result.error);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "missing git");
            }
            return result;
        }
        // Git clone (quote URL and path for spaces and special characters)
        std::string cmd = quote(git_exe) + " clone --depth 1 " + quote(normalized_url) + " " + quote(staging_dir) + " 2>&1";
        std::fprintf(stderr, "[vivid] PackageManager: %s\n", cmd.c_str());

        std::string output;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            result.error_code = "git_clone_failed";
            result.error = "Failed to execute git clone";
            if (build_console_) {
                build_console_->append_system_line(task_id, result.error);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "launch failed");
            }
            return result;
        }
        std::array<char, 256> buf;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            output += buf.data();
            if (build_console_)
                build_console_->append_line(task_id, BuildConsoleStreamKind::Stdout, buf.data());
        }
        int status = pclose(pipe);
        if (status != 0) {
            result.error_code = "git_clone_failed";
            result.error = "git clone failed: " + output;
            if (build_console_)
                build_console_->finish_task(task_id, BuildTaskState::Failed,
                                            "failed (exit " + std::to_string(status) + ")");
            std::filesystem::remove_all(staging_dir);
            return result;
        }
        if (build_console_)
            build_console_->finish_task(task_id, BuildTaskState::Succeeded, "cloned");
    }

    // Parse manifest to get canonical package name
    auto [manifest_code, manifest_msg] = parse_manifest(staging_dir, result.info);
    if (!manifest_code.empty()) {
        std::string hint = diagnose_non_package_dir(staging_dir);
        result.error_code = manifest_code;
        result.error = manifest_msg;
        if (!hint.empty()) result.error += "\n" + hint;
        std::filesystem::remove_all(staging_dir);
        return result;
    }

    // Move staging to final location using the canonical name from the manifest
    std::string pkg_dir = packages_dir() + "/" + result.info.name;

    // Check if already installed
    if (std::filesystem::exists(pkg_dir)) {
        result.error_code = "already_installed";
        result.error = "Package already installed: " + result.info.name +
                       " (uninstall first, or remove " + pkg_dir + ")";
        std::filesystem::remove_all(staging_dir);
        return result;
    }

    // --- Resolve & install dependencies ---
    installing_chain.insert(result.info.name);
    for (const auto& dep_name : result.info.dependencies.packages) {
        if (is_installed(dep_name)) continue;

        if (installing_chain.count(dep_name)) {
            result.error_code = "circular_dependency";
            result.error = "Circular dependency detected: " + result.info.name +
                           " -> " + dep_name + " (already in install chain)";
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        if (!resolver_) {
            result.error_code = "no_resolver";
            result.error = "Package '" + result.info.name + "' depends on '" +
                           dep_name + "' but no package resolver is configured";
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        std::string dep_url = resolver_(dep_name);
        if (dep_url.empty()) {
            result.error_code = "dependency_not_found";
            result.error = "Dependency '" + dep_name + "' not found in package catalog";
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        auto dep_result = install_with_chain(dep_url, installing_chain, installed_deps);
        if (!dep_result.success) {
            result.error_code = dep_result.error_code.empty() ? "dependency_install_failed" : dep_result.error_code;
            result.error = "Failed to install dependency '" + dep_name + "': " + dep_result.error;
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        installed_deps.push_back(dep_name);
    }

    std::filesystem::rename(staging_dir, pkg_dir);
    result.info.path = pkg_dir;

    if (!compile_package(pkg_dir, result)) {
        // Roll back failed installs so partial packages don't remain on disk.
        std::error_code ec;
        std::filesystem::remove_all(pkg_dir, ec);
        if (ec) {
            std::fprintf(stderr, "[vivid] PackageManager: warning: failed to clean up %s after compile failure: %s\n",
                         pkg_dir.c_str(), ec.message().c_str());
        }
        return result;
    }

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: installed %s (%zu operators)\n",
                 result.info.name.c_str(),
                 result.info.operators.size() + result.info.gpu_operators.size());
    return result;
}

InstallResult PackageManager::link(const std::string& path) {
    InstallResult result;

    // Resolve to absolute path
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    if (ec) {
        result.error_code = "path_not_found";
        result.error = "Path does not exist: " + path;
        return result;
    }

    // Validate it's a directory with a manifest
    if (!std::filesystem::is_directory(canonical)) {
        result.error_code = "not_a_directory";
        result.error = "Not a directory: " + canonical.string();
        return result;
    }

    if (!std::filesystem::exists(canonical / "vivid-package.json")) {
        result.error_code = "link_no_manifest";
        result.error = "No vivid-package.json found in " + canonical.string();
        return result;
    }

    // Parse manifest to get canonical package name
    auto [link_manifest_code, link_manifest_msg] = parse_manifest(canonical.string(), result.info);
    if (!link_manifest_code.empty()) {
        result.error_code = link_manifest_code;
        result.error = link_manifest_msg + " (in " + canonical.string() + ")";
        return result;
    }

    // Create packages directory
    std::filesystem::create_directories(packages_dir());

    // Check no existing package with same name
    std::string pkg_dir = packages_dir() + "/" + result.info.name;
    if (std::filesystem::exists(pkg_dir)) {
        result.error_code = "already_installed";
        result.error = "Package already exists: " + result.info.name +
                       " (uninstall or unlink first)";
        return result;
    }

    // Create symlink
    std::filesystem::create_directory_symlink(canonical, pkg_dir, ec);
    if (ec) {
        result.error_code = "link_failed";
        result.error = "Failed to create symlink: " + ec.message();
        return result;
    }

    result.info.path = pkg_dir;
    result.info.linked = true;

    // Compile (build/ dir ends up in the original source tree through the symlink)
    if (!compile_package(pkg_dir, result))
        return result;

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: linked %s -> %s (%zu operators)\n",
                 result.info.name.c_str(), canonical.string().c_str(),
                 result.info.operators.size() + result.info.gpu_operators.size());
    return result;
}

bool PackageManager::unlink(const std::string& name) {
    std::string pkg_dir = packages_dir() + "/" + name;

    // Check for symlink first (is_symlink works even when target is gone)
    if (!std::filesystem::is_symlink(pkg_dir) && !std::filesystem::exists(pkg_dir)) {
        std::fprintf(stderr, "[vivid] PackageManager: package not found: %s\n", name.c_str());
        return false;
    }

    if (!std::filesystem::is_symlink(pkg_dir)) {
        std::fprintf(stderr, "[vivid] PackageManager: '%s' is not a linked package (use uninstall instead)\n",
                     name.c_str());
        return false;
    }

    // Unregister operators from registry (best-effort; error discarded)
    PackageInfo info;
    if (parse_manifest(pkg_dir, info).first.empty()) {
        auto unregister_op = [&](const std::string& op_path) {
            auto slash = op_path.rfind('/');
            std::string target = (slash != std::string::npos) ? op_path.substr(slash + 1) : op_path;
            const std::string* type_name = registry_.type_name_for_target(target);
            if (type_name) {
                registry_.unregister_package_operator(*type_name);
            } else {
                registry_.unregister_package_operator(target);
            }
        };
        for (const auto& op : info.operators)
            unregister_op(op);
        for (const auto& op : info.gpu_operators)
            unregister_op(op);
    }

    // Remove symlink only — never follows into source tree
    std::error_code ec;
    std::filesystem::remove(pkg_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[vivid] PackageManager: failed to remove symlink %s: %s\n",
                     pkg_dir.c_str(), ec.message().c_str());
        return false;
    }

    std::fprintf(stderr, "[vivid] PackageManager: unlinked %s\n", name.c_str());
    return true;
}

InstallResult PackageManager::rebuild(const std::string& name) {
    InstallResult result;

    std::string pkg_dir = packages_dir() + "/" + name;
    if (!std::filesystem::exists(pkg_dir)) {
        result.error_code = "package_not_found";
        result.error = "Package not found: " + name;
        return result;
    }

    auto [rebuild_manifest_code, rebuild_manifest_msg] = parse_manifest(pkg_dir, result.info);
    if (!rebuild_manifest_code.empty()) {
        result.error_code = rebuild_manifest_code;
        result.error = rebuild_manifest_msg + " (in " + name + ")";
        return result;
    }

    result.info.linked = std::filesystem::is_symlink(pkg_dir);

    if (!compile_package(pkg_dir, result, false))
        return result;

    // Re-register operators only after a successful rebuild so failed rebuilds
    // leave the old package/runtime state intact.
    auto unregister_op = [&](const std::string& op_path) {
        auto slash = op_path.rfind('/');
        std::string target = (slash != std::string::npos) ? op_path.substr(slash + 1) : op_path;
        const std::string* type_name = registry_.type_name_for_target(target);
        if (type_name) {
            registry_.unregister_package_operator(*type_name);
        } else {
            registry_.unregister_package_operator(target);
        }
    };
    for (const auto& op : result.info.operators)
        unregister_op(op);
    for (const auto& op : result.info.gpu_operators)
        unregister_op(op);

    std::error_code build_ec;
    std::string build_root = std::filesystem::canonical(pkg_dir, build_ec).string();
    if (build_ec || build_root.empty())
        build_root = pkg_dir;
    std::string build_dir = build_root + "/build";
    registry_.clear_deferred_probe_handles_for_dir(build_dir);
    registry_.scan_deferred(build_dir.c_str());
    auto abi_mismatches = registry_.abi_mismatch_diagnostics_for_dir(build_dir);
    if (!abi_mismatches.empty()) {
        result.error_code = "abi_mismatch";
        result.error = abi_mismatch_error_for_package(result.info.name, abi_mismatches);
        return result;
    }
    registry_.register_package(result.info.name, build_dir);

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: rebuilt %s (%zu operators)\n",
                 result.info.name.c_str(),
                 result.info.operators.size() + result.info.gpu_operators.size());
    return result;
}

bool PackageManager::uninstall(const std::string& name) {
    std::string pkg_dir = packages_dir() + "/" + name;

    if (!std::filesystem::exists(pkg_dir)) {
        std::fprintf(stderr, "[vivid] PackageManager: package not found: %s\n", name.c_str());
        return false;
    }

    // Warn if other installed packages depend on this one
    for (auto& entry : std::filesystem::directory_iterator(packages_dir())) {
        if (!entry.is_directory()) continue;
        PackageInfo dep_info;
        if (!parse_manifest(entry.path().string(), dep_info).first.empty()) continue;
        if (dep_info.name == name) continue;
        for (const auto& dep : dep_info.dependencies.packages) {
            if (dep == name) {
                std::fprintf(stderr, "[vivid] PackageManager: warning: '%s' depends on '%s'\n",
                             dep_info.name.c_str(), name.c_str());
            }
        }
    }

    // Parse manifest to find operators to unregister.
    // The manifest uses relative paths like "control/test_mgr_op" where the last
    // segment is the cmake target name (dylib stem). We need to look up the actual
    // descriptor type name from the target→type mapping.
    PackageInfo info;
    if (parse_manifest(pkg_dir, info).first.empty()) {
        auto unregister_op = [&](const std::string& op_path) {
            auto slash = op_path.rfind('/');
            std::string target = (slash != std::string::npos) ? op_path.substr(slash + 1) : op_path;
            const std::string* type_name = registry_.type_name_for_target(target);
            if (type_name) {
                registry_.unregister_package_operator(*type_name);
            } else {
                // Fallback: try using target name directly as type name
                registry_.unregister_package_operator(target);
            }
        };
        for (const auto& op : info.operators)
            unregister_op(op);
        for (const auto& op : info.gpu_operators)
            unregister_op(op);
    }

    // Remove directory (symlink-safe: remove link only, never follow into source)
    std::error_code ec;
    if (std::filesystem::is_symlink(pkg_dir))
        std::filesystem::remove(pkg_dir, ec);
    else
        std::filesystem::remove_all(pkg_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[vivid] PackageManager: failed to remove %s: %s\n",
                     pkg_dir.c_str(), ec.message().c_str());
        return false;
    }

    std::fprintf(stderr, "[vivid] PackageManager: uninstalled %s\n", name.c_str());
    return true;
}

} // namespace vivid
