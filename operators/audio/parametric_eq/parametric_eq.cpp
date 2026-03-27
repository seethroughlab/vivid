#include "operator_api/operator.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Parametric EQ — 4-band biquad equalizer (RBJ Audio EQ Cookbook) (mono)
// ---------------------------------------------------------------------------

struct BiquadState {
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;
};

struct ParametricEQ : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "ParametricEQ";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> band_count{"band_count", 4, 1, 4};

    // Band 1
    vivid::Param<float> freq_1{"freq_1", 100.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_1{"gain_1", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_1   {"q_1",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_1{"type_1", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    // Band 2
    vivid::Param<float> freq_2{"freq_2", 500.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_2{"gain_2", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_2   {"q_2",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_2{"type_2", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    // Band 3
    vivid::Param<float> freq_3{"freq_3", 2000.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_3{"gain_3", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_3   {"q_3",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_3{"type_3", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    // Band 4
    vivid::Param<float> freq_4{"freq_4", 8000.0f, 20.0f, 20000.0f};
    vivid::Param<float> gain_4{"gain_4", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> q_4   {"q_4",    1.0f, 0.1f, 20.0f};
    vivid::Param<int>   type_4{"type_4", 0, {"Peak", "Low Shelf", "High Shelf", "Low Pass", "High Pass"}};

    BiquadState bands_[4];

    ParametricEQ() {
        vivid::semantic_tag(band_count, "count");
        vivid::semantic_shape(band_count, "scalar");
        vivid::display_hint(band_count, VIVID_DISPLAY_KNOB);

        auto setup_band = [](vivid::Param<float>& freq, vivid::Param<float>& gain,
                             vivid::Param<float>& q, vivid::Param<int>& type,
                             const char* group) {
            vivid::param_group(freq, group);
            vivid::semantic_tag(freq, "frequency_hz");
            vivid::semantic_shape(freq, "scalar");
            vivid::semantic_unit(freq, "Hz");
            vivid::display_hint(freq, VIVID_DISPLAY_KNOB);
            vivid::layout_row(freq, 2, 0);

            vivid::param_group(gain, group);
            vivid::semantic_tag(gain, "gain_db");
            vivid::semantic_shape(gain, "scalar");
            vivid::semantic_unit(gain, "dB");
            vivid::display_hint(gain, VIVID_DISPLAY_KNOB);
            vivid::layout_row(gain, 2, 1);

            vivid::param_group(q, group);
            vivid::semantic_tag(q, "resonance");
            vivid::semantic_shape(q, "scalar");
            vivid::display_hint(q, VIVID_DISPLAY_KNOB);
            vivid::layout_row(q, 2, 0);

            vivid::param_group(type, group);
            vivid::display_hint(type, VIVID_DISPLAY_KNOB);
            vivid::layout_row(type, 2, 1);
        };

        setup_band(freq_1, gain_1, q_1, type_1, "Band 1");
        setup_band(freq_2, gain_2, q_2, type_2, "Band 2");
        setup_band(freq_3, gain_3, q_3, type_3, "Band 3");
        setup_band(freq_4, gain_4, q_4, type_4, "Band 4");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&band_count);
        out.push_back(&freq_1); out.push_back(&gain_1); out.push_back(&q_1); out.push_back(&type_1);
        out.push_back(&freq_2); out.push_back(&gain_2); out.push_back(&q_2); out.push_back(&type_2);
        out.push_back(&freq_3); out.push_back(&gain_3); out.push_back(&q_3); out.push_back(&type_3);
        out.push_back(&freq_4); out.push_back(&gain_4); out.push_back(&q_4); out.push_back(&type_4);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",     VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",    VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"freq_1_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    }

    struct BiquadCoeffs {
        float b0, b1, b2, a1, a2;
    };

    static BiquadCoeffs compute_coeffs(int type, float freq, float gain_db, float Q, float sr) {
        float w0 = 2.0f * static_cast<float>(M_PI) * freq / sr;
        float cos_w0 = std::cos(w0);
        float sin_w0 = std::sin(w0);
        float alpha = sin_w0 / (2.0f * Q);
        float A = std::pow(10.0f, gain_db / 40.0f);

        float b0, b1, b2, a0, a1, a2;

        switch (type) {
            default:
            case 0: { // Peak
                b0 = 1.0f + alpha * A;
                b1 = -2.0f * cos_w0;
                b2 = 1.0f - alpha * A;
                a0 = 1.0f + alpha / A;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha / A;
                break;
            }
            case 1: { // Low Shelf
                float sq = 2.0f * std::sqrt(A) * alpha;
                b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 + sq);
                b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w0);
                b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w0 - sq);
                a0 = (A + 1.0f) + (A - 1.0f) * cos_w0 + sq;
                a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w0);
                a2 = (A + 1.0f) + (A - 1.0f) * cos_w0 - sq;
                break;
            }
            case 2: { // High Shelf
                float sq = 2.0f * std::sqrt(A) * alpha;
                b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 + sq);
                b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w0);
                b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w0 - sq);
                a0 = (A + 1.0f) - (A - 1.0f) * cos_w0 + sq;
                a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w0);
                a2 = (A + 1.0f) - (A - 1.0f) * cos_w0 - sq;
                break;
            }
            case 3: { // Low Pass
                b0 = (1.0f - cos_w0) / 2.0f;
                b1 = 1.0f - cos_w0;
                b2 = (1.0f - cos_w0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha;
                break;
            }
            case 4: { // High Pass
                b0 = (1.0f + cos_w0) / 2.0f;
                b1 = -(1.0f + cos_w0);
                b2 = (1.0f + cos_w0) / 2.0f;
                a0 = 1.0f + alpha;
                a1 = -2.0f * cos_w0;
                a2 = 1.0f - alpha;
                break;
            }
        }

        float inv_a0 = 1.0f / a0;
        return {b0 * inv_a0, b1 * inv_a0, b2 * inv_a0, a1 * inv_a0, a2 * inv_a0};
    }

    static void apply_biquad(BiquadState& s, const BiquadCoeffs& c, float* buf, uint32_t frames) {
        for (uint32_t i = 0; i < frames; i++) {
            float x0 = buf[i];
            float y0 = c.b0 * x0 + c.b1 * s.x1 + c.b2 * s.x2
                                  - c.a1 * s.y1 - c.a2 * s.y2;
            s.x2 = s.x1; s.x1 = x0;
            s.y2 = s.y1; s.y1 = y0;
            buf[i] = y0;
        }
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;
        float sr = static_cast<float>(ctx->sample_rate);

        // Copy input to output, then filter in-place
        for (uint32_t i = 0; i < frames; i++)
            out[i] = in[i];

        float freq_1_cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;

        int bc = band_count.int_value();

        // Band params arrays for cleaner iteration
        float freqs[] = {freq_1.value, freq_2.value, freq_3.value, freq_4.value};
        float gains[] = {gain_1.value, gain_2.value, gain_3.value, gain_4.value};
        float qs[]    = {q_1.value,    q_2.value,    q_3.value,    q_4.value};
        int   types[] = {type_1.int_value(), type_2.int_value(), type_3.int_value(), type_4.int_value()};

        // Apply CV to band 1 frequency
        freqs[0] *= std::pow(2.0f, freq_1_cv / 12.0f);
        if (freqs[0] < 20.0f)    freqs[0] = 20.0f;
        if (freqs[0] > 20000.0f) freqs[0] = 20000.0f;

        for (int b = 0; b < bc; b++) {
            auto c = compute_coeffs(types[b], freqs[b], gains[b], qs[b], sr);
            apply_biquad(bands_[b], c, out, frames);
        }
    }
};

VIVID_REGISTER(ParametricEQ)
