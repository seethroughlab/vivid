#pragma once
#include <cstddef>
#include <cstdint>

// The native Sampler instrument operator plays a slice of in-memory PCM per note
// (drum-rack / slicer semantics). Unlike a normal operator its state is a large decoded
// asset — PCM + slice boundaries — that doesn't fit the float/FILE param channel, so the
// host hands it over through this typed escape hatch instead. `SamplerLoadable` is a small
// polymorphic interface the SamplerOp implements; the audio-op runtime RTTI-cross-casts an
// OperatorBase to it (`audio_op_load_sampler`) to inject the data on the UI thread, before
// the instance is published to the audio thread (so the load never races the RT reader).
namespace vivid {

struct SamplerLoadable {
    virtual ~SamplerLoadable() = default;
    // Copy `n` frames of stereo PCM (planar L/R; R may be null for mono) sampled at `sr`,
    // plus `nslices` [start,end) sample regions. `base_note` is the MIDI pitch that maps to
    // slice 0 (ascending pitches select ascending slices). Called on the UI/main thread.
    virtual void load_pcm(const float* L, const float* R, size_t n, uint32_t sr,
                          const uint32_t* slice_starts, const uint32_t* slice_ends,
                          int nslices, int base_note) = 0;
};

// Read side of the same escape hatch: the UI reads a downsampled peak envelope of the loaded
// sample to draw the node's waveform thumbnail (VividThumbnailContext carries only params, not
// PCM). The op caches the peaks at load time, so this copy is cheap and UI-thread-only.
struct SamplerPreviewable {
    virtual ~SamplerPreviewable() = default;
    // Fill out[0..n) with the loaded sample's per-bin absolute-peak envelope (0..1), across all
    // regions/slices in order. Returns bins written (0 if nothing loaded). UI/main thread.
    virtual int copy_peaks(float* out, int n) const = 0;
};

}  // namespace vivid
