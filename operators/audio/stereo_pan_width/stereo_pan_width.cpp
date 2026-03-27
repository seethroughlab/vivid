#include "operator_api/operator.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Stereo Pan/Width — pan, width, and mid/side balance (stereo in/out)
// ---------------------------------------------------------------------------

struct StereoPanWidth : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "StereoPanWidth";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> pan       {"pan",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> width     {"width",      1.0f,  0.0f, 2.0f};
    vivid::Param<float> ms_balance{"ms_balance", 0.5f,  0.0f, 1.0f};

    StereoPanWidth() {
        vivid::semantic_tag(pan, "pan");
        vivid::semantic_shape(pan, "scalar");
        vivid::display_hint(pan, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(width, "amplitude_linear");
        vivid::semantic_shape(width, "scalar");
        vivid::display_hint(width, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(ms_balance, "probability_01");
        vivid::semantic_shape(ms_balance, "scalar");
        vivid::display_hint(ms_balance, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&pan);
        out.push_back(&width);
        out.push_back(&ms_balance);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",    VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"output",   VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"pan_cv",   VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"width_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const uint32_t frames = ctx->buffer_size;

        // Stereo planar layout: L = buf[0], R = buf[0] + buffer_size
        const float* L_in  = ctx->input_buffers[0];
        const float* R_in  = ctx->input_buffers[0] + frames;
        float* L_out = ctx->output_buffers[0];
        float* R_out = ctx->output_buffers[0] + frames;

        // CV offsets
        float pan_cv_val   = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float width_cv_val = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;

        float p = pan.value + pan_cv_val;
        if (p < -1.0f) p = -1.0f;
        if (p >  1.0f) p =  1.0f;

        float w = width.value + width_cv_val;
        if (w < 0.0f) w = 0.0f;
        if (w > 2.0f) w = 2.0f;

        float ms = ms_balance.value;

        // M/S balance gains
        float mid_gain  = 2.0f * (1.0f - ms);
        if (mid_gain < 0.0f) mid_gain = 0.0f;
        if (mid_gain > 1.0f) mid_gain = 1.0f;
        float side_gain = 2.0f * ms;
        if (side_gain < 0.0f) side_gain = 0.0f;
        if (side_gain > 1.0f) side_gain = 1.0f;

        // Constant-power pan law
        static constexpr float kPiOver4 = 3.14159265358979f * 0.25f;
        float angle = (p + 1.0f) * kPiOver4;
        float pan_L = std::cos(angle);
        float pan_R = std::sin(angle);

        for (uint32_t i = 0; i < frames; i++) {
            // M/S encode
            float mid  = (L_in[i] + R_in[i]) * 0.5f;
            float side = (L_in[i] - R_in[i]) * 0.5f;

            // M/S balance
            mid  *= mid_gain;
            side *= side_gain;

            // Width
            side *= w;

            // M/S decode
            float L = mid + side;
            float R = mid - side;

            // Pan law
            L_out[i] = L * pan_L;
            R_out[i] = R * pan_R;
        }
    }
};

VIVID_REGISTER(StereoPanWidth)
