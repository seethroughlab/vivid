#pragma once
// ADR-0053 Phase B: edge-owned shaping for a VISUAL param control edge — the typed replacement for
// the hidden, string-keyed audio->visual MappingRegistry (app/src/mapping.h). A control edge carries
// a source node's value lane into a visual op's parameter; this struct is the per-edge shaping and
// `visual_control_resolve` is the single combine that turns (base, source) into the resolved param.
//
// It is DELIBERATELY NOT audio_graph.h's `ControlShape`: that model is relative-to-base + bipolar
// (0 -> base-amount, 0.5 -> base), which is right for audio-internal modulation and wrong for the
// audio->visual bridge, whose output is an ABSOLUTE [out_lo,out_hi] range scaled by amount. The two
// right-sized models (ADR-0022) share only the gamma `shape_curve` from signal_shape.h.
//
// The resolve below is byte-identical to the Phase-A pair it supersedes — NodeGraph::apply_params
// (node_graph.cpp) combined with MappingRegistry::dest_value (mapping.h) — so migrating a project
// from string mappings to control edges leaves every pixel unchanged (verified by test_visual_control
// + the migration test). Keep the two in lockstep if either formula ever changes.
#include "signal_shape.h"   // vivid::shape_curve (shared gamma shaper)
#include <cmath>

namespace vivid {

// The nine fields carried by one control edge: the seven author-set shaping fields (mirroring
// vivid::Mapping's shaping half, minus source/dest) plus two mutable envelope-follower state cells.
struct VisualControlShape {
    float amount  = 1.0f;   // output gain
    float curve   = 0.0f;   // -1 ease-out .. 0 linear .. +1 ease-in (shape_curve)
    bool  invert  = false;  // polarity (1 - s), before shaping
    float out_lo  = 0.0f;   // shaped 0..1 maps into [out_lo, out_hi]
    float out_hi  = 1.0f;
    float attack  = 0.0f;   // rise time constant (s); 0 = instantaneous
    float release = 0.0f;   // fall time constant (s); 0 = instantaneous
    float smoothed = 0.0f;  // current smoothed shaped value (maintained by visual_control_advance)
    bool  primed   = false; // false until the first advance seeds `smoothed`
};

// The shaped 0..1 signal (clamp -> polarity -> curve), BEFORE range + gain. Matches mapping_shaped().
inline float visual_control_shaped(const VisualControlShape& sh, float raw_source) {
    float s = raw_source < 0.f ? 0.f : (raw_source > 1.f ? 1.f : raw_source);
    if (sh.invert) s = 1.f - s;
    return shape_curve(s, sh.curve);
}

// Advance the edge's envelope one frame (dt seconds) toward the shaped target — a one-pole with a
// separate attack/release time constant, so a bass pump snaps up then glides down instead of
// jittering. Mirrors MappingRegistry::advance for a single mapping. Call once per frame before
// resolve(); a no-op (target-follows-instantly) for an edge without smoothing.
inline void visual_control_advance(VisualControlShape& sh, float raw_source, float dt) {
    if (dt < 0.f) dt = 0.f;
    const float target = visual_control_shaped(sh, raw_source);
    if (!sh.primed) { sh.smoothed = target; sh.primed = true; return; }
    const float tau = (target > sh.smoothed) ? sh.attack : sh.release;
    if (tau <= 1e-5f) { sh.smoothed = target; return; }
    const float k = 1.f - std::exp(-dt / tau);   // one-pole coefficient for this dt
    sh.smoothed += (target - sh.smoothed) * k;
}

// Resolve a modulated param's effective value: base + modulation, clamped to the param's DECLARED
// range [pmin,pmax]. Byte-identical to apply_params + dest_value:
//   shaped = smoothed (if attack/release set) else shape(clamp(invert? 1-s : s), curve)
//   mod    = (out_lo + (out_hi - out_lo) * shaped) * amount        [range, then gain]
//   value  = clamp(base + mod * (pmax - pmin), pmin, pmax)         [scale by declared range]
// `raw_source` is the source's current 0..1 value (used only when the edge has no smoothing; the
// smoothed path reads sh.smoothed, kept current by visual_control_advance).
inline float visual_control_resolve(float base, float raw_source, const VisualControlShape& sh,
                                    float pmin, float pmax) {
    const bool smooth = (sh.attack > 1e-5f || sh.release > 1e-5f);
    const float shaped = smooth ? sh.smoothed : visual_control_shaped(sh, raw_source);
    const float mod = (sh.out_lo + (sh.out_hi - sh.out_lo) * shaped) * sh.amount;
    const float v = base + mod * (pmax - pmin);
    return v < pmin ? pmin : (v > pmax ? pmax : v);
}

}  // namespace vivid
