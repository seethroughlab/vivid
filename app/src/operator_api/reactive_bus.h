#pragma once
// Reactive-signal bus — lets a GPU visual SOURCE op (a sandboxed dylib on the render thread) read the
// live per-frame audio characteristics as scalar values, so audio->visual reactivity is a REAL graph
// node (ReactiveMaster / ReactiveTrack) emitting value lanes, not a hidden string-keyed mapping
// (ADR-0053 Phase B; completes ADR-0047's "hidden buses become visible nodes"). Mirrors spectrum_bus.h
// (whole-array snapshot) + note_bus.h (per-track stable-id slots). Host-provided symbols resolved at
// dlopen (operators link -undefined dynamic_lookup).
//
// One publisher (the UI/frame thread, once per frame from the same mvals/tvals it feeds the registry),
// many readers (render-thread ops). Signals are atomics, count release-gated: a torn read mixes
// signals from adjacent frames (each self-consistent) — a 1-frame cosmetic glitch at worst, and clean
// under ThreadSanitizer (ADR-0029). All values are the 0..1 characteristics the master.*/track_*
// bridge publishes; the ordering below IS the canonical signal grammar consumers index by.
#include <stdint.h>

// Master signals, in canonical order (index == output-port ordinal of ReactiveMaster):
//   0 level  1 transient  2 low  3 mid  4 high         (the five master scalars)
//   5 beat   6 bar_phase  7 downbeat  8 beat_pulse     (the four transport signals)
#define VIVID_REACTIVE_MASTER_SIGNALS 9
// Track signals, in canonical order (index == output-port ordinal of ReactiveTrack):
//   0 level  1 transient  2 low  3 mid  4 high  5 note  6 velocity  7 gate
#define VIVID_REACTIVE_TRACK_SIGNALS  8
#define VIVID_REACTIVE_BUS_TRACKS     32   // publish slots (matches note_bus)

#ifdef __cplusplus
extern "C" {
#endif

// --- consumer side (a GPU op, render thread) ---
// Copy up to `max` master signals (canonical order above) into `out`. Returns the count available
// (0 before any audio has been analysed). Cheap, lock-free snapshot.
uint32_t vivid_master_signals(float* out, uint32_t max);
// Copy up to `max` signals of the track with STABLE id `track_id` into `out`. Returns the count
// copied (0 if that track isn't live this frame). Addresses by stable id so the op follows its track
// across reorder/delete — like every other visual source.
uint32_t vivid_track_signals(int track_id, float* out, uint32_t max);

// --- host wiring (the app, UI/frame thread) ---
// Publish `count` master signals (clamped to VIVID_REACTIVE_MASTER_SIGNALS). Called once per frame.
void     vivid_reactive_bus_publish_master(const float* signals, uint32_t count);
// Publish `count` signals into position `slot` (0..VIVID_REACTIVE_BUS_TRACKS-1), tagged with the
// track's stable `track_id`. Pass track_id < 0 to free a slot no live track occupies. Once per frame.
void     vivid_reactive_bus_publish_track(int slot, int track_id, const float* signals, uint32_t count);

#ifdef __cplusplus
}
#endif
