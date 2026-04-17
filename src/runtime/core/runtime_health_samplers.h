#pragma once

#include <cstddef>
#include <optional>

namespace vivid {

// ---------------------------------------------------------------------------
// RuntimeHealthSamplers — fixed-capacity ring buffers + window-aggregating
// reducers used by runtime_health to detect sustained silence / black output.
//
// Owned by RuntimeCore. Sampled once per frame via
// `RuntimeCore::sample_runtime_health(time)`. Read by `runtime_health::collect()`
// to populate `audio.silence_active` / `gpu.black_active`.
//
// The `time` axis is caller-domain: live runtime passes wall-clock-ish time;
// tests pass `frame * dt` sim time. The sampler stores whatever the caller
// hands it and compares to the same caller's `now` in window queries —
// internally consistent within each domain.
//
// Visual sampling is optional: when no GPU sink exists (control-only graph)
// or `frame_analysis` isn't populated, callers pass `std::nullopt` and the
// sampler keeps `visual_ever_sampled_ == false`. Black detection then stays
// false regardless of window queries — distinguishes "no GPU" from "all-black".
// ---------------------------------------------------------------------------

class RuntimeHealthSamplers {
public:
    // Internal POD made public so the anonymous-namespace scan_window helper
    // in the .cpp can deref it. Not part of any external contract.
    struct Slot { double time = 0.0; float value = 0.0f; bool valid = false; };


    // ~6 seconds of headroom at 60 Hz. Each ring slot is 24 bytes
    // (double + float + bool + padding); two rings = ~17 KB total.
    static constexpr size_t kCapacity = 360;

    // Detection thresholds. Audio matches the existing test_demo_graphs
    // silence check; visual is a small fraction of full-bright.
    static constexpr float kSilenceThreshold = 0.001f;
    static constexpr float kBlackThreshold   = 4.0f;     // 0..255 brightness scale

    // Minimum number of samples in the window before either active flag
    // can fire. ~0.5s at 60 Hz — guards against false positives during
    // an envelope's attack ramp at graph startup.
    static constexpr int kMinSamples = 30;

    static constexpr double kDefaultWindowSeconds = 5.0;

    void clear();

    // Record one frame's worth of analysis output. `gpu_brightness` is
    // nullopt when no GPU sink is available or analysis hasn't published yet.
    void sample(double time, float audio_peak,
                std::optional<float> gpu_brightness);

    // Window queries. Both return true iff:
    //   - at least kMinSamples valid samples exist within (now-window, now], AND
    //   - every such sample is below the corresponding threshold.
    // Visual queries also require visual_ever_sampled_ == true.
    bool audio_silence_active(double now,
                              double window = kDefaultWindowSeconds) const;
    bool visual_black_active(double now,
                             double window = kDefaultWindowSeconds) const;

    // Span (in seconds) of valid in-window samples — oldest_in_window → now.
    // 0 when no samples are in window. Surfaced in the snapshot so consumers
    // can see how much evidence backs the active flag.
    double audio_window_seconds(double now,
                                double window = kDefaultWindowSeconds) const;
    double visual_window_seconds(double now,
                                 double window = kDefaultWindowSeconds) const;

private:
    Slot   audio_ring_[kCapacity]{};
    Slot   visual_ring_[kCapacity]{};
    size_t audio_head_ = 0;   // next write index; modular over kCapacity
    size_t visual_head_ = 0;
    bool   visual_ever_sampled_ = false;
};

}  // namespace vivid
