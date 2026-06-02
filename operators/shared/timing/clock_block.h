#pragma once
// Shared timing-control helpers for time-driven operators.
//
// Every time-driven operator in Vivid presents the same timing surface: a
// `clock_mode` selector plus mode-dependent sub-controls. The canonical
// vocabulary lives in operator_api/metronome_sync.h:
//
//   Param<int>   clock_mode    {"clock_mode",    default, vivid::clock_mode_full_labels()};
//   Param<float> frequency     {"frequency",     hz, lo, hi};   // internal mode (Hz)
//   Param<int>   sync_division {"sync_division", div, vivid::metronome_division_labels()};
//
//   clock_mode = internal  -> own `frequency` (Hz)
//                external  -> a `beat_phase` input port
//                metronome -> the global transport + `sync_division`
//
// Operators that only make sense when clocked use vivid::clock_mode_synced_labels()
// {external, metronome}; self-clocked-or-metronome operators (Delay, Clock) use
// vivid::clock_mode_tempo_labels() {internal, metronome}.
//
// This header supplies the conditional-visibility wiring so every operator lays
// its timing controls out the same way: the mode selector is always shown, and
// the relevant sub-control appears beneath it.

#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"

namespace vivid {

// Standard "full" timing layout: `frequency` is shown only in internal mode,
// `sync_division` only in metronome mode (external shows neither — the
// beat_phase input port is the control surface). Call from the constructor.
inline void wire_clock_visibility(Param<float>& frequency,
                                  Param<int>& sync_division,
                                  Param<int>& clock_mode) {
    visible_when_eq(frequency, clock_mode, kClockModeInternal);
    visible_when_eq(sync_division, clock_mode, kClockModeMetronome);
}

// "Synced" timing layout (no internal clock): only `sync_division` is
// mode-dependent (shown in metronome mode, value 1 in the 2-option set).
inline void wire_clock_visibility_synced(Param<int>& sync_division,
                                         Param<int>& clock_mode) {
    visible_when_eq(sync_division, clock_mode, kClockModeSyncedMetronome);
}

}  // namespace vivid
