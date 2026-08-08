// ADR-0054 Stage 1: a package operator can `#include` a VENDORED header declared in its
// manifest's `dependencies.vendor` block. The fixture op includes <vendorlib/answer.h>, which
// lives under vendor/inc/ and is reachable ONLY through the vendor include dir — so if the parser
// didn't resolve it and the compiler didn't add the -I, the compile fails. A clean install→load
// proves the whole path. A second fixture proves the escape guard rejects a `../..` include.
#include "packages/package_manifest.h"
#include "packages/package_manager.h"
#include "gpu/operator_loader.h"
#include "gpu/operator_scan.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

int main() {
    using namespace vivid;
    namespace fs = std::filesystem;

    // 1. Manifest parses and resolves the vendor include onto the operator (absolute path).
    PackageManifest m = parse_package_manifest(PKG_VENDOR_DIR);
    CHECK(m.ok);
    CHECK(m.operators.size() == 1);
    CHECK(m.operators[0].name == "VendorNoop");
    CHECK(m.operators[0].include_dirs.size() == 1);
    CHECK(!m.operators[0].include_dirs.empty() &&
          fs::path(m.operators[0].include_dirs[0]).is_absolute());

    // 2. Install: the op only compiles if the vendor -I reached the compile line.
    const std::string out = (fs::temp_directory_path() / "vivid_pkg_vendor_out").string();
    fs::remove_all(out);
    setenv("VIVID_OPERATORS_DIR", out.c_str(), 1);
    PackageInstallResult r = install_package(PKG_VENDOR_DIR);
    CHECK(r.ok);
    if (!r.compiles.empty() && !r.compiles[0].success)
        std::fprintf(stderr, "  compile error:\n%s\n", r.compiles[0].error_output.c_str());
    CHECK(!r.compiles.empty() && r.compiles[0].success);
    CHECK(!r.compiles.empty() && fs::exists(r.compiles[0].dylib_path));

    // 3. Load + register the produced dylib.
    OpRegistry reg;
    std::vector<std::unique_ptr<OperatorLoader>> loaders;
    const std::string name = load_and_register_operator(r.compiles[0].dylib_path, reg, loaders);
    CHECK(name == "VendorNoop");
    CHECK(reg.has("VendorNoop"));

    // 4. Guard: a vendor include that escapes the package root is a hard manifest error.
    PackageManifest bad = parse_package_manifest(PKG_VENDOR_ESCAPE_DIR);
    CHECK(!bad.ok);
    CHECK(!bad.error.empty());

    fs::remove_all(out);
    return vivid::test::summary("test_package_vendor_include");
}
