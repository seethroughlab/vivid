#pragma once
#include <cstdint>

// Minimal façade over the extracted VST3 host (vst3_host_common.h lives in an
// anonymous namespace, so all the real work happens in vst3_host.cpp and main
// only talks to these C-style entry points).
namespace vivid_poc {

struct Vst3Player;  // opaque

// Scan the standard macOS VST3 dirs, load the first *instrument* (one with an
// event input bus), and setProcessing(true). Returns nullptr if none found.
Vst3Player* vst3_player_create(uint32_t sample_rate);

const char* vst3_player_name(Vst3Player*);

// Audio thread: render `frames` of interleaved stereo f32 into `out`, driving a
// simple arpeggio off the transport (one note per beat). Returns false (and
// writes nothing) if there is no plugin to render.
bool vst3_player_process(Vst3Player*, float* out, uint32_t frames,
                         uint32_t sample_rate, double bpm, double beats,
                         uint32_t beats_per_bar);

void vst3_player_destroy(Vst3Player*);

}  // namespace vivid_poc
