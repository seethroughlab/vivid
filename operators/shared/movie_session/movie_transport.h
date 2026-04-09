#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

enum class PlayMode { Loop, Once, HoldLast };

// Time utilities — shared across both movie operators and tests.
// Extracted from MovieFileIn (previously private statics).

inline double wrap_time(double t, double duration) {
    if (duration <= 0.0) return std::max(0.0, t);
    double out = std::fmod(t, duration);
    if (out < 0.0) out += duration;
    return out;
}

inline double shortest_circular_diff(double target, double current, double duration) {
    if (duration <= 0.0) return target - current;
    double d = target - current;
    const double half = duration * 0.5;
    while (d > half) d -= duration;
    while (d < -half) d += duration;
    return d;
}

// Correction tier for AV sync drift.
enum class CorrectionType {
    None,       // Drift within normal jitter window
    DropRepeat, // Medium drift, handled implicitly by queue frame selection
    Seek,       // Large drift, requires explicit repositioning
};

// Result of MovieTransport::evaluate_correction().
struct CorrectionDecision {
    CorrectionType type = CorrectionType::None;
    double seek_target = 0.0;       // valid only when type == Seek
    double drift_seconds = 0.0;     // absolute measured drift for telemetry
    bool budget_exhausted = false;   // true if Seek was downgraded due to budget/cooldown
};

// Transport-time abstraction for movie playback.
//
// Owns transport time computation, seek policy (thresholds, cooldown, budget),
// source generation tracking, and play-mode semantics.  The caller (MovieFileIn)
// retains ownership of the VideoDecoder and issues seeks based on the returned
// SeekDecision.
class MovieTransport {
public:
    static constexpr double kSeekCooldownSec = 0.150;
    static constexpr uint64_t kMaxSeeksPerSecond = 4;
    static constexpr double kSmallDriftFrames = 2.0;     // drift below this = None (normal jitter)
    static constexpr double kSeekDriftSeconds = 0.200;    // drift above 200ms = Seek

    // --- Source lifecycle ---

    // Called when a new source is loaded.  Increments source_generation_
    // and resets seek bookkeeping.  Both operators may call this independently
    // for the same source — the double increment is harmless.
    void set_source(double duration);

    // Called by the video side only (audio side doesn't know video frame rate).
    void set_frame_rate(float fps);

    // Called when the source is being replaced.  Increments source_generation_
    // and resets all timing state.
    void clear_source();

    uint64_t source_generation() const;

    // --- Transport parameters ---

    void set_play_mode(PlayMode mode);
    void set_speed(float speed);
    PlayMode play_mode() const;
    float speed() const;
    double duration() const;
    float frame_rate() const;

    // --- Time computation ---

    // Self-clock mode: returns decoder_time, clamped for HoldLast.
    double compute_self_clock_time(double decoder_time) const;

    // Audio-master mode: returns wrap_time(audio_time + phase_offset, duration).
    double compute_audio_master_time(float audio_time, double phase_offset_s) const;

    // --- Correction policy ---

    // Evaluate the appropriate correction for AV sync drift.
    // Returns a tiered decision: None (small), DropRepeat (medium), or Seek (large).
    CorrectionDecision evaluate_correction(double desired_local, double decoder_time,
                                           double desired_mono, double graph_time);

    // Record that a seek was successfully issued.  Only call this if
    // decoder_->seek() returned true.
    void record_seek_issued(double mono_time);

    // --- Drift measurement ---

    double drift_seconds(double desired_local, double decoder_time) const;

private:
    double duration_ = 0.0;
    float frame_rate_ = 30.0f;
    PlayMode play_mode_ = PlayMode::Loop;
    float speed_ = 1.0f;
    uint64_t source_generation_ = 0;

    double last_seek_mono_s_ = -1000.0;
    uint64_t last_seek_generation_ = 0;
    uint64_t seek_budget_count_ = 0;
    double seek_budget_window_start_s_ = 0.0;
};
