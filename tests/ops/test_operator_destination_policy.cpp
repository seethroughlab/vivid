#include "runtime/operators/operator_destination_policy.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include "test_helpers.h"

namespace fs = std::filesystem;

static fs::path make_sandbox() {
    char tmpl[] = "/tmp/vivid_test_dest_policy_XXXXXX";
    char* out = mkdtemp(tmpl);
    return out ? fs::path(out) : fs::path("/tmp/vivid_test_dest_policy_fallback");
}

static void write_text(const fs::path& p, const char* text) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs(p);
    ofs << text;
}

static fs::path make_package_layout(const fs::path& root, const std::string& name) {
    fs::path pkg = root / name;
    fs::create_directories(pkg / "src");
    write_text(pkg / "vivid-package.json",
               "{\n"
               "  \"name\": \"pkg\",\n"
               "  \"version\": \"0.0.1\",\n"
               "  \"operators\": [],\n"
               "  \"gpu_operators\": []\n"
               "}\n");
    write_text(pkg / "CMakeLists.txt",
               "cmake_minimum_required(VERSION 3.20)\n"
               "project(pkg)\n"
               "set(CONTROL_OPS\n"
               ")\n");
    return pkg;
}

static vivid::PackageInfo make_pkg(const std::string& name,
                                   const std::string& path,
                                   const std::string& scope,
                                   bool linked = true) {
    vivid::PackageInfo p;
    p.name = name;
    p.path = path;
    p.source_scope = scope;
    p.linked = linked;
    return p;
}

int main() {
    fs::path sandbox = make_sandbox();
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    auto core = sandbox / "vivid-core";
    fs::create_directories(core);

    auto pkg_local = make_package_layout(sandbox, "pkg_local");
    auto pkg_workspace = make_package_layout(sandbox, "pkg_workspace");
    auto pkg_user = make_package_layout(sandbox, "pkg_user");
    auto root_hint = make_package_layout(sandbox, "root_hint");

    std::vector<vivid::PackageInfo> packages;
    packages.push_back(make_pkg("pkg_local", pkg_local.string(), "local", true));
    packages.push_back(make_pkg("pkg_workspace", pkg_workspace.string(), "workspace", true));
    packages.push_back(make_pkg("pkg_user", pkg_user.string(), "user", true));

    vivid::Settings settings;
    settings.operator_clone_destination_mode = "project_default";
    settings.project_package_name = "pkg_workspace";
    settings.project_operator_root = root_hint.string();

    vivid::OperatorDestination out;
    std::string error;

    // 1) explicit core always wins
    check(vivid::resolve_operator_destination("core", core.string(), packages, &settings, out, error),
          "explicit core resolves");
    check(out.root == core.string(), "explicit core uses core root");
    check(!out.package_layout, "explicit core is not package layout");

    // 2) explicit package wins even if user scope
    check(vivid::resolve_operator_destination("package:pkg_user", core.string(), packages, &settings, out, error),
          "explicit package resolves");
    check(out.package_layout, "explicit package marks package layout");
    check(out.package_name == "pkg_user", "explicit package selects requested name");

    // 3) auto in project_default picks local/workspace project package before settings hints
    check(vivid::resolve_operator_destination("auto", core.string(), packages, &settings, out, error),
          "auto resolves in project_default");
    check(out.package_layout, "auto picks package layout");
    check(out.package_name == "pkg_local", "auto prefers local package first");

    // 4) project request also picks project package even if mode changes later
    settings.operator_clone_destination_mode = "core_explicit";
    check(vivid::resolve_operator_destination("project", core.string(), packages, &settings, out, error),
          "project resolves regardless of mode");
    check(out.package_name == "pkg_local", "project request still picks local package");

    // 5) auto in core_explicit goes to core
    check(vivid::resolve_operator_destination("auto", core.string(), packages, &settings, out, error),
          "auto resolves in core_explicit");
    check(out.root == core.string(), "auto core_explicit chooses core");
    check(!out.package_layout, "auto core_explicit is not package");

    // 6) with no workspace/local package, configured project_package_name is used
    std::vector<vivid::PackageInfo> no_project_pkgs;
    no_project_pkgs.push_back(make_pkg("pkg_workspace", pkg_workspace.string(), "workspace", true));
    settings.operator_clone_destination_mode = "project_default";
    settings.project_package_name = "pkg_workspace";
    check(vivid::resolve_operator_destination("auto", core.string(), no_project_pkgs, &settings, out, error),
          "auto uses configured project_package_name");
    check(out.package_name == "pkg_workspace", "configured project_package_name selected");

    // 7) project_operator_root is used when package hint is unavailable
    settings.project_package_name = "does_not_exist";
    check(vivid::resolve_operator_destination("auto", core.string(), {}, &settings, out, error),
          "auto uses project_operator_root fallback");
    check(out.root == root_hint.string(), "project_operator_root selected");

    // 8) fallback to core with warning
    settings.project_operator_root.clear();
    check(vivid::resolve_operator_destination("auto", core.string(), {}, &settings, out, error),
          "auto falls back to core");
    check(out.root == core.string(), "fallback root is core");
    check(out.used_core_fallback, "fallback flagged");
    check(!out.warning.empty(), "fallback emits warning");

    // 9) invalid explicit destination errors
    check(!vivid::resolve_operator_destination("relative/path", core.string(), packages, &settings, out, error),
          "relative explicit destination rejects");
    check(!error.empty(), "relative destination provides error");

    fs::remove_all(sandbox);
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}

