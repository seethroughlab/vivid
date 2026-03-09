#include "operator_api/operator.h"
#include <cmath>
#include <algorithm>

struct Random : vivid::OperatorBase {
    static constexpr const char* kName   = "Random";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> min_val     {"min",          0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> max_val     {"max",          1.0f, -10000.0f, 10000.0f};
    vivid::Param<int>   distribution{"distribution", 0, {"uniform", "gaussian"}};
    vivid::Param<int>   seed        {"seed",         12345, 1, 99999};
    vivid::Param<bool>  free_run    {"free_run",     true};

    Random() {
        vivid::semantic_tag(min_val, "amplitude_linear");
        vivid::semantic_shape(min_val, "scalar");
        vivid::semantic_intent(min_val, "range_min");

        vivid::semantic_tag(max_val, "amplitude_linear");
        vivid::semantic_shape(max_val, "scalar");
        vivid::semantic_intent(max_val, "range_max");

        vivid::semantic_tag(seed, "seed");
        vivid::semantic_shape(seed, "int");

        vivid::semantic_tag(free_run, "enabled");
        vivid::semantic_shape(free_run, "bool");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&min_val);
        out.push_back(&max_val);
        out.push_back(&distribution);
        out.push_back(&seed);
        out.push_back(&free_run);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(VividProcessContext* ctx) override {
        if (!seeded_) {
            rng_state_ = static_cast<uint32_t>(seed.int_value());
            if (rng_state_ == 0) rng_state_ = 1;
            current_value_ = generate();
            seeded_ = true;
        }

        bool trig = ctx->input_values[0] > 0.5f;
        bool rising = trig && !prev_trigger_;
        prev_trigger_ = trig;

        if (free_run.bool_value() || rising)
            current_value_ = generate();

        ctx->output_values[0] = current_value_;
    }

private:
    uint32_t rng_state_ = 12345;
    float current_value_ = 0.0f;
    bool prev_trigger_ = false;
    bool seeded_ = false;

    uint32_t rng_next() {
        rng_state_ ^= rng_state_ << 13;
        rng_state_ ^= rng_state_ >> 17;
        rng_state_ ^= rng_state_ << 5;
        return rng_state_;
    }

    float generate() {
        float lo = min_val.value;
        float hi = max_val.value;

        if (distribution.int_value() == 0) {
            // Uniform
            float u = static_cast<float>(rng_next()) / 4294967295.0f;
            return lo + u * (hi - lo);
        } else {
            // Gaussian via Box-Muller
            float u1 = (static_cast<float>(rng_next()) + 1.0f) / 4294967296.0f;
            float u2 = static_cast<float>(rng_next()) / 4294967295.0f;
            float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
            float mid = (lo + hi) * 0.5f;
            float spread = (hi - lo) / 6.0f;
            return std::clamp(mid + z * spread, lo, hi);
        }
    }
};

VIVID_REGISTER(Random)
