#include "audio/plugin_scan.h"
#include "audio/plugin_cache.h"
#include "audio/plugin_catalog.h"
#include "audio/plugin_probe.h"
#include "platform/platform.h"

#include <atomic>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vivid::session {
namespace {

struct Job    { std::string path; int format; };
struct Result { std::string path; int format; int cls; std::string name, vendor, uid;
                std::int64_t mtime, size; bool permanent = false; };

std::mutex              g_mtx;         // guards the queue + results (worker <-> UI)
std::deque<Job>         g_queue;
std::vector<Result>     g_results;
std::thread             g_worker;
std::atomic<bool>       g_stop{false};
std::atomic<int>        g_pending{0};
PluginCache             g_cache;       // UI thread only
bool                    g_started = false;

// The sentinel: the plugin we are *currently* inside. Its presence at startup means the last run
// died in there.
std::string sentinel_path() {
    const std::string dir = platform::user_data_dir();
    if (dir.empty()) return {};
    return (std::filesystem::path(dir) / "plugin_probe.lock").string();
}
void sentinel_write(const std::string& plugin_path) {
    const std::string p = sentinel_path();
    if (p.empty()) return;
    std::ofstream out(p, std::ios::trunc);
    if (out) { out << plugin_path; out.flush(); }
}
void sentinel_clear() {
    const std::string p = sentinel_path();
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::remove(p, ec);
}
std::string sentinel_read() {
    const std::string p = sentinel_path();
    if (p.empty()) return {};
    std::ifstream in(p);
    if (!in) return {};
    std::string s;
    std::getline(in, s);
    return s;
}

void worker_main() {
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (g_stop.load(std::memory_order_acquire) || g_queue.empty()) return;
            job = g_queue.front();
            g_queue.pop_front();
        }
        // Probe OUT OF PROCESS. A plugin that segfaults or hangs while being opened then kills a
        // throwaway child instead of the app — which is not hypothetical: probing in-process here
        // crashed the app on two different installed plugins, one per launch. The sentinel below
        // stays as a second net, for a crash that somehow happens on our side of the fence.
        sentinel_write(job.path);
        const ProbeRun pr = probe_plugin_subprocess(job.path, job.format);
        sentinel_clear();

        Result res;
        res.path = job.path;
        res.format = job.format;
        res.cls = pr.crashed ? kClassCrashed : pr.cls;
        res.permanent = pr.permanent;
        res.name = pr.name;
        res.vendor = pr.vendor;
        res.uid = pr.uid;
        if (pr.crashed)
            std::fprintf(stderr, "[vivid] plugin '%s' crashed/hung the probe — marking it unusable\n",
                         job.path.c_str());
        plugin_executable_stat(job.path, res.mtime, res.size);
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_results.push_back(std::move(res));
        }
        g_pending.fetch_sub(1, std::memory_order_release);
    }
}

// Copy a cached verdict onto the live catalog entry.
void apply_cached(const PluginCacheEntry& e) {
    plugin_set_probe_result(e.path, e.name, e.vendor, e.uid, e.cls);
}

}  // namespace

void plugin_scan_start(bool force_rescan) {
    if (g_started && !force_rescan) return;
    plugin_scan_stop();   // idempotent: a forced rescan restarts the worker

    rescan_plugins();
    g_cache = load_plugin_cache(plugin_cache_path());

    // Did the last run die inside a probe? Then that plugin is poison: record it and never open it
    // again on our own. (Without this, the app re-crashes on every launch and can't be started.)
    if (const std::string dead = sentinel_read(); !dead.empty()) {
        std::fprintf(stderr, "[vivid] plugin probe crashed on '%s' last run — marking it unusable\n",
                     dead.c_str());
        PluginCacheEntry e;
        e.path = dead;
        e.cls = kClassCrashed;
        plugin_executable_stat(dead, e.exe_mtime, e.exe_size);
        g_cache.put(e);
        save_plugin_cache(plugin_cache_path(), g_cache);
        sentinel_clear();
    }

    if (force_rescan) g_cache.entries.clear();   // an explicit rescan re-probes everything, even crashers

    int queued = 0;
    const int n = plugin_count();
    for (int i = 0; i < n; ++i) {
        const PluginInfo& p = plugin_at(i);
        std::int64_t mt = 0, sz = 0;
        plugin_executable_stat(p.path, mt, sz);
        if (!plugin_cache_is_stale(g_cache, p.path, mt, sz)) {
            if (const PluginCacheEntry* e = g_cache.find(p.path)) apply_cached(*e);   // instant
            continue;
        }
        std::lock_guard<std::mutex> lk(g_mtx);
        g_queue.push_back({ p.path, p.format });
        ++queued;
    }
    g_pending.store(queued, std::memory_order_release);
    g_started = true;
    if (queued > 0) {
        g_stop.store(false, std::memory_order_release);
        g_worker = std::thread(worker_main);
        std::fprintf(stderr, "[vivid] classifying %d plugin(s) in the background\n", queued);
    }
}

void plugin_scan_poll() {
    std::vector<Result> done;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_results.empty()) return;
        done.swap(g_results);
    }
    for (const Result& r : done) {
        plugin_set_probe_result(r.path, r.name, r.vendor, r.uid, r.cls);
        PluginCacheEntry e;
        e.path = r.path; e.exe_mtime = r.mtime; e.exe_size = r.size;
        e.format = r.format; e.cls = r.cls;
        e.name = r.name; e.vendor = r.vendor; e.uid = r.uid;
        e.permanent = r.permanent;
        g_cache.put(e);
    }
    g_cache.version = kPluginCacheVersion;
    save_plugin_cache(plugin_cache_path(), g_cache);   // atomic; after every batch, so a crash keeps the work
}

void plugin_scan_stop() {
    g_stop.store(true, std::memory_order_release);
    if (g_worker.joinable()) g_worker.join();
    g_stop.store(false, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(g_mtx); g_queue.clear(); }
    g_pending.store(0, std::memory_order_release);
}

int plugin_scan_pending() { return g_pending.load(std::memory_order_acquire); }

}  // namespace vivid::session
