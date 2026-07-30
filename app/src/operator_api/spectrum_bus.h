#pragma once
// Master-spectrum bus — lets a GPU visual op (a sandboxed dylib on the render thread) read the live
// audio spectrum (log-spaced magnitude bands, low→high) so it can drive geometry per FREQUENCY BAND: a
// 3D equaliser, a spectral ridge, per-band instance heights. Mirrors note_bus.h / movie_audio.h — the
// host fills the bands once per frame; the op pulls a snapshot. Host-provided symbols, resolved at
// dlopen like vivid_track_active_notes (operators link with -undefined dynamic_lookup).
//
// One publisher (the UI/frame thread), many readers (render-thread ops). Bands are stored as atomics;
// a torn read mixes bands from adjacent frames — a 1-frame cosmetic glitch at worst, and well-defined
// under ThreadSanitizer (ADR-0029), never UB. Magnitudes are the same 0..~1 log-band values the
// `master.fft.*` bridge publishes, just carried as a whole array at higher band resolution.
#include <stdint.h>

#define VIVID_SPECTRUM_MAX_BANDS 64

#ifdef __cplusplus
extern "C" {
#endif

// --- consumer side (a GPU op, render thread) ---
// Copy up to `max` master spectrum bands (index 0 = lowest frequency) into `out`. Returns the count
// available this frame (0 before any audio has been analysed). Cheap, lock-free snapshot.
uint32_t vivid_master_spectrum(float* out, uint32_t max);

// --- host wiring (the app, UI/frame thread) ---
// Publish `count` master spectrum bands (clamped to VIVID_SPECTRUM_MAX_BANDS). Called once per frame.
void     vivid_spectrum_bus_publish_master(const float* bands, uint32_t count);

#ifdef __cplusplus
}
#endif
