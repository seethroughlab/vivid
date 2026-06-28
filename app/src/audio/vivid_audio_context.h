#pragma once
#include <cstdint>

// Minimal stand-in for classic's operator_api/types.h VividAudioContext.
// vst3_host_common.h only reads these four fields (verified). Names + types
// match classic so vst3_build_process_context() compiles unchanged.
struct VividAudioContext {
    uint32_t sample_rate = 48000;             // device sample rate
    float    metronome_bpm = 120.0f;          // tempo
    uint32_t metronome_beats_per_bar = 4;     // time-signature numerator
    double   metronome_beats_elapsed = 0.0;   // total beats since start
};
