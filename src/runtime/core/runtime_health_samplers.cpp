#include "runtime/core/runtime_health_samplers.h"

#include <cmath>

namespace vivid {

void RuntimeHealthSamplers::clear() {
    for (auto& s : audio_ring_)  s = {};
    for (auto& s : visual_ring_) s = {};
    audio_head_ = 0;
    visual_head_ = 0;
    visual_ever_sampled_ = false;
}

void RuntimeHealthSamplers::sample(double time, float audio_peak,
                                   std::optional<float> gpu_brightness) {
    audio_ring_[audio_head_] = {time, std::fabs(audio_peak), true};
    audio_head_ = (audio_head_ + 1) % kCapacity;

    if (gpu_brightness.has_value()) {
        visual_ring_[visual_head_] = {time, *gpu_brightness, true};
        visual_head_ = (visual_head_ + 1) % kCapacity;
        visual_ever_sampled_ = true;
    }
}

namespace {

// Sweep `ring` for valid samples whose timestamp falls within
// (now - window, now]. Returns (count_in_window, max_value, oldest_time).
struct WindowStats {
    int count = 0;
    float max_value = 0.0f;
    double oldest_time = 0.0;
};

WindowStats scan_window(const RuntimeHealthSamplers::Slot* ring,
                        size_t /*head*/,
                        double now, double window) {
    WindowStats out;
    out.oldest_time = now;
    const double cutoff = now - window;
    for (size_t i = 0; i < RuntimeHealthSamplers::kCapacity; ++i) {
        const auto& s = ring[i];
        if (!s.valid) continue;
        if (s.time <= cutoff) continue;
        if (s.time > now)     continue;   // future timestamps ignored
        out.count++;
        if (s.value > out.max_value) out.max_value = s.value;
        if (s.time < out.oldest_time) out.oldest_time = s.time;
    }
    return out;
}

}  // namespace

bool RuntimeHealthSamplers::audio_silence_active(double now, double window) const {
    auto w = scan_window(audio_ring_, audio_head_, now, window);
    return w.count >= kMinSamples && w.max_value < kSilenceThreshold;
}

bool RuntimeHealthSamplers::visual_black_active(double now, double window) const {
    if (!visual_ever_sampled_) return false;
    auto w = scan_window(visual_ring_, visual_head_, now, window);
    return w.count >= kMinSamples && w.max_value < kBlackThreshold;
}

double RuntimeHealthSamplers::audio_window_seconds(double now, double window) const {
    auto w = scan_window(audio_ring_, audio_head_, now, window);
    return w.count > 0 ? (now - w.oldest_time) : 0.0;
}

double RuntimeHealthSamplers::visual_window_seconds(double now, double window) const {
    if (!visual_ever_sampled_) return 0.0;
    auto w = scan_window(visual_ring_, visual_head_, now, window);
    return w.count > 0 ? (now - w.oldest_time) : 0.0;
}

}  // namespace vivid
