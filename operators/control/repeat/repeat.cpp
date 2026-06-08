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

    vivid::Param<int>   count   {"count",    4, 1, 1024};
    vivid::Param<int>   mode    {"mode",     0, {"copy", "linear", "random", "phase", "golden"}};
    vivid::Param<float> spread  {"spread",   0.0f, 0.0f, 1.0f};
    vivid::Param<int>   seed    {"seed",     42, 0, 9999};
    vivid::Param<float> rotation{"rotation", 0.0f, 0.0f, 1.0f};

    Repeat() {
        vivid::description(count,    "Number of output lanes");
        vivid::description(mode,     "Distribution pattern across lanes");
        vivid::description(spread,   "Range of offset applied to each lane (±spread)");
        vivid::description(seed,     "Random seed for deterministic per-lane offsets");
        vivid::description(rotation, "Cyclically rotate the lane offset pattern (0-1)");
        vivid::semantic_shape(mode, "enum");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&count);
        out.push_back(&mode);
        out.push_back(&spread);
        out.push_back(&seed);
        out.push_back(&rotation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr});
        out.push_back({.name="output", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT,
                       .transport=VIVID_PORT_TRANSPORT_SIGNAL, .multiplicity=VIVID_MULTIPLICITY_MANY});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values,
                ctx->value_outputs, ctx->output_values);
    }

private:
    static constexpr float kGoldenRatio = 0.6180339887498949f;

    static float hash_float(uint32_t& s) {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s)) / 2147483648.0f; // −1..1
    }

    // Smooth barrel-shift: rotation 0-1 maps to 0..n lane positions,
    // interpolating between neighbors so no lane ever teleports.
    static void barrel_shift(const float* offsets, uint32_t n, float rot,
                             float input, float* buf) {
        float shift = rot * static_cast<float>(n);
        float shift_floor = std::floor(shift);
        uint32_t shift_int = static_cast<uint32_t>(shift_floor) % n;
        float frac = shift - shift_floor;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t a = (i + shift_int) % n;
            uint32_t b = (i + shift_int + 1) % n;
            buf[i] = input + offsets[a] + frac * (offsets[b] - offsets[a]);
        }
    }

    void compute(float input, const float* params,
                 VividValueOutput* value_outputs, float* output_values) {
        if (!value_outputs) return;
        uint32_t n = std::clamp(static_cast<uint32_t>(params[0]), 1u, 1024u);
        int   m = static_cast<int>(params[1]);
        float s = params[2];
        int   sd = static_cast<int>(params[3]);
        float rot = params[4];

        float* buf = vivid_value_output_floats(&value_outputs[0], n);
        if (!buf) return;

        float inv_n = 1.0f / static_cast<float>(n);

        switch (m) {
        default: // 0 = copy
            for (uint32_t i = 0; i < n; ++i)
                buf[i] = input;
            break;

        case 1: { // linear — evenly spaced from (input-s) to (input+s)
            if (n == 1) {
                buf[0] = input;
            } else {
                float offsets[1024];
                float denom = 1.0f / static_cast<float>(n - 1);
                for (uint32_t i = 0; i < n; ++i) {
                    float t = static_cast<float>(i) * denom; // 0..1
                    offsets[i] = s * (2.0f * t - 1.0f);
                }
                if (rot == 0.0f) {
                    for (uint32_t i = 0; i < n; ++i)
                        buf[i] = input + offsets[i];
                } else {
                    barrel_shift(offsets, n, rot, input, buf);
                }
            }
            break;
        }
        case 2: { // random — per-lane deterministic offset within ±s
            float offsets[1024];
            uint32_t rng = static_cast<uint32_t>(sd) * 2654435761u + 1u;
            for (uint32_t i = 0; i < n; ++i)
                offsets[i] = s * hash_float(rng);
            if (rot == 0.0f) {
                for (uint32_t i = 0; i < n; ++i)
                    buf[i] = input + offsets[i];
            } else {
                barrel_shift(offsets, n, rot, input, buf);
            }
            break;
        }
        case 3: { // phase — evenly spaced 0..1 ramp, ignores input
            float offsets[1024];
            for (uint32_t i = 0; i < n; ++i)
                offsets[i] = static_cast<float>(i) * inv_n;
            if (rot == 0.0f) {
                for (uint32_t i = 0; i < n; ++i)
                    buf[i] = offsets[i];
            } else {
                barrel_shift(offsets, n, rot, 0.0f, buf);
            }
            break;
        }
        case 4: { // golden — golden-ratio offsets for maximal decorrelation
            float offsets[1024];
            for (uint32_t i = 0; i < n; ++i) {
                float offset = std::fmod(static_cast<float>(i) * kGoldenRatio, 1.0f);
                offsets[i] = s * (2.0f * offset - 1.0f);
            }
            if (rot == 0.0f) {
                for (uint32_t i = 0; i < n; ++i)
                    buf[i] = input + offsets[i];
            } else {
                barrel_shift(offsets, n, rot, input, buf);
            }
            break;
        }
        }

        vivid_value_output_commit(&value_outputs[0], n);

        if (output_values)
            output_values[0] = input;
    }
};

VIVID_DEFINE_OP(Repeat) {
    name = "Repeat";
    keywords = {"repeat", "lanes", "spread", "broadcast", "polyphony", "copy", "distribute"};
    summary = "Broadcasts a scalar to N output lanes with optional spread distribution.";
}

