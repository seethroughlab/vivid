#pragma once

#include "runtime/simd/fft.h"
#include "runtime/simd/simd_config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vivid::convolution_reverb_dsp {

enum class Backend {
    Scalar,
    Accelerate,
};

enum class IrLayout {
    Mono,
    Stereo,
    TrueStereo,
};

const char* backend_name(Backend backend);
Backend preferred_backend();

struct ProcessParams {
    int ir_preset = 0;
    const char* ir_file = nullptr;
    float mix = 0.35f;
    float pre_delay_ms = 0.0f;
    float width = 1.0f;
    float ir_gain_db = 0.0f;
    float tail_seconds = 4.0f;
};

struct ProcessStats {
    Backend backend = Backend::Scalar;
    IrLayout layout = IrLayout::Stereo;
    uint32_t sample_rate = 0;
    uint32_t block_size = 0;
    uint32_t ir_frames = 0;
    uint32_t partition_count = 0;
    uint32_t zone_count = 0;
    uint32_t latency_samples = 0;
    int plan_rebuild_count = 0;
};

struct ImpulseResponse {
    std::vector<float> ll;
    std::vector<float> lr;
    std::vector<float> rl;
    std::vector<float> rr;
    uint32_t sample_rate = 0;
    IrLayout layout = IrLayout::Stereo;

    uint32_t frames() const { return static_cast<uint32_t>(ll.size()); }
};

struct IrConfig {
    int preset = 0;
    std::string file_path;
    uint32_t sample_rate = 48000;
    float tail_seconds = 4.0f;
    float pre_delay_ms = 0.0f;
    float gain_db = 0.0f;
};

ImpulseResponse build_impulse_response(const IrConfig& config);

class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void reset();
    void process(const float* input_stereo,
                 float* output_stereo,
                 uint32_t frames,
                 uint32_t sample_rate,
                 const ProcessParams& params,
                 Backend backend = preferred_backend());

    ProcessStats last_stats() const { return last_stats_; }

private:
    // Split-complex buffer pair. Matches the layout Accelerate's vDSP_fft_zip
    // and vDSP_zvma consume natively — no per-call deinterleave/reinterleave
    // overhead, and the partition MAC can run as vectorized complex
    // multiply-accumulate across fft_size elements in one vDSP call.
    struct Split {
        std::vector<float> re;
        std::vector<float> im;

        void assign(size_t n, float v = 0.0f) {
            re.assign(n, v);
            im.assign(n, v);
        }
        void zero() {
            std::fill(re.begin(), re.end(), 0.0f);
            std::fill(im.begin(), im.end(), 0.0f);
        }
        size_t size() const { return re.size(); }
    };

    struct Zone {
        uint32_t partition_size = 0;
        uint32_t fft_size = 0;
        uint32_t partition_count = 0;
        uint32_t ir_offset = 0;

        // IR partitions, 4 channels × partition_count × fft_size split-complex.
        std::vector<Split> tail_ll;
        std::vector<Split> tail_lr;
        std::vector<Split> tail_rl;
        std::vector<Split> tail_rr;

        // Frequency-delay-line input history, 2 channels × partition_count.
        std::vector<Split> input_history_l;
        std::vector<Split> input_history_r;
        uint32_t history_pos = 0;
        uint32_t input_fill = 0;

        // Time-domain per-zone input accumulator (fills to partition_size
        // before firing).
        std::vector<float> input_accum_l;
        std::vector<float> input_accum_r;

        // Per-zone FFT scratch. fft_l/r hold the current input FFT; fft_sum_l/r
        // hold the partitioned MAC accumulator before IFFT.
        Split fft_l;
        Split fft_r;
        Split fft_sum_l;
        Split fft_sum_r;
    };

    struct Plan {
        uint32_t sample_rate = 0;
        uint32_t block_size = 0;
        uint32_t early_len = 0;
        uint32_t tail_accum_size = 0;
        uint32_t max_fft_size = 0;
        uint32_t latency_samples = 0;
        uint32_t total_partition_count = 0;
        IrLayout layout = IrLayout::Stereo;
        Backend backend = Backend::Scalar;

        std::string file_path;
        int preset = -1;
        float tail_seconds = 0.0f;
        float pre_delay_ms = 0.0f;
        float ir_gain_db = 0.0f;

        std::vector<float> early_ll;
        std::vector<float> early_lr;
        std::vector<float> early_rl;
        std::vector<float> early_rr;

        std::vector<Zone> zones;

        std::vector<float> prev_l;
        std::vector<float> prev_r;
        std::vector<float> tail_accum_l;
        std::vector<float> tail_accum_r;
        std::vector<float> wet_l;
        std::vector<float> wet_r;
    };

    bool plan_matches(uint32_t frames,
                      uint32_t sample_rate,
                      const ProcessParams& params,
                      Backend backend) const;
    void rebuild_plan(uint32_t frames,
                      uint32_t sample_rate,
                      const ProcessParams& params,
                      Backend backend);

    void render_direct(const float* in_l, const float* in_r, uint32_t frames);
    void render_tail(const float* in_l, const float* in_r, uint32_t frames, Backend backend);
    void submit_zone_partition(Zone& zone, Backend backend);
    void update_previous_input(const float* in_l, const float* in_r, uint32_t frames);

    Plan plan_{};
    bool has_plan_ = false;
    int plan_rebuild_count_ = 0;
    ProcessStats last_stats_{};

    vivid::simd::FftPlanCache fft_cache_;
};

} // namespace vivid::convolution_reverb_dsp
