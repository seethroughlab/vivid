#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace vivid {

// One wire in the unified mapping model: a named source drives a named
// destination, scaled by `amount`. Source/dest IDs are strings so the same model
// covers audio->visual ("track_2.transient" -> "uniform.warp") and, later,
// visual->audio ("viz.feedback_energy" -> "track_0.dev1.param.123"). Mirrors
// classic's ModAssignmentDef shape (amount; polarity/curve can follow).
struct Mapping {
    std::string source;
    std::string dest;
    float       amount = 1.0f;
};

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
    // Current value driving `dst` (source value * amount), or 0 if unmapped.
    float dest_value(const std::string& dst) const {
        for (const auto& m : maps_) if (m.dest == dst) return source_value(m.source) * m.amount;
        return 0.f;
    }

    const std::vector<Mapping>& mappings() const { return maps_; }
    void clear_mappings() { maps_.clear(); }

private:
    std::vector<Mapping> maps_;
    std::unordered_map<std::string, float> sources_;
};

}  // namespace vivid
