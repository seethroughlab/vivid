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

}  // namespace vivid

#endif  // __APPLE__
