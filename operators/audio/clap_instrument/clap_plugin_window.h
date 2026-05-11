#pragma once
#include <clap/clap.h>

// Opaque handle for a CLAP plugin's native Cocoa GUI window.
// All functions must be called from the main thread only.
struct ClapPluginWindow;

// Open the CLAP plugin GUI in a new Cocoa NSWindow.
// Returns nullptr if the plugin does not support embedded Cocoa GUI.
ClapPluginWindow* clap_plugin_window_open(const clap_plugin_t* plugin,
                                           const clap_plugin_gui_t* gui_ext,
                                           const char* title);

// True if the native window has not been closed by the user.
bool clap_plugin_window_is_open(const ClapPluginWindow* win);

// Hide, destroy the CLAP GUI, and free the handle.
void clap_plugin_window_close(ClapPluginWindow* win);
