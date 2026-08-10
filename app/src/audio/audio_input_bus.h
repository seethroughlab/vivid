#pragma once
// ADR-0032 Phase D2 — host-side hardware-INPUT bus.
//
// The RT audio callback writes a DUPLEX device's capture block here (producer); an AudioInput graph
// source op drains it (consumer) so live external audio (mic / line-in / interface) flows through the
// audio graph — MONITORING (route the op to Output) and, via the existing per-track/master analysis, a
// REACTIVE source (visuals respond to live sound). One lock-free SPSC stereo ring: a single producer
// (the callback thread) and a single logical consumer (the AudioInput op — route/split downstream in
// the graph rather than placing multiple AudioInput ops). RT-safe: no lock, no allocation on either
// path; the ring storage lives for the app lifetime. No-op / silence when the device is playback-only.
#include <cstdint>

namespace vivid {

// Producer (audio callback thread): push `frames` of INTERLEAVED stereo capture. Drops the tail on a
// full ring (a stalled consumer never builds unbounded latency — the ring is deliberately small).
void audio_input_write(const float* interleaved_stereo, uint32_t frames);

// Consumer (AudioInput op, audio thread / worker): pull `frames` into PLANAR left[]/right[],
// zero-padded on underrun (or when there is no input). Returns the frames actually available. NOT
// transport-gated — live input stays audible while paused.
uint32_t audio_input_pull(float* left, float* right, uint32_t frames);

// Drop any buffered input (called around a device reopen so a fresh capture stream doesn't play out a
// stale tail). Main thread, with the device stopped — no concurrent producer.
void audio_input_reset();

}  // namespace vivid
