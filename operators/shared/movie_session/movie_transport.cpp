#include "movie_transport.h"

void MovieTransport::set_source(double duration) {
    duration_ = duration;
    source_generation_++;
    last_seek_mono_s_ = -1000.0;
    last_seek_generation_ = 0;
    seek_budget_count_ = 0;
    seek_budget_window_start_s_ = 0.0;
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
}

uint64_t MovieTransport::source_generation() const { return source_generation_; }
void MovieTransport::set_play_mode(PlayMode mode) { play_mode_ = mode; }
void MovieTransport::set_speed(float speed) { speed_ = speed; }
PlayMode MovieTransport::play_mode() const { return play_mode_; }
float MovieTransport::speed() const { return speed_; }
double MovieTransport::duration() const { return duration_; }
float MovieTransport::frame_rate() const { return frame_rate_; }

double MovieTransport::compute_self_clock_time(double decoder_time) const {
    double t = std::max(0.0, decoder_time);
    if (play_mode_ == PlayMode::HoldLast && duration_ > 0.0 && t >= duration_) {
        t = duration_;
    }
    return t;
}

double MovieTransport::compute_audio_master_time(float audio_time, double phase_offset_s) const {
    double desired_mono = static_cast<double>(audio_time) + phase_offset_s;
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
        result.type = CorrectionType::Seek;
        result.seek_target = desired_local;
        return result;
    }

    // Small drift: no correction needed — normal frame-level jitter.
    if (abs_err <= small_threshold) {
        result.type = CorrectionType::None;
        return result;
    }

    // Large drift: seek if cooldown + budget allow.
    if (abs_err > kSeekDriftSeconds) {
        const bool cooldown_ok = std::abs(desired_mono - last_seek_mono_s_) > kSeekCooldownSec;

        if (graph_time - seek_budget_window_start_s_ > 1.0) {
            seek_budget_count_ = 0;
            seek_budget_window_start_s_ = graph_time;
        }
        const bool budget_ok = seek_budget_count_ < kMaxSeeksPerSecond;

        if (cooldown_ok && budget_ok) {
            result.type = CorrectionType::Seek;
            result.seek_target = desired_local;
            return result;
        }
        // Seek needed but prevented — degrade to DropRepeat.
        result.budget_exhausted = !budget_ok;
    }

    // Medium drift: handled implicitly by queue frame selection (drop/repeat).
    result.type = CorrectionType::DropRepeat;
    return result;
}

void MovieTransport::record_seek_issued(double mono_time) {
    seek_budget_count_++;
    last_seek_mono_s_ = mono_time;
    last_seek_generation_ = source_generation_;
}

double MovieTransport::drift_seconds(double desired_local, double decoder_time) const {
    return shortest_circular_diff(desired_local, decoder_time, duration_);
}
