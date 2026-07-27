#pragma once

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>

namespace vivid::control_mapping {

inline std::string lower_copy(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

inline bool valid_audio_characteristic(const std::string& characteristic, bool master) {
    static constexpr const char* kMaster[] = { "level", "transient", "low", "mid", "high" };
    static constexpr const char* kTrack[] = { "level", "transient", "low", "mid", "high", "note", "velocity", "gate" };
    const auto* first = master ? std::begin(kMaster) : std::begin(kTrack);
    const auto* last = master ? std::end(kMaster) : std::end(kTrack);
    return std::find(first, last, characteristic) != last;
}

inline std::string master_source(const std::string& characteristic) {
    return "master." + characteristic;
}

inline std::string track_source(int stable_track_id, const std::string& characteristic) {
    return "track_" + std::to_string(stable_track_id) + "." + characteristic;
}

}  // namespace vivid::control_mapping
