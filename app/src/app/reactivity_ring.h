#pragma once
// Reliable time-based visual perception (reactive-visuals loop, Phase 1). A rolling per-frame ring
// of visual metrics PLUS the same-frame master-audio energy, populated INSIDE the frame loop (like
// the video recorder tick) at a fixed sample rate. Because the samples accumulate on the frame
// thread — independent of MCP poll cadence — a SINGLE analyze_output(mode="av") call reads a genuine
// time-series over the last window and can correlate audio energy against visual motion. This
// replaces the old cross-call MotionRing, which could only ever see one frame per call (motion 0.0).
//
// Threading: push() runs on the frame/UI thread; every reader (control handler) runs on that SAME
// thread via ControlServer::process_pending. So the ring needs NO locks. Do not touch it from the
// audio thread or any Gemini/eval background job.
#include "cli/control_handlers.h"   // nlohmann::json alias

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct Transport;

namespace vivid {

using nlohmann::json;

class ReactivityRing {
public:
    // One entry per pushed frame. Visual metrics come from analyze_rgba on a downsampled thumbnail;
    // audio comes straight off the Transport atomics read the same frame.
    struct Sample {
        double t = 0.0;              // monotonic steady_clock seconds
        // visual
        float  brightness = 0.f;
        float  contrast   = 0.f;
        float  activity   = 0.f;
        std::string hash;           // 64-bit average-hash (16 hex chars) from analyze_rgba
        float  motion     = 0.f;    // hamming(prev.hash, hash) / 64, in [0,1]; 0 for the first sample
        // audio (raw master energy this frame — the thing visuals should be reacting to)
        float  energy     = 0.f;    // transport.level
        float  band_low   = 0.f;
        float  band_mid   = 0.f;
        float  band_high  = 0.f;
        float  transient  = 0.f;
        bool   onset      = false;  // rising-edge on transient (a discrete audio event)
        // Phase 2 (multimodal judge): a small RGBA thumbnail so a montage can be assembled from the
        // exact frames the metrics were computed on. thumb.size() == thumb_w*thumb_h*4.
        uint16_t thumb_w = 0, thumb_h = 0;
        std::vector<uint8_t> thumb;
    };

    // Frame-loop throttle: true when enough time has elapsed to take another sample (~12 fps). The
    // caller gates the GPU readback on this so we sample at a bounded rate regardless of frame rate.
    bool due(double now) const { return now - last_push_ >= kPushInterval; }

    // Push one frame. `rgba` is tightly-packed RGBA8 at (w,h) — the live Output readback. Downsamples
    // to a <=64px thumbnail, runs analyze_rgba on it, reads the Transport atomics, computes motion vs
    // the previous sample, edge-detects onsets, and trims the ring to the retention window.
    void push(const uint8_t* rgba, uint32_t w, uint32_t h, const Transport& t, double now);

    int  samples() const { return static_cast<int>(s_.size()); }

    // Legacy motion summary (shape-compatible with the old MotionRing) for analyze_visual_motion /
    // summarize_visual_output: {samples, span_seconds, inter_frame_change, motion_score, is_moving,
    // brightness_range} computed over the last `window` seconds. Reliable in a single call now.
    json motion(double window, double now) const;

    // Windowed audio stats (mode="audio"): {samples, rms, transient, band_low/mid/high, onsets}.
    json audio_window(double window, double now) const;

    // The full three-lens reactivity block (mode="av"): per-axis correlations, per-band correlations,
    // and onset-aligned response rate + latency. Returns {status:"insufficient_samples", samples} when
    // there are too few samples to trust — never a fake 0.0 that reads as "dead".
    json av_metrics(double window, double now) const;

    // Phase 2 (multimodal judge): assemble up to `max_cells` of the window's retained thumbnails into a
    // single left-to-right, top-to-bottom montage RGBA image (so a vision model sees how the visual
    // evolved over the window). Evenly samples across the window. Writes `out` (out_w*out_h*4) and the
    // montage dimensions; returns false if there are no usable frames. Frame-thread only.
    bool capture_montage(int max_cells, double window, double now,
                         std::vector<uint8_t>& out, uint32_t& out_w, uint32_t& out_h) const;

    // A compact textual sparkline of the window's audio energy + onset times, injected into the judge
    // prompt so the vision model can align frames to audio it cannot hear.
    std::string energy_sparkline(double window, double now) const;

private:
    static constexpr double   kPushInterval = 0.08;   // ~12.5 fps sampling
    static constexpr double   kWindowMax    = 12.0;   // retain ~12 s of history
    static constexpr uint32_t kThumbMax     = 128;    // thumbnail longest edge — metrics are fine at any
                                                      // size; 128 gives the multimodal judge's montage
                                                      // enough detail to read the form (64 looked like dots)
    static constexpr float    kOnsetThresh  = 0.28f;  // transient rising-edge threshold for an onset

    std::deque<Sample> s_;
    double last_push_ = -1e9;

    // Samples within [now-window, now]; at least the most recent 2 if the window is very short.
    std::vector<const Sample*> window_samples(double window, double now) const;
};

}  // namespace vivid
