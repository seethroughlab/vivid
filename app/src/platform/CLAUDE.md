# `app/src/platform/`

`macos_frame_timer.{h,cpp}` — `macos_run_frame_loop(poll_events, tick)`: drives
rendering from a `CFRunLoopTimer` instead of a `glfwPollEvents` busy-loop, so frames
keep firing while macOS runs a nested tracking run-loop (the one a hosted plugin GUI
enters on mouse-down) — otherwise plugin editor windows render but ignore clicks.
The app must be foreground for the timer to pump (see [README](../../README.md)).

macOS-only; a cross-platform frame/loop seam is P3 in the
[roadmap](../../../docs/roadmap/poc-to-product.md).
