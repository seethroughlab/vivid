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
    // ADR-0049: remember where the sample came from, so the Sampler editor can show its identity.
    // Optional (default no-op); the host sets it after load_pcm since load_pcm carries only PCM.
    virtual void set_source_path(const char* /*path*/) {}
};

// ADR-0049: the READ side a Sampler editor needs beyond copy_peaks — the loaded sample's geometry and
// its slice→note mapping, so the UI can draw slice markers, the key-zone strip, and the root marker
// without guessing. UI/main-thread only (mirrors copy_peaks' safety: reads the bank the UI thread owns).
struct SamplerInfo {
    unsigned long long frames = 0;   // total sample length (frames, concatenated across regions)
    uint32_t sample_rate = 0;
    int      channels = 0;           // 1 mono / 2 stereo (0 = nothing loaded)
    int      slice_count = 0;        // 1 for a melodic load; N for a sliced drum-rack
    int      base_note = 0;          // root pitch of slice 0
    int      gate = 0;               // 0 one-shot / 1 gated (the `gate` param)
};
struct SamplerSlice {
    uint32_t start = 0, end = 0;     // [start,end) frames within the concatenated buffer (0..frames)
    int      root_note = 0, lo_note = 0, hi_note = 127;   // the slice→key mapping
};
struct SamplerInspectable {
    virtual ~SamplerInspectable() = default;
    // Loaded-sample geometry + playback mode. Returns false if nothing is loaded.
    virtual bool sample_info(SamplerInfo& out) const = 0;
    // Per-slice region + note map, in order. Fills up to `cap`; returns the total slice count.
    virtual int  slices(SamplerSlice* out, int cap) const = 0;
    // The loaded sample's source path (for identity), or "" if unknown.
    virtual const char* source_path() const = 0;
};

// ADR-0049 slice 6: the EDIT side — re-cut the played window (trim in/out) and the slice→note map from
// the retained source PCM, with no re-decode, on a LIVE op (the atomic bank publish makes it safe). All
// frame counts are 0..source_frames(). UI/main-thread only. `set_trim`/`reslice` are mutually exclusive
// framings: trim = one melodic region [in,out) across the keyboard; reslice = one single-note region per
// [starts[i],ends[i]) drum-rack slice, mapped to ascending pitches from `base`.
struct SamplerEditable {
    virtual ~SamplerEditable() = default;
    virtual bool has_source() const = 0;                  // false until a sample is loaded
    virtual unsigned long long source_frames() const = 0; // length of the retained source (frames)
    virtual void set_trim(uint32_t in, uint32_t out) = 0; // out<=in => to end of sample
    virtual void reslice(const uint32_t* starts, const uint32_t* ends, int n, int base) = 0;
    // The editor draws the WHOLE retained source and overlays the play/slice markers in SOURCE space
    // (the read side's SamplerSlice positions are in the concatenated result, which loses the trim).
    // source_peaks: per-bin absolute-peak envelope (0..1) of the retained source; returns bins written.
    virtual int source_peaks(float* out, int n) const = 0;
    // edit_boundaries: the current played window / slice edges in SOURCE frames — one [start,end) per
    // region, in order. Fills up to `cap`; returns the region count (1 for a melodic trim).
    virtual int edit_boundaries(uint32_t* starts, uint32_t* ends, int cap) const = 0;
};

// Read side of the same escape hatch: the UI reads a downsampled peak envelope of the loaded
// sample to draw the node's waveform thumbnail (VividThumbnailContext carries only params, not
// PCM). The op caches the peaks at load time, so this copy is cheap and UI-thread-only.
struct SamplerPreviewable {
    virtual ~SamplerPreviewable() = default;
    // Fill out[0..n) with the loaded sample's per-bin absolute-peak envelope (0..1), across all
    // regions/slices in order. Returns bins written (0 if nothing loaded). UI/main thread.
    virtual int copy_peaks(float* out, int n) const = 0;
    // The most-recent active voice's playback position (0..1 across the whole sample) for the animated
    // waveform playhead, or -1 when nothing is sounding. Published by the audio thread (atomic).
    virtual float playhead() const = 0;
};

}  // namespace vivid
