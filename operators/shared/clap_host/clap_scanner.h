#pragma once
// clap_scanner.h — Scan standard CLAP plugin directories and cache results.
// Header-only, anonymous namespace. Uses clap_host_common.h for bundle resolution.

#include "shared/clap_host/clap_host_common.h"
#include <clap/clap.h>
#include <dlfcn.h>
#include <dirent.h>
#include <cstdlib>
#include <mutex>
#include <algorithm>

namespace {

struct ClapPluginInfo {
    std::string path;       // full path to .clap bundle
    std::string plugin_id;  // CLAP descriptor id
    std::string name;       // display name
    std::string vendor;     // vendor name
};

// ---------------------------------------------------------------------------
// Implementation detail — filesystem helpers
// ---------------------------------------------------------------------------

static std::string clap_expand_home(const std::string& p) {
    if (p.empty() || p[0] != '~') return p;
    const char* home = getenv("HOME");
    if (!home) return p;
    return std::string(home) + p.substr(1);
}

// Recursively collect all paths ending in ".clap" under `dir`, up to `depth`.
static void clap_collect_files(const std::string& dir,
                                std::vector<std::string>& out,
                                int depth = 0) {
    if (depth > 3) return;
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string name = ent->d_name;
        std::string full = dir + "/" + name;
        const std::string ext = ".clap";
        if (name.size() > ext.size() &&
            name.compare(name.size() - ext.size(), ext.size(), ext) == 0) {
            out.push_back(full);
        } else if (ent->d_type == DT_DIR || ent->d_type == DT_UNKNOWN) {
            clap_collect_files(full, out, depth + 1);
        }
    }
    closedir(d);
}

// ---------------------------------------------------------------------------
// Scan state
// ---------------------------------------------------------------------------

static std::vector<ClapPluginInfo> kClapPluginCache;
static std::once_flag              kClapScanOnce;

static std::vector<std::string> clap_search_paths() {
    std::vector<std::string> paths;
#ifdef __APPLE__
    paths.push_back("/Library/Audio/Plug-Ins/CLAP");
    paths.push_back(clap_expand_home("~/Library/Audio/Plug-Ins/CLAP"));
#elif defined(__linux__)
    paths.push_back("/usr/lib/clap");
    paths.push_back("/usr/local/lib/clap");
    paths.push_back(clap_expand_home("~/.clap"));
#endif
    return paths;
}

// Scan implementation — called once via call_once.
static void clap_scan_impl() {
    std::vector<std::string> files;
    for (const auto& dir : clap_search_paths())
        clap_collect_files(dir, files);

    std::vector<ClapPluginInfo> results;
    for (const auto& bundle_path : files) {
        std::string binary = clap_resolve_binary(bundle_path.c_str());
        void* lib = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) continue;

        auto* entry = reinterpret_cast<const clap_plugin_entry_t*>(
            dlsym(lib, "clap_entry"));
        if (!entry || !clap_version_is_compatible(entry->clap_version)) {
            dlclose(lib); continue;
        }
        if (!entry->init(binary.c_str())) {
            dlclose(lib); continue;
        }
        auto* factory = reinterpret_cast<const clap_plugin_factory_t*>(
            entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (factory) {
            uint32_t n = factory->get_plugin_count(factory);
            for (uint32_t i = 0; i < n; ++i) {
                auto* d = factory->get_plugin_descriptor(factory, i);
                if (!d) continue;
                ClapPluginInfo info;
                info.path      = bundle_path;
                info.plugin_id = d->id     ? d->id     : "";
                info.name      = d->name   ? d->name   : "";
                info.vendor    = d->vendor ? d->vendor : "";
                results.push_back(std::move(info));
            }
        }
        entry->deinit();
        dlclose(lib);
    }

    std::sort(results.begin(), results.end(),
        [](const ClapPluginInfo& a, const ClapPluginInfo& b) {
            return a.name < b.name;
        });

    kClapPluginCache = std::move(results);
}

// Public API:

// Trigger scan (no-op on subsequent calls). Safe to call from main thread.
static void clap_scan_plugins() {
    std::call_once(kClapScanOnce, clap_scan_impl);
}

// Read cached results. Empty until clap_scan_plugins() has been called.
static const std::vector<ClapPluginInfo>& clap_get_plugins() {
    return kClapPluginCache;
}

} // namespace
