// Host side of the reactive-signal bus (operator_api/reactive_bus.h). One fixed master signal array +
// a small fixed set of per-track slots the frame thread fills each frame from the same mvals/tvals it
// feeds the MappingRegistry; render-thread ops (ReactiveMaster / ReactiveTrack) read a lock-free
// snapshot to drive visual params through real graph edges (ADR-0053 Phase B). Single publisher, many
// readers — signals are atomics written/read relaxed, gated by an atomic `count` (release/acquire) so a
// reader that sees N signals sees N published values. A track slot is TAGGED with the stable track id
// (published last, release) so a reader matching the id sees the signals that go with it — the note_bus
// discipline. A torn read mixes signals from adjacent frames (each self-consistent), a 1-frame glitch
// at worst; clean under ThreadSanitizer (ADR-0029) since every cross-thread access is an ordered atomic.
#include "operator_api/reactive_bus.h"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace {
inline uint32_t f2b(float f) { uint32_t b; std::memcpy(&b, &f, sizeof(b)); return b; }
inline float    b2f(uint32_t b) { float f; std::memcpy(&f, &b, sizeof(f)); return f; }

struct Master {
    std::atomic<uint32_t> count{0};                                    // # valid signals (release-gated)
    std::atomic<uint32_t> sig[VIVID_REACTIVE_MASTER_SIGNALS];          // float bits, one atomic per signal
};
Master g_master;

struct TrackChannel {
    std::atomic<int>      track_id{-1};                                // stable id in this slot, or -1 (free)
    std::atomic<uint32_t> sig[VIVID_REACTIVE_TRACK_SIGNALS];           // float bits, one atomic per signal
    std::atomic<uint32_t> count{0};
};
TrackChannel g_tracks[VIVID_REACTIVE_BUS_TRACKS];
}  // namespace

extern "C" uint32_t vivid_master_signals(float* out, uint32_t max) {
    if (!out || max == 0) return 0;
    uint32_t n = g_master.count.load(std::memory_order_acquire);   // acquire: orders the signal loads below
    if (n > VIVID_REACTIVE_MASTER_SIGNALS) n = VIVID_REACTIVE_MASTER_SIGNALS;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) out[i] = b2f(g_master.sig[i].load(std::memory_order_relaxed));
    return n;
}

extern "C" uint32_t vivid_track_signals(int track_id, float* out, uint32_t max) {
    if (track_id < 0 || !out || max == 0) return 0;
    for (int s = 0; s < VIVID_REACTIVE_BUS_TRACKS; ++s) {
        TrackChannel& c = g_tracks[s];
        if (c.track_id.load(std::memory_order_acquire) != track_id) continue;
        uint32_t n = c.count.load(std::memory_order_acquire);   // acquire: orders the signal loads below
        if (n > VIVID_REACTIVE_TRACK_SIGNALS) n = VIVID_REACTIVE_TRACK_SIGNALS;
        if (n > max) n = max;
        for (uint32_t i = 0; i < n; ++i) out[i] = b2f(c.sig[i].load(std::memory_order_relaxed));
        return n;
    }
    return 0;
}

extern "C" void vivid_reactive_bus_publish_master(const float* signals, uint32_t count) {
    if (count > VIVID_REACTIVE_MASTER_SIGNALS) count = VIVID_REACTIVE_MASTER_SIGNALS;
    for (uint32_t i = 0; i < count && signals; ++i)
        g_master.sig[i].store(f2b(signals[i]), std::memory_order_relaxed);
    g_master.count.store(signals ? count : 0, std::memory_order_release);   // release: publishes the signals
}

extern "C" void vivid_reactive_bus_publish_track(int slot, int track_id, const float* signals, uint32_t count) {
    if (slot < 0 || slot >= VIVID_REACTIVE_BUS_TRACKS) return;
    TrackChannel& c = g_tracks[slot];
    if (count > VIVID_REACTIVE_TRACK_SIGNALS) count = VIVID_REACTIVE_TRACK_SIGNALS;
    for (uint32_t i = 0; i < count && signals; ++i)
        c.sig[i].store(f2b(signals[i]), std::memory_order_relaxed);
    c.count.store(signals ? count : 0, std::memory_order_release);   // release: publishes the signal stores
    c.track_id.store(track_id, std::memory_order_release);           // published last: gates the signals above
}
