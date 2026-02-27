#include "operator_api/operator.h"
#include <cmath>

struct Smooth : vivid::OperatorBase {
    static constexpr const char* kName   = "Smooth";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> rise_time{"rise_time", 0.1f, 0.0f, 10.0f};
    vivid::Param<float> fall_time{"fall_time", 0.1f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rise_time);
        out.push_back(&fall_time);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float target = ctx->input_values[0];

        if (first_frame_) {
            current_ = target;
            first_frame_ = false;
        } else {
            float dt = static_cast<float>(ctx->delta_time);
            float tau = (target > current_) ? rise_time.value : fall_time.value;
            if (tau > 0.0001f) {
                float coeff = 1.0f - std::exp(-dt / tau);
                current_ += (target - current_) * coeff;
            } else {
                current_ = target;
            }
        }

        ctx->output_values[0] = current_;
    }

private:
    float current_ = 0.0f;
    bool first_frame_ = true;
};

VIVID_REGISTER(Smooth)
