#pragma once
// ADR-0032 Phase E1 — playback plugin-delay compensation (PDC): the pure delay-ring primitive.
//
// PDC aligns tracks that carry different plugin latencies by delaying each compensable track by
// (L_max - L_track), so every compensable track emits at the same total latency L_max (== the latency
// the most-latent track already had). This header holds ONLY the ring math — a fixed read-behind-write
// delay over a per-track stereo ring — with no dependency on the audio engine, so it is unit-testable in
// isolation. The engine (master_mix) owns the ring storage on each Track and calls pdc_delay_accumulate.
//
// RT-safety: no allocation, no lock; a single write cursor advanced per block. The ring is a power of two
// so the modulo is a mask and unsigned wraparound of (w + i - delay) is exact for delay in [0, kPdcMaxComp].
#include <cstdint>

namespace vivid::audio {

// Ring capacity per track (power of two → mask modulo). kMaxTracks × 2ch × kPdcRingCap × 4B = 16 MiB at
// 32 tracks — negligible next to the node pool. kPdcMaxComp reserves one max audio block (kGraphMaxBlock
// = 4096) so the oldest read (w - delay) and newest write (w + frames - 1) never alias: the span
// delay + frames stays <= kPdcRingCap for any legal block. (Coupling asserted in vst3_host_internal.h.)
constexpr uint32_t kPdcRingCap  = 65536;
constexpr uint32_t kPdcRingMask = kPdcRingCap - 1;
constexpr uint32_t kPdcMaxComp  = kPdcRingCap - 4096;   // 61440 samples (~1.28 s @ 48 kHz)

// Delay a planar-stereo input block by `delay` samples through the ring and accumulate the delayed,
// `scale`-multiplied result into interleaved-stereo `out` (out[2i], out[2i+1]). ringL/ringR are
// kPdcRingCap floats each; `w` is the current write cursor (returned advanced by `frames`). Preconditions:
// delay <= kPdcMaxComp, frames <= kGraphMaxBlock. delay==0 still round-trips (write then read the same
// cell) — the engine branches on delay<=0 for the byte-identical no-delay fast path before calling this.
inline uint32_t pdc_delay_accumulate(float* ringL, float* ringR, uint32_t w, uint32_t delay,
                                     const float* L, const float* R, float* out, float scale,
                                     uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        const uint32_t wi = (w + i) & kPdcRingMask;
        ringL[wi] = L[i];
        ringR[wi] = R[i];
        const uint32_t ri = (w + i - delay) & kPdcRingMask;   // read `delay` behind; unsigned wrap is exact
        out[2 * i]     += scale * ringL[ri];
        out[2 * i + 1] += scale * ringR[ri];
    }
    return (w + frames) & kPdcRingMask;
}

}  // namespace vivid::audio
