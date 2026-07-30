#pragma once

namespace vivid::session { struct ClapHandle; }

// Opaque handle for a CLAP plugin's native Cocoa GUI window (the clap.gui extension).
// The peer of vst3_plugin_window.h. All functions must be called from the main thread only.
struct ClapPluginWindow;

// Open the CLAP plugin GUI in a new Cocoa NSWindow. Returns nullptr if the plugin has no
// clap.gui extension, doesn't support the Cocoa embedding API, or fails to create/attach.
ClapPluginWindow* clap_plugin_window_open(vivid::session::ClapHandle* handle, const char* title);

// True if the native window has not been closed by the user.
bool clap_plugin_window_is_open(const ClapPluginWindow* win);

// Hide, destroy the CLAP GUI, and free the handle. Must run BEFORE ClapHandle::destroy().
void clap_plugin_window_close(ClapPluginWindow* win);
