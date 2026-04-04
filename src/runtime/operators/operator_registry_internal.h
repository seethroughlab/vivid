#pragma once

#include "runtime/operators/operator_registry.h"
#include "runtime/platform/platform.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <thread>
#include <unordered_set>

namespace vivid::operator_registry_internal {

inline std::string resolve_alias_once(const std::unordered_map<std::string, std::string>& aliases,
                                      const std::string& type_name) {
    std::string cur = type_name;
    std::unordered_set<std::string> seen;
    while (true) {
        auto it = aliases.find(cur);
        if (it == aliases.end()) break;
        if (!seen.insert(cur).second) break;
        cur = it->second;
    }
    return cur;
}

inline std::string guess_package_name_from_plugin_path(const std::string& plugin_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::path(plugin_path).lexically_normal();
    if (p.empty()) return {};
    fs::path parent = p.parent_path();
    if (parent.empty()) return {};
    if (parent.filename() == "build") {
        fs::path pkg = parent.parent_path();
        if (!pkg.empty()) return pkg.filename().string();
    }
    return {};
}

inline std::string normalized_extension(std::string ext) {
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

inline void maybe_delay_registry_lazy_load_for_tests() {
    const char* env = std::getenv("VIVID_TEST_REGISTRY_LOAD_DELAY_MS");
    if (!env || !*env) return;
    char* end = nullptr;
    long delay_ms = std::strtol(env, &end, 10);
    if (end == env || delay_ms <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
}

template<typename DiagnosticT>
std::vector<DiagnosticT> diagnostics_for_dir(
        const std::unordered_map<std::string, DiagnosticT>& diagnostics_by_path,
        const std::string& directory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::weakly_canonical(fs::path(directory), ec);
    if (ec) dir = fs::path(directory).lexically_normal();
    const std::string dir_s = dir.string();
    const std::string dir_slash = dir_s.empty() ? dir_s : (dir_s + "/");

    std::vector<DiagnosticT> out;
    for (const auto& [path, diag] : diagnostics_by_path) {
        fs::path path_norm = fs::weakly_canonical(fs::path(path), ec);
        if (ec) {
            ec.clear();
            path_norm = fs::path(path).lexically_normal();
        }
        const std::string path_s = path_norm.string();
        if (path_s == dir_s || path_s.rfind(dir_slash, 0) == 0) out.push_back(diag);
    }
    std::sort(out.begin(), out.end(), [](const DiagnosticT& a, const DiagnosticT& b) {
        return a.plugin_path < b.plugin_path;
    });
    return out;
}

template<typename Fn>
bool scan_plugin_dir(const char* directory, Fn&& fn) {
    DIR* dir = opendir(directory);
    if (!dir) {
        std::fprintf(stderr, "[vivid] Registry: failed to open directory: %s\n", directory);
        return false;
    }

    size_t suffix_len = std::strlen(kPluginSuffix);

    std::unordered_set<std::string> skipped;
    const char* env = std::getenv("VIVID_SKIP_PLUGINS");
    if (env && *env) {
        std::string s(env);
        size_t pos = 0;
        while (pos < s.size()) {
            size_t next = s.find(',', pos);
            std::string item = s.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            while (!item.empty() && item.front() == ' ') item.erase(item.begin());
            while (!item.empty() && item.back() == ' ') item.pop_back();
            if (!item.empty()) skipped.insert(std::move(item));
            if (next == std::string::npos) break;
            pos = next + 1;
        }
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        size_t len = std::strlen(name);
        if (len < suffix_len + 1 || std::strcmp(name + len - suffix_len, kPluginSuffix) != 0)
            continue;
        if (std::strncmp(name, "lib", 3) == 0)
            continue;
        std::string stem(name, len - suffix_len);
        if (skipped.count(name) || skipped.count(stem)) {
            std::fprintf(stderr, "[vivid] Registry: skipping plugin %s (VIVID_SKIP_PLUGINS)\n", name);
            continue;
        }

        std::string path = std::string(directory) + "/" + name;
        fn(path, name, len - suffix_len);
    }

    closedir(dir);
    return true;
}

} // namespace vivid::operator_registry_internal
