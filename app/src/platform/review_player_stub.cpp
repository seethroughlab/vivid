#ifndef __APPLE__
#include "platform/review_player.h"

#include <chrono>

// Non-macOS: no AVFoundation, so review media cannot be played. open() reports the failure through
// error() so the Review workspace shows "playback unavailable on this platform" instead of a dead
// control, and the app still links and runs. The steady-clock host time keeps the audition logic's
// time base consistent for tests.
namespace vivid {

namespace {
class NullReviewPlayer final : public ReviewPlayer {
public:
    bool open(const std::string& path) override { path_ = path; return false; }
    void close() override { path_.clear(); }
    bool is_open()  const override { return false; }
    bool is_ready() const override { return false; }
    std::string error() const override { return "review playback is unavailable on this platform"; }
    const std::string& path() const override { return path_; }
    double duration()     const override { return 0.0; }
    double current_time() const override { return 0.0; }
    bool   playing()      const override { return false; }
    bool   at_end()       const override { return false; }
    void play() override {}
    void pause() override {}
    void seek(double) override {}
    void play_at(double, double) override {}
    void  set_gain(float g) override { gain_ = g; }
    float gain() const override { return gain_; }
    void  set_muted(bool m) override { muted_ = m; }
    bool  muted() const override { return muted_; }
    bool poll_frame(ReviewFrame&) override { return false; }
    uint32_t video_width()  const override { return 0; }
    uint32_t video_height() const override { return 0; }
private:
    std::string path_;
    float gain_ = 1.f;
    bool  muted_ = false;
};
}  // namespace

std::unique_ptr<ReviewPlayer> make_platform_review_player() { return std::make_unique<NullReviewPlayer>(); }

double review_host_time_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace vivid
#endif
