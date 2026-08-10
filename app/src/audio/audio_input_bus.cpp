// Host side of the hardware-input bus (see audio_input_bus.h). One lock-free SPSC stereo ring: the RT
// callback writes the capture block, an AudioInput graph op drains it. Adapted from the movie-audio bus
// Ring, minus the master A/V clock (hardware input has no media time — it's a plain live FIFO). The ring
// is deliberately SMALL so a stalled consumer (no AudioInput op in the graph) never builds latency: it
// fills, drops, and the moment a consumer appears it catches up within a few blocks.
#include "audio/audio_input_bus.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace vivid {

namespace {

struct Ring {
    static constexpr uint32_t kCapacity = 8192;   // ~185 ms @ 44.1 kHz — headroom for worker jitter, small enough to self-heal
    std::array<float, kCapacity> left{};
    std::array<float, kCapacity> right{};
    std::atomic<uint32_t> write_pos{0};   // producer advances (release)
    std::atomic<uint32_t> read_pos{0};    // consumer advances (release)

    uint32_t available_read() const {
        return (write_pos.load(std::memory_order_acquire) - read_pos.load(std::memory_order_relaxed) + kCapacity) % kCapacity;
    }
    uint32_t available_write() const {
        // -1: keep one slot empty so full and empty are distinguishable.
        return (read_pos.load(std::memory_order_acquire) - write_pos.load(std::memory_order_relaxed) - 1 + kCapacity) % kCapacity;
    }
};

Ring g_ring;   // one hardware input → one bus, app-lifetime storage

}  // namespace

void audio_input_write(const float* in, uint32_t frames) {
    if (!in || frames == 0) return;
    const uint32_t n = std::min(frames, g_ring.available_write());
    const uint32_t wp = g_ring.write_pos.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t idx = (wp + i) % Ring::kCapacity;
        g_ring.left[idx]  = in[i * 2 + 0];
        g_ring.right[idx] = in[i * 2 + 1];
    }
    g_ring.write_pos.store((wp + n) % Ring::kCapacity, std::memory_order_release);
}

uint32_t audio_input_pull(float* l, float* r, uint32_t frames) {
    if (!l || !r || frames == 0) return 0;
    const uint32_t n = std::min(frames, g_ring.available_read());
    const uint32_t rp = g_ring.read_pos.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t idx = (rp + i) % Ring::kCapacity;
        l[i] = g_ring.left[idx];
        r[i] = g_ring.right[idx];
    }
    if (n < frames) {   // underrun / no input → zero-pad the remainder
        std::memset(l + n, 0, (frames - n) * sizeof(float));
        std::memset(r + n, 0, (frames - n) * sizeof(float));
    }
    g_ring.read_pos.store((rp + n) % Ring::kCapacity, std::memory_order_release);
    return n;
}

void audio_input_reset() {
    // Called with the device stopped (no concurrent producer): collapse read to write so nothing stale
    // remains buffered. Relaxed is fine — the RT threads are quiesced at this point.
    g_ring.read_pos.store(g_ring.write_pos.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

}  // namespace vivid
