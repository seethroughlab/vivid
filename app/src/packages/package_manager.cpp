#include "packages/package_manager.h"

#include <filesystem>
#include <cstdlib>

namespace vivid {

std::string user_operators_dir() {
    namespace fs = std::filesystem;
    if (const char* env = std::getenv("VIVID_OPERATORS_DIR")) return env;
    const char* home = std::getenv("HOME");
    fs::path dir = (home ? fs::path(home) : fs::path("."))
        / "Library" / "Application Support" / "Vivid" / "operators";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

PackageInstallResult install_package(const std::string& package_dir) {
    PackageInstallResult r;
    PackageManifest m = parse_package_manifest(package_dir);
    if (!m.ok) { r.error = m.error; return r; }
    r.name = m.name;

    const std::string out = user_operators_dir();
    PackageCompiler compiler;
    for (const auto& op : m.operators)
        r.compiles.push_back(compiler.compile_operator(package_dir, op, out));

    r.ok = true;  // the install ran; per-operator success is in compiles[].success
    return r;
}

std::vector<PackageManifest> discover_packages(const std::string& scope_dir) {
    namespace fs = std::filesystem;
    std::vector<PackageManifest> out;
    std::error_code ec;
    if (!fs::is_directory(scope_dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(scope_dir, ec)) {
        if (ec) break;
        if (!e.is_directory()) continue;
        PackageManifest m = parse_package_manifest(e.path().string());
        if (m.ok) out.push_back(std::move(m));
    }
    return out;
}

}  // namespace vivid
