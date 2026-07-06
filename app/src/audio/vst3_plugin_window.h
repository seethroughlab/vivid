#pragma once
#include "pluginterfaces/vst/ivsteditcontroller.h"

// Opaque handle for a VST3 plugin's native Cocoa GUI window.
// All functions must be called from the main thread only.
struct Vst3PluginWindow;

// Open the VST3 plugin GUI in a new Cocoa NSWindow.
// Returns nullptr if the plugin has no Cocoa GUI (IPlugView unavailable or
// kPlatformTypeNSView not supported).
Vst3PluginWindow* vst3_plugin_window_open(Steinberg::Vst::IEditController* controller,
                                           const char* title);

// True if the native window has not been closed by the user.
bool vst3_plugin_window_is_open(const Vst3PluginWindow* win);

// Hide, detach the VST3 view, and free the handle.
void vst3_plugin_window_close(Vst3PluginWindow* win);
