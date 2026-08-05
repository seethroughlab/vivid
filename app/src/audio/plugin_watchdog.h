#pragma once
// ADR-0045 Tier 2a — the realtime plugin WATCHDOG.
//
// A hosted VST3/CLAP plugin's process() runs on the RT audio thread with no time bound. Tier 1
// (crash_guard.h) attributes a *crash*; this bounds a plugin that goes *over budget*. Each plugin
// handle embeds a PluginFaultState; after every process() call the render site measures the elapsed
// time and calls watchdog_note_process(), which counts over-budget strikes and, past a threshold,
// marks the plugin `faulted`. process_step then skips a faulted plugin (fail-to-silence for an
// instrument, dry-passthrough for an effect — the auto-bypass semantics), so a plugin that can't keep
// up is disabled instead of stalling audio every block. The frame thread drains the fault ring into a
// toast + a diagnostics row.
//
// Pure + RT-safe: only atomic loads/stores + integer math on the hot path (getenv is read ONCE, cached
// in a function-local static that must be warmed before the RT thread — see watchdog_config()). No
// alloc, no lock. <chrono> is deliberately NOT included here so the plugin-handle headers stay light;
// the render site measures elapsed microseconds and passes them in.
#include "audio/plugin_fault_ring.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace vivid::audio {

// Per-plugin over-budget accounting, embedded in each plugin handle. The RT thread writes strikes/
// faulted; the frame thread and the hang monitor (slice 2) read `faulted`. std::atomic makes the
// enclosing handle non-copyable — which is correct: the handles own raw plugin pointers and are always
// managed by pointer, never value-copied.
struct PluginFaultState {
    std::atomic<uint32_t> strikes{0};        // net consecutive over-budget blocks (a good block decays one)
    std::atomic<bool>     faulted{false};    // latched once strikes hit the limit; cleared only by reload
};

// Tunables, read once from the environment (getenv is not RT-safe) and cached. Warm it on the main
// thread during audio setup so the RT thread never triggers the first-call initialization.
struct WatchdogConfig {
    double   budget_mult;    // over-budget threshold = mult * the block's realtime duration (frames/sr)
    uint32_t strike_limit;   // net over-budget strikes before a plugin is faulted
    uint32_t hang_ms;        // permanent-hang deadline for the monitor thread (slice 2)
};

inline const WatchdogConfig& watchdog_config() {
    static const WatchdogConfig cfg = [] {
        WatchdogConfig c{};
        const char* m  = std::getenv("VIVID_PLUGIN_BUDGET_MULT");
        const char* k  = std::getenv("VIVID_PLUGIN_STRIKES");
        const char* hg = std::getenv("VIVID_PLUGIN_HANG_MS");
        c.budget_mult  = m  ? std::atof(m)  : 1.0;   // one plugin alone exceeding realtime is over budget
        c.strike_limit = k  ? static_cast<uint32_t>(std::atoi(k))  : 8u;
        c.hang_ms      = hg ? static_cast<uint32_t>(std::atoi(hg)) : 1000u;
        if (!(c.budget_mult > 0.0)) c.budget_mult = 1.0;
        if (c.strike_limit == 0)    c.strike_limit = 8u;
        if (c.hang_ms == 0)         c.hang_ms = 1000u;
        return c;
    }();
    return cfg;
}

// Update strike accounting after a process() call. Returns true iff this call JUST faulted the plugin
// (the caller then emits exactly one fault report). RT-safe.
inline bool watchdog_tick(PluginFaultState& fs, uint32_t elapsed_us, uint32_t frames, double sample_rate,
                          const WatchdogConfig& cfg) {
    if (fs.faulted.load(std::memory_order_relaxed)) return false;   // already disabled — nothing to do
    const double block_us  = (sample_rate > 0.0 ? static_cast<double>(frames) / sample_rate : 0.0) * 1e6;
    const double budget_us = block_us * cfg.budget_mult;
    uint32_t s = fs.strikes.load(std::memory_order_relaxed);
    if (budget_us > 0.0 && static_cast<double>(elapsed_us) > budget_us) {
        s += 1;
        if (s >= cfg.strike_limit) {
            fs.strikes.store(s, std::memory_order_relaxed);
            fs.faulted.store(true, std::memory_order_release);   // publishes the disable to readers
            return true;
        }
    } else if (s > 0) {
        s -= 1;   // a within-budget block forgives one strike, so a lone spike never disables a plugin
    }
    fs.strikes.store(s, std::memory_order_relaxed);
    return false;
}

// The one call a render site makes after timing a process(): account the sample, and on a fresh fault
// push one named report onto the RT→UI ring. `name` must be a stable pointer (plugin_crash_name()).
inline void watchdog_note_process(PluginFaultState& fs, const char* name, int track_id,
                                  uint32_t elapsed_us, uint32_t frames, double sample_rate) {
    if (watchdog_tick(fs, elapsed_us, frames, sample_rate, watchdog_config()))
        plugin_fault_ring().push(name, track_id, elapsed_us, PluginFaultReason::OverBudget);
}

}  // namespace vivid::audio
