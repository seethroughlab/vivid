// Non-Apple stub for the CLAP plugin GUI window. The real Cocoa implementation lives in
// clap_plugin_window.mm (compiled only on macOS). Guarded so exactly one backend links.
#ifndef __APPLE__
#include "clap_plugin_window.h"

struct ClapPluginWindow {};

ClapPluginWindow* clap_plugin_window_open(vivid::session::ClapHandle*, const char*) { return nullptr; }
bool clap_plugin_window_is_open(const ClapPluginWindow*) { return false; }
void clap_plugin_window_close(ClapPluginWindow*) {}
#endif
