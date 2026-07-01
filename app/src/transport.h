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
    std::atomic<float>  band_low{0.0f};   // 3-band energy (one-pole crossover, audio thread)
    std::atomic<float>  band_mid{0.0f};
    std::atomic<float>  band_high{0.0f};

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
};
