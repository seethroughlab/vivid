#pragma once
// ADR-0031 §3 — cheap, RT-safe audio-thread health counters.
//
// Single-writer (the RT audio callback, and the session_process it calls on that SAME thread) /
// multi-reader (the frame thread, via collect_health). Relaxed atomics suffice — these are monotonic
// health tallies, not a synchronization channel — so every writer path is lock-free, alloc-free, and
// syscall-free, exactly like vivid::perf (perf_stats.h). collect_health reads the totals once per frame
// and rolls DELTAS into HealthSnapshot (ADR-0031 §4).
//
// Offline bounce also calls session_process (directly, off the RT thread, after ma_device_stop), so the
// in_session_process try_lock skip counter is gated behind a thread-local RT scope: only audio_callback
// enters RtScope, so a bounce never pollutes the realtime-contention metric.
#include <atomic>
#include <cstdint>

namespace vivid::audio::health {

// --- Monotonic counters (fetch_add, relaxed). collect_health rolls the deltas. --------------------
inline std::atomic<uint64_t> g_callbacks{0};        // realtime callbacks entered (denominator)
inline std::atomic<uint64_t> g_render_bailouts{0};  // block > max supported size -> session_process filled
                                                    // silence and bailed. THE render-bail-to-silence event;
                                                    // an idle/empty session's silence fill is by-design, not
                                                    // a bailout, and is deliberately NOT counted.
inline std::atomic<uint64_t> g_over_budget{0};      // callback wall-time exceeded its realtime budget
inline std::atomic<uint64_t> g_handoff_skips{0};    // try_lock handoff skipped on contention (RT thread only)

// --- Gauges (store, relaxed) — most-recent sample, single RT writer so no CAS needed. -------------
inline std::atomic<uint32_t> g_last_callback_us{0};
inline std::atomic<uint32_t> g_max_callback_us{0};  // high-water since start

// Thread-local RT scope: true only inside audio_callback. session_process consults in_rt() before
// crediting a handoff skip, so the offline bounce (which calls session_process off the RT thread) never
// ticks realtime-contention metrics. Zero-cost: a TLS bool, no guard variable.
inline thread_local bool t_in_rt = false;
struct RtScope {
    RtScope()  { t_in_rt = true; }
    ~RtScope() { t_in_rt = false; }
    RtScope(const RtScope&) = delete;
    RtScope& operator=(const RtScope&) = delete;
};
inline bool in_rt() { return t_in_rt; }

// Credit a skipped try_lock handoff — a no-op unless called on the RT thread (inside RtScope).
inline void note_handoff_skip() {
    if (t_in_rt) g_handoff_skips.fetch_add(1, std::memory_order_relaxed);
}

// Publish this callback's wall time and, if it blew the budget, credit an over-budget block. Single RT
// writer, so the high-water update is a plain load/compare/store (no CAS).
inline void note_callback_us(uint32_t us, uint32_t frames, double sample_rate, double budget_mult) {
    g_last_callback_us.store(us, std::memory_order_relaxed);
    if (us > g_max_callback_us.load(std::memory_order_relaxed))
        g_max_callback_us.store(us, std::memory_order_relaxed);
    const double block_us = (sample_rate > 0.0 ? static_cast<double>(frames) / sample_rate : 0.0) * 1e6;
    const double budget_us = block_us * budget_mult;
    if (budget_us > 0.0 && static_cast<double>(us) > budget_us)
        g_over_budget.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace vivid::audio::health
