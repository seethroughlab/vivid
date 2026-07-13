#pragma once
#include <string>

// Discovery of the plugins installed on this machine — a filesystem scan of the plugin folders
// (bundle enumeration; it NEVER loads a plugin dylib). VST3 + CLAP; the `format` field + scan
// structure leave room for AU once that host lands.
//
// A plugin's CLASS (instrument vs effect) is not knowable from the filesystem, so it starts as
// kClassUnknown and is filled in by the background probe + cache (audio/plugin_probe.h), which
// reads the plugin's factory descriptor WITHOUT instantiating it. This catalog is the single
// source of truth for "what can I add" — shared by the PLUGINS browser and the Tab chooser.
//
// UI-thread only: the list is read every frame by the browser and mutated only by the UI-thread
// poll that drains probe results, so neither side needs a lock.
namespace vivid::session {

enum PluginFormat { kFmtVST3 = 0, kFmtCLAP = 1 };   // kFmtAU appends here later

// What a plugin IS. kClassFailed = the probe couldn't read it (broken/incompatible bundle);
// kClassCrashed = it took the app down (or hung) while being probed, so we never touch it again
// without an explicit rescan — one bad plugin must not make the app unlaunchable.
enum PluginClass { kClassUnknown = 0, kClassInstrument, kClassEffect, kClassNoteEffect,
                   kClassFailed, kClassCrashed };
const char* plugin_class_name(int cls);
const char* plugin_format_name(int fmt);

struct PluginInfo {
    std::string path;      // the bundle
    std::string name;      // display name (the bundle stem until probed, then the plugin's own)
    std::string vendor;    // "" until probed
    std::string uid;       // VST3 class cid hex — lets the loader pick the EXACT class, not guess
    int  format = kFmtVST3;
    int  cls    = kClassUnknown;
    bool probed = false;   // false = the class is a placeholder, not a verdict
};

int               plugin_count();
const PluginInfo& plugin_at(int i);   // bounds-safe: an empty static on a bad index
void              rescan_plugins();   // (re)build the list from the plugin folders (fast; no probe)

// Fill in a scanned plugin's probe result (UI thread; matched by path). Returns false if the path
// isn't in the catalog (e.g. it was rescanned away while a probe was in flight).
bool plugin_set_probe_result(const std::string& path, const std::string& name,
                             const std::string& vendor, const std::string& uid, int cls);

}  // namespace vivid::session
