#include "runtime/package_manager.h"
#include "runtime/operator_registry.h"
#include "runtime/platform.h"
#include "yyjson.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace vivid {

static std::string quote(const std::string& s) {
    std::string escaped;
    for (char c : s) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    return "'" + escaped + "'";
}

static std::string trim_copy(const std::string& s);

struct ScopeRoot {
    std::string scope;
    std::string root;
    int precedence = 0;
};

struct PackageCandidate {
    PackageInfo info;
    std::string source_scope;
    std::string scope_root;
    int precedence = 0;
    bool invalid_same_scope = false;
};

static std::vector<std::string> split_path_list(const std::string& s) {
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

static std::string try_normalize_dir(const std::string& p) {
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(std::filesystem::path(p), ec);
    if (!ec) return canon.string();
    return std::filesystem::path(p).lexically_normal().string();
}

static std::string discover_workspace_root() {
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

static void append_scope_root(std::vector<ScopeRoot>& roots,
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

static std::vector<ScopeRoot> discover_scope_roots() {
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

static std::string trim_copy(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

static bool parse_semver_triplet(const std::string& raw, std::array<int, 3>& out,
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

static bool compare_semver(const std::string& a, const std::string& b, int& cmp) {
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

static bool eval_constraint_cmp(int cmp, const std::string& op) {
    if (op == ">")  return cmp > 0;
    if (op == ">=") return cmp >= 0;
    if (op == "<")  return cmp < 0;
    if (op == "<=") return cmp <= 0;
    if (op == "=" || op == "==") return cmp == 0;
    return false;
}

// Evaluate whether core_version satisfies a vivid_core constraint range.
// Semantics:
//   - Space-separated tokens are evaluated as implicit AND (all must pass).
//   - Supported operator prefixes: >=, <=, ==, >, <, =.
//   - A bare version token (no prefix) defaults to exact equality (= operator).
//   - Empty range string means "no constraint" and always returns true.
static bool is_core_version_compatible(const std::string& core_version,
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

static bool tool_forced_missing(const std::string& tool) {
    const char* env = std::getenv("VIVID_MOCK_MISSING_TOOL");
    if (!env || !*env) return false;
    std::string s(env);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t next = s.find(',', pos);
        std::string item = s.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item == tool) return true;
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return false;
}

static std::string find_tool(const char* tool) {
    if (tool_forced_missing(tool)) return {};
    std::string cmd = "PATH=$PATH:/opt/homebrew/bin:/usr/local/bin command -v ";
    cmd += tool;
    cmd += " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[512] = {};
    fgets(buf, sizeof(buf), f);
    pclose(f);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

static bool command_exists(const char* tool) {
    return !find_tool(tool).empty();
}

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
            if (!parse_manifest(entry.path().string(), info)) {
                if (emit_warnings) {
                    std::fprintf(stderr, "[vivid] PackageManager: warning: invalid manifest in %s (scope=%s)\n",
                                 entry.path().string().c_str(), sr.scope.c_str());
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

static std::string missing_tool_error(const char* tool) {
    std::string msg = "Missing required build tool: ";
    msg += tool;
    if (std::string(tool) == "clang++") {
        msg += ". Install Xcode Command Line Tools with `xcode-select --install`.";
    } else if (std::string(tool) == "cmake") {
        msg += ". Install CMake (e.g. `brew install cmake`).";
    } else if (std::string(tool) == "git") {
        msg += ". Install Git (included with Xcode Command Line Tools).";
    }
    return msg;
}

static std::string abi_mismatch_error_for_package(const std::string& package_name,
                                                  const std::vector<AbiMismatchDiagnostic>& mismatches) {
    std::ostringstream oss;
    oss << "Plugin ABI mismatch for package '" << package_name << "'. "
        << "Rebuild vivid and rerun package rebuild.";
    if (!mismatches.empty()) {
        oss << "\n";
        for (const auto& m : mismatches) {
            oss << "  - " << (m.plugin_name.empty() ? m.plugin_path : m.plugin_name)
                << ": plugin ABI " << m.plugin_abi
                << ", runtime ABI " << m.runtime_abi << "\n";
        }
    }
    return oss.str();
}

PackageManager::PackageManager(PackageCompiler& compiler, OperatorRegistry& registry)
    : compiler_(compiler)
    , registry_(registry) {}

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

void PackageManager::set_resolver(PackageResolver resolver) {
    resolver_ = std::move(resolver);
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

bool PackageManager::parse_manifest(const std::string& package_dir, PackageInfo& info) {
    std::string manifest_path = package_dir + "/vivid-package.json";
    std::ifstream ifs(manifest_path);
    if (!ifs) return false;

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json_str = ss.str();

    yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
    if (!doc) return false;

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root) { yyjson_doc_free(doc); return false; }

    yyjson_val* name_v = yyjson_obj_get(root, "name");
    yyjson_val* ver_v  = yyjson_obj_get(root, "version");
    yyjson_val* desc_v = yyjson_obj_get(root, "description");
    yyjson_val* core_v = yyjson_obj_get(root, "vivid_core");

    if (!name_v || !yyjson_is_str(name_v)) {
        yyjson_doc_free(doc);
        return false;
    }

    info.name = yyjson_get_str(name_v);
    info.version = (ver_v && yyjson_is_str(ver_v)) ? yyjson_get_str(ver_v) : "0.0.0";
    info.vivid_core = (core_v && yyjson_is_str(core_v)) ? yyjson_get_str(core_v) : "";
    info.description = (desc_v && yyjson_is_str(desc_v)) ? yyjson_get_str(desc_v) : "";
    info.path = package_dir;

    yyjson_val* build_v = yyjson_obj_get(root, "build");
    info.build_type = (build_v && yyjson_is_str(build_v)) ? yyjson_get_str(build_v) : "";

    // Validate that an operator name is a safe relative path with no traversal components.
    // Names like "audio/drum_kick" are fine; "../../etc/passwd" or "../bad" are not.
    auto is_valid_op_name = [](const std::string& name) -> bool {
        if (name.empty() || name[0] == '/' || name[0] == '.') return false;
        size_t pos = 0;
        while (pos <= name.size()) {
            size_t next = name.find('/', pos);
            std::string component = name.substr(
                pos, next == std::string::npos ? std::string::npos : next - pos);
            if (component.empty() || component == "." || component == "..") return false;
            if (component[0] == '.') return false;
            for (char c : component) {
                if (!std::isalnum(static_cast<unsigned char>(c)) &&
                    c != '_' && c != '-') return false;
            }
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        return true;
    };

    yyjson_val* ops = yyjson_obj_get(root, "operators");
    if (ops && yyjson_is_arr(ops)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(ops, idx, max, val) {
            if (yyjson_is_str(val)) {
                std::string op_name = yyjson_get_str(val);
                if (!is_valid_op_name(op_name)) {
                    yyjson_doc_free(doc);
                    return false;
                }
                info.operators.push_back(std::move(op_name));
            }
        }
    }

    yyjson_val* gpu_ops = yyjson_obj_get(root, "gpu_operators");
    if (gpu_ops && yyjson_is_arr(gpu_ops)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(gpu_ops, idx, max, val) {
            if (yyjson_is_str(val)) {
                std::string op_name = yyjson_get_str(val);
                if (!is_valid_op_name(op_name)) {
                    yyjson_doc_free(doc);
                    return false;
                }
                info.gpu_operators.push_back(std::move(op_name));
            }
        }
    }

    // author (optional string)
    yyjson_val* author_v = yyjson_obj_get(root, "author");
    info.author = (author_v && yyjson_is_str(author_v)) ? yyjson_get_str(author_v) : "";

    // dependencies (optional object)
    yyjson_val* deps_v = yyjson_obj_get(root, "dependencies");
    if (deps_v && yyjson_is_obj(deps_v)) {
        yyjson_val* dep_pkgs = yyjson_obj_get(deps_v, "packages");
        if (dep_pkgs && yyjson_is_arr(dep_pkgs)) {
            size_t idx, max;
            yyjson_val* val;
            yyjson_arr_foreach(dep_pkgs, idx, max, val) {
                if (yyjson_is_str(val))
                    info.dependencies.packages.push_back(yyjson_get_str(val));
            }
        }
        yyjson_val* dep_vendor = yyjson_obj_get(deps_v, "vendor");
        if (dep_vendor && yyjson_is_arr(dep_vendor)) {
            size_t idx, max;
            yyjson_val* val;
            yyjson_arr_foreach(dep_vendor, idx, max, val) {
                if (yyjson_is_obj(val)) {
                    VendorDependency vd;
                    yyjson_val* vn = yyjson_obj_get(val, "name");
                    yyjson_val* vi = yyjson_obj_get(val, "include");
                    if (vn && yyjson_is_str(vn)) vd.name = yyjson_get_str(vn);
                    if (vi && yyjson_is_str(vi)) vd.include = yyjson_get_str(vi);
                    if (!vd.name.empty())
                        info.dependencies.vendor.push_back(std::move(vd));
                }
            }
        }
    }

    // tests (optional object)
    yyjson_val* tests_v = yyjson_obj_get(root, "tests");
    if (tests_v && yyjson_is_obj(tests_v)) {
        yyjson_val* test_graphs = yyjson_obj_get(tests_v, "graphs");
        if (test_graphs && yyjson_is_arr(test_graphs)) {
            size_t idx, max;
            yyjson_val* val;
            yyjson_arr_foreach(test_graphs, idx, max, val) {
                if (yyjson_is_str(val))
                    info.tests.graphs.push_back(yyjson_get_str(val));
            }
        }
        yyjson_val* test_cpp = yyjson_obj_get(tests_v, "cpp");
        if (test_cpp && yyjson_is_arr(test_cpp)) {
            size_t idx, max;
            yyjson_val* val;
            yyjson_arr_foreach(test_cpp, idx, max, val) {
                if (yyjson_is_str(val))
                    info.tests.cpp.push_back(yyjson_get_str(val));
            }
        }
    }

    yyjson_doc_free(doc);
    return true;
}

InstallResult PackageManager::install(const std::string& url) {
    std::set<std::string> chain;
    std::vector<std::string> installed_deps;
    auto result = install_with_chain(url, chain, installed_deps);
    result.installed_deps = std::move(installed_deps);
    return result;
}

InstallResult PackageManager::install_with_chain(const std::string& url,
                                                  std::set<std::string>& installing_chain,
                                                  std::vector<std::string>& installed_deps) {
    InstallResult result;

    // Create packages directory
    std::filesystem::create_directories(packages_dir());

    // Use a temporary staging dir, then rename after parsing the manifest
    // to get the canonical package name.
    std::string staging_name = ".staging_" + std::to_string(
        std::hash<std::string>{}(url) % 999999);
    std::string staging_dir = packages_dir() + "/" + staging_name;

    // Clean up any leftover staging dir
    std::filesystem::remove_all(staging_dir);

    // Check if URL is a local path
    bool is_local = std::filesystem::exists(url);

    if (is_local) {
        // Copy local directory
        std::error_code ec;
        std::filesystem::copy(url, staging_dir,
            std::filesystem::copy_options::recursive, ec);
        if (ec) {
            result.error = "Failed to copy local package: " + ec.message();
            return result;
        }
    } else {
        if (!command_exists("git")) {
            result.error = missing_tool_error("git");
            return result;
        }
        // Git clone (quote URL and path for spaces and special characters)
        std::string cmd = "git clone --depth 1 " + quote(url) + " " + quote(staging_dir) + " 2>&1";
        std::fprintf(stderr, "[vivid] PackageManager: %s\n", cmd.c_str());

        std::string output;
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            result.error = "Failed to execute git clone";
            return result;
        }
        std::array<char, 256> buf;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            output += buf.data();
        }
        int status = pclose(pipe);
        if (status != 0) {
            result.error = "git clone failed: " + output;
            std::filesystem::remove_all(staging_dir);
            return result;
        }
    }

    // Parse manifest to get canonical package name
    if (!parse_manifest(staging_dir, result.info)) {
        result.error = "Invalid or missing vivid-package.json in package";
        std::filesystem::remove_all(staging_dir);
        return result;
    }

    // Move staging to final location using the canonical name from the manifest
    std::string pkg_dir = packages_dir() + "/" + result.info.name;

    // Check if already installed
    if (std::filesystem::exists(pkg_dir)) {
        result.error = "Package already installed: " + result.info.name +
                       " (uninstall first, or remove " + pkg_dir + ")";
        std::filesystem::remove_all(staging_dir);
        return result;
    }

    // --- Resolve & install dependencies ---
    installing_chain.insert(result.info.name);
    for (const auto& dep_name : result.info.dependencies.packages) {
        if (is_installed(dep_name)) continue;

        if (installing_chain.count(dep_name)) {
            result.error = "Circular dependency detected: " + result.info.name +
                           " -> " + dep_name + " (already in install chain)";
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        if (!resolver_) {
            result.error = "Package '" + result.info.name + "' depends on '" +
                           dep_name + "' but no package resolver is configured";
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        std::string dep_url = resolver_(dep_name);
        if (dep_url.empty()) {
            result.error = "Dependency '" + dep_name + "' not found in package catalog";
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        auto dep_result = install_with_chain(dep_url, installing_chain, installed_deps);
        if (!dep_result.success) {
            result.error = "Failed to install dependency '" + dep_name + "': " + dep_result.error;
            std::filesystem::remove_all(staging_dir);
            return result;
        }

        installed_deps.push_back(dep_name);
    }

    std::filesystem::rename(staging_dir, pkg_dir);
    result.info.path = pkg_dir;

    if (!compile_package(pkg_dir, result)) {
        // Roll back failed installs so partial packages don't remain on disk.
        std::error_code ec;
        std::filesystem::remove_all(pkg_dir, ec);
        if (ec) {
            std::fprintf(stderr, "[vivid] PackageManager: warning: failed to clean up %s after compile failure: %s\n",
                         pkg_dir.c_str(), ec.message().c_str());
        }
        return result;
    }

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: installed %s (%zu operators)\n",
                 result.info.name.c_str(),
                 result.info.operators.size() + result.info.gpu_operators.size());
    return result;
}

bool PackageManager::compile_package(const std::string& pkg_dir, InstallResult& result) {
    // build_dir is always inside pkg_dir; callers that remove pkg_dir on failure
    // implicitly clean up build_dir — no separate remove_all needed.
    std::string build_dir = pkg_dir + "/build";

    if (result.info.build_type == "cmake") {
        std::string cmake_exe = find_tool("cmake");
        if (cmake_exe.empty()) {
            result.error = missing_tool_error("cmake");
            return false;
        }
        // CMake-based package: configure + build
        std::filesystem::create_directories(build_dir);

        std::string src_dir = compiler_.src_dir();
        std::string vivid_build = compiler_.build_dir();

        // Configure
        std::string cmake_cmd = quote(cmake_exe)
            + " -B " + quote(build_dir) +
            " -S " + quote(pkg_dir) +
            " -DVIVID_SRC_DIR=" + quote(src_dir) +
            " -DVIVID_BUILD_DIR=" + quote(vivid_build) +
            " -DVIVID_PLUGIN_SUFFIX=" + kPluginSuffix +
            " 2>&1";

        std::fprintf(stderr, "[vivid] PackageManager: %s\n", cmake_cmd.c_str());

        std::string output;
        FILE* pipe = popen(cmake_cmd.c_str(), "r");
        if (!pipe) {
            result.error = "Failed to execute cmake configure";
            return false;
        }
        std::array<char, 256> buf;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr)
            output += buf.data();
        int status = pclose(pipe);

        if (status != 0) {
            result.error = "cmake configure failed:\n" + output;
            return false;
        }

        // Build
        std::string build_cmd = quote(cmake_exe) + " --build " + quote(build_dir) + " 2>&1";
        std::fprintf(stderr, "[vivid] PackageManager: %s\n", build_cmd.c_str());

        output.clear();
        pipe = popen(build_cmd.c_str(), "r");
        if (!pipe) {
            result.error = "Failed to execute cmake build";
            return false;
        }
        while (fgets(buf.data(), buf.size(), pipe) != nullptr)
            output += buf.data();
        status = pclose(pipe);

        if (status != 0) {
            result.error = "cmake build failed:\n" + output;
            return false;
        }

        // Synthesize compile results by scanning for dylibs in build dir
        for (auto& entry : std::filesystem::recursive_directory_iterator(build_dir)) {
            auto ext = entry.path().extension().string();
            if (ext == ".dylib" || ext == ".so" || ext == ".dll") {
                CompileResult cr;
                cr.success = true;
                cr.dylib_path = entry.path().string();
                cr.operator_name = entry.path().stem().string();
                result.compile_results.push_back(std::move(cr));
            }
        }
    } else {
        if (!command_exists("clang++")) {
            result.error = missing_tool_error("clang++");
            return false;
        }
        // Default: clang++ compilation via PackageCompiler
        std::vector<std::string> vendor_includes;
        for (const auto& vd : result.info.dependencies.vendor)
            vendor_includes.push_back(pkg_dir + "/" + vd.include);

        result.compile_results = compiler_.compile_all(pkg_dir,
            result.info.operators, result.info.gpu_operators, vendor_includes);

        bool all_ok = true;
        for (const auto& cr : result.compile_results) {
            if (!cr.success) {
                all_ok = false;
                break;
            }
        }

        if (!all_ok) {
            result.error = "Some operators failed to compile";
            return false;
        }
    }

    // Scan compiled operators into registry
    registry_.scan_deferred(build_dir.c_str());
    auto abi_mismatches = registry_.abi_mismatch_diagnostics_for_dir(build_dir);
    if (!abi_mismatches.empty()) {
        result.error = abi_mismatch_error_for_package(result.info.name, abi_mismatches);
        return false;
    }

    // Track provenance
    registry_.register_package(result.info.name, build_dir);

    return true;
}

InstallResult PackageManager::link(const std::string& path) {
    InstallResult result;

    // Resolve to absolute path
    std::error_code ec;
    auto canonical = std::filesystem::canonical(path, ec);
    if (ec) {
        result.error = "Path does not exist: " + path;
        return result;
    }

    // Validate it's a directory with a manifest
    if (!std::filesystem::is_directory(canonical)) {
        result.error = "Not a directory: " + canonical.string();
        return result;
    }

    if (!std::filesystem::exists(canonical / "vivid-package.json")) {
        result.error = "No vivid-package.json found in " + canonical.string();
        return result;
    }

    // Parse manifest to get canonical package name
    if (!parse_manifest(canonical.string(), result.info)) {
        result.error = "Invalid vivid-package.json in " + canonical.string();
        return result;
    }

    // Create packages directory
    std::filesystem::create_directories(packages_dir());

    // Check no existing package with same name
    std::string pkg_dir = packages_dir() + "/" + result.info.name;
    if (std::filesystem::exists(pkg_dir)) {
        result.error = "Package already exists: " + result.info.name +
                       " (uninstall or unlink first)";
        return result;
    }

    // Create symlink
    std::filesystem::create_directory_symlink(canonical, pkg_dir, ec);
    if (ec) {
        result.error = "Failed to create symlink: " + ec.message();
        return result;
    }

    result.info.path = pkg_dir;
    result.info.linked = true;

    // Compile (build/ dir ends up in the original source tree through the symlink)
    if (!compile_package(pkg_dir, result))
        return result;

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: linked %s -> %s (%zu operators)\n",
                 result.info.name.c_str(), canonical.string().c_str(),
                 result.info.operators.size() + result.info.gpu_operators.size());
    return result;
}

bool PackageManager::unlink(const std::string& name) {
    std::string pkg_dir = packages_dir() + "/" + name;

    if (!std::filesystem::exists(pkg_dir)) {
        std::fprintf(stderr, "[vivid] PackageManager: package not found: %s\n", name.c_str());
        return false;
    }

    if (!std::filesystem::is_symlink(pkg_dir)) {
        std::fprintf(stderr, "[vivid] PackageManager: '%s' is not a linked package (use uninstall instead)\n",
                     name.c_str());
        return false;
    }

    // Unregister operators from registry
    PackageInfo info;
    if (parse_manifest(pkg_dir, info)) {
        auto unregister_op = [&](const std::string& op_path) {
            auto slash = op_path.rfind('/');
            std::string target = (slash != std::string::npos) ? op_path.substr(slash + 1) : op_path;
            const std::string* type_name = registry_.type_name_for_target(target);
            if (type_name) {
                registry_.unregister_package_operator(*type_name);
            } else {
                registry_.unregister_package_operator(target);
            }
        };
        for (const auto& op : info.operators)
            unregister_op(op);
        for (const auto& op : info.gpu_operators)
            unregister_op(op);
    }

    // Remove symlink only — never follows into source tree
    std::error_code ec;
    std::filesystem::remove(pkg_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[vivid] PackageManager: failed to remove symlink %s: %s\n",
                     pkg_dir.c_str(), ec.message().c_str());
        return false;
    }

    std::fprintf(stderr, "[vivid] PackageManager: unlinked %s\n", name.c_str());
    return true;
}

InstallResult PackageManager::rebuild(const std::string& name) {
    InstallResult result;

    std::string pkg_dir = packages_dir() + "/" + name;
    if (!std::filesystem::exists(pkg_dir)) {
        result.error = "Package not found: " + name;
        return result;
    }

    if (!parse_manifest(pkg_dir, result.info)) {
        result.error = "Invalid vivid-package.json in " + name;
        return result;
    }

    result.info.linked = std::filesystem::is_symlink(pkg_dir);

    // Unregister existing operators before recompile
    auto unregister_op = [&](const std::string& op_path) {
        auto slash = op_path.rfind('/');
        std::string target = (slash != std::string::npos) ? op_path.substr(slash + 1) : op_path;
        const std::string* type_name = registry_.type_name_for_target(target);
        if (type_name) {
            registry_.unregister_package_operator(*type_name);
        } else {
            registry_.unregister_package_operator(target);
        }
    };
    for (const auto& op : result.info.operators)
        unregister_op(op);
    for (const auto& op : result.info.gpu_operators)
        unregister_op(op);

    if (!compile_package(pkg_dir, result))
        return result;

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: rebuilt %s (%zu operators)\n",
                 result.info.name.c_str(),
                 result.info.operators.size() + result.info.gpu_operators.size());
    return result;
}

bool PackageManager::uninstall(const std::string& name) {
    std::string pkg_dir = packages_dir() + "/" + name;

    if (!std::filesystem::exists(pkg_dir)) {
        std::fprintf(stderr, "[vivid] PackageManager: package not found: %s\n", name.c_str());
        return false;
    }

    // Warn if other installed packages depend on this one
    for (auto& entry : std::filesystem::directory_iterator(packages_dir())) {
        if (!entry.is_directory()) continue;
        PackageInfo dep_info;
        if (!parse_manifest(entry.path().string(), dep_info)) continue;
        if (dep_info.name == name) continue;
        for (const auto& dep : dep_info.dependencies.packages) {
            if (dep == name) {
                std::fprintf(stderr, "[vivid] PackageManager: warning: '%s' depends on '%s'\n",
                             dep_info.name.c_str(), name.c_str());
            }
        }
    }

    // Parse manifest to find operators to unregister.
    // The manifest uses relative paths like "control/test_mgr_op" where the last
    // segment is the cmake target name (dylib stem). We need to look up the actual
    // descriptor type name from the target→type mapping.
    PackageInfo info;
    if (parse_manifest(pkg_dir, info)) {
        auto unregister_op = [&](const std::string& op_path) {
            auto slash = op_path.rfind('/');
            std::string target = (slash != std::string::npos) ? op_path.substr(slash + 1) : op_path;
            const std::string* type_name = registry_.type_name_for_target(target);
            if (type_name) {
                registry_.unregister_package_operator(*type_name);
            } else {
                // Fallback: try using target name directly as type name
                registry_.unregister_package_operator(target);
            }
        };
        for (const auto& op : info.operators)
            unregister_op(op);
        for (const auto& op : info.gpu_operators)
            unregister_op(op);
    }

    // Remove directory (symlink-safe: remove link only, never follow into source)
    std::error_code ec;
    if (std::filesystem::is_symlink(pkg_dir))
        std::filesystem::remove(pkg_dir, ec);
    else
        std::filesystem::remove_all(pkg_dir, ec);
    if (ec) {
        std::fprintf(stderr, "[vivid] PackageManager: failed to remove %s: %s\n",
                     pkg_dir.c_str(), ec.message().c_str());
        return false;
    }

    std::fprintf(stderr, "[vivid] PackageManager: uninstalled %s\n", name.c_str());
    return true;
}

std::vector<PackageInfo> PackageManager::list() {
    return PackageManager::resolve_packages(true);
}

void PackageManager::scan_installed() {
    std::vector<PackageInfo> all_packages = PackageManager::resolve_packages(true);
    if (all_packages.empty()) return;

    // First pass: keep only packages that have a build directory.
    std::vector<PackageInfo> loadable_packages;
    std::unordered_map<std::string, size_t> name_to_idx;
    for (auto& info : all_packages) {
        std::string build_dir = info.path + "/build";
        if (!std::filesystem::exists(build_dir)) continue;
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
        registry_.scan_deferred(build_dir.c_str());
        registry_.register_package(info.name, build_dir);
        count++;
        std::fprintf(stderr, "[vivid] PackageManager: loaded package %s [%s] (%zu operators)\n",
                     info.name.c_str(),
                     info.source_scope.empty() ? "unknown" : info.source_scope.c_str(),
                     info.operators.size() + info.gpu_operators.size());
    }

    if (count > 0) {
        std::fprintf(stderr, "[vivid] PackageManager: %d package(s) loaded\n", count);
    }
}

} // namespace vivid
