#include "packages/package_manifest.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace vivid {

namespace {
// True iff `candidate` (once normalized) is `root` or lives under it — the guard that keeps a
// manifest-declared vendor `include` from escaping the package with `../..`. Ported from
// vivid-classic's PackageCompiler::path_within_root.
bool path_within_root(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    namespace fs = std::filesystem;
    const fs::path r = fs::absolute(root).lexically_normal();
    const fs::path c = fs::absolute(candidate).lexically_normal();
    auto ri = r.begin();
    auto ci = c.begin();
    for (; ri != r.end() && ci != c.end(); ++ri, ++ci)
        if (*ri != *ci) return false;
    return ri == r.end();
}
}  // namespace

PackageManifest parse_package_manifest(const std::string& package_dir) {
    namespace fs = std::filesystem;
    PackageManifest m;
    m.dir = package_dir;

    const fs::path mf = fs::path(package_dir) / "vivid-package.json";
    std::ifstream f(mf);
    if (!f) { m.error = "no vivid-package.json in " + package_dir; return m; }
    m.manifest_present = true;   // Ph5 P2-02: the file exists, so any error below is a REAL bad package

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
        op.kind   = jo.value("kind", std::string());
        if (op.name.empty() || op.source.empty()) {
            m.error = "each operator needs \"name\" and \"source\""; return m;
        }
        if (!op.kind.empty() && op.kind != "gpu_visual" && op.kind != "audio_effect" &&
            op.kind != "instrument" && op.kind != "frame" && op.kind != "generator" &&
            op.kind != "note_effect" && op.kind != "modulator") {
            m.error = "operator \"" + op.name + "\" has unknown \"kind\": " + op.kind +
                      " (expected gpu_visual | audio_effect | instrument | frame | generator | "
                      "note_effect | modulator)"; return m;
        }
        // wgpu link flag: an explicit "gpu" wins; otherwise derive from kind (only gpu_visual
        // links wgpu); with neither, default to linking (back-compat with kind-less manifests).
        if (jo.contains("gpu"))       op.gpu = jo["gpu"].get<bool>();
        else if (!op.kind.empty())    op.gpu = (op.kind == "gpu_visual");
        else                          op.gpu = true;
        m.operators.push_back(std::move(op));
    }
    if (m.operators.empty()) { m.error = "manifest lists no operators"; return m; }

    // ADR-0054 Stage 1: package-level vendored-header include dirs. Resolve each package-relative
    // `dependencies.vendor[].include` to an absolute dir, reject anything that escapes the package
    // root or isn't a real directory (ADR-0019 loud), then fan the resolved list onto every op so it
    // reaches the compiler through both install_package and hot_reload_manager unchanged.
    std::vector<std::string> vendor_includes;
    if (j.contains("dependencies") && j["dependencies"].is_object()) {
        const auto& deps = j["dependencies"];
        if (deps.contains("vendor")) {
            if (!deps["vendor"].is_array()) {
                m.error = "\"dependencies.vendor\" must be an array"; return m;
            }
            const fs::path root = fs::path(package_dir);
            for (const auto& jv : deps["vendor"]) {
                const std::string inc = jv.value("include", std::string());
                if (inc.empty()) {
                    m.error = "each \"dependencies.vendor\" entry needs an \"include\" path"; return m;
                }
                const fs::path resolved = (root / inc).lexically_normal();
                if (!path_within_root(root, resolved)) {
                    m.error = "vendor include \"" + inc + "\" escapes the package directory"; return m;
                }
                std::error_code ec;
                if (!fs::is_directory(resolved, ec)) {
                    m.error = "vendor include \"" + inc + "\" is not a directory in the package"; return m;
                }
                vendor_includes.push_back(resolved.string());
            }
        }
    }
    if (!vendor_includes.empty())
        for (auto& op : m.operators) op.include_dirs = vendor_includes;

    m.ok = true;
    return m;
}

}  // namespace vivid
