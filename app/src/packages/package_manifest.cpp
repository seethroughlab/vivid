#include "packages/package_manifest.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace vivid {

PackageManifest parse_package_manifest(const std::string& package_dir) {
    namespace fs = std::filesystem;
    PackageManifest m;
    m.dir = package_dir;

    const fs::path mf = fs::path(package_dir) / "vivid-package.json";
    std::ifstream f(mf);
    if (!f) { m.error = "no vivid-package.json in " + package_dir; return m; }

    nlohmann::json j;
    try { f >> j; } catch (const std::exception& e) {
        m.error = std::string("invalid JSON: ") + e.what(); return m;
    }

    m.name    = j.value("name", std::string());
    m.version = j.value("version", std::string());
    m.abi     = j.value("abi", 0);   // optional declared operator ABI (0 = unspecified)
    if (m.name.empty()) { m.error = "manifest missing \"name\""; return m; }

    if (!j.contains("operators") || !j["operators"].is_array()) {
        m.error = "manifest missing \"operators\" array"; return m;
    }
    for (const auto& jo : j["operators"]) {
        PackageOperator op;
        op.name   = jo.value("name", std::string());
        op.source = jo.value("source", std::string());
        op.gpu    = jo.value("gpu", true);
        if (op.name.empty() || op.source.empty()) {
            m.error = "each operator needs \"name\" and \"source\""; return m;
        }
        m.operators.push_back(std::move(op));
    }
    if (m.operators.empty()) { m.error = "manifest lists no operators"; return m; }

    m.ok = true;
    return m;
}

}  // namespace vivid
