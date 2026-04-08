#include "runtime/packages/package_manager_internal.h"

#include "runtime/assets/asset_library.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/platform/platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace vivid::package_manager_internal {

std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

std::vector<std::string> split_path_list(const std::string& s) {
    std::vector<std::string> out;
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t next = s.find(sep, pos);
        std::string tok = trim_copy(s.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
        if (!tok.empty()) out.push_back(tok);
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return out;
}

std::string try_normalize_dir(const std::string& p) {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(std::filesystem::path(p), ec);
    if (!ec) return canon.string();
    return std::filesystem::path(p).lexically_normal().string();
}

std::string discover_workspace_root() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    if (ec) return "";
    for (int depth = 0; depth < 20; ++depth) {
        if (fs::exists(cur / "CMakeLists.txt") && fs::exists(cur / "src" / "runtime")) {
            return cur.string();
        }
        if (!cur.has_parent_path()) break;
        auto parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return "";
}

void append_scope_root(std::vector<ScopeRoot>& roots,
                              std::unordered_set<std::string>& seen,
                              const std::string& scope,
                              const std::string& path,
                              int precedence) {
    if (path.empty()) return;
    std::string norm = try_normalize_dir(path);
    if (seen.insert(norm).second) {
        roots.push_back({scope, norm, precedence});
    }
}

std::vector<ScopeRoot> discover_scope_roots() {
    namespace fs = std::filesystem;
    std::vector<ScopeRoot> roots;
    std::unordered_set<std::string> seen;

    std::error_code ec;
    fs::path cwd = fs::current_path(ec);
    if (!ec) {
        append_scope_root(roots, seen, "local", (cwd / "packages").string(), 0);
        append_scope_root(roots, seen, "local", (cwd / "operators" / "packages").string(), 0);
    }

    std::string workspace_root = discover_workspace_root();
    if (!workspace_root.empty()) {
        append_scope_root(roots, seen, "workspace",
                          (fs::path(workspace_root) / "packages").string(), 1);
        append_scope_root(roots, seen, "workspace",
                          (fs::path(workspace_root) / "operators" / "packages").string(), 1);
    }

    append_scope_root(roots, seen, "user", PackageManager::packages_dir(), 2);

    const char* extra_env = std::getenv("VIVID_PACKAGE_PATHS");
    if (extra_env && *extra_env) {
        for (const auto& p : split_path_list(extra_env)) {
            append_scope_root(roots, seen, "extra", p, 3);
        }
    }

    return roots;
}

bool parse_semver_triplet(const std::string& raw, std::array<int, 3>& out,
                                 bool& is_prerelease) {
    std::string s = trim_copy(raw);
    if (s.empty()) return false;
    if (s[0] == 'v' || s[0] == 'V') s.erase(0, 1);

    size_t metadata = s.find_first_of("-+");
    // '-' indicates a pre-release identifier (e.g. 1.0.0-alpha); '+' is build metadata only
    is_prerelease = (metadata != std::string::npos && s[metadata] == '-');
    if (metadata != std::string::npos) s = s.substr(0, metadata);
    if (s.empty()) return false;

    out = {0, 0, 0};
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t next = s.find('.', pos);
        parts.push_back(s.substr(pos, next == std::string::npos ? std::string::npos : (next - pos)));
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    if (parts.empty() || parts.size() > 3) return false;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].empty()) return false;
        for (char c : parts[i]) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        try { out[i] = std::stoi(parts[i]); }
        catch (const std::exception&) { return false; }
    }
    return true;
}

bool compare_semver(const std::string& a, const std::string& b, int& cmp) {
    std::array<int, 3> va{};
    std::array<int, 3> vb{};
    bool a_pre = false, b_pre = false;
    if (!parse_semver_triplet(a, va, a_pre) || !parse_semver_triplet(b, vb, b_pre)) {
        cmp = std::numeric_limits<int>::min();
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (va[i] < vb[i]) { cmp = -1; return true; }
        if (va[i] > vb[i]) { cmp = 1; return true; }
    }
    // Same numeric triplet: per SemVer, pre-release < stable (e.g. 1.0.0-alpha < 1.0.0)
    if (a_pre && !b_pre) { cmp = -1; return true; }
    if (!a_pre && b_pre) { cmp = 1; return true; }
    cmp = 0;
    return true;
}

bool eval_constraint_cmp(int cmp, const std::string& op) {
    if (op == ">")  return cmp > 0;
    if (op == ">=") return cmp >= 0;
    if (op == "<")  return cmp < 0;
    if (op == "<=") return cmp <= 0;
    if (op == "=" || op == "==") return cmp == 0;
    return false;
}

bool is_core_version_compatible(const std::string& core_version,
                                       const std::string& vivid_core_range,
                                       bool& constraint_valid) {
    constraint_valid = true;
    std::string range = trim_copy(vivid_core_range);
    if (range.empty()) return true;  // no constraint == compatible by default

    std::istringstream iss(range);
    std::string token;
    while (iss >> token) {
        std::string op = "=";
        std::string rhs = token;
        if (token.rfind(">=", 0) == 0 || token.rfind("<=", 0) == 0 || token.rfind("==", 0) == 0) {
            op = token.substr(0, 2);
            rhs = token.substr(2);
        } else if (!token.empty() && (token[0] == '>' || token[0] == '<' || token[0] == '=')) {
            op = token.substr(0, 1);
            rhs = token.substr(1);
        }

        rhs = trim_copy(rhs);
        if (rhs.empty()) {
            constraint_valid = false;
            return false;
        }

        int cmp = 0;
        if (!compare_semver(core_version, rhs, cmp)) {
            constraint_valid = false;
            return false;
        }
        if (!eval_constraint_cmp(cmp, op)) return false;
    }
    return true;
}

} // namespace vivid::package_manager_internal

namespace vivid {

using package_manager_internal::PackageCandidate;
using package_manager_internal::compare_semver;
using package_manager_internal::discover_workspace_root;
using package_manager_internal::discover_scope_roots;
using package_manager_internal::is_core_version_compatible;
using package_manager_internal::parse_semver_triplet;

std::vector<PackageInfo> PackageManager::resolve_packages(bool emit_warnings) {
    namespace fs = std::filesystem;
    std::vector<PackageCandidate> candidates;

    auto scope_roots = discover_scope_roots();
    for (const auto& sr : scope_roots) {
        if (!fs::exists(sr.root)) continue;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(sr.root, ec)) {
            if (ec) break;
            if (!entry.is_directory()) continue;

            // Skip in-progress staging directories — they're transient and must not
            // appear as installed packages (otherwise circular-dep detection breaks).
            std::string dir_name = entry.path().filename().string();
            if (dir_name.size() > 9 && dir_name.compare(0, 9, ".staging_") == 0) continue;

            PackageInfo info;
            auto [manifest_code, manifest_msg] = parse_manifest(entry.path().string(), info);
            if (!manifest_code.empty()) {
                if (emit_warnings) {
                    std::fprintf(stderr, "[vivid] PackageManager: warning: %s in %s (scope=%s)\n",
                                 manifest_msg.c_str(), entry.path().string().c_str(), sr.scope.c_str());
                }
                continue;
            }

            info.path = entry.path().string();
            info.linked = entry.is_symlink();
            info.source_scope = sr.scope;
            candidates.push_back({std::move(info), sr.scope, sr.root, sr.precedence});
        }
    }

    // Same-scope duplicate names are treated as errors; skip all duplicates in that scope.
    std::map<std::pair<std::string, std::string>, std::vector<size_t>> by_scope_and_name;
    for (size_t i = 0; i < candidates.size(); ++i) {
        by_scope_and_name[{candidates[i].source_scope, candidates[i].info.name}].push_back(i);
    }
    for (const auto& [key, idxs] : by_scope_and_name) {
        if (idxs.size() <= 1) continue;
        for (size_t idx : idxs) candidates[idx].invalid_same_scope = true;
        if (emit_warnings) {
            std::fprintf(stderr,
                         "[vivid] PackageManager: error: duplicate package name '%s' in scope '%s' (skipping all duplicates)\n",
                         key.second.c_str(), key.first.c_str());
            for (size_t idx : idxs) {
                std::fprintf(stderr, "  - %s\n", candidates[idx].info.path.c_str());
            }
        }
    }

    // Cross-scope resolution: first by precedence wins.
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const PackageCandidate& a, const PackageCandidate& b) {
            if (a.precedence != b.precedence) return a.precedence < b.precedence;
            return a.info.path < b.info.path;
        });

    std::vector<PackageInfo> resolved;
    std::unordered_map<std::string, size_t> selected_by_name;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto& c = candidates[i];
        if (c.invalid_same_scope) continue;
        auto it = selected_by_name.find(c.info.name);
        if (it == selected_by_name.end()) {
            selected_by_name[c.info.name] = resolved.size();
            resolved.push_back(c.info);
        } else if (emit_warnings) {
            const auto& winner = resolved[it->second];
            std::fprintf(stderr,
                         "[vivid] PackageManager: warning: shadowed package '%s' (%s:%s) by (%s:%s)\n",
                         c.info.name.c_str(),
                         c.info.source_scope.c_str(), c.info.path.c_str(),
                         winner.source_scope.c_str(), winner.path.c_str());
        }
    }

    std::sort(resolved.begin(), resolved.end(),
              [](const PackageInfo& a, const PackageInfo& b) { return a.name < b.name; });
    return resolved;
}

PackageUpdateAssessment PackageManager::assess_update(const PackageInfo& installed,
                                                      const std::string& remote_version,
                                                      const std::string& remote_vivid_core,
                                                      const std::string& core_version) {
    PackageUpdateAssessment out;
    out.package_name = installed.name;
    out.installed_version = installed.version;
    out.remote_version = remote_version;
    out.remote_vivid_core = remote_vivid_core;

    int cmp = 0;
    if (!compare_semver(installed.version, remote_version, cmp)) {
        out.classification = PackageUpdateClass::InvalidVersionData;
        out.compatible = false;
        out.constraint_valid = false;
        out.message = "invalid installed or remote semantic version";
        return out;
    }

    if (cmp < 0) {
        out.update_available = true;
        bool constraint_valid = true;
        bool compatible = is_core_version_compatible(core_version, remote_vivid_core, constraint_valid);
        out.compatible = compatible;
        out.constraint_valid = constraint_valid;
        if (!constraint_valid) {
            out.classification = PackageUpdateClass::InvalidVersionData;
            out.message = "invalid vivid_core compatibility constraint";
        } else if (compatible) {
            out.classification = PackageUpdateClass::CompatibleUpdate;
            out.message = "newer compatible version available";
        } else {
            out.classification = PackageUpdateClass::IncompatibleUpdate;
            out.message = "newer version requires different vivid core version";
        }
        return out;
    }

    if (cmp == 0) {
        out.classification = PackageUpdateClass::UpToDate;
        out.message = "package is up to date";
        return out;
    }

    out.classification = PackageUpdateClass::RemoteOlderOrEqual;
    out.message = "remote version is not newer than installed version";
    return out;
}

PackageUpdateClass PackageManager::classify_version_delta(const std::string& saved_version,
                                                          const std::string& installed_version) {
    if (saved_version.empty() || installed_version.empty())
        return PackageUpdateClass::InvalidVersionData;
    std::array<int, 3> sv{}, iv{};
    bool sv_pre = false, iv_pre = false;
    if (!parse_semver_triplet(saved_version, sv, sv_pre) ||
        !parse_semver_triplet(installed_version, iv, iv_pre))
        return PackageUpdateClass::InvalidVersionData;
    int cmp = 0;
    if (!compare_semver(saved_version, installed_version, cmp))
        return PackageUpdateClass::InvalidVersionData;
    if (cmp == 0) return PackageUpdateClass::UpToDate;
    if (sv[0] != iv[0]) return PackageUpdateClass::IncompatibleUpdate;
    return PackageUpdateClass::CompatibleUpdate;
}

bool PackageManager::is_installed(const std::string& name) const {
    auto pkgs = PackageManager::resolve_packages(false);
    for (const auto& p : pkgs) {
        if (p.name == name) return true;
    }
    return false;
}

std::string PackageManager::resolve_package_path(const std::string& name) const {
    auto pkgs = PackageManager::resolve_packages(false);
    for (const auto& p : pkgs) {
        if (p.name == name) return p.path;
    }
    return {};
}

std::string PackageManager::packages_dir() {
    return get_config_dir() + "/packages";
}

std::vector<PackageInfo> PackageManager::list() {
    return PackageManager::resolve_packages(true);
}

void PackageManager::scan_installed() {
    namespace fs = std::filesystem;
    discovery_report_ = {};

    // Clear stale package-scoped assets before re-scanning
    if (asset_library_) asset_library_->clear_package_assets();

    // Record scopes searched
    auto scope_roots = discover_scope_roots();
    for (const auto& sr : scope_roots) {
        std::error_code ec;
        discovery_report_.scopes_searched.push_back({
            sr.scope, sr.root, fs::exists(sr.root, ec)
        });
    }

    // Record workspace detection
    std::string ws = discover_workspace_root();
    discovery_report_.workspace_detected = !ws.empty();
    if (ws.empty()) {
        std::fprintf(stderr,
            "[vivid] Package discovery: workspace scope not detected "
            "(no CMakeLists.txt + src/runtime/ found above CWD)\n");
    }

    std::vector<PackageInfo> all_packages = PackageManager::resolve_packages(true);
    if (all_packages.empty()) return;

    // Register expected operators for ALL packages (even unbuilt) so provenance
    // diagnostics can explain why an operator is missing.
    for (const auto& info : all_packages) {
        bool built = std::filesystem::exists(info.path + "/build");
        auto register_ops = [&](const std::vector<std::string>& ops) {
            for (const auto& op : ops) {
                // Manifest uses target paths like "audio/spread_adsr"; extract stem
                std::string stem = op;
                auto slash = stem.rfind('/');
                if (slash != std::string::npos) stem = stem.substr(slash + 1);
                OperatorProvenance prov;
                prov.package_name = info.name;
                prov.package_path = info.path;
                prov.package_built = built;
                registry_.register_expected_operator(stem, std::move(prov));
            }
        };
        register_ops(info.operators);
        register_ops(info.gpu_operators);
    }

    // First pass: keep only packages that have a build directory.
    std::vector<PackageInfo> loadable_packages;
    std::unordered_map<std::string, size_t> name_to_idx;
    for (auto& info : all_packages) {
        std::string build_dir = info.path + "/build";
        if (!fs::exists(build_dir)) {
            discovery_report_.skipped_packages.push_back({
                info.name, info.path, info.source_scope,
                "not_built",
                "No build/ directory. Run 'vivid rebuild " + info.name + "'."
            });
            continue;
        }
        name_to_idx[info.name] = loadable_packages.size();
        loadable_packages.push_back(std::move(info));
    }
    if (loadable_packages.empty()) return;

    // Build adjacency list and in-degree counts for topological sort
    size_t n = loadable_packages.size();
    std::vector<std::vector<size_t>> dependents(n);  // dep → packages that depend on it
    std::vector<int> in_degree(n, 0);

    for (size_t i = 0; i < n; i++) {
        for (const auto& dep_name : loadable_packages[i].dependencies.packages) {
            auto it = name_to_idx.find(dep_name);
            if (it != name_to_idx.end()) {
                dependents[it->second].push_back(i);
                in_degree[i]++;
            }
        }
    }

    // Kahn's algorithm
    std::queue<size_t> ready;
    for (size_t i = 0; i < n; i++) {
        if (in_degree[i] == 0) ready.push(i);
    }

    std::vector<size_t> sorted_order;
    sorted_order.reserve(n);
    while (!ready.empty()) {
        size_t idx = ready.front();
        ready.pop();
        sorted_order.push_back(idx);
        for (size_t dep_idx : dependents[idx]) {
            if (--in_degree[dep_idx] == 0)
                ready.push(dep_idx);
        }
    }

    // If cycle detected among installed packages, warn and append remaining
    if (sorted_order.size() < n) {
        std::fprintf(stderr, "[vivid] PackageManager: warning: dependency cycle detected among installed packages\n");
        std::unordered_set<size_t> sorted_set(sorted_order.begin(), sorted_order.end());
        for (size_t i = 0; i < n; i++) {
            if (sorted_set.find(i) == sorted_set.end())
                sorted_order.push_back(i);
        }
    }

    // Load in topological order
    int count = 0;
    for (size_t idx : sorted_order) {
        const auto& info = loadable_packages[idx];
        std::string build_dir = info.path + "/build";
        registry_.clear_deferred_probe_handles_for_dir(build_dir);
        registry_.scan_deferred(build_dir.c_str());
        registry_.register_package(info.name, build_dir);
        registry_.scan_shader_operators(info.path + "/filters", false, info.name);
        registry_.scan_factory_presets(info.path + "/factory_presets");

        // Discover package assets for the asset library
        if (asset_library_) {
            for (const auto& handler_ptr : asset_library_->kind_registry().handlers()) {
                const AssetKindHandler& handler = *handler_ptr;
                std::vector<std::string> dirs;

                auto declared = info.assets.dirs_by_kind.find(handler.kind_name());
                if (declared != info.assets.dirs_by_kind.end())
                    dirs = declared->second;
                else
                    dirs = handler.conventional_package_dirs();

                if (!dirs.empty()) {
                    asset_library_->discover_package_assets(
                        info.name, info.path, handler.kind(), dirs);
                }
            }
        }

        // Load subgraph modules declared in package manifest
        if (subgraph_modules_ && !info.modules.empty()) {
            for (const auto& mod_path : info.modules) {
                std::string abs_path = info.path + "/" + mod_path;
                if (subgraph_modules_->load(abs_path)) {
                    std::fprintf(stderr, "[vivid] PackageManager: loaded module %s from %s\n",
                                 mod_path.c_str(), info.name.c_str());
                }
            }
        }

        count++;
        discovery_report_.loaded_packages.push_back(info);
        std::fprintf(stderr, "[vivid] PackageManager: loaded package %s [%s] (%zu operators)\n",
                     info.name.c_str(),
                     info.source_scope.empty() ? "unknown" : info.source_scope.c_str(),
                     info.operators.size() + info.gpu_operators.size());
    }

    // Startup summary
    size_t skipped = discovery_report_.skipped_packages.size();
    std::fprintf(stderr, "[vivid] Package discovery: %zu scope(s), %d loaded, %zu skipped\n",
                 discovery_report_.scopes_searched.size(), count, skipped);
    for (const auto& sp : discovery_report_.skipped_packages) {
        std::fprintf(stderr, "[vivid]   skipped: %s (%s)\n",
                     sp.name.c_str(), sp.detail.c_str());
    }
}

} // namespace vivid
