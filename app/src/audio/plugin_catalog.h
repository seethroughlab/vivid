#pragma once
#include <string>

// Discovery of the plugins installed on this machine — a lightweight filesystem
// scan (bundle enumeration only; it NEVER loads a plugin dylib). VST3 + CLAP; the
// `format` field + scan structure leave room for AU once that host lands.
// Cached after the first query; call rescan_plugins() to rebuild. UI-thread only.
namespace vivid::session {

enum PluginFormat { kFmtVST3 = 0, kFmtCLAP = 1 };   // kFmtAU appends here later
struct PluginInfo { std::string path; std::string name; int format = kFmtVST3; };

int               plugin_count();
const PluginInfo& plugin_at(int i);   // bounds-safe: an empty static on a bad index
void              rescan_plugins();   // (re)build the cache from the plugin folders

}  // namespace vivid::session
