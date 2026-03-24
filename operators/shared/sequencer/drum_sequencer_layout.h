#pragma once

#include <array>
#include <cstddef>

namespace vivid_sequencers::drum_layout {

inline constexpr std::size_t kDrumCount = 6;
inline constexpr std::size_t kStepCount = 16;

inline constexpr std::array<const char*, kDrumCount> kTriggerPrefixes = {
    "kick_", "snare_", "hat_", "oh_", "clap_", "tom_"
};

inline constexpr std::array<const char*, kDrumCount> kDrumLabels = {
    "KK", "SN", "CH", "OH", "CP", "TM"
};

inline constexpr std::array<const char*, kDrumCount> kModAPrefixes = {
    "kick_ma_", "snare_ma_", "hat_ma_", "oh_ma_", "clap_ma_", "tom_ma_"
};

inline constexpr std::array<const char*, kDrumCount> kModBPrefixes = {
    "kick_mb_", "snare_mb_", "hat_mb_", "oh_mb_", "clap_mb_", "tom_mb_"
};

inline constexpr std::array<int, kDrumCount> kNoteParamIndices = {2, 3, 4, 5, 6, 7};
inline constexpr std::array<int, kDrumCount> kTriggerParamBases = {8, 24, 40, 56, 72, 88};
inline constexpr std::array<int, kDrumCount> kModAParamBases = {104, 120, 136, 152, 168, 184};
inline constexpr std::array<int, kDrumCount> kModBParamBases = {200, 216, 232, 248, 264, 280};

inline constexpr std::size_t kStepOutputIndex = 6;
inline constexpr std::size_t kModAOutputBase = 7;
inline constexpr std::size_t kModBOutputBase = 13;
inline constexpr std::size_t kGatesSpreadOutputIndex = 19;
inline constexpr std::size_t kNotesSpreadOutputIndex = 20;
inline constexpr std::size_t kVelocitiesSpreadOutputIndex = 21;

inline constexpr int trigger_param_index(std::size_t drum, int step) {
    return kTriggerParamBases[drum] + step;
}

inline constexpr int mod_a_param_index(std::size_t drum, int step) {
    return kModAParamBases[drum] + step;
}

inline constexpr int mod_b_param_index(std::size_t drum, int step) {
    return kModBParamBases[drum] + step;
}

inline constexpr int note_param_index(std::size_t drum) {
    return kNoteParamIndices[drum];
}

inline constexpr std::size_t mod_a_output_index(std::size_t drum) {
    return kModAOutputBase + drum;
}

inline constexpr std::size_t mod_b_output_index(std::size_t drum) {
    return kModBOutputBase + drum;
}

} // namespace vivid_sequencers::drum_layout
