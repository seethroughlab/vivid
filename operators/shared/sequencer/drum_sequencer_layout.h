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

// Patterns C and D (added on top of the original A/B bank). Triggers only —
// velocity / mod B / probability / roll remain shared across all four
// patterns. New bases live AFTER the roll block so every pre-existing index
// stays valid for older saves. Defaults are 0.0f.
inline constexpr std::size_t   kPatternCount = 4;
inline constexpr std::array<const char*, kDrumCount> kTrigCPrefixes = {
    "kick_c_", "snare_c_", "hat_c_", "oh_c_", "clap_c_", "tom_c_"
};
inline constexpr std::array<const char*, kDrumCount> kTrigDPrefixes = {
    "kick_d_", "snare_d_", "hat_d_", "oh_d_", "clap_d_", "tom_d_"
};
inline constexpr std::array<int, kDrumCount> kTrigCParamBases  =
    {588, 604, 620, 636, 652, 668};
inline constexpr std::array<int, kDrumCount> kTrigDParamBases  =
    {684, 700, 716, 732, 748, 764};

// Song mode: when on, the playing pattern auto-advances 0→1→2→3→0 every
// time the pattern wraps. Manual mode (default) keeps `active_pattern`
// the playing pattern. Output port `current_pattern` is at index 1 (right
// after `step` at index 0). Appended after trig_d_ to preserve every
// existing index for older saves.
inline constexpr int kSongModeIndex          = 780;
// bars_per_pattern: how many pattern wraps each song-mode section holds
// for before advancing. 1 = advance every bar (default), 4 = standard
// "4-bar section" song. Range 1..8.
inline constexpr int kBarsPerPatternIndex    = 781;
inline constexpr std::size_t kCurrentPatternOutputIndex = 1;

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

inline constexpr int trig_c_param_index(std::size_t drum, int step) {
    return kTrigCParamBases[drum] + step;
}

inline constexpr int trig_d_param_index(std::size_t drum, int step) {
    return kTrigDParamBases[drum] + step;
}

// Unified trigger lookup keyed by pattern index 0..3. Used by compute() and
// the editor so neither has to know which pattern lives in which storage.
inline constexpr int trigger_param_index_for_pattern(int pattern,
                                                     std::size_t drum,
                                                     int step) {
    switch (pattern) {
        case 0:  return trigger_param_index(drum, step);
        case 1:  return trig_b_param_index(drum, step);
        case 2:  return trig_c_param_index(drum, step);
        default: return trig_d_param_index(drum, step);
    }
}

inline constexpr int prob_param_index(std::size_t drum, int step) {
    return kProbParamBases[drum] + step;
}

inline constexpr int roll_param_index(std::size_t drum, int step) {
    return kRollParamBases[drum] + step;
}

} // namespace vivid_sequencers::drum_layout
