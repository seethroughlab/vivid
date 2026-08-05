#pragma once
// ADR-0045 Tier 2a — the RT→UI channel that reports a plugin the watchdog just disabled.
//
// A lock-free SPSC ring (same discipline as note_event_ring.h): the audio thread (and the hang monitor,
// slice 2) push one record when a plugin is faulted; the frame thread drains everything queued since
// last frame and turns each into a toast + a diagnostics row. `name` is a stable pointer owned by the
// plugin handle (plugin_crash_name()), valid until the frame drains a few ms later. Overflow drops the
// newest records — bounded, and at worst a fault goes un-toasted (the plugin is still disabled).
//
// One audio engine per process, so the ring is a process-global reached through plugin_fault_ring()
// (defined in vst3_host.cpp), mirroring how the other RT singletons (movie audio, spectrum bus) work.
#include <atomic>
#include <cstdint>

namespace vivid::audio {

enum class PluginFaultReason : uint8_t { OverBudget = 0, Hang = 1 };

struct PluginFaultRecord {
    const char*       name;         // stable pointer (plugin_crash_name); "VST3/CLAP plugin" if unnamed
    int               track_id;     // the track the plugin runs on
    uint32_t          elapsed_us;   // the offending process() duration
    PluginFaultReason reason;
};

template <int N>
struct PluginFaultRing {
    // Audio thread / monitor thread: enqueue one record. Drops if full (frame drains every frame).
    void push(const char* name, int track_id, uint32_t elapsed_us, PluginFaultReason reason) {
        const uint64_t w = w_.load(std::memory_order_relaxed);
        const uint64_t r = r_.load(std::memory_order_acquire);
        if (w - r >= static_cast<uint64_t>(N)) return;   // full → drop newest
        buf_[w % N] = { name, track_id, elapsed_us, reason };
        w_.store(w + 1, std::memory_order_release);      // publishes the slot write above
    }

    // Frame thread: drain up to `max` queued records into `out`, advancing the read cursor. Returns count.
    int drain(PluginFaultRecord* out, int max) {
        const uint64_t w = w_.load(std::memory_order_acquire);   // orders the slot reads below
        uint64_t r = r_.load(std::memory_order_relaxed);
        int n = 0;
        while (r < w && n < max) { out[n++] = buf_[r % N]; ++r; }
        r_.store(r, std::memory_order_release);
        return n;
    }

private:
    std::atomic<uint64_t> w_{0};   // producer advances
    std::atomic<uint64_t> r_{0};   // frame thread advances
    PluginFaultRecord     buf_[N]{};
};

// The process-global fault ring (defined in vst3_host.cpp). 64 slots is far more than a frame can queue.
PluginFaultRing<64>& plugin_fault_ring();

}  // namespace vivid::audio
