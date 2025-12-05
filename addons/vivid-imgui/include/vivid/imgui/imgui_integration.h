// Vivid ImGui Integration
// Simple API for using Dear ImGui with Vivid

#pragma once

#include <vivid/vivid.h>

namespace vivid::imgui {

/// Initialize ImGui for use with Vivid
/// Call this in your setup() function
void init(Context& ctx);

/// Shutdown ImGui
/// Called automatically on destruction, but can be called manually
void shutdown();

/// Begin a new ImGui frame
/// Call this at the start of your update() function before any ImGui calls
void beginFrame(Context& ctx);

/// Render ImGui to the current render target
/// Call this at the end of your update() function after all ImGui calls
void render(Context& ctx);

/// Check if ImGui wants to capture mouse input
/// Use this to disable your own mouse handling when ImGui is active
bool wantsMouse();

/// Check if ImGui wants to capture keyboard input
/// Use this to disable your own keyboard handling when ImGui is active
bool wantsKeyboard();

} // namespace vivid::imgui
