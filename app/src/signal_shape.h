#pragma once
// Gamma shaping for a normalized 0..1 control signal — the one piece of math shared by the two
// right-sized modulation models ADR-0022 keeps deliberately separate:
//
//   - the flat `MappingRegistry` (app/src/mapping.h) — the audio<->visual bridge, UI thread,
//     frame rate, absolute output range.
//   - `EdgeKind::Control` (app/src/audio/audio_graph.h) — audio-internal modulation, audio
//     thread, block rate, range relative to the destination param's live base.
//
// They share the SHAPER and nothing else. It lives here rather than in mapping.h so the pure
// audio topology core can use it without taking a dependency on the bridge's registry (and its
// <string>/<unordered_map>) — the core is included by the RT executor and must stay lean.
//
// Pure and allocation-free: safe to call on the audio thread.
#include <cmath>

namespace vivid {

// curve 0 = linear; >0 eases in (exponent up to 4); <0 eases out (down to 1/4). Negative inputs
// clamp to 0 (std::pow of a negative base with a fractional exponent is NaN).
inline float shape_curve(float s, float curve) {
    if (curve == 0.0f) return s;
    const float e = curve > 0.0f ? (1.0f + curve * 3.0f) : 1.0f / (1.0f - curve * 3.0f);
    return std::pow(s < 0.0f ? 0.0f : s, e);
}

}  // namespace vivid
