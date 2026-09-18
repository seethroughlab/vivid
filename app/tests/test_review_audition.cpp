// Headless tests for the ADR-0064 synchronized A/B audition (ui/review_audition.h) over a MOCK
// ReviewPlayer — no AVFoundation, no GPU. The audition's contract: all sources start from ONE shared
// host time (lockstep), exactly one is audible (A/B is a mute swap, never a re-seek), seeking while
// playing re-arms the group, the excerpt loop wraps the group together, drift beyond tolerance
// re-syncs to the audible source, and the listening-level match is audition-only gain that toggling
// off fully reverts.
#include "ui/review_audition.h"
#include "test_helpers.h"

#include <chrono>
#include <string>
#include <vector>

// The test supplies the platform seam the audition calls (the real one is AVFoundation's host clock
// in review_player.mm, which this headless test must not link).
double vivid::review_host_time_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

using vivid::ReviewPlayer;
using vivid::ReviewFrame;
using vivid::ui::ReviewAudition;

struct MockPlayer final : ReviewPlayer {
    std::string path_, err_;
    bool open_ = false, ready_ = true, playing_ = false, ended_ = false, muted_ = false;
    float gain_ = 1.f;
    double dur_ = 30.0, t_ = 0.0;
    // Recorded calls.
    int plays = 0, pauses = 0, seeks = 0, play_ats = 0;
    double last_seek = -1, last_play_at_media = -1, last_play_at_host = -1;

    bool open(const std::string& p) override { path_ = p; open_ = true; return true; }
    void close() override { open_ = false; }
    bool is_open()  const override { return open_; }
    bool is_ready() const override { return ready_; }
    std::string error() const override { return err_; }
    const std::string& path() const override { return path_; }
    double duration()     const override { return dur_; }
    double current_time() const override { return t_; }
    bool   playing()      const override { return playing_; }
    bool   at_end()       const override { return ended_; }
    void play() override { ++plays; playing_ = true; }
    void pause() override { ++pauses; playing_ = false; }
    void seek(double s) override { ++seeks; last_seek = s; t_ = s; ended_ = false; }
    void play_at(double m, double h) override { ++play_ats; last_play_at_media = m; last_play_at_host = h; t_ = m; playing_ = true; ended_ = false; }
    void  set_gain(float g) override { gain_ = g; }
    float gain() const override { return gain_; }
    void  set_muted(bool m) override { muted_ = m; }
    bool  muted() const override { return muted_; }
    bool poll_frame(ReviewFrame&) override { return false; }
    uint32_t video_width()  const override { return 1920; }
    uint32_t video_height() const override { return 1080; }
};

static void test_lockstep_start_and_mute_swap() {
    MockPlayer base, a, b;
    ReviewAudition au;
    au.add_source(&base); au.add_source(&a); au.add_source(&b);
    CHECK(au.source_count() == 3);
    CHECK(au.all_ready());
    // Default: source 0 audible, others muted; nobody plays yet.
    CHECK(au.audible() == 0 && !base.muted_ && a.muted_ && b.muted_);
    CHECK(!au.playing());

    au.play_from(12.0);
    CHECK(au.playing());
    CHECK(base.play_ats == 1 && a.play_ats == 1 && b.play_ats == 1);
    CHECK(base.last_play_at_media == 12.0 && a.last_play_at_media == 12.0 && b.last_play_at_media == 12.0);
    // ONE shared host time, in the future.
    CHECK(base.last_play_at_host == a.last_play_at_host && a.last_play_at_host == b.last_play_at_host);
    CHECK(base.last_play_at_host > vivid::review_host_time_now() - 1.0);
    // A/B is a mute swap: no transport calls at all.
    au.set_audible(1);
    CHECK(au.audible() == 1 && base.muted_ && !a.muted_ && b.muted_);
    CHECK(base.play_ats == 1 && a.play_ats == 1 && b.play_ats == 1 && a.seeks == 0 && a.plays == 0);
    au.next_audible(); CHECK(au.audible() == 2 && !b.muted_);
    au.next_audible(); CHECK(au.audible() == 0 && !base.muted_);
    au.set_audible(99); CHECK(au.audible() == 0);   // out of range ignored

    au.pause();
    CHECK(!au.playing() && base.pauses == 1 && a.pauses == 1 && b.pauses == 1);
}

static void test_not_ready_refuses_to_start() {
    MockPlayer a, b; b.ready_ = false;
    ReviewAudition au; au.add_source(&a); au.add_source(&b);
    CHECK(!au.all_ready());
    au.play_from(0.0);
    CHECK(!au.playing() && a.play_ats == 0 && b.play_ats == 0);
    ReviewAudition empty; CHECK(!empty.all_ready()); empty.play(); CHECK(!empty.playing());
}

static void test_seek_paused_vs_playing_and_clamp() {
    MockPlayer a, b; a.dur_ = 20.0; b.dur_ = 30.0;
    ReviewAudition au; au.add_source(&a); au.add_source(&b);
    CHECK(au.duration() == 20.0);              // the shortest source bounds the comparison
    au.seek(5.0);                              // paused: plain seeks, no start
    CHECK(a.seeks == 1 && b.seeks == 1 && a.last_seek == 5.0 && !au.playing() && a.play_ats == 0);
    au.seek(-3.0); CHECK(a.last_seek == 0.0);  // clamped low
    au.seek(99.0); CHECK(a.last_seek == 20.0); // clamped to the shortest duration
    au.play();                                 // resumes from the audible source's position
    CHECK(au.playing() && a.play_ats == 1 && a.last_play_at_media == 20.0);
    au.seek(7.0);                              // playing: re-arms a synchronized start, not a bare seek
    CHECK(a.play_ats == 2 && b.play_ats == 2 && a.last_play_at_media == 7.0 && b.last_play_at_media == 7.0);
    CHECK(a.seeks == 3);                       // no extra seek() from the playing path
    au.toggle_play(); CHECK(!au.playing()); au.toggle_play(); CHECK(au.playing());
}

static void test_loop_wraps_group_and_end_stops() {
    MockPlayer a, b;
    ReviewAudition au; au.add_source(&a); au.add_source(&b);
    au.set_loop(8.0, 16.0);
    CHECK(au.looping() && au.loop_start() == 8.0 && au.loop_end() == 16.0);
    au.play_from(8.0);
    a.t_ = 12.0; b.t_ = 12.0;
    CHECK(!au.tick());                         // inside the loop: nothing
    a.t_ = 16.0; b.t_ = 16.0;
    CHECK(au.tick());                          // at the end: wrapped the WHOLE group
    CHECK(a.last_play_at_media == 8.0 && b.last_play_at_media == 8.0 && a.play_ats == 2 && b.play_ats == 2);
    // A source running off its end also wraps while looping …
    a.ended_ = true; a.t_ = 8.0; b.t_ = 8.0;
    CHECK(au.tick() && a.play_ats == 3);
    // … but with no loop, playing through stops the group rather than restarting silently.
    au.clear_loop(); CHECK(!au.looping());
    b.ended_ = true;
    CHECK(!au.tick());
    CHECK(!au.playing() && a.pauses == 1 && b.pauses == 1);
    CHECK(!au.tick());                         // paused: tick is inert
}

static void test_drift_resyncs_to_audible() {
    MockPlayer a, b;
    ReviewAudition au; au.add_source(&a); au.add_source(&b);
    au.play_from(0.0);
    au.set_audible(1);                         // B is the reference
    a.t_ = 10.00; b.t_ = 10.03;                // 30 ms: within tolerance
    CHECK(!au.tick());
    a.t_ = 10.00; b.t_ = 10.10;                // 100 ms: out of lockstep
    CHECK(au.tick());
    CHECK(a.last_play_at_media == 10.10 && b.last_play_at_media == 10.10);   // re-armed at B's time
    CHECK(a.last_play_at_host == b.last_play_at_host);
}

static void test_level_match_is_audition_only_and_reversible() {
    MockPlayer a, b;
    ReviewAudition au; au.add_source(&a); au.add_source(&b);
    au.set_source_gain(0, 0.5f); au.set_source_gain(1, 1.4f);
    CHECK(au.source_gain(0) == 0.5f && au.source_gain(1) == 1.4f);
    CHECK(!au.level_match() && a.gain_ == 1.f && b.gain_ == 1.f);   // stored, not applied until on
    au.set_level_match(true);
    CHECK(au.level_match() && a.gain_ == 0.5f && b.gain_ == 1.4f);
    au.set_source_gain(1, 1.2f); CHECK(b.gain_ == 1.2f);            // live update while on
    au.set_level_match(false);
    CHECK(a.gain_ == 1.f && b.gain_ == 1.f);                        // fully reverted: files as rendered
    au.set_source_gain(7, 2.f);                                     // out of range ignored
    // Mute state is independent of gain (a muted source keeps its match gain for when it becomes audible).
    au.set_level_match(true); au.set_audible(1);
    CHECK(a.muted_ && a.gain_ == 0.5f && !b.muted_ && b.gain_ == 1.2f);
}

int main() {
    test_lockstep_start_and_mute_swap();
    test_not_ready_refuses_to_start();
    test_seek_paused_vs_playing_and_clamp();
    test_loop_wraps_group_and_end_stops();
    test_drift_resyncs_to_audible();
    test_level_match_is_audition_only_and_reversible();
    return vivid::test::summary("test_review_audition");
}
