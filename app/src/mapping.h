#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace vivid {

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
};

// Gamma shaping: curve 0 = linear; >0 eases in (exp up to 4); <0 eases out.
inline float mapping_shape(float s, float curve) {
    if (curve == 0.0f) return s;
    const float e = curve > 0.0f ? (1.0f + curve * 3.0f) : 1.0f / (1.0f - curve * 3.0f);
    return std::pow(s < 0.0f ? 0.0f : s, e);
}

// Central registry: holds the mappings + the current value of every source.
// One mapping per destination (matches the old one-wire-per-port behaviour).
class MappingRegistry {
public:
    void  set_source(const std::string& id, float v) { sources_[id] = v; }
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
    // Current value driving `dst`: clamp -> polarity -> curve -> gain. 0 if unmapped.
    float dest_value(const std::string& dst) const {
        for (const auto& m : maps_) if (m.dest == dst) {
            float s = source_value(m.source);
            s = s < 0.f ? 0.f : (s > 1.f ? 1.f : s);
            if (m.invert) s = 1.f - s;
            const float shaped = mapping_shape(s, m.curve);
            return (m.out_lo + (m.out_hi - m.out_lo) * shaped) * m.amount;  // range, then gain
        }
        return 0.f;
    }

    const std::vector<Mapping>& mappings() const { return maps_; }
    void clear_mappings() { maps_.clear(); }

private:
    std::vector<Mapping> maps_;
    std::unordered_map<std::string, float> sources_;
};

}  // namespace vivid
