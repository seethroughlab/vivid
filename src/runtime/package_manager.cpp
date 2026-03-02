#include "runtime/package_manager.h"
#include "runtime/operator_registry.h"
#include "runtime/platform.h"
#include "yyjson.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vivid {

PackageManager::PackageManager(PackageCompiler& compiler, OperatorRegistry& registry)
    : compiler_(compiler)
    , registry_(registry) {}

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

    yyjson_doc_free(doc);
    return true;
}

InstallResult PackageManager::install(const std::string& url) {
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

    std::filesystem::rename(staging_dir, pkg_dir);
    result.info.path = pkg_dir;

    // Compile all operators
    result.compile_results = compiler_.compile_all(pkg_dir);

    // Check if any compilations failed
    bool all_ok = true;
    for (const auto& cr : result.compile_results) {
        if (!cr.success) {
            all_ok = false;
            break;
        }
    }

    if (!all_ok) {
        result.error = "Some operators failed to compile";
        // Don't clean up — leave package for user to inspect
        return result;
    }

    // Scan compiled operators into registry
    std::string build_dir = pkg_dir + "/build";
    registry_.scan_deferred(build_dir.c_str());

    // Track provenance
    registry_.register_package(result.info.name, build_dir);

    result.success = true;
    std::fprintf(stderr, "[vivid] PackageManager: installed %s (%zu operators)\n",
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

    // Remove directory
    std::error_code ec;
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
            packages.push_back(std::move(info));
        }
    }

    return packages;
}

void PackageManager::scan_installed() {
    std::string dir = packages_dir();
    if (!std::filesystem::exists(dir)) return;

    int count = 0;
    for (auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;

        std::string build_dir = entry.path().string() + "/build";
        if (!std::filesystem::exists(build_dir)) continue;

        PackageInfo info;
        if (parse_manifest(entry.path().string(), info)) {
            registry_.scan_deferred(build_dir.c_str());
            registry_.register_package(info.name, build_dir);
            count++;
            std::fprintf(stderr, "[vivid] PackageManager: loaded package %s (%zu operators)\n",
                         info.name.c_str(),
                         info.operators.size() + info.gpu_operators.size());
        }
    }

    if (count > 0) {
        std::fprintf(stderr, "[vivid] PackageManager: %d package(s) loaded\n", count);
    }
}

} // namespace vivid
