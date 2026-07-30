// Host side of the master-spectrum bus (operator_api/spectrum_bus.h). One fixed band array the frame
// thread fills each frame from the master FFT; render-thread ops read a lock-free snapshot to drive
// geometry per frequency band. Single publisher, many readers — bands are atomics written/read relaxed,
// gated by an atomic `count` (release/acquire) so a reader that sees N bands sees N published values.
// A torn read mixes bands from adjacent frames (each band self-consistent), a 1-frame glitch at worst;
// clean under ThreadSanitizer (ADR-0029) since every cross-thread access is an ordered atomic.
#include "operator_api/spectrum_bus.h"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace {
struct Spectrum {
    std::atomic<uint32_t> count{0};                               // # valid bands (release-gated)
    std::atomic<uint32_t> band[VIVID_SPECTRUM_MAX_BANDS];         // float bits, one atomic per band
};
Spectrum g_master;

inline uint32_t f2b(float f) { uint32_t b; std::memcpy(&b, &f, sizeof(b)); return b; }
inline float    b2f(uint32_t b) { float f; std::memcpy(&f, &b, sizeof(f)); return f; }
}  // namespace

extern "C" uint32_t vivid_master_spectrum(float* out, uint32_t max) {
    if (!out || max == 0) return 0;
    uint32_t n = g_master.count.load(std::memory_order_acquire);   // acquire: orders the band loads below
    if (n > VIVID_SPECTRUM_MAX_BANDS) n = VIVID_SPECTRUM_MAX_BANDS;
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; ++i) out[i] = b2f(g_master.band[i].load(std::memory_order_relaxed));
    return n;
}

extern "C" void vivid_spectrum_bus_publish_master(const float* bands, uint32_t count) {
    if (count > VIVID_SPECTRUM_MAX_BANDS) count = VIVID_SPECTRUM_MAX_BANDS;
    for (uint32_t i = 0; i < count && bands; ++i)
        g_master.band[i].store(f2b(bands[i]), std::memory_order_relaxed);
    g_master.count.store(bands ? count : 0, std::memory_order_release);   // release: publishes the bands
}
