#pragma once

// Shared snapshot types for cross-cadence communication.
// Used by CadenceBridge (owns the double-buffered snapshots) and
// AudioExecutor (reads/writes them on the audio thread).
//
// These types were originally defined in audio_engine.h and are
// re-exported here for the new executor architecture.

#include "runtime/audio_engine.h"

namespace vivid {

// Re-export from audio_engine.h:
//   SpreadSnapshot
//   CustomPortSnapshot
//   ParamSnapshot
//   AnalysisSnapshot
//   RecordingTap
//   AutoDupGroup
//   AudioToControlMapping

// All these types are already in namespace vivid via audio_engine.h.
// This header exists so that new code (cadence_bridge, audio_executor)
// can include snapshot_types.h without depending on the full AudioEngine class.
//
// Once the old AudioEngine is removed, these type definitions will move
// here directly.

} // namespace vivid
