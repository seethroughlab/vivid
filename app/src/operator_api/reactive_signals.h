#pragma once
// ADR-0053: the canonical audio-reactivity SIGNAL grammar — the ordered signal names the Reactive SOURCE
// ops emit. This order IS the value-lane ordinal a control edge references, AND it matches the
// audio→visual bridge suffix grammar (app/src/app/bridge_source.h), so a legacy "master.low" /
// "transport.beat" / "track_N.gate" mapping migrates to lane == the suffix's index here. ONE source of
// truth for reactive_master.cpp, reactive_track.cpp, the migration parser (node_graph.cpp), and the
// contract test that keeps this in lockstep with the bridge grammar (test_reactive_signals).
//
// Master lanes 0..4 are the master-bus scalars; lanes 5..8 are the transport signals (bridge splits
// these across master.* and transport.* ids, but one ReactiveMaster op emits all nine).
#include "operator_api/reactive_bus.h"   // VIVID_REACTIVE_MASTER_SIGNALS / VIVID_REACTIVE_TRACK_SIGNALS

namespace vivid::reactive {

inline constexpr const char* kMasterSignals[VIVID_REACTIVE_MASTER_SIGNALS] = {
    "level", "transient", "low", "mid", "high",        // master bus (bridge master.*)
    "beat", "bar_phase", "downbeat", "beat_pulse" };   // transport (bridge transport.*)
inline constexpr int kMasterScalarCount = 5;           // lanes [0,5) are master.*, [5,9) are transport.*

inline constexpr const char* kTrackSignals[VIVID_REACTIVE_TRACK_SIGNALS] = {
    "level", "transient", "low", "mid", "high", "note", "velocity", "gate" };

}  // namespace vivid::reactive
