// test_package_scope_resolver.cpp — deterministic scope precedence and conflict handling
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/operators/operator_registry.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "test_helpers.h"

static void write_manifest(const std::filesystem::path& dir,
                           const std::string& name,
                           const std::string& version) {
    std::filesystem::create_directories(dir);
    std::ofstream ofs(dir / "vivid-package.json");
    ofs << "{\n"
        << "  \"name\": \"" << name << "\",\n"
        << "  \"version\": \"" << version << "\",\n"
        << "  \"operators\": []\n"
        << "}\n";
}

static bool find_pkg(const std::vector<vivid::PackageInfo>& pkgs,
                     const std::string& name,
                     vivid::PackageInfo& out) {
    for (const auto& p : pkgs) {
        if (p.name == name) {
            out = p;
            return true;
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    fs::path build_root = fs::absolute(fs::path(build_dir));

    std::fprintf(stderr, "\n=== Test: Package Scope Resolver ===\n\n");

    vivid::OperatorRegistry registry;
    vivid::PackageCompiler compiler(".", build_root.string());
    vivid::PackageManager pm(compiler, registry);

    fs::path sandbox = build_root / ".test_scope_resolver";
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    fs::path fake_home = sandbox / "home";
    fs::create_directories(fake_home);

    fs::path ws_root = sandbox / "workspace_root";
    fs::create_directories(ws_root / "src" / "runtime");
    {
        std::ofstream(ws_root / "CMakeLists.txt") << "cmake_minimum_required(VERSION 3.20)\n";
    }
    fs::path local_dir = ws_root / "projects" / "graph_a";
    fs::path local_pkgs = local_dir / "packages";
    fs::path ws_pkgs = ws_root / "packages";
    fs::path extra_root = sandbox / "extra_packages";
    fs::path dup_extra = sandbox / "extra_dups";

    fs::create_directories(local_dir);
    fs::create_directories(extra_root);
    fs::create_directories(dup_extra);

    auto old_cwd = fs::current_path();
    const char* old_home = std::getenv("HOME");
    std::string old_home_val = old_home ? old_home : "";
    const char* old_env = std::getenv("VIVID_PACKAGE_PATHS");
    std::string old_env_val = old_env ? old_env : "";

    setenv("HOME", fake_home.string().c_str(), 1);
    fs::path user_pkg = fs::path(vivid::PackageManager::packages_dir()) / "scope_pkg";
    fs::remove_all(user_pkg);

    fs::current_path(local_dir);

    // Seed all scopes with same package name and distinct versions.
    write_manifest(local_pkgs / "scope_local", "scope_pkg", "3.0.0");
    write_manifest(ws_pkgs / "scope_workspace", "scope_pkg", "2.0.0");
    write_manifest(user_pkg, "scope_pkg", "1.0.0");
    write_manifest(extra_root / "scope_extra", "scope_pkg", "9.0.0");
    // Separate package name to validate uninstall semantics across scopes.
    fs::path user_uninstall_pkg = fs::path(vivid::PackageManager::packages_dir()) / "scope_uninstall_pkg";
    fs::remove_all(user_uninstall_pkg);
    write_manifest(local_pkgs / "scope_uninstall_local", "scope_uninstall_pkg", "3.0.0");
    write_manifest(user_uninstall_pkg, "scope_uninstall_pkg", "1.0.0");
    setenv("VIVID_PACKAGE_PATHS", extra_root.string().c_str(), 1);

    {
        vivid::PackageInfo p;
        auto pkgs = pm.list();
        check(find_pkg(pkgs, "scope_pkg", p), "scope_pkg resolved");
        check(p.version == "3.0.0", "local scope wins");
        check(p.source_scope == "local", "source scope is local");
        check(pm.resolve_package_path("scope_pkg") == p.path, "resolved package path points to local package");
    }

    {
        vivid::PackageInfo p;
        auto pkgs = pm.list();
        check(find_pkg(pkgs, "scope_uninstall_pkg", p), "scope_uninstall_pkg resolved");
        check(p.source_scope == "local", "scope_uninstall_pkg initially resolved from local");
        check(pm.uninstall("scope_uninstall_pkg").success, "uninstall removes user-scope package entry");
        check(!fs::exists(user_uninstall_pkg), "user-scope package directory removed by uninstall");
        auto pkgs_after = pm.list();
        check(find_pkg(pkgs_after, "scope_uninstall_pkg", p),
              "scope_uninstall_pkg still resolves after user uninstall");
        check(p.source_scope == "local", "scope_uninstall_pkg remains from local scope after uninstall");
    }

    fs::remove_all(local_pkgs / "scope_local");
    {
        vivid::PackageInfo p;
        auto pkgs = pm.list();
        check(find_pkg(pkgs, "scope_pkg", p), "scope_pkg resolved after local removal");
        check(p.version == "2.0.0", "workspace scope wins after local removal");
        check(p.source_scope == "workspace", "source scope is workspace");
        check(pm.resolve_package_path("scope_pkg") == p.path, "resolved package path points to workspace package");
    }

    fs::remove_all(ws_pkgs / "scope_workspace");
    {
        vivid::PackageInfo p;
        auto pkgs = pm.list();
        check(find_pkg(pkgs, "scope_pkg", p), "scope_pkg resolved after workspace removal");
        check(p.version == "1.0.0", "user scope wins after workspace removal");
        check(p.source_scope == "user", "source scope is user");
        check(pm.resolve_package_path("scope_pkg") == p.path, "resolved package path points to user package");
    }

    fs::remove_all(user_pkg);
    {
        vivid::PackageInfo p;
        auto pkgs = pm.list();
        check(find_pkg(pkgs, "scope_pkg", p), "scope_pkg resolved after user removal");
        check(p.version == "9.0.0", "extra scope used when only extra remains");
        check(p.source_scope == "extra", "source scope is extra");
        check(pm.resolve_package_path("scope_pkg") == p.path, "resolved package path points to extra package");
    }

    // Same-scope duplicate names should be rejected entirely.
    write_manifest(dup_extra / "dup_a", "dup_pkg", "1.0.0");
    write_manifest(dup_extra / "dup_b", "dup_pkg", "2.0.0");
    setenv("VIVID_PACKAGE_PATHS", dup_extra.string().c_str(), 1);
    {
        vivid::PackageInfo p;
        auto pkgs = pm.list();
        check(!find_pkg(pkgs, "dup_pkg", p), "same-scope duplicate package name is rejected");
    }

    // Restore process state and cleanup.
    if (old_home) setenv("HOME", old_home_val.c_str(), 1);
    else unsetenv("HOME");
    if (old_env) setenv("VIVID_PACKAGE_PATHS", old_env_val.c_str(), 1);
    else unsetenv("VIVID_PACKAGE_PATHS");
    fs::current_path(old_cwd);

    fs::remove_all(sandbox);
    fs::remove_all(user_pkg);
    fs::remove_all(user_uninstall_pkg);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
