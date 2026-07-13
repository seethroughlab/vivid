#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The plugin probe's on-disk memory: what each installed plugin IS, so the (slow) probe runs once
// per plugin per install rather than on every launch.
//
// Invalidation is keyed on the bundle's EXECUTABLE (not the bundle directory): a .vst3/.clap is a
// *directory*, and an installer replacing the inner binary does not reliably change the directory's
// mtime — so keying on the directory would serve stale answers after an update.
namespace vivid::session {

// A probe verdict for one bundle. `cls` is a PluginClass (audio/plugin_catalog.h).
struct PluginCacheEntry {
    std::string path;
    std::int64_t exe_mtime = 0;   // the bundle's executable, not the bundle dir
    std::int64_t exe_size  = 0;
    int format = 0;
    int cls    = 0;               // PluginClass
    std::string name, vendor, uid;
};

struct PluginCache {
    int version = 0;
    std::vector<PluginCacheEntry> entries;

    const PluginCacheEntry* find(const std::string& path) const;
    void put(const PluginCacheEntry& e);   // insert or replace by path
};

// Bump when the probe's meaning changes (new fields, a fixed classifier) — every entry re-probes.
constexpr int kPluginCacheVersion = 1;

// Should `path` be probed again? True when: the cache is from an older schema, we've never seen the
// plugin, or its executable changed size/mtime. A `crashed`/`failed` verdict is NOT retried — one
// bad plugin must not re-crash (or re-hang) the app on every launch; an explicit rescan clears it.
bool plugin_cache_is_stale(const PluginCache& cache, const std::string& path,
                           std::int64_t exe_mtime, std::int64_t exe_size);

std::string plugin_cache_path();                                  // user_data_dir()/plugin_cache.json
PluginCache load_plugin_cache(const std::string& path);
bool        save_plugin_cache(const std::string& path, const PluginCache& c);   // atomic (tmp + rename)

// The bundle's executable, per the platform layout (<bundle>/Contents/MacOS/<stem>). Returns false
// if it can't be stat'ed (a broken bundle) — such a plugin is probed anyway and will fail loudly.
bool plugin_executable_stat(const std::string& bundle, std::int64_t& mtime, std::int64_t& size);

}  // namespace vivid::session
