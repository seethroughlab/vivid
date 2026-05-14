#pragma once

#include <string>
#include <vector>

namespace vivid::platform {

// Register an NSAppleEventManager handler for kCoreEventClass/kAEOpenDocuments,
// overriding NSDocumentController's default handler. Call once after glfwInit()
// (which brings up NSApp and creates NSDocumentController). Any file paths macOS
// hands us (launch-with-file, Finder "Open With", Dock-icon drop, or
// `open -a Vivid foo.json`) are buffered until drain_pending_open_files().
void install_open_file_handler();

// Register a NSApplicationWillFinishLaunchingNotification observer that injects
// application:openFile: into GLFW's delegate as soon as it is set. Call once,
// BEFORE glfwInit(). The notification fires inside glfwInit()'s [NSApp run], after
// GLFW sets its delegate but before applicationDidFinishLaunching where
// NSDocumentController processes any queued kAEOpenDocuments events.
void schedule_open_file_injection();

// Isa-swizzle GLFW's NSApplicationDelegate to inject application:openFile:. This
// is the method NSDocumentController calls before falling back to its own document-
// opening logic. Call once after glfwInit() as a belt-and-suspenders guard
// (schedule_open_file_injection handles the cold-launch timing).
void inject_open_file_delegate();

// Drain and return any file paths queued since the last call.
// Safe to call from the main thread only.
std::vector<std::string> drain_pending_open_files();

} // namespace vivid::platform
