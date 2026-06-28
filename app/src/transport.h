#pragma once
#include <atomic>

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

    void advance(uint32_t frames, double sample_rate) {
        if (!playing.load(std::memory_order_relaxed)) return;
        const double bps = bpm.load(std::memory_order_relaxed) / 60.0;
        const double next = beats.load(std::memory_order_relaxed) + frames * bps / sample_rate;
        beats.store(next, std::memory_order_relaxed);
    }
};
