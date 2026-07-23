#pragma once
// Movie-audio mix bus — lets a self-decoding Video op (a sandboxed visual dylib on the render
// thread) hand its movie's audio to the real-time engine so it can flow through the AUDIO GRAPH
// (and its effects), sample-accurately locked to the video.
//
// The bus is a small fixed set of CHANNELS (0..kMovieAudioChannels-1), each a lock-free stereo ring
// with a master clock. The Video op DECODES audio on the render thread and WRITES it to its channel
// (`audio_bus` param). A MovieAudio audio-graph source op DRAINS that channel in process_audio via
// pull() — so the movie audio enters the graph and can be wired through effects. The drain advances
// the channel's master clock; the Video op READS that clock back to present the matching frame. The
// two ops are linked by the channel index alone (both are ordinary int params — no file-path
// plumbing). All functions are host-provided (resolved at dlopen like vivid_report_gpu_error).
#include <stdint.h>

#define VIVID_MOVIE_AUDIO_CHANNELS 4

#ifdef __cplusplus
extern "C" {
#endif

// --- producer side (the Video op, render thread) ---
// Append `frames` stereo samples (planar left[]/right[]) to `channel` at monotonic media time `pts`
// (seconds). Returns frames accepted (< frames if the ring is momentarily full).
uint32_t vivid_movie_audio_write(int channel, const float* left, const float* right,
                                 uint32_t frames, double pts, float sample_rate);
// The master playback time (s) the audio thread has advanced `channel` to — the clock to present
// video against. 0 before anything has been consumed.
double   vivid_movie_audio_read_head(int channel);
// Frames currently buffered ahead in `channel` (so the producer knows whether to decode more).
uint32_t vivid_movie_audio_buffered(int channel);
// True while a MovieAudio op is actively draining `channel` (so the Video op locks to the master
// clock rather than self-clocking the video).
int      vivid_movie_audio_master_active(int channel);
// Reset `channel` to media time `t` (a file change / seek): clear buffered audio, re-base the clock.
void     vivid_movie_audio_reset(int channel, double t);

// --- consumer side (the MovieAudio audio op, audio thread) ---
// Drain `frames` from `channel` into planar left[]/right[] (zero-padded on underrun), advancing the
// master clock — but ONLY while the transport plays (see vivid_movie_audio_set_playing); when paused
// it writes silence and does not advance, so the video holds its frame in sync. Marks the channel as
// having an active drain. Returns frames actually available.
uint32_t vivid_movie_audio_pull(int channel, float* left, float* right, uint32_t frames);

// --- host wiring ---
// Publish whether the transport is playing (called by the audio callback each block); gates pull().
void     vivid_movie_audio_set_playing(int playing);

#ifdef __cplusplus
}
#endif
