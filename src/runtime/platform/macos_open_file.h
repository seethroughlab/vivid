#pragma once

#include <string>
#include <vector>

namespace vivid::platform {

// Register an NSAppleEventManager handler for kCoreEventClass/kAEOpenDocuments.
// Call once, very early in main(), before glfwInit(). Any file paths macOS
// hands us (launch-with-file, Finder "Open With", Dock-icon drop, or
// `open -a Vivid foo.json`) are buffered until drain_pending_open_files()
// is called by the main loop.
void install_open_file_handler();

// Drain and return any file paths queued since the last call.
// Safe to call from the main thread only.
std::vector<std::string> drain_pending_open_files();

} // namespace vivid::platform
