#include "audio/plugin_cache.h"
#include "audio/plugin_catalog.h"   // PluginClass (kClassFailed / kClassCrashed)
#include "platform/platform.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace vivid::session {

using nlohmann::json;

const PluginCacheEntry* PluginCache::find(const std::string& path) const {
    for (const PluginCacheEntry& e : entries)
        if (e.path == path) return &e;
    return nullptr;
}

void PluginCache::put(const PluginCacheEntry& e) {
    for (PluginCacheEntry& x : entries)
        if (x.path == e.path) { x = e; return; }
    entries.push_back(e);
}

bool plugin_cache_is_stale(const PluginCache& cache, const std::string& path,
                           std::int64_t exe_mtime, std::int64_t exe_size) {
    if (cache.version != kPluginCacheVersion) return true;   // the probe's meaning changed
    const PluginCacheEntry* e = cache.find(path);
    if (!e) return true;                                     // never seen
    // A plugin that crashed or hung the probe is NEVER retried automatically — that's the whole
    // point of recording it. Retrying would re-crash the app on every single launch.
    if (e->cls == kClassCrashed) return false;
    return e->exe_mtime != exe_mtime || e->exe_size != exe_size;   // reinstalled / updated
}

bool plugin_executable_stat(const std::string& bundle, std::int64_t& mtime, std::int64_t& size) {
    namespace fs = std::filesystem;
    const fs::path b(bundle);
    // <bundle>/Contents/MacOS/<stem>  — the stem is the bundle name without its extension.
    const fs::path exe = b / "Contents" / "MacOS" / b.stem();
    struct stat st{};
    if (::stat(exe.c_str(), &st) != 0) {
        // A broken/odd bundle: fall back to the directory so we still have *a* key (it just can't
        // detect an in-place binary swap).
        if (::stat(b.c_str(), &st) != 0) return false;
    }
    mtime = static_cast<std::int64_t>(st.st_mtime);
    size  = static_cast<std::int64_t>(st.st_size);
    return true;
}

std::string plugin_cache_path() {
    const std::string dir = platform::user_data_dir();
    if (dir.empty()) return {};
    return (std::filesystem::path(dir) / "plugin_cache.json").string();
}

PluginCache load_plugin_cache(const std::string& path) {
    PluginCache c;
    if (path.empty()) return c;
    std::ifstream in(path);
    if (!in) return c;
    json j;
    try { in >> j; } catch (...) { return c; }   // a corrupt cache is just an empty one: re-probe
    if (!j.is_object()) return c;
    c.version = j.value("version", 0);
    if (const auto it = j.find("entries"); it != j.end() && it->is_array()) {
        for (const json& je : *it) {
            if (!je.is_object()) continue;
            PluginCacheEntry e;
            e.path      = je.value("path", std::string());
            e.exe_mtime = je.value("exe_mtime", std::int64_t{0});
            e.exe_size  = je.value("exe_size", std::int64_t{0});
            e.format    = je.value("format", 0);
            e.cls       = je.value("cls", 0);
            e.name      = je.value("name", std::string());
            e.vendor    = je.value("vendor", std::string());
            e.uid       = je.value("uid", std::string());
            if (!e.path.empty()) c.entries.push_back(std::move(e));
        }
    }
    return c;
}

bool save_plugin_cache(const std::string& path, const PluginCache& c) {
    if (path.empty()) return false;
    json j;
    j["version"] = kPluginCacheVersion;
    json arr = json::array();
    for (const PluginCacheEntry& e : c.entries) {
        arr.push_back({ {"path", e.path}, {"exe_mtime", e.exe_mtime}, {"exe_size", e.exe_size},
                        {"format", e.format}, {"cls", e.cls},
                        {"name", e.name}, {"vendor", e.vendor}, {"uid", e.uid} });
    }
    j["entries"] = std::move(arr);
    // Atomic: write a temp file and rename over the real one. The probe rewrites the cache after
    // EVERY result, so a crash (or a plugin taking the app down) can't shred the work already done.
    const std::string tmp = path + ".tmp";
    { std::ofstream out(tmp, std::ios::trunc);
      if (!out) return false;
      out << j.dump(2);
      if (!out) return false; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(tmp, ec); return false; }
    return true;
}

}  // namespace vivid::session
