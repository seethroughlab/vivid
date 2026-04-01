#include "operator_api/operator.h"
#include <cmath>
#include <algorithm>

struct QuantizerAu : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "quantizer_au";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   mode     {"mode",      0, {"pitch", "range", "steps"}};
    vivid::Param<int>   scale    {"scale",     0, {"chromatic", "major", "minor", "pentatonic",
                                                    "whole_tone", "blues", "dorian", "mixolydian"}};
    vivid::Param<int>   root     {"root",      0, 0, 11};
    vivid::Param<int>   num_steps{"num_steps", 12, 2, 128};
    vivid::Param<float> min_val  {"min_val",   0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> max_val  {"max_val",   1.0f, -10000.0f, 10000.0f};

    QuantizerAu() {
        vivid::description(mode, "Quantization method: pitch snaps to scale notes, range divides a region, steps uses uniform levels");
        vivid::description(scale, "Musical scale used in pitch mode");
        vivid::description(root, "Root note of the scale as semitone offset (0 = C)");
        vivid::description(num_steps, "Number of quantization steps for range and steps modes");
        vivid::description(min_val, "Lower bound of the quantization range");
        vivid::description(max_val, "Upper bound of the quantization range");

        vivid::semantic_shape(mode, "enum");
        vivid::semantic_shape(scale, "enum");
        vivid::semantic_shape(root, "int");
        vivid::semantic_tag(num_steps, "count");
        vivid::semantic_shape(num_steps, "int");
        vivid::semantic_tag(min_val, "amplitude_linear");
        vivid::semantic_shape(min_val, "scalar");
        vivid::semantic_intent(min_val, "range_min");
        vivid::semantic_tag(max_val, "amplitude_linear");
        vivid::semantic_shape(max_val, "scalar");
        vivid::semantic_intent(max_val, "range_max");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
        out.push_back(&scale);
        out.push_back(&root);
        out.push_back(&num_steps);
        out.push_back(&min_val);
        out.push_back(&max_val);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"step",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    struct Result { float value; float step; };

    Result quantize(float input) const {
        switch (mode.int_value()) {
            case 0:  return quantize_pitch(input);
            case 1:  return quantize_range(input);
            case 2:  return quantize_steps(input);
            default: return quantize_pitch(input);
        }
    }

    void process_audio(const VividAudioContext* ctx) override {
        auto r = quantize(0.0f);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            ctx->output_buffers[0][i] = r.value;
            ctx->output_buffers[1][i] = r.step;
        }
    }

private:
    static constexpr int kChromatic[]  = {0,1,2,3,4,5,6,7,8,9,10,11};
    static constexpr int kMajor[]      = {0,2,4,5,7,9,11};
    static constexpr int kMinor[]      = {0,2,3,5,7,8,10};
    static constexpr int kPentatonic[] = {0,2,4,7,9};
    static constexpr int kWholeTone[]  = {0,2,4,6,8,10};
    static constexpr int kBlues[]      = {0,3,5,6,7,10};
    static constexpr int kDorian[]     = {0,2,3,5,7,9,10};
    static constexpr int kMixolydian[] = {0,2,4,5,7,9,10};

    struct ScaleInfo {
        const int* intervals;
        int count;
    };

    static constexpr ScaleInfo kScales[] = {
        {kChromatic,  12}, {kMajor,     7}, {kMinor,      7}, {kPentatonic, 5},
        {kWholeTone,   6}, {kBlues,     6}, {kDorian,     7}, {kMixolydian, 7},
    };

    Result quantize_pitch(float input) const {
        int scale_idx = std::clamp(scale.int_value(), 0, 7);
        const auto& sc = kScales[scale_idx];
        int root_note = std::clamp(root.int_value(), 0, 11);

        float note = std::clamp(input, 0.0f, 127.0f);
        int best_note = static_cast<int>(std::round(note));
        int best_dist = 128;
        int best_step = 0;

        int center_oct = static_cast<int>(note) / 12;
        for (int oct = std::max(0, center_oct - 1); oct <= center_oct + 1; ++oct) {
            for (int i = 0; i < sc.count; ++i) {
                int candidate = oct * 12 + root_note + sc.intervals[i];
                int dist = std::abs(candidate - static_cast<int>(std::round(note)));
                if (dist < best_dist) {
                    best_dist = dist;
                    best_note = candidate;
                    best_step = (oct * sc.count + i);
                }
            }
        }

        return {static_cast<float>(std::clamp(best_note, 0, 127)),
                static_cast<float>(best_step)};
    }

    Result quantize_range(float input) const {
        float lo = min_val.value;
        float hi = max_val.value;
        int n = std::max(num_steps.int_value(), 2);

        if (hi <= lo)
            return {lo, 0.0f};

        float clamped = std::clamp(input, lo, hi);
        float norm = (clamped - lo) / (hi - lo);
        int step = std::clamp(static_cast<int>(std::round(norm * static_cast<float>(n - 1))), 0, n - 1);

        return {lo + static_cast<float>(step) * (hi - lo) / static_cast<float>(n - 1),
                static_cast<float>(step)};
    }

    Result quantize_steps(float input) const {
        int n = std::max(num_steps.int_value(), 2);

        float clamped = std::clamp(input, 0.0f, 1.0f);
        int step = std::clamp(static_cast<int>(std::round(clamped * static_cast<float>(n - 1))), 0, n - 1);

        return {static_cast<float>(step) / static_cast<float>(n - 1),
                static_cast<float>(step)};
    }
};

VIVID_REGISTER(QuantizerAu)
