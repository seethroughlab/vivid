#include "runtime/packages/package_manager_internal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vivid::package_manager_internal {
// Scan a directory for recognizable project files when vivid-package.json is absent.
// Returns a diagnostic hint string (empty if nothing recognizable found).
std::string diagnose_non_package_dir(const std::string& dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return {};

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        if (fs::exists(entry.path() / "vivid-package.json")) {
            return "Found vivid-package.json in subdirectory: " +
                   entry.path().filename().string() + "/";
        }
    }

    if (fs::exists(fs::path(dir) / "package.json"))
        return "This appears to be a Node.js project, not a Vivid package";
    if (fs::exists(fs::path(dir) / "Cargo.toml"))
        return "This appears to be a Rust project, not a Vivid package";
    if (fs::exists(fs::path(dir) / "setup.py") || fs::exists(fs::path(dir) / "pyproject.toml"))
        return "This appears to be a Python project, not a Vivid package";
    if (fs::exists(fs::path(dir) / "CMakeLists.txt"))
        return "Has CMakeLists.txt but no vivid-package.json";

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.path().extension() == ".cpp") {
            return "Contains C++ source but no vivid-package.json. "
                   "Use `vivid scaffold-package` to create one.";
        }
    }

    return {};
}
} // namespace vivid::package_manager_internal

namespace vivid {
std::pair<std::string, std::string> PackageManager::parse_manifest(const std::string& package_dir, PackageInfo& info) {
    std::string manifest_path = package_dir + "/vivid-package.json";
    std::ifstream ifs(manifest_path);
    if (!ifs) return {"manifest_not_found", "vivid-package.json not found"};

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json_str = ss.str();

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_str);
    } catch (const nlohmann::json::parse_error& e) {
        std::string msg = "vivid-package.json contains invalid JSON at byte ";
        msg += std::to_string(e.byte);
        msg += ": ";
        msg += e.what();
        return {"manifest_invalid_json", msg};
    }

    if (!root.is_object()) return {"manifest_no_root_object", "vivid-package.json has no root object"};

    if (!root.contains("name"))
        return {"manifest_missing_field", "vivid-package.json is missing required field 'name'"};
    if (!root["name"].is_string())
        return {"manifest_field_type", "'name' field in vivid-package.json must be a string"};

    info.name = root["name"].get<std::string>();
    info.version = root.value("version", "0.0.0");
    info.vivid_core = root.value("vivid_core", "");
    info.description = root.value("description", "");
    info.path = package_dir;

    info.build_type = root.value("build", "");

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

    auto parse_string_array = [](const nlohmann::json& obj, const char* key,
                                 std::vector<std::string>& out) {
        auto it = obj.find(key);
        if (it != obj.end() && it->is_array()) {
            for (const auto& val : *it) {
                if (val.is_string())
                    out.push_back(val.get<std::string>());
            }
        }
    };

    // operators
    if (root.contains("operators") && root["operators"].is_array()) {
        for (const auto& val : root["operators"]) {
            if (val.is_string()) {
                std::string op_name = val.get<std::string>();
                if (!is_valid_op_name(op_name))
                    return {"manifest_invalid_operator_name", "Invalid operator name '" + op_name + "': contains invalid characters or path traversal"};
                info.operators.push_back(std::move(op_name));
            }
        }
    }

    // gpu_operators
    if (root.contains("gpu_operators") && root["gpu_operators"].is_array()) {
        for (const auto& val : root["gpu_operators"]) {
            if (val.is_string()) {
                std::string op_name = val.get<std::string>();
                if (!is_valid_op_name(op_name))
                    return {"manifest_invalid_operator_name", "Invalid operator name '" + op_name + "': contains invalid characters or path traversal"};
                info.gpu_operators.push_back(std::move(op_name));
            }
        }
    }

    // modules (optional array of .vivid-module.json paths)
    if (root.contains("modules") && root["modules"].is_array()) {
        for (const auto& val : root["modules"]) {
            if (val.is_string())
                info.modules.push_back(val.get<std::string>());
        }
    }

    // author (optional string)
    info.author = root.value("author", "");

    // category (optional string)
    info.category = root.value("category", "");

    // tags (optional string array)
    parse_string_array(root, "tags", info.tags);

    // dependencies (optional object)
    if (root.contains("dependencies") && root["dependencies"].is_object()) {
        const auto& deps = root["dependencies"];
        parse_string_array(deps, "packages", info.dependencies.packages);

        auto vendor_it = deps.find("vendor");
        if (vendor_it != deps.end() && vendor_it->is_array()) {
            for (const auto& val : *vendor_it) {
                if (val.is_object()) {
                    VendorDependency vd;
                    vd.name = val.value("name", "");
                    vd.include = val.value("include", "");
                    if (!vd.name.empty())
                        info.dependencies.vendor.push_back(std::move(vd));
                }
            }
        }
    }

    // tests (optional object)
    if (root.contains("tests") && root["tests"].is_object()) {
        const auto& tests = root["tests"];
        parse_string_array(tests, "graphs", info.tests.graphs);
        parse_string_array(tests, "cpp", info.tests.cpp);
    }

    // assets (optional object — keys are asset kinds, values are directory arrays)
    if (root.contains("assets") && root["assets"].is_object()) {
        const auto& assets_obj = root["assets"];
        for (auto it = assets_obj.begin(); it != assets_obj.end(); ++it) {
            if (!it.value().is_array()) continue;
            auto& dirs = info.assets.dirs_by_kind[it.key()];
            for (const auto& v : it.value()) {
                if (v.is_string()) dirs.push_back(v.get<std::string>());
            }
        }
    }

    return {"", ""};  // success
}

std::string PackageManager::normalize_github_url(const std::string& url) {
    // Trim whitespace and trailing slashes
    std::string s = package_manager_internal::trim_copy(url);
    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.empty()) return s;

    // Skip if it starts with . or / (relative/absolute path)
    if (s[0] == '.' || s[0] == '/') return s;

    // Shorthand expansion: user/repo → https://github.com/user/repo.git
    // Only if exactly one slash, no dots (avoids my.server/path), no protocol prefix
    if (s.find("://") == std::string::npos && s.find(':') == std::string::npos) {
        auto slash_count = std::count(s.begin(), s.end(), '/');
        bool has_dot = s.find('.') != std::string::npos;
        if (slash_count == 1 && !has_dot) {
            std::string expanded = "https://github.com/" + s + ".git";
            std::fprintf(stderr, "[vivid] PackageManager: expanded '%s' → '%s'\n",
                         url.c_str(), expanded.c_str());
            return expanded;
        }
    }

    // Missing protocol: github.com/... → https://github.com/...
    if (s.rfind("github.com/", 0) == 0) {
        s = "https://" + s;
        std::fprintf(stderr, "[vivid] PackageManager: added protocol: '%s'\n", s.c_str());
    }

    // Strip /tree/<ref>/... from GitHub browser URLs
    if (s.find("github.com/") != std::string::npos) {
        auto tree_pos = s.find("/tree/");
        if (tree_pos != std::string::npos) {
            // Check if there's a subdirectory path after /tree/<ref>/
            auto after_tree = s.substr(tree_pos + 6);  // skip "/tree/"
            auto ref_slash = after_tree.find('/');
            if (ref_slash != std::string::npos) {
                std::string subdir = after_tree.substr(ref_slash + 1);
                if (!subdir.empty()) {
                    std::fprintf(stderr, "[vivid] PackageManager: warning: stripping subdirectory path '%s' from URL "
                                 "(vivid install operates on whole repositories)\n", subdir.c_str());
                }
            }
            s = s.substr(0, tree_pos);
            std::fprintf(stderr, "[vivid] PackageManager: stripped browser path: '%s'\n", s.c_str());
        }

        // Also strip /blob/<ref>/...
        auto blob_pos = s.find("/blob/");
        if (blob_pos != std::string::npos) {
            s = s.substr(0, blob_pos);
            std::fprintf(stderr, "[vivid] PackageManager: stripped browser path: '%s'\n", s.c_str());
        }

        // Ensure .git suffix for github.com URLs
        if (s.size() >= 4 && s.substr(s.size() - 4) != ".git") {
            s += ".git";
        }
    }

    return s;
}

} // namespace vivid
