#pragma once
#ifdef __APPLE__

#include <functional>

namespace vivid {

/// Runs a frame loop that separates event polling from rendering.
///
/// `poll_events` is called from the outer loop — it may block when macOS
/// enters a tracking run-loop (resize, menus). Return false to exit.
///
/// `tick` is called from a CFRunLoopTimer registered in default, tracking,
/// and modal modes, so it fires continuously even during resize/menus.
/// It should render, tick the runtime, and push audio. Return false to exit.
void macos_run_frame_loop(std::function<bool()> poll_events,
                          std::function<bool()> tick);

/// Defeat macOS App Nap for this process, so the frame timer (and the control
/// server it drains each tick) keeps pumping even when the app is backgrounded —
/// required for agent-driven/MCP use where the app isn't the foreground app.
/// Holds an NSProcessInfo activity for the process lifetime; idempotent.
void macos_disable_app_nap(const char* reason);

}  // namespace vivid

#endif  // __APPLE__
