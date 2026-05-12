#include "midi_clip_editor_shared.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace midi_clip {

std::string serialize_pattern(const std::vector<ParsedNote>& notes) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& n : notes) {
        nlohmann::json obj;
        obj["p"] = n.pitch;
        obj["s"] = n.start_beat;
        obj["d"] = n.duration_beats;
        obj["v"] = n.velocity;
        if (n.pitch_bend != 0.0f) obj["b"]  = n.pitch_bend;
        if (n.pressure   != 0.0f) obj["pr"] = n.pressure;
        arr.push_back(std::move(obj));
    }
    return arr.dump();
}

bool parse_pattern(const std::string& json, std::vector<ParsedNote>& out) {
    out.clear();
    if (json.empty() || json == "[]") return true;
    try {
        auto root = nlohmann::json::parse(json);
        if (!root.is_array()) return false;
        out.reserve(root.size());
        for (auto& obj : root) {
            ParsedNote n{};
            n.pitch          = static_cast<uint8_t>(
                std::clamp(obj.value("p", 60), 0, 127));
            n.start_beat     = obj.value("s", 0.0);
            n.duration_beats = std::max(0.01, obj.value("d", 0.25));
            n.velocity       = std::clamp(
                static_cast<float>(obj.value("v", 0.8)), 0.0f, 1.0f);
            n.pitch_bend     = std::clamp(
                static_cast<float>(obj.value("b",  0.0)), -12.0f, 12.0f);
            n.pressure       = std::clamp(
                static_cast<float>(obj.value("pr", 0.0)),  0.0f,  1.0f);
            out.push_back(n);
        }
        return true;
    } catch (...) {
        out.clear();
        return false;
    }
}

} // namespace midi_clip
