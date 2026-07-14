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
    // A failure this machine can never recover from (no slice for our CPU). Only such a failure is
    // remembered; an ordinary one is retried next launch. See plugin_cache_is_stale().
    bool permanent = false;
};

struct PluginCache {
    int version = 0;
    std::vector<PluginCacheEntry> entries;

    const PluginCacheEntry* find(const std::string& path) const;
    void put(const PluginCacheEntry& e);   // insert or replace by path
};

// Bump when the probe's meaning changes (new fields, a fixed classifier) — every entry re-probes.
// v2: the probe moved OUT OF PROCESS and gained a host-arch pre-check, so verdicts recorded by v1
// (which included plugins wrongly marked failed by the in-process probe) must not be trusted.
constexpr int kPluginCacheVersion = 2;

// Should `path` be probed again? True when: the cache is from an older schema, we've never seen the
// plugin, or its executable changed size/mtime.
//
// A verdict is only STICKY if it earned it:
//   - `crashed` — the plugin took the probe down or hung it. Never retried on our own: retrying
//     would burn a subprocess (and, before the subprocess, the whole app) on every launch.
//   - a `permanent` failure — the binary has no slice for this CPU. Retrying cannot change that.
// Every OTHER failure is retried next launch. A probe failure can be transient (a plugin mid-install,
// a licence daemon not up yet) or our own bug — and a sticky WRONG "failed" is the worst kind of
// wrong, because the plugin simply never appears and nothing tells the user why. Retrying costs one
// cheap subprocess in a background worker; being permanently blind to a working plugin costs a lot
// more. (An explicit rescan re-probes even the crashers.)
bool plugin_cache_is_stale(const PluginCache& cache, const std::string& path,
                           std::int64_t exe_mtime, std::int64_t exe_size);

std::string plugin_cache_path();                                  // user_data_dir()/plugin_cache.json
PluginCache load_plugin_cache(const std::string& path);
bool        save_plugin_cache(const std::string& path, const PluginCache& c);   // atomic (tmp + rename)

// The bundle's executable, per the platform layout (<bundle>/Contents/MacOS/<stem>). Returns false
// if it can't be stat'ed (a broken bundle) — such a plugin is probed anyway and will fail loudly.
bool plugin_executable_stat(const std::string& bundle, std::int64_t& mtime, std::int64_t& size);

}  // namespace vivid::session
