#include "movie_transport.h"

#include <cmath>

void MovieTransport::set_source(double duration) {
    duration_ = duration;
    source_generation_++;
    last_seek_mono_s_ = -1000.0;
    last_seek_generation_ = 0;
    seek_budget_count_ = 0;
    seek_budget_window_start_s_ = 0.0;
    consecutive_drop_repeat_ = 0;
    reset_sync_calibration();
}

void MovieTransport::set_frame_rate(float fps) {
    frame_rate_ = fps > 0.0f ? fps : 30.0f;
}

void MovieTransport::clear_source() {
    duration_ = 0.0;
    frame_rate_ = 30.0f;
    play_mode_ = PlayMode::Loop;
    speed_ = 1.0f;
    source_generation_++;
    last_seek_mono_s_ = -1000.0;
    last_seek_generation_ = 0;
    seek_budget_count_ = 0;
    seek_budget_window_start_s_ = 0.0;
    consecutive_drop_repeat_ = 0;
    reset_sync_calibration();
}

uint64_t MovieTransport::source_generation() const { return source_generation_; }
void MovieTransport::set_play_mode(PlayMode mode) { play_mode_ = mode; }
void MovieTransport::set_speed(float speed) { speed_ = speed; }
PlayMode MovieTransport::play_mode() const { return play_mode_; }
float MovieTransport::speed() const { return speed_; }
double MovieTransport::duration() const { return duration_; }
float MovieTransport::frame_rate() const { return frame_rate_; }
double MovieTransport::auto_phase_offset_seconds() const { return auto_phase_offset_s_; }

double MovieTransport::compute_self_clock_time(double decoder_time) const {
    double t = std::max(0.0, decoder_time);
    if (play_mode_ == PlayMode::HoldLast && duration_ > 0.0 && t >= duration_) {
        t = duration_;
    }
    return t;
}

double MovieTransport::compute_audio_master_time(float audio_time, double phase_offset_s) const {
    double desired_mono = static_cast<double>(audio_time) + phase_offset_s + auto_phase_offset_s_;
    return wrap_time(desired_mono, duration_);
}

CorrectionDecision MovieTransport::evaluate_correction(double desired_local, double decoder_time,
                                                       double desired_mono, double graph_time) {
    CorrectionDecision result;

    const double err = shortest_circular_diff(desired_local, decoder_time, duration_);
    const double abs_err = std::abs(err);
    result.drift_seconds = abs_err;

    const double frame_dur = 1.0 / std::max(1.0, static_cast<double>(frame_rate_));
    const double small_threshold = frame_dur * kSmallDriftFrames;

    const bool source_changed = (last_seek_generation_ != source_generation_);

    // Source change always seeks (initial positioning after load).
    if (source_changed) {
        consecutive_drop_repeat_ = 0;
        reset_sync_calibration();
        result.type = CorrectionType::Seek;
        result.seek_target = desired_local;
        return result;
    }

    // Small drift: no correction needed — normal frame-level jitter.
    if (abs_err <= small_threshold) {
        consecutive_drop_repeat_ = 0;
        if (std::abs(auto_phase_offset_s_) < 0.001) {
            auto_phase_offset_s_ = 0.0;
        }
        result.type = CorrectionType::None;
        return result;
    }

    // Large drift: seek if cooldown + budget allow.
    // Medium drift stays in DropRepeat mode even if it persists. Repeated
    // exact seeks against a stable offset produce visible playback stutter.
    if (abs_err > kSeekDriftSeconds) {
        const bool cooldown_ok = std::abs(desired_mono - last_seek_mono_s_) > kSeekCooldownSec;

        if (graph_time - seek_budget_window_start_s_ > 1.0) {
            seek_budget_count_ = 0;
            seek_budget_window_start_s_ = graph_time;
        }
        const bool budget_ok = seek_budget_count_ < kMaxSeeksPerSecond;

        if (cooldown_ok && budget_ok) {
            consecutive_drop_repeat_ = 0;
            reset_sync_calibration();
            result.type = CorrectionType::Seek;
            result.seek_target = desired_local;
            return result;
        }
        // Seek needed but prevented — degrade to DropRepeat.
        result.budget_exhausted = !budget_ok;
    }

    update_sync_calibration(err, small_threshold);

    // Medium drift: handled implicitly by queue frame selection (drop/repeat).
    consecutive_drop_repeat_++;
    result.type = CorrectionType::DropRepeat;
    return result;
}

void MovieTransport::record_seek_issued(double mono_time) {
    seek_budget_count_++;
    last_seek_mono_s_ = mono_time;
    last_seek_generation_ = source_generation_;
    reset_sync_calibration();
}

double MovieTransport::drift_seconds(double desired_local, double decoder_time) const {
    return shortest_circular_diff(desired_local, decoder_time, duration_);
}

void MovieTransport::reset_sync_calibration() {
    auto_phase_drift_sign_ = 0;
    auto_phase_stable_frames_ = 0;
}

void MovieTransport::update_sync_calibration(double signed_drift_seconds,
                                             double small_threshold_seconds) {
    const double abs_err = std::abs(signed_drift_seconds);
    if (abs_err <= small_threshold_seconds || abs_err > kSeekDriftSeconds) {
        reset_sync_calibration();
        return;
    }

    const int sign = (signed_drift_seconds > 0.0) ? 1 : -1;
    if (sign == auto_phase_drift_sign_) {
        auto_phase_stable_frames_++;
    } else {
        auto_phase_drift_sign_ = sign;
        auto_phase_stable_frames_ = 1;
    }

    // Only absorb drift that persists for several frame ticks; this avoids
    // learning transient decode jitter as a phase bias.
    if (auto_phase_stable_frames_ < 6) return;

    constexpr double kMaxAdjustmentPerTick = 0.010; // 10 ms
    constexpr double kAdjustmentGain = 0.10;        // remove 10% of stable bias per tick
    const double adjustment = std::clamp(-signed_drift_seconds * kAdjustmentGain,
                                         -kMaxAdjustmentPerTick,
                                         kMaxAdjustmentPerTick);
    auto_phase_offset_s_ = std::clamp(auto_phase_offset_s_ + adjustment,
                                      -kAutoPhaseMaxSeconds,
                                      kAutoPhaseMaxSeconds);
}
