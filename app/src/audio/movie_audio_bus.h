#pragma once
// Host-side movie-audio bus entry points called from the audio callback (the op-facing ABI is in
// operator_api/movie_audio.h). A movie channel filled by a self-decoding Video op is either drained
// by a MovieAudio graph op (routed through effects) or, if none is, mixed straight to the master here
// so a lone Video node still plays its movie's sound.
#include <cstdint>

namespace vivid {

// Reset the per-block "a MovieAudio drained this channel" flags. Call at the START of the audio
// block, before session_process runs the audio graph (where MovieAudio ops pull their channels).
void movie_audio_begin_block();

// Mix any channel NOT drained by a MovieAudio op this block into the interleaved-stereo master
// (additive), advancing that channel's clock. Call AFTER session_process. No-op when not playing, so
// paused freezes movie sound + the video frame (which is locked to the same clock) in sync.
void movie_audio_mix_master(float* out, uint32_t frames, bool playing);

}  // namespace vivid
