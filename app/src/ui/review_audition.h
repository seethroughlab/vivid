#pragma once
// ADR-0064 §3 — the synchronized A/B AUDITION over a set of review players (baseline, A, B …).
//
// The trick that makes A/B comparison honest: every source plays IN LOCKSTEP from the same host time
// and only ONE is audible. Switching A→B is a mute swap — instant, no re-seek, no re-sync glitch — so
// the creator hears the same passage in both versions at the same bar. Video is polled per source by
// the caller (each keeps its own texture), so the comparison can show both pictures while one sounds.
//
// Listening-level match (§3) is a per-source audition GAIN supplied by the caller from the render's
// evidence (e.g. the candidate's measured loudness) — it is labeled, reversible, and never touches the
// authored mix; toggling it off restores the files as rendered.
//
// Pure logic over the ReviewPlayer interface: no AVFoundation, no GPU — unit-tested with a mock
// (tests/test_review_audition.cpp). Main thread only; call tick() once per frame.
#include "platform/review_player.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace vivid::ui {

class ReviewAudition {
public:
    // How far in the future a synchronized start is armed, so every player can preroll.
    static constexpr double kStartLeadSec = 0.05;
    // Lockstep tolerance: beyond this the group is re-synced to the audible source's time.
    static constexpr double kDriftToleranceSec = 0.040;

    // Non-owning. Sources are indexed in the order added (0 is conventionally the baseline).
    void add_source(ReviewPlayer* p) { sources_.push_back(p); gains_.push_back(1.f); apply_levels(); }
    void clear_sources() { sources_.clear(); gains_.clear(); audible_ = 0; }
    std::size_t source_count() const { return sources_.size(); }
    ReviewPlayer* source(std::size_t i) const { return i < sources_.size() ? sources_[i] : nullptr; }

    // Every source loaded and ready to play?
    bool all_ready() const {
        if (sources_.empty()) return false;
        for (auto* p : sources_) if (!p || !p->is_ready()) return false;
        return true;
    }

    // --- which one you hear ---
    std::size_t audible() const { return audible_; }
    void set_audible(std::size_t i) {           // instant: a mute swap, playback untouched
        if (i >= sources_.size()) return;
        audible_ = i; apply_levels();
    }
    void next_audible() { if (!sources_.empty()) set_audible((audible_ + 1) % sources_.size()); }

    // --- transport (applies to the whole group) ---
    bool playing() const { return playing_; }
    // Position of the audible source (the reference for drift + display).
    double position() const {
        auto* p = source(audible_);
        return p ? p->current_time() : 0.0;
    }
    double duration() const {   // the shortest source (the comparison is over what all of them have)
        double d = 0.0; bool any = false;
        for (auto* p : sources_) if (p && p->is_ready()) { d = any ? std::min(d, p->duration()) : p->duration(); any = true; }
        return d;
    }

    void play_from(double sec) {
        if (!all_ready()) return;
        const double host = review_host_time_now() + kStartLeadSec;
        for (auto* p : sources_) p->play_at(clamp_pos(sec), host);
        playing_ = true;
    }
    void play() { play_from(position()); }
    void pause() { for (auto* p : sources_) if (p) p->pause(); playing_ = false; }
    void toggle_play() { if (playing_) pause(); else play(); }
    // Seek the group. While playing this re-arms a synchronized start at the new time.
    void seek(double sec) {
        if (playing_) { play_from(sec); return; }
        for (auto* p : sources_) if (p) p->seek(clamp_pos(sec));
    }

    // --- excerpt loop (bars are the caller's business; this takes seconds) ---
    void set_loop(double start_sec, double end_sec) { loop_ = true; loop_start_ = start_sec; loop_end_ = end_sec; }
    void clear_loop() { loop_ = false; }
    bool looping() const { return loop_; }
    double loop_start() const { return loop_start_; }
    double loop_end() const { return loop_end_; }

    // --- listening-level match (audition only) ---
    void set_source_gain(std::size_t i, float linear) { if (i < gains_.size()) { gains_[i] = linear; apply_levels(); } }
    float source_gain(std::size_t i) const { return i < gains_.size() ? gains_[i] : 1.f; }
    void set_level_match(bool on) { level_match_ = on; apply_levels(); }
    bool level_match() const { return level_match_; }

    // Once per frame: loop wrap + lockstep drift correction. Returns true when it re-synced the group
    // (so a UI can note it), false when nothing happened.
    bool tick() {
        if (!playing_ || sources_.empty()) return false;
        const double pos = position();
        // Loop wrap: when the audible source reaches the loop end (or any source ran off its end).
        bool wrap = false;
        if (loop_ && loop_end_ > loop_start_ && pos >= loop_end_) wrap = true;
        for (auto* p : sources_) if (p && p->at_end()) wrap = true;
        if (wrap) {
            if (loop_) { play_from(loop_start_); return true; }
            pause();                       // played through: stop at the end, do not restart silently
            return false;
        }
        // Drift: a source has fallen out of lockstep — re-arm the group at the audible source's time.
        for (auto* p : sources_)
            if (p && std::fabs(p->current_time() - pos) > kDriftToleranceSec) { play_from(pos); return true; }
        return false;
    }

private:
    double clamp_pos(double sec) const {
        const double d = duration();
        if (sec < 0.0) return 0.0;
        return d > 0.0 && sec > d ? d : sec;
    }
    void apply_levels() {
        for (std::size_t i = 0; i < sources_.size(); ++i) {
            if (!sources_[i]) continue;
            sources_[i]->set_muted(i != audible_);
            sources_[i]->set_gain(level_match_ ? gains_[i] : 1.f);
        }
    }

    std::vector<ReviewPlayer*> sources_;
    std::vector<float>         gains_;
    std::size_t audible_ = 0;
    bool   playing_ = false;
    bool   loop_ = false;
    double loop_start_ = 0.0, loop_end_ = 0.0;
    bool   level_match_ = false;
};

}  // namespace vivid::ui
