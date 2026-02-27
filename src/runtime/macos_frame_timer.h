#pragma once
#ifdef __APPLE__

#include <functional>

namespace vivid {

/// Runs `tick` repeatedly via a CFRunLoopTimer in kCFRunLoopCommonModes,
/// which keeps firing during window drag/resize (NSEventTrackingRunLoopMode).
/// Returns when tick() returns false.
void macos_run_frame_loop(std::function<bool()> tick);

}  // namespace vivid

#endif  // __APPLE__
