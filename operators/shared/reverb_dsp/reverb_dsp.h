#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace vivid::reverb_dsp {

static constexpr int kCombCount = 8;
static constexpr int kAllPassCount = 4;

enum class Backend {
    Scalar,
};

const char* backend_name(Backend backend);
Backend preferred_backend();

struct ProcessParams {
    float room_size = 0.5f;
    float damping = 0.5f;
    float mix = 0.3f;
};

struct ProcessStats {
    Backend backend = Backend::Scalar;
    uint32_t sample_rate = 0;
    int initialization_count = 0;
    std::array<int, kCombCount> comb_sizes{};
    std::array<int, kAllPassCount> allpass_sizes{};
};

class Engine {
public:
    void reset();
    void process(const float* in,
                 float* out,
                 uint32_t frames,
                 uint32_t sample_rate,
                 const ProcessParams& params,
                 Backend backend = preferred_backend());

    ProcessStats last_stats() const { return last_stats_; }

private:
    struct CombFilter {
        std::vector<float> buffer;
        int size = 0;
        int idx = 0;
        float filterstore = 0.0f;

        void init(int len);
        float process(float input, float feedback, float damp1, float damp2);
    };

    struct AllPassDelay {
        std::vector<float> buffer;
        int size = 0;
        int idx = 0;

        void init(int len);
        float process(float input);
    };

    void lazy_init(uint32_t sample_rate);

    std::array<CombFilter, kCombCount> combs_{};
    std::array<AllPassDelay, kAllPassCount> allpasses_{};
    bool initialized_ = false;
    uint32_t init_rate_ = 0;
    int initialization_count_ = 0;
    ProcessStats last_stats_{};
};

} // namespace vivid::reverb_dsp
