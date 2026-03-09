#include "operator_api/operator.h"
#include <cmath>
#include <cstdint>

// =============================================================================
// SpreadNoise — animated hash-based 1D value noise spread
// =============================================================================

struct SpreadNoise : vivid::OperatorBase {
    static constexpr const char* kName   = "SpreadNoise";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   count     {"count",     125, 1, 1024};
    vivid::Param<float> speed     {"speed",     1.0f, 0.0f, 20.0f};
    vivid::Param<float> amplitude {"amplitude", 1.0f, 0.0f, 100.0f};
    vivid::Param<float> offset    {"offset",    0.0f, -100.0f, 100.0f};
    vivid::Param<int>   seed      {"seed",      42, 0, 99999};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&count);
        out.push_back(&speed);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&seed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"values", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // 0
    }

    void process(VividProcessContext* ctx) override {
        float dt = static_cast<float>(ctx->delta_time);
        float spd = ctx->param_values[1];     // speed
        float amp = ctx->param_values[2];     // amplitude
        float off = ctx->param_values[3];     // offset
        uint32_t s = static_cast<uint32_t>(ctx->param_values[4]); // seed

        time_ += dt * spd;

        uint32_t n = static_cast<uint32_t>(ctx->param_values[0]);
        if (n < 1) n = 1;
        if (n > 1024) n = 1024;

        if (ctx->output_spreads) {
            auto& sp = ctx->output_spreads[0];
            if (sp.capacity >= n) {
                sp.length = n;
                for (uint32_t i = 0; i < n; ++i) {
                    // Hash-based value noise: smooth interpolation between hashed time steps
                    float sample_offset = static_cast<float>(i) * 0.618033988749895f; // golden ratio
                    float t = time_ + sample_offset;

                    // Integer and fractional parts for interpolation
                    float t_floor = std::floor(t);
                    float frac = t - t_floor;
                    int32_t t0 = static_cast<int32_t>(t_floor);
                    int32_t t1 = t0 + 1;

                    float v0 = hash_float(static_cast<uint32_t>(t0 + static_cast<int32_t>(i * 7919 + s)));
                    float v1 = hash_float(static_cast<uint32_t>(t1 + static_cast<int32_t>(i * 7919 + s)));

                    // Smoothstep interpolation
                    float smooth = frac * frac * (3.0f - 2.0f * frac);
                    float noise = v0 + (v1 - v0) * smooth;  // [0, 1]
                    sp.data[i] = noise * amp + off;
                }
            }
        }
    }

private:
    float time_ = 0.0f;

    // PCG-based hash → float in [0, 1]
    static float hash_float(uint32_t input) {
        uint32_t state = input * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        uint32_t result = (word >> 22u) ^ word;
        return static_cast<float>(result) / 4294967295.0f;
    }
};

VIVID_REGISTER(SpreadNoise)
