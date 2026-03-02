#include "runtime/package_manager.h"
#include "runtime/operator_registry.h"
#include "runtime/platform.h"
#include "yyjson.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace vivid {

static std::string quote(const std::string& s) {
    return "'" + s + "'";
}

PackageManager::PackageManager(PackageCompiler& compiler, OperatorRegistry& registry)
    : compiler_(compiler)
    , registry_(registry) {}

void PackageManager::set_resolver(PackageResolver resolver) {
    resolver_ = std::move(resolver);
}

bool PackageManager::is_installed(const std::string& name) const {
    return std::filesystem::exists(packages_dir() + "/" + name + "/vivid-package.json");
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

    if (!name_v || !yyjson_is_str(name_v)) {
        yyjson_doc_free(doc);
        return false;
    }

    info.name = yyjson_get_str(name_v);
    info.version = (ver_v && yyjson_is_str(ver_v)) ? yyjson_get_str(ver_v) : "0.0.0";
    info.description = (desc_v && yyjson_is_str(desc_v)) ? yyjson_get_str(desc_v) : "";
    info.path = package_dir;

    yyjson_val* build_v = yyjson_obj_get(root, "build");
    info.build_type = (build_v && yyjson_is_str(build_v)) ? yyjson_get_str(build_v) : "";

    yyjson_val* ops = yyjson_obj_get(root, "operators");
    if (ops && yyjson_is_arr(ops)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(ops, idx, max, val) {
            if (yyjson_is_str(val))
                info.operators.push_back(yyjson_get_str(val));
        }
    }

    yyjson_val* gpu_ops = yyjson_obj_get(root, "gpu_operators");
    if (gpu_ops && yyjson_is_arr(gpu_ops)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(gpu_ops, idx, max, val) {
            if (yyjson_is_str(val))
                info.gpu_operators.push_back(yyjson_get_str(val));
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
        // Git clone (quote URL and path for spaces)
        std::string cmd = "git clone --depth 1 '" + url + "' '" + staging_dir + "' 2>&1";
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

    if (!compile_package(pkg_dir, result))
        return result;

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: installed %s (%zu operators)\n",
                 result.info.name.c_str(),
                 result.info.operators.size() + result.info.gpu_operators.size());
    return result;
}

bool PackageManager::compile_package(const std::string& pkg_dir, InstallResult& result) {
    std::string build_dir = pkg_dir + "/build";

    if (result.info.build_type == "cmake") {
        // CMake-based package: configure + build
        std::filesystem::create_directories(build_dir);

        std::string src_dir = compiler_.src_dir();
        std::string vivid_build = compiler_.build_dir();

        // Configure
        std::string cmake_cmd = "cmake"
            " -B " + quote(build_dir) +
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
        std::string build_cmd = "cmake --build " + quote(build_dir) + " 2>&1";
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
    std::vector<PackageInfo> packages;
    std::string dir = packages_dir();

    if (!std::filesystem::exists(dir))
        return packages;

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;

        PackageInfo info;
        if (parse_manifest(entry.path().string(), info)) {
            info.linked = entry.is_symlink();
            packages.push_back(std::move(info));
        }
    }

    return packages;
}

void PackageManager::scan_installed() {
    std::string dir = packages_dir();
    if (!std::filesystem::exists(dir)) return;

    // First pass: parse all manifests
    std::vector<PackageInfo> all_packages;
    std::unordered_map<std::string, size_t> name_to_idx;

    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;

        std::string build_dir = entry.path().string() + "/build";
        if (!std::filesystem::exists(build_dir)) continue;

        PackageInfo info;
        if (parse_manifest(entry.path().string(), info)) {
            name_to_idx[info.name] = all_packages.size();
            all_packages.push_back(std::move(info));
        }
    }

    if (all_packages.empty()) return;

    // Build adjacency list and in-degree counts for topological sort
    size_t n = all_packages.size();
    std::vector<std::vector<size_t>> dependents(n);  // dep → packages that depend on it
    std::vector<int> in_degree(n, 0);

    for (size_t i = 0; i < n; i++) {
        for (const auto& dep_name : all_packages[i].dependencies.packages) {
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
        const auto& info = all_packages[idx];
        std::string build_dir = info.path + "/build";
        registry_.scan_deferred(build_dir.c_str());
        registry_.register_package(info.name, build_dir);
        count++;
        std::fprintf(stderr, "[vivid] PackageManager: loaded package %s (%zu operators)\n",
                     info.name.c_str(),
                     info.operators.size() + info.gpu_operators.size());
    }

    if (count > 0) {
        std::fprintf(stderr, "[vivid] PackageManager: %d package(s) loaded\n", count);
    }
}

} // namespace vivid
