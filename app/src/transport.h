#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

// The master musical clock. The audio thread advances `beats` every block; the
// frame thread reads it for the UI/visuals. `level` is the audio->frame bridge
// (a measured output level the visuals can react to). Single-producer
// (audio thread) / single-consumer (frame thread) — relaxed atomics suffice.
struct Transport {
    std::atomic<bool>   playing{true};
    std::atomic<double> bpm{124.0};
    std::atomic<double> beats{0.0};   // total beats elapsed (audio thread writes)
    std::atomic<float>  level{0.0f};      // output RMS, 0..1 (audio thread writes)
    std::atomic<float>  transient{0.0f};  // onset/transient, 0..1 (audio thread writes)
    std::atomic<float>  band_low{0.0f};   // 3-band energy (one-pole crossover, audio thread)
    std::atomic<float>  band_mid{0.0f};
    std::atomic<float>  band_high{0.0f};

    void configure_capture(uint32_t sample_rate, double seconds = 30.0) {
        if (sample_rate == 0) return;
        const size_t frames = static_cast<size_t>(std::max(1.0, seconds) * static_cast<double>(sample_rate));
        std::lock_guard<std::mutex> lk(capture_mtx_);
        capture_l_.assign(frames, 0.f);
        capture_r_.assign(frames, 0.f);
        capture_sample_rate_ = sample_rate;
        capture_write_pos_ = 0;
        capture_filled_ = 0;
        capture_enabled_.store(true, std::memory_order_release);
    }

    void capture_write_interleaved(const float* stereo, uint32_t frames, uint32_t sample_rate) {
        if (!stereo || frames == 0 || !capture_enabled_.load(std::memory_order_acquire)) return;
        if (!capture_mtx_.try_lock()) return;
        if (capture_l_.empty() || capture_r_.empty()) { capture_mtx_.unlock(); return; }
        capture_sample_rate_ = sample_rate ? sample_rate : capture_sample_rate_;
        const size_t cap = capture_l_.size();
        size_t pos = capture_write_pos_;
        size_t filled = capture_filled_;
        for (uint32_t i = 0; i < frames; ++i) {
            capture_l_[pos] = stereo[i * 2 + 0];
            capture_r_[pos] = stereo[i * 2 + 1];
            pos = (pos + 1) % cap;
            filled = std::min(cap, filled + 1);
        }
        capture_write_pos_ = pos;
        capture_filled_ = filled;
        capture_mtx_.unlock();
    }

    size_t capture_snapshot(double seconds, std::vector<float>& outL, std::vector<float>& outR,
                            uint32_t* out_sample_rate) {
        outL.clear(); outR.clear();
        if (out_sample_rate) *out_sample_rate = 0;
        std::lock_guard<std::mutex> lk(capture_mtx_);
        if (capture_l_.empty() || capture_filled_ == 0 || capture_sample_rate_ == 0) return 0;
        size_t want = capture_filled_;
        if (seconds > 0.0) {
            want = std::min(capture_filled_,
                            static_cast<size_t>(seconds * static_cast<double>(capture_sample_rate_)));
        }
        if (want == 0) return 0;
        outL.resize(want);
        outR.resize(want);
        const size_t cap = capture_l_.size();
        const size_t start = (capture_write_pos_ + cap - want) % cap;
        for (size_t i = 0; i < want; ++i) {
            const size_t src = (start + i) % cap;
            outL[i] = capture_l_[src];
            outR[i] = capture_r_[src];
        }
        if (out_sample_rate) *out_sample_rate = capture_sample_rate_;
        return want;
    }

    size_t capture_capacity_frames() const {
        std::lock_guard<std::mutex> lk(capture_mtx_);
        return capture_l_.size();
    }

    void advance(uint32_t frames, double sample_rate) {
        if (!playing.load(std::memory_order_relaxed)) return;
        const double bps = bpm.load(std::memory_order_relaxed) / 60.0;
        const double next = beats.load(std::memory_order_relaxed) + frames * bps / sample_rate;
        beats.store(next, std::memory_order_relaxed);
    }

    // Transport control (UI/main thread). play(false) pauses the clock (clips stop advancing);
    // reset returns to the top (bar 1) — pair reset()+play(false) for a full stop.
    void set_playing(bool p) { playing.store(p, std::memory_order_relaxed); }
    bool toggle_playing()    { const bool n = !playing.load(std::memory_order_relaxed);
                               playing.store(n, std::memory_order_relaxed); return n; }
    void reset()             { beats.store(0.0, std::memory_order_relaxed); }
    bool is_playing() const  { return playing.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> capture_enabled_{false};
    mutable std::mutex capture_mtx_;
    std::vector<float> capture_l_;
    std::vector<float> capture_r_;
    uint32_t capture_sample_rate_ = 0;
    size_t capture_write_pos_ = 0;
    size_t capture_filled_ = 0;
};
