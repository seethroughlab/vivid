#include "operator_api/operator.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Feed-forward compressor with envelope follower and optional sidechain input
// ---------------------------------------------------------------------------

static constexpr float kFloorDB = -96.0f;

struct Compressor : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Compressor";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> threshold{"threshold", -20.0f, -60.0f,    0.0f};
    vivid::Param<float> ratio    {"ratio",       4.0f,   1.0f,   20.0f};
    vivid::Param<float> attack   {"attack",     10.0f,   0.1f,  100.0f};
    vivid::Param<float> release  {"release",   100.0f,  10.0f, 1000.0f};
    vivid::Param<float> knee     {"knee",        6.0f,   0.0f,   30.0f};
    vivid::Param<float> makeup   {"makeup",      0.0f,   0.0f,   40.0f};

    float env_ = 0.0f;

    Compressor() {
        vivid::semantic_tag(threshold, "gain_db");
        vivid::semantic_shape(threshold, "scalar");
        vivid::semantic_unit(threshold, "dB");
        vivid::display_hint(threshold, VIVID_DISPLAY_KNOB);

        vivid::semantic_shape(ratio, "scalar");
        vivid::display_hint(ratio, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(attack, "time_milliseconds");
        vivid::semantic_shape(attack, "scalar");
        vivid::semantic_unit(attack, "ms");
        vivid::display_hint(attack, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(release, "time_milliseconds");
        vivid::semantic_shape(release, "scalar");
        vivid::semantic_unit(release, "ms");
        vivid::display_hint(release, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(knee, "gain_db");
        vivid::semantic_shape(knee, "scalar");
        vivid::semantic_unit(knee, "dB");
        vivid::display_hint(knee, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(makeup, "gain_db");
        vivid::semantic_shape(makeup, "scalar");
        vivid::semantic_unit(makeup, "dB");
        vivid::display_hint(makeup, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&threshold);
        out.push_back(&ratio);
        out.push_back(&attack);
        out.push_back(&release);
        out.push_back(&knee);
        out.push_back(&makeup);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",        VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"sidechain",    VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",       VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"threshold_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        // Sidechain: use second audio input if present, otherwise follow main input
        float* sc = ctx->input_buffers[1];
        bool has_sidechain = false;
        if (sc) {
            for (uint32_t i = 0; i < frames; i++) {
                if (sc[i] != 0.0f) { has_sidechain = true; break; }
            }
        }
        float* detect = has_sidechain ? sc : in;

        float thresh_cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float thresh = threshold.value + thresh_cv;
        if (thresh < -60.0f) thresh = -60.0f;
        if (thresh > 0.0f)   thresh = 0.0f;

        float r       = ratio.value;
        float knee_w  = knee.value;
        float makeup_g = std::pow(10.0f, makeup.value / 20.0f);

        float sr = static_cast<float>(ctx->sample_rate);
        float att_coeff = std::exp(-1.0f / (attack.value  * 0.001f * sr));
        float rel_coeff = std::exp(-1.0f / (release.value * 0.001f * sr));

        float half_knee = knee_w * 0.5f;
        float env = env_;

        for (uint32_t i = 0; i < frames; i++) {
            // Envelope follower (peak mode)
            float abs_sample = std::fabs(detect[i]);
            float input_db = (abs_sample > 0.0f)
                ? 20.0f * std::log10(abs_sample)
                : kFloorDB;
            if (input_db < kFloorDB) input_db = kFloorDB;

            // Smooth envelope in dB domain
            if (input_db > env)
                env = att_coeff * env + (1.0f - att_coeff) * input_db;
            else
                env = rel_coeff * env + (1.0f - rel_coeff) * input_db;

            // Gain computer with soft knee
            float gain_db = 0.0f;
            float over = env - thresh;

            if (knee_w > 0.0f && over > -half_knee && over < half_knee) {
                // Soft knee region: quadratic interpolation
                float x = over + half_knee;
                gain_db = ((1.0f / r - 1.0f) * x * x) / (2.0f * knee_w);
            } else if (over >= half_knee) {
                // Above knee: full compression
                gain_db = (1.0f / r - 1.0f) * over;
            }
            // Below knee: gain_db stays 0 (no compression)

            float gain_linear = std::pow(10.0f, gain_db / 20.0f) * makeup_g;
            out[i] = in[i] * gain_linear;
        }

        env_ = env;
    }
};

VIVID_REGISTER(Compressor)
