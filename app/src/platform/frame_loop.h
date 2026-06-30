#pragma once

#include <functional>

namespace vivid {

/// Run the app's frame loop until poll_events() or tick() returns false.
///
/// `poll_events` pumps OS/window events (may block during native tracking, e.g. a
/// macOS window resize). `tick` renders, advances the runtime, drains the control
/// server, and pushes audio.
///
/// macOS backend (platform/macos_frame_timer.cpp): a CFRunLoopTimer registered in the
/// nested tracking/modal run-loop modes, so rendering keeps firing while a hosted VST3
/// plugin GUI holds a tracking loop. Generic backend (platform/frame_loop_generic.cpp):
/// a plain poll-then-tick loop (no plugin-GUI hosting off macOS). Exactly one backend
/// is compiled per OS.
void run_platform_frame_loop(std::function<bool()> poll_events,
                             std::function<bool()> tick);

}  // namespace vivid
