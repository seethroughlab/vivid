#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Frame-thread performance read-out. Published each frame by the always-on FPS HUD (draw_perf_hud in
// frame.cpp) and read by the control server's `get_perf` endpoint, so the operator-audit harness
// (ADR-0042) can time per-operator frame cost via an A/B delta. Single-producer (frame thread) /
// single-consumer (control thread) — relaxed atomics suffice.
namespace vivid::perf {

inline std::atomic<double> g_frame_ms{1000.0 / 60.0};   // EMA-smoothed whole-frame time (ms), seeded at 60 fps
inline std::atomic<double> g_fps{60.0};                 // 1000 / g_frame_ms

// --- GPU-side timing (GpuTimer, via timestamp queries). CPU wall-clock (g_frame_ms) alone can't tell
// GPU-bound from CPU-bound; these expose the real GPU cost of the main frame. 0 when the adapter lacks
// timestamp support. g_gpu_ms is the whole-frame GPU time; g_gpu_regions is a per-segment breakdown
// (e.g. "visuals" = the output render, "ui" = editor UI + composite + MSAA resolve). Read a few frames
// late (non-blocking readback), so they lag the live CPU number slightly — fine for profiling. ---
inline std::atomic<double> g_gpu_ms{0.0};               // whole-frame GPU time (ms), 0 if unsupported
inline std::atomic<bool>   g_present_uncapped{false};   // true when present mode is Immediate (vsync off)

// Per-segment GPU breakdown, mutex-guarded (variable length; written by the frame thread's readback,
// read by the control thread). Kept tiny (a handful of entries) so the lock is uncontended and cheap.
inline std::mutex                                  g_gpu_regions_mtx;
inline std::vector<std::pair<std::string, double>> g_gpu_regions;   // {label, ms}, most-recent frame

inline void set_gpu_regions(std::vector<std::pair<std::string, double>> regions) {
    std::lock_guard<std::mutex> lk(g_gpu_regions_mtx);
    g_gpu_regions = std::move(regions);
}
inline std::vector<std::pair<std::string, double>> get_gpu_regions() {
    std::lock_guard<std::mutex> lk(g_gpu_regions_mtx);
    return g_gpu_regions;
}

}  // namespace vivid::perf
