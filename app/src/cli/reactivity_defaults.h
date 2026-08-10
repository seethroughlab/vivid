#pragma once
// Legible reactivity defaults — the band->role convention baked into the mapping-recommendation tools
// so an intent-wired audio->visual mapping produces an OBVIOUS excursion, not an invisible one. The
// old intent tools defaulted every mapping to amount=1.0 with no envelope; demos hand-tuned amounts as
// low as 0.04 (a 4% wobble = invisible). This picks a per-(source role x dest semantic) default set,
// validated against the analyze_output(av) loop. Callers may still override any field explicitly.
//
// Semantics (mapping.h): dest_value = (lo + (hi-lo)*shaped) * amount, added to the param base. So
// `amount` is the peak excursion; attack/release shape the envelope (snap up on a hit, glide back).
#include <initializer_list>
#include <string>

namespace vivid {

struct MappingDefaults {
    float amount  = 0.4f;
    float curve   = 0.0f;
    float lo      = 0.0f;
    float hi      = 1.0f;
    float attack  = 0.02f;
    float release = 0.18f;
};

// Given a bridge source id (e.g. "master.low", "track_3.transient", "transport.downbeat") and a
// destination param NAME (e.g. "scale_x", "hue", "emission"), return legible mapping defaults.
inline MappingDefaults reactivity_defaults(const std::string& src, const std::string& dst_param) {
    std::string s = src, p = dst_param;
    auto low = [](std::string& x) { for (auto& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    low(s); low(p);
    auto has = [](const std::string& h, std::initializer_list<const char*> ks) {
        for (auto k : ks) if (h.find(k) != std::string::npos) return true; return false; };

    MappingDefaults d;

    // Source role -> envelope. Punctual sources (a discrete audio event) snap up fast then glide back;
    // sustained band energy uses a gentler follower. This is what makes reactivity read as PUNCTUAL.
    if (has(s, {".transient", ".gate", ".note", ".beat_pulse", ".downbeat", "transient", "onset"})) {
        d.attack = 0.005f; d.release = 0.22f;
    } else if (has(s, {".low", ".sub", "bass"})) {
        d.attack = 0.010f; d.release = 0.20f;
    } else if (has(s, {".high", "treble", "air"})) {
        d.attack = 0.008f; d.release = 0.12f;
    } else if (has(s, {".mid"})) {
        d.attack = 0.020f; d.release = 0.18f;
    } else {  // .level / other continuous energy
        d.attack = 0.030f; d.release = 0.20f;
    }

    // Dest semantic -> peak excursion (amount) + baseline. These are the numbers that were missing:
    // a scale wants a big monotonic swing; a hue wants to sweep its full range; a glow wants a punchy
    // pop with a faint always-on floor so it doesn't blink out entirely between hits.
    if (has(p, {"hue", "color", "tint", "palette", "phase", "chroma"})) {
        d.amount = 1.0f;                       // color should sweep fully to read
    } else if (has(p, {"emission", "glow", "intensity", "bright", "light", "bloom", "exposure"})) {
        d.amount = 0.7f; d.lo = 0.03f;         // punchy glow with a faint floor
    } else if (has(p, {"scale", "size", "radius", "inflate", "deform", "warp", "displace", "swell"})) {
        d.amount = 0.5f;                       // big, obvious monotonic inflation
    } else if (has(p, {"count", "density", "spawn", "rate", "particles", "instances"})) {
        d.amount = 0.5f;
    } else if (has(p, {"rot", "spin", "angle", "orbit", "twist", "phase"})) {
        d.amount = 0.4f;
    } else if (has(p, {"pos", "offset", "translate", "move", "cam", "target"})) {
        d.amount = 0.4f;
    } else {
        d.amount = 0.4f;                       // sensible visible default (NOT 1.0, NOT 0.04)
    }

    return d;
}

}  // namespace vivid
