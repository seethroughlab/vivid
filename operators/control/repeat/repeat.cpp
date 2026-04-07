#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

/**
 * @brief Broadcast a scalar value to N lanes with optional spread.
 *
 * Takes a single input value and produces an output lane array of the
 * specified length.  In Copy mode every element equals the input.  Other
 * modes distribute the lanes around the input value:
 *   - Linear: evenly spaced from (input − spread) to (input + spread)
 *   - Random: per-lane random offset within ±spread
 *   - Phase:  evenly spaced 0..1 ramp (ignores input), useful for cyclic quantities
 *   - Golden: offsets by successive golden-ratio multiples for maximal decorrelation
 *
 * @tip Connect a knob or LFO to the input, then wire the output alongside
 *      a polyphonic lane set to apply spread modulation to every voice.
 * @see Tile, Select, Stack
 */
struct Repeat : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "Repeat";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<int>   count {"count",  4, 1, 1024};
    vivid::Param<int>   mode  {"mode",   0, {"copy", "linear", "random", "phase", "golden"}};
    vivid::Param<float> spread{"spread", 0.0f, 0.0f, 1.0f};
    vivid::Param<int>   seed  {"seed",   42, 0, 9999};

    Repeat() {
        vivid::description(count,  "Number of output lanes");
        vivid::description(mode,   "Distribution pattern across lanes");
        vivid::description(spread, "Range of offset applied to each lane (±spread)");
        vivid::description(seed,   "Random seed for deterministic per-lane offsets");
        vivid::semantic_shape(mode, "enum");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&count);
        out.push_back(&mode);
        out.push_back(&spread);
        out.push_back(&seed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values,
                ctx->output_lanes, ctx->output_values);
    }

private:
    static constexpr float kGoldenRatio = 0.6180339887498949f;

    static float hash_float(uint32_t& s) {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f; // −1..1
    }

    void compute(float input, const float* params,
                 VividLaneOutput* out_lanes, float* output_values) {
        if (!out_lanes) return;
        auto& out = out_lanes[0];
        uint32_t n = std::clamp(static_cast<uint32_t>(params[0]), 1u, 1024u);
        int   m = static_cast<int>(params[1]);
        float s = params[2];
        int   sd = static_cast<int>(params[3]);

        float* buf = out.resize(out.handle, n);
        if (!buf) return;

        switch (m) {
        default: // 0 = copy
            for (uint32_t i = 0; i < n; ++i)
                buf[i] = input;
            break;

        case 1: { // linear — evenly spaced from (input-s) to (input+s)
            if (n == 1) {
                buf[0] = input;
            } else {
                float denom = 1.0f / static_cast<float>(n - 1);
                for (uint32_t i = 0; i < n; ++i) {
                    float t = static_cast<float>(i) * denom; // 0..1
                    buf[i] = input + s * (2.0f * t - 1.0f);
                }
            }
            break;
        }
        case 2: { // random — per-lane deterministic offset within ±s
            uint32_t rng = static_cast<uint32_t>(sd) * 2654435761u + 1u;
            for (uint32_t i = 0; i < n; ++i)
                buf[i] = input + s * hash_float(rng);
            break;
        }
        case 3: { // phase — evenly spaced 0..1 ramp, ignores input
            float inv_n = 1.0f / static_cast<float>(n);
            for (uint32_t i = 0; i < n; ++i)
                buf[i] = static_cast<float>(i) * inv_n;
            break;
        }
        case 4: { // golden — golden-ratio offsets for maximal decorrelation
            for (uint32_t i = 0; i < n; ++i) {
                float offset = std::fmod(static_cast<float>(i) * kGoldenRatio, 1.0f);
                buf[i] = input + s * (2.0f * offset - 1.0f);
            }
            break;
        }
        }

        out.commit(out.handle, n);

        if (output_values)
            output_values[0] = input;
    }
};

VIVID_REGISTER(Repeat)
