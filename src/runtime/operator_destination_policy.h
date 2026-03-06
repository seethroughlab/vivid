#pragma once

#include "runtime/package_manager.h"
#include "runtime/settings.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace vivid {

struct OperatorDestination {
    std::string root;
    bool package_layout = false;
    std::string package_name;
    bool used_core_fallback = false;
    std::string warning;
};

inline bool is_supported_package_layout(const std::string& root_path) {
    namespace fs = std::filesystem;
    fs::path p(root_path);
    return fs::exists(p / "vivid-package.json") &&
           fs::exists(p / "src") &&
           fs::exists(p / "CMakeLists.txt");
}

inline bool is_workspace_project_package(const PackageInfo& pkg) {
    if (!pkg.linked) return false;
    if (pkg.source_scope != "local" && pkg.source_scope != "workspace") return false;
    return is_supported_package_layout(pkg.path);
}

inline const PackageInfo* select_workspace_project_package(const std::vector<PackageInfo>& packages) {
    std::vector<const PackageInfo*> candidates;
    for (const auto& pkg : packages) {
        if (is_workspace_project_package(pkg)) candidates.push_back(&pkg);
    }
    if (candidates.empty()) return nullptr;
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const PackageInfo* a, const PackageInfo* b) {
        int a_scope = (a->source_scope == "local") ? 0 : 1;
        int b_scope = (b->source_scope == "local") ? 0 : 1;
        if (a_scope != b_scope) return a_scope < b_scope;
        return a->path < b->path;
    });
    return candidates.front();
}

inline const PackageInfo* find_package_by_name(const std::vector<PackageInfo>& packages,
                                               const std::string& name) {
    for (const auto& pkg : packages) {
        if (pkg.name == name) return &pkg;
    }
    return nullptr;
}

inline const PackageInfo* find_package_by_path(const std::vector<PackageInfo>& packages,
                                               const std::string& path) {
    for (const auto& pkg : packages) {
        if (std::filesystem::path(pkg.path).lexically_normal() ==
            std::filesystem::path(path).lexically_normal())
            return &pkg;
    }
    return nullptr;
}

inline bool resolve_operator_destination(const std::string& requested_destination,
                                         const std::string& core_src_root,
                                         const std::vector<PackageInfo>& packages,
                                         const Settings* settings,
                                         OperatorDestination& out,
                                         std::string& error) {
    namespace fs = std::filesystem;

    auto set_core = [&](const std::string& warn) -> bool {
        if (core_src_root.empty()) {
            error = "core source root unavailable; pass --src-dir or configure a project destination";
            return false;
        }
        out = {};
        out.root = core_src_root;
        out.used_core_fallback = !warn.empty();
        out.warning = warn;
        return true;
    };

    auto set_package = [&](const PackageInfo& pkg) -> bool {
        out = {};
        out.root = pkg.path;
        out.package_layout = true;
        out.package_name = pkg.name;
        return true;
    };

    auto set_path = [&](const std::string& path) -> bool {
        if (!fs::exists(path)) {
            error = "destination path does not exist: " + path;
            return false;
        }
        out = {};
        out.root = path;
        out.package_layout = is_supported_package_layout(path);
        if (out.package_layout) {
            if (const auto* pkg = find_package_by_path(packages, path))
                out.package_name = pkg->name;
        }
        return true;
    };

    std::string request = requested_destination.empty() ? "auto" : requested_destination;
    bool auto_core_mode = settings && settings->operator_clone_destination_mode == "core_explicit";

    if (request == "core") return set_core("");
    if (request.rfind("package:", 0) == 0) {
        std::string pkg_name = request.substr(8);
        if (pkg_name.empty()) {
            error = "destination package name is empty";
            return false;
        }
        const auto* pkg = find_package_by_name(packages, pkg_name);
        if (!pkg) {
            error = "destination package not found: " + pkg_name;
            return false;
        }
        if (!is_supported_package_layout(pkg->path)) {
            error = "destination package does not use supported src/ layout: " + pkg_name;
            return false;
        }
        return set_package(*pkg);
    }
    if (fs::path(request).is_absolute()) return set_path(request);
    if (request != "auto" && request != "project") {
        error = "destination must be auto|project|core|package:<name>|absolute path";
        return false;
    }

    // 'auto' honors mode. 'project' always tries project-first resolution.
    if (request == "auto" && auto_core_mode) return set_core("");

    // Precedence:
    // 1) explicit user choice handled above
    // 2) graph/workspace-local project package
    if (const auto* pkg = select_workspace_project_package(packages))
        return set_package(*pkg);

    // 3a) configured project package name
    if (settings && !settings->project_package_name.empty()) {
        if (const auto* pkg = find_package_by_name(packages, settings->project_package_name)) {
            if (is_workspace_project_package(*pkg))
                return set_package(*pkg);
        }
    }

    // 3b) configured project operator root
    if (settings && !settings->project_operator_root.empty()) {
        if (!fs::path(settings->project_operator_root).is_absolute()) {
            return set_core("Configured project_operator_root is not absolute; using core destination.");
        }
        if (set_path(settings->project_operator_root)) return true;
        // set_path populated error; downgrade to warning and continue to fallback.
        std::string path_err = error;
        error.clear();
        return set_core(path_err + "; using core destination.");
    }

    // 4) fallback core with warning
    return set_core("No workspace project package configured; using core destination.");
}

} // namespace vivid

