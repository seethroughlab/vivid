// Headless test for the P2.3 package pipeline: parse a vivid-package.json, compile
// its (GPU-free) operator to a loadable module with the package compiler, then load
// + register it through the operator loader — proving install → load end to end.
#include "packages/package_manifest.h"
#include "packages/package_manager.h"
#include "gpu/operator_loader.h"
#include "gpu/operator_scan.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

int main() {
    using namespace vivid;
    namespace fs = std::filesystem;

    // 1. Manifest parses.
    PackageManifest m = parse_package_manifest(PKG_DIR);
    CHECK(m.ok);
    CHECK(m.name == "test-pkg");
    CHECK(m.operators.size() == 1);
    CHECK(m.operators[0].name == "PkgNoop");
    CHECK(m.operators[0].gpu == false);

    // 2. Install: compile the operator into a temp managed dir (VIVID_OPERATORS_DIR).
    const std::string out = (fs::temp_directory_path() / "vivid_pkg_test_out").string();
    fs::remove_all(out);
    setenv("VIVID_OPERATORS_DIR", out.c_str(), 1);
    PackageInstallResult r = install_package(PKG_DIR);
    CHECK(r.ok);
    CHECK(r.compiles.size() == 1);
    if (!r.compiles.empty() && !r.compiles[0].success)
        std::fprintf(stderr, "  compile error:\n%s\n", r.compiles[0].error_output.c_str());
    CHECK(!r.compiles.empty() && r.compiles[0].success);
    CHECK(!r.compiles.empty() && fs::exists(r.compiles[0].dylib_path));

    // 3. Load + register the produced dylib through the operator loader.
    OpRegistry reg;
    std::vector<std::unique_ptr<OperatorLoader>> loaders;
    const std::string name = load_and_register_operator(r.compiles[0].dylib_path, reg, loaders);
    CHECK(name == "PkgNoop");
    CHECK(reg.has("PkgNoop"));
    const VividOperatorDescriptor* d = reg.descriptor_for("PkgNoop");
    CHECK(d != nullptr);
    CHECK(d->param_count == 1u);
    CHECK(d->display_name && std::string(d->display_name) == "Pkg Noop");  // metadata survived install

    fs::remove_all(out);
    return vivid::test::summary("test_package_compile");
}
