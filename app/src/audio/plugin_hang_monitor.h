#pragma once
// ADR-0045 Tier 2a — the permanent-HANG monitor.
//
// The over-budget watchdog (plugin_watchdog.h) handles a plugin that RETURNS too slowly: the RT thread
// measures it after the fact and disables it. But a plugin that NEVER returns (a spinlock, blocking IO, a
// wavetable rescan that deadlocks) leaves the RT thread stuck INSIDE process() — it can't measure or
// disable itself. So a separate monitor thread watches an in-flight beacon the RT thread publishes before
// each process() call. If a call has been running past the hang deadline, the monitor names it, latches it
// `faulted` (so it is skipped if/when the RT thread ever returns), and pushes a Hang report — turning a
// silent freeze into a named, logged, auto-disabled fault. It CANNOT unblock the frozen RT thread; true
// rescue (bounded-deadline off-thread processing) is Tier 2b, deferred.
//
// The beacon is a process-global (one audio engine → one RT thread → one process() in flight at a time).
// RT thread writes it (release); the monitor reads it (acquire). Kept out of the plugin-handle headers so
// they stay light — only the render sites, main.cpp and the monitor include this.
#include "audio/plugin_watchdog.h"
#include "audio/plugin_fault_ring.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace vivid::audio {

inline uint64_t watchdog_now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// What plugin, if any, the RT thread is currently inside process() for. `active` gates the other fields
// (published by its release-store). For a permanent hang the beacon is stable (the RT thread is stuck, so
// nothing rotates it), which is exactly the case the monitor must get right.
struct PluginInFlight {
    std::atomic<uint64_t>          start_ns{0};
    std::atomic<PluginFaultState*> fs{nullptr};
    std::atomic<const char*>       name{nullptr};
    std::atomic<int>               track_id{-1};
    std::atomic<bool>              active{false};
};

// The process-global beacon (defined in vst3_host.cpp, beside the fault ring).
PluginInFlight& plugin_inflight();

// RT thread: mark that we are about to call a plugin's process(). Cleared on return; if process() hangs,
// clear never runs and the beacon stays active for the monitor to find.
inline void watchdog_mark_inflight(PluginFaultState* fs, const char* name, int track_id) {
    PluginInFlight& b = plugin_inflight();
    b.start_ns.store(watchdog_now_ns(), std::memory_order_relaxed);
    b.fs.store(fs, std::memory_order_relaxed);
    b.name.store(name, std::memory_order_relaxed);
    b.track_id.store(track_id, std::memory_order_relaxed);
    b.active.store(true, std::memory_order_release);   // publishes the fields above
}
inline void watchdog_clear_inflight() {
    plugin_inflight().active.store(false, std::memory_order_release);
}

// A low-priority thread (like plugin_scan's worker) that trips the hang deadline. Lifecycle is tied to the
// audio device in main.cpp: start() after the device starts, stop() before it tears down.
class PluginHangMonitor {
public:
    ~PluginHangMonitor() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        th_ = std::thread([this] { run(); });
    }
    void stop() {
        if (!running_.exchange(false)) return;
        if (th_.joinable()) th_.join();
    }

private:
    void run() {
        const uint64_t hang_ns = static_cast<uint64_t>(watchdog_config().hang_ms) * 1000000ull;
        // Poll often enough to catch a hang shortly after the deadline, but never busier than 5 ms.
        const uint32_t poll_ms = watchdog_config().hang_ms / 4 < 5 ? 5
                               : (watchdog_config().hang_ms / 4 > 250 ? 250 : watchdog_config().hang_ms / 4);
        uint64_t reported_start = 0;   // the in-flight episode we've already reported (report once)
        while (running_.load(std::memory_order_relaxed)) {
            PluginInFlight& b = plugin_inflight();
            if (b.active.load(std::memory_order_acquire)) {
                const uint64_t start = b.start_ns.load(std::memory_order_relaxed);
                const uint64_t now   = watchdog_now_ns();
                if (now > start && now - start > hang_ns && start != reported_start) {
                    PluginFaultState* fs = b.fs.load(std::memory_order_relaxed);
                    const char* name     = b.name.load(std::memory_order_relaxed);
                    const int   tid      = b.track_id.load(std::memory_order_relaxed);
                    // Re-check the beacon hasn't rotated to a different plugin since we sampled `start`
                    // (for a real hang it never does — the RT thread is stuck).
                    if (b.active.load(std::memory_order_acquire) &&
                        b.start_ns.load(std::memory_order_relaxed) == start) {
                        if (fs) fs->faulted.store(true, std::memory_order_release);
                        plugin_fault_ring().push(name, tid, static_cast<uint32_t>((now - start) / 1000ull),
                                                 PluginFaultReason::Hang);
                        reported_start = start;
                    }
                }
            } else {
                reported_start = 0;   // idle — arm for the next episode
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        }
    }

    std::atomic<bool> running_{false};
    std::thread       th_;
};

}  // namespace vivid::audio
