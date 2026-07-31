#pragma once
#include <atomic>

// Frame-thread performance read-out. Published each frame by the always-on FPS HUD (draw_perf_hud in
// frame.cpp) and read by the control server's `get_perf` endpoint, so the operator-audit harness
// (ADR-0042) can time per-operator frame cost via an A/B delta. Single-producer (frame thread) /
// single-consumer (control thread) — relaxed atomics suffice.
namespace vivid::perf {

inline std::atomic<double> g_frame_ms{1000.0 / 60.0};   // EMA-smoothed whole-frame time (ms), seeded at 60 fps
inline std::atomic<double> g_fps{60.0};                 // 1000 / g_frame_ms

}  // namespace vivid::perf
