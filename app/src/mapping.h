#pragma once
#include "signal_shape.h"   // vivid::shape_curve — shared with the audio-graph control model

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace vivid {

// Audio mapping sources encode the track's STABLE id ("track_3.transient"). Parse one into its
// id + the remainder (".transient"); returns false for non-track sources ("master.*",
// "viz.*"). Pure — shared by the delete-fix-up + its test.
inline bool parse_track_source(const std::string& src, int& idx, std::string& rest) {
    if (src.rfind("track_", 0) != 0) return false;
    size_t i = 6;
    if (i >= src.size() || !std::isdigit(static_cast<unsigned char>(src[i]))) return false;
    size_t j = i;
    while (j < src.size() && std::isdigit(static_cast<unsigned char>(src[j]))) ++j;
    idx  = std::atoi(src.substr(i, j - i).c_str());
    rest = src.substr(j);   // includes the leading '.'
    return true;
}

// One wire in the unified mapping model: a named source drives a named
// destination. The source value (clamped 0..1) is optionally inverted (polarity),
// gamma-shaped (curve), then scaled by `amount`. Source/dest IDs are strings so
// the same model covers audio->visual ("track_2.transient" -> "node:0.warp") and
// visual->audio ("viz.feedback" -> "param:0:1:3"). Mirrors classic's
// ModAssignmentDef shape (amount + polarity + curve).
struct Mapping {
    std::string source;
    std::string dest;
    float       amount = 1.0f;   // output gain
    float       curve  = 0.0f;   // -1 ease-out .. 0 linear .. +1 ease-in
    bool        invert = false;  // polarity (1 - s)
    float       out_lo = 0.0f;   // output range: shaped 0..1 maps to [out_lo, out_hi]
    float       out_hi = 1.0f;
    // Temporal smoothing (envelope follower on the shaped 0..1 signal). Raw audio envelopes are
    // jumpy; a fast attack + slow release lets a param SNAP up on a hit then glide back, killing the
    // frame-to-frame jitter that reads as "glitchy". 0/0 = instantaneous (old behaviour). Seconds.
    float       attack  = 0.0f;  // time constant while rising toward a higher target
    float       release = 0.0f;  // time constant while falling toward a lower target
    mutable float smoothed = 0.0f;  // current smoothed shaped value (maintained by advance())
    mutable bool  primed   = false; // false until advance() seeds `smoothed` on first tick
};

// Gamma shaping: curve 0 = linear; >0 eases in (exp up to 4); <0 eases out.
// The math moved to signal_shape.h when the audio-graph control model needed the same shaper
// (ADR-0022); this name is the bridge's spelling of it.
inline float mapping_shape(float s, float curve) { return shape_curve(s, curve); }

// The shaped 0..1 signal for a mapping (clamp -> polarity -> curve), before range + gain.
inline float mapping_shaped(const Mapping& m, float raw_source) {
    float s = raw_source < 0.f ? 0.f : (raw_source > 1.f ? 1.f : raw_source);
    if (m.invert) s = 1.f - s;
    return mapping_shape(s, m.curve);
}

// Central registry: holds the mappings + the current value of every source.
// One mapping per destination (matches the old one-wire-per-port behaviour).
class MappingRegistry {
public:
    void  set_source(const std::string& id, float v) { sources_[id] = v; }
    // ADR-0028: intern a source id to a STABLE value cell. std::unordered_map never invalidates
    // pointers/references to its elements on insert or rehash (only on erase — and sources are never
    // erased), so the returned pointer stays valid for the registry's lifetime. A hot publisher resolves
    // the id once and writes `*cell = v` each frame instead of re-hashing the string.
    float* intern_source(const std::string& id) { return &sources_[id]; }
    float source_value(const std::string& id) const {
        auto it = sources_.find(id);
        return it != sources_.end() ? it->second : 0.f;
    }

    // Wire `src` -> `dst` (replacing any existing wire into `dst`).
    void connect(const std::string& src, const std::string& dst, float amount = 1.0f) {
        for (auto& m : maps_) if (m.dest == dst) { m.source = src; m.amount = amount; return; }
        maps_.push_back({ src, dst, amount });
    }
    void disconnect(const std::string& dst) {
        maps_.erase(std::remove_if(maps_.begin(), maps_.end(),
                                   [&](const Mapping& m) { return m.dest == dst; }), maps_.end());
    }
    const std::string* source_of(const std::string& dst) const {
        for (const auto& m : maps_) if (m.dest == dst) return &m.source;
        return nullptr;
    }
    Mapping* find(const std::string& dst) {           // mutable access for editing
        for (auto& m : maps_) if (m.dest == dst) return &m;
        return nullptr;
    }
    // Current value driving `dst`: shaped signal (smoothed if the mapping asks for it) -> range -> gain.
    // 0 if unmapped.
    float dest_value(const std::string& dst) const {
        for (const auto& m : maps_) if (m.dest == dst) {
            const bool smooth = (m.attack > 1e-5f || m.release > 1e-5f);
            const float shaped = smooth ? m.smoothed : mapping_shaped(m, source_value(m.source));
            return (m.out_lo + (m.out_hi - m.out_lo) * shaped) * m.amount;  // range, then gain
        }
        return 0.f;
    }

    // Advance every smoothed mapping one frame (dt seconds). A one-pole toward the shaped target with a
    // separate attack/release time constant, so a bass pump snaps up then glides down instead of
    // jittering. Call once per frame before resolving params; a no-op for mappings without smoothing.
    void advance(float dt) {
        if (dt < 0.f) dt = 0.f;
        for (auto& m : maps_) {
            const float target = mapping_shaped(m, source_value(m.source));
            if (!m.primed) { m.smoothed = target; m.primed = true; continue; }
            const float tau = (target > m.smoothed) ? m.attack : m.release;
            if (tau <= 1e-5f) { m.smoothed = target; continue; }
            const float k = 1.f - std::exp(-dt / tau);   // one-pole coefficient for this dt
            m.smoothed += (target - m.smoothed) * k;
        }
    }

    const std::vector<Mapping>& mappings() const { return maps_; }
    void clear_mappings() { maps_.clear(); }

    // A track with stable id `id` was deleted: drop mappings sourced from it. No renumbering
    // — sources encode the stable id, so survivors are untouched (a mapping always follows
    // the same track). Dest IDs reference visual nodes, not tracks, so they're left. Returns
    // # dropped.
    int drop_track_sources(int id) {
        int dropped = 0;
        std::vector<Mapping> kept;
        kept.reserve(maps_.size());
        for (auto& m : maps_) {
            int n; std::string rest;
            if (parse_track_source(m.source, n, rest) && n == id) { ++dropped; continue; }
            kept.push_back(m);
        }
        maps_.swap(kept);
        return dropped;
    }

private:
    std::vector<Mapping> maps_;
    std::unordered_map<std::string, float> sources_;
};

}  // namespace vivid
