# `app/src/platform/` — cross-platform seams (P3)

Every OS-specific assumption is isolated here behind a neutral header, with one backend
compiled per platform (selected by `#ifdef __APPLE__` in the source + `if(APPLE)` in
CMake). macOS is the only **verified** backend; the Linux/Windows branches make the tree
cross-platform-*ready* (build it green there is the user's CI, not done here). Everything
else (GPU/wgpu, audio/miniaudio, input/GLFW) is already cross-platform via its library.

- **`platform.{h,cpp}`** — `plugin_suffix()` (`.dylib`/`.so`/`.dll`), `executable_path()`
  (`_NSGetExecutablePath` / `readlink(/proc/self/exe)` / `GetModuleFileNameW`),
  `user_data_dir()` (Application Support / `$XDG_DATA_HOME` / `%APPDATA%`). Used by
  operator_scan, package_compiler/manager, main.cpp, input.cpp.
- **`frame_loop.h`** — `run_platform_frame_loop(poll, tick)`. Backends:
  `macos_frame_timer.cpp` (CFRunLoopTimer in tracking/modal modes so frames keep firing
  during a hosted plugin GUI's nested run-loop; outer loop drains the MCP server so it
  works backgrounded) and `frame_loop_generic.cpp` (a plain poll-then-tick loop — no
  plugin-GUI hosting off macOS).
- **`app_nap.h`** — `disable_app_nap(reason)`: `macos_app_nap.mm` (NSProcessInfo
  activity) vs `app_nap_stub.cpp` (noop).
- **`menu_bar.{h,mm}`** — the native macOS menu bar: File (New/Open/Save + Open Recent /
  Open Example) and the **Edit menu** (ADR-0017 Undo/Redo, dynamically relabeled; no ⌘Z
  key-equivalent so AppKit doesn't steal it from the clip editor). `menu_bar_stub.cpp` is
  the no-op off macOS.

Heavy macOS-only features stub elsewhere (same signatures, feature disabled):
`gpu/video_player.mm` (AVFoundation) ↔ `gpu/video_player_stub.cpp`;
`audio/vst3_plugin_window.mm` (Cocoa) ↔ `audio/vst3_plugin_window_stub.cpp`. A real
Linux/Windows video decoder + plugin-GUI host are a future P3 step, not done here.
