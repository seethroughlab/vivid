#pragma once

// The plugin catalog's background classifier: scan the folders (fast), apply what we already know
// from the cache (instant), and probe only what's new — on a worker thread, because probing loads
// third-party dylibs and can take seconds each.
//
// Lifecycle, all on the UI thread except the probing itself:
//   plugin_scan_start()  — at app init: rescan, apply the cache, enqueue the misses, spawn the worker
//   plugin_scan_poll()   — once per frame: drain finished probes into the catalog + the cache
//   plugin_scan_stop()   — at shutdown: join the worker
//
// Crash safety: a sentinel file records the plugin currently being probed. If it's still there at
// the next launch, that plugin took the app down (or hung) and is recorded as `crashed` and never
// probed again — otherwise one bad plugin makes the app permanently unlaunchable.
namespace vivid::session {

void plugin_scan_start(bool force_rescan = false);
void plugin_scan_poll();
void plugin_scan_stop();
int  plugin_scan_pending();   // plugins still queued/in-flight (0 = the catalog is fully classified)

}  // namespace vivid::session
