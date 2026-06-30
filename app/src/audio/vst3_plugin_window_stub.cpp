#ifndef __APPLE__
#include "audio/vst3_plugin_window.h"

// Hosting a VST3 plugin's native GUI is macOS-only (Cocoa; vst3_plugin_window.mm). On
// other platforms it's disabled — the plugin's parameters remain fully controllable via
// the VST3 param C-API + the mapping bridge; only the embedded editor window is absent.

Vst3PluginWindow* vst3_plugin_window_open(Steinberg::Vst::IEditController*, const char*) { return nullptr; }
bool              vst3_plugin_window_is_open(const Vst3PluginWindow*) { return false; }
void              vst3_plugin_window_close(Vst3PluginWindow*) {}

#endif  // !__APPLE__
