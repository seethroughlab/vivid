#pragma once

#include "operator_api/audio_dsp.h"

#include <array>
#include <cstdint>
#include <vector>

namespace vivid::granular_dsp {

static constexpr int kMaxGrains = 32;
static constexpr float kMaxCaptureSec = 4.0f;
static constexpr int kWaveformBins = 280;

enum class Backend {
    Scalar,
};

const char* backend_name(Backend backend);
Backend preferred_backend();

struct CaptureBuffer {
    std::vector<float> buffer;
    int size = 0;
    int write = 0;

    void init(int max_samples);
    void push(float v);
    float read_linear(float abs_pos) const;
};

struct Grain {
    bool active = false;
    float start_pos = 0.0f;
    int length = 0;
    float cursor = 0.0f;
    float playback_rate = 1.0f;
    int window_type = 0;
};

struct WaveformBin {
    float min_val = 0.0f;
    float max_val = 0.0f;
};

struct GrainSnapshot {
    bool active = false;
    float bin_start = 0.0f;
    float bin_width = 0.0f;
    float phase = 0.0f;
};

struct InspectorSnapshot {
    WaveformBin bins[kWaveformBins] = {};
    GrainSnapshot grains[kMaxGrains] = {};
    int active_count = 0;
    int window_type = 0;
    float position_norm = 0.0f;
};

struct ProcessParams {
    float position = 0.8f;
    float pitch = 0.0f;
    float density = 10.0f;
    float grain_size_ms = 80.0f;
    float randomize = 0.1f;
    int window_type = 0;
    float mix = 1.0f;
};

struct ProcessStats {
    Backend backend = Backend::Scalar;
    int active_grains = 0;
};

class Engine {
public:
    Engine();

    void reset();
    void process(const float* in,
                 float* out,
                 uint32_t frames,
                 uint32_t sample_rate,
                 const ProcessParams& params,
                 Backend backend = preferred_backend());

    void fill_inspector_snapshot(InspectorSnapshot& snap,
                                 float position_norm,
                                 int window_type) const;

    ProcessStats last_stats() const { return last_stats_; }

private:
    static constexpr int kWindowTableSize = 2048;

    void lazy_init(uint32_t sample_rate);
    void init_window_tables();
    int spawn_grain(float sample_rate,
                    float position,
                    float pitch,
                    float grain_size_ms,
                    float randomize,
                    int window_type);
    int compact_active(std::array<int, kMaxGrains>& active) const;
    float window_lookup(float phase, int type) const;

    CaptureBuffer capture_;
    Grain grains_[kMaxGrains] = {};
    double sched_phase_ = 0.0;
    audio_dsp::WhiteNoise rng_;
    bool initialized_ = false;
    bool window_tables_initialized_ = false;
    uint32_t init_rate_ = 0;
    ProcessStats last_stats_{};
    std::array<std::array<float, kWindowTableSize + 1>, 4> window_tables_{};
};

} // namespace vivid::granular_dsp
