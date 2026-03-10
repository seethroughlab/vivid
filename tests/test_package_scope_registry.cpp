// test_package_scope_registry.cpp — registry loading uses resolved package winner only
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static std::string find_source_dir(const std::filesystem::path& build_root) {
    namespace fs = std::filesystem;
    fs::path probe = build_root;
    for (int i = 0; i < 8 && !probe.empty(); ++i) {
        if (fs::exists(probe / "CMakeLists.txt") && fs::exists(probe / "src" / "runtime")) {
            return probe.string();
        }
        if (!probe.has_parent_path()) break;
        auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    return {};
}

static void write_manifest_with_operator(const std::filesystem::path& package_dir,
                                         const std::string& package_name,
                                         const std::string& op_name) {
    std::filesystem::create_directories(package_dir);
    std::ofstream ofs(package_dir / "vivid-package.json");
    ofs << "{\n"
        << "  \"name\": \"" << package_name << "\",\n"
        << "  \"version\": \"1.0.0\",\n"
        << "  \"operators\": [\"control/" << op_name << "\"]\n"
        << "}\n";
}

static void write_control_operator(const std::filesystem::path& package_dir,
                                   const std::string& op_name,
                                   const std::string& type_name) {
    auto op_dir = package_dir / "operators" / "control" / op_name;
    std::filesystem::create_directories(op_dir);
    std::ofstream ofs(op_dir / (op_name + ".cpp"));
    ofs << "#include \"operator_api/operator.h\"\n\n"
        << "struct " << type_name << " : vivid::ControlOperatorBase {\n"
        << "    static constexpr const char* kName   = \"" << type_name << "\";\n"
        << "    static constexpr bool kTimeDependent = false;\n\n"
        << "    void collect_params(std::vector<vivid::ParamBase*>& out) override {\n"
        << "        (void)out;\n"
        << "    }\n\n"
        << "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
        << "        out.push_back({\"out\", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});\n"
        << "    }\n\n"
        << "    void process(const VividProcessContext* ctx) override {\n"
        << "        (void)ctx;\n"
        << "    }\n"
        << "};\n\n"
        << "VIVID_REGISTER(" << type_name << ")\n";
}

static void dump_file_if_exists(const std::filesystem::path& p) {
    if (!std::filesystem::exists(p)) return;
    std::fprintf(stderr, "--- %s ---\n", p.string().c_str());
    std::ifstream ifs(p);
    std::string line;
    while (std::getline(ifs, line)) {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    std::fprintf(stderr, "--- end ---\n");
}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    fs::path build_root = fs::absolute(fs::path(build_dir));

    std::string source_dir = find_source_dir(build_root);
    if (source_dir.empty()) {
        std::fprintf(stderr, "Cannot determine source directory, skipping test\n");
        return 0;
    }

    std::fprintf(stderr, "\n=== Test: Package Scope Registry ===\n\n");

    vivid::OperatorRegistry registry;
    vivid::PackageCompiler compiler(source_dir, build_root.string());
    vivid::PackageManager pm(compiler, registry);

    fs::path sandbox = build_root / ".test_scope_registry";
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    fs::path fake_home = sandbox / "home";
    fs::create_directories(fake_home);

    fs::path ws_root = sandbox / "workspace_root";
    fs::create_directories(ws_root / "src" / "runtime");
    { std::ofstream(ws_root / "CMakeLists.txt") << "cmake_minimum_required(VERSION 3.20)\n"; }
    fs::path local_dir = ws_root / "projects" / "graph_a";
    fs::path local_pkgs = local_dir / "packages";
    fs::create_directories(local_dir);

    auto old_cwd = fs::current_path();
    const char* old_home = std::getenv("HOME");
    std::string old_home_val = old_home ? old_home : "";
    const char* old_paths = std::getenv("VIVID_PACKAGE_PATHS");
    std::string old_paths_val = old_paths ? old_paths : "";

    setenv("HOME", fake_home.string().c_str(), 1);
    unsetenv("VIVID_PACKAGE_PATHS");
    fs::current_path(local_dir);

    const std::string pkg_name = "scope_registry_pkg";
    fs::path local_pkg = local_pkgs / pkg_name;
    fs::path user_pkg = fs::path(vivid::PackageManager::packages_dir()) / pkg_name;

    write_manifest_with_operator(local_pkg, pkg_name, "local_scope_op");
    write_control_operator(local_pkg, "local_scope_op", "ScopeLocalOnlyOp");
    write_manifest_with_operator(user_pkg, pkg_name, "user_scope_op");
    write_control_operator(user_pkg, "user_scope_op", "ScopeUserOnlyOp");

    auto local_cr = compiler.compile_operator(local_pkg.string(), "control/local_scope_op", false);
    auto user_cr = compiler.compile_operator(user_pkg.string(), "control/user_scope_op", false);
    if (!local_cr.success) {
        dump_file_if_exists(local_pkg / "operators" / "control" / "local_scope_op" / "local_scope_op.cpp");
    }
    if (!user_cr.success) {
        dump_file_if_exists(user_pkg / "operators" / "control" / "user_scope_op" / "user_scope_op.cpp");
    }
    check(local_cr.success, "local package operator compiles");
    check(user_cr.success, "user package operator compiles");

    pm.scan_installed();

    vivid::PackageInfo resolved_pkg;
    bool found_pkg = false;
    for (const auto& p : pm.list()) {
        if (p.name == pkg_name) {
            resolved_pkg = p;
            found_pkg = true;
            break;
        }
    }
    check(found_pkg, "resolved package exists");
    check(resolved_pkg.source_scope == "local", "local scope package wins for registry load");

    auto names = registry.type_names();
    bool found_local_type = false;
    bool found_user_type = false;
    for (const auto& n : names) {
        if (n == "ScopeLocalOnlyOp") found_local_type = true;
        if (n == "ScopeUserOnlyOp") found_user_type = true;
    }
    check(found_local_type, "winner scope operator is present in registry");
    check(!found_user_type, "shadowed scope operator is not loaded");

    const auto* local_pkg_name = registry.package_for_type("ScopeLocalOnlyOp");
    check(local_pkg_name && *local_pkg_name == pkg_name, "winner operator package attribution is correct");
    check(registry.package_for_type("ScopeUserOnlyOp") == nullptr,
          "shadowed operator has no package attribution");

    if (old_home) setenv("HOME", old_home_val.c_str(), 1);
    else unsetenv("HOME");
    if (old_paths) setenv("VIVID_PACKAGE_PATHS", old_paths_val.c_str(), 1);
    else unsetenv("VIVID_PACKAGE_PATHS");
    fs::current_path(old_cwd);
    fs::remove_all(sandbox);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
