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

inline constexpr std::array<int, kDrumCount> kNoteParamIndices = {4, 5, 6, 7, 8, 9};
inline constexpr std::array<int, kDrumCount> kTriggerParamBases = {10, 26, 42, 58, 74, 90};
inline constexpr std::array<int, kDrumCount> kModAParamBases = {106, 122, 138, 154, 170, 186};
inline constexpr std::array<int, kDrumCount> kModBParamBases = {202, 218, 234, 250, 266, 282};

// bar_sync lives at 298 (pushed last in the legacy-compatible block).

// --- Follow-up: richer sequencer workflows (A/B pattern + probability + roll) ---
// These bases live AFTER bar_sync so old saved graphs retain stable indices
// for every pre-existing param. Probability defaults to 1.0 (always fires);
// roll defaults to 1 (single hit, no ratchet). Pattern-B triggers default to 0.
inline constexpr int           kActivePatternIndex = 299;
inline constexpr std::array<const char*, kDrumCount> kTrigBPrefixes = {
    "kick_b_", "snare_b_", "hat_b_", "oh_b_", "clap_b_", "tom_b_"
};
inline constexpr std::array<const char*, kDrumCount> kProbPrefixes = {
    "kick_prob_", "snare_prob_", "hat_prob_", "oh_prob_",
    "clap_prob_", "tom_prob_"
};
inline constexpr std::array<const char*, kDrumCount> kRollPrefixes = {
    "kick_roll_", "snare_roll_", "hat_roll_", "oh_roll_",
    "clap_roll_", "tom_roll_"
};
inline constexpr std::array<int, kDrumCount> kTrigBParamBases  =
    {300, 316, 332, 348, 364, 380};
inline constexpr std::array<int, kDrumCount> kProbParamBases   =
    {396, 412, 428, 444, 460, 476};
inline constexpr std::array<int, kDrumCount> kRollParamBases   =
    {492, 508, 524, 540, 556, 572};

// "step" scalar is the sole non-custom output; runtime writes
// current_step → output_values[0].  The older per-drum mod-output /
// gates-/notes-/velocities-spread layout was removed when DrumSequencer
// narrowed to a single scalar + custom midi_out port; don't re-add those
// constants without re-adding the matching ports.
inline constexpr std::size_t kStepOutputIndex = 0;

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

inline constexpr int trig_b_param_index(std::size_t drum, int step) {
    return kTrigBParamBases[drum] + step;
}

inline constexpr int prob_param_index(std::size_t drum, int step) {
    return kProbParamBases[drum] + step;
}

inline constexpr int roll_param_index(std::size_t drum, int step) {
    return kRollParamBases[drum] + step;
}

} // namespace vivid_sequencers::drum_layout
