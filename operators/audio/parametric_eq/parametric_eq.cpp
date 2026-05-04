#include "parametric_eq.h"
#include "parametric_eq_editor_shared.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace pe = ::vivid::parametric_eq_editor;

ParametricEQ::ParametricEQ() {
    vivid::semantic_tag(band_count, "count");
    vivid::semantic_shape(band_count, "scalar");
    vivid::display_hint(band_count, VIVID_DISPLAY_KNOB);
    vivid::description(band_count, "Number of active EQ bands (1–4)");

    auto setup_band = [](vivid::Param<float>& freq, vivid::Param<float>& gain,
                         vivid::Param<float>& q, vivid::Param<int>& type,
                         const char* group) {
        vivid::param_group(freq, group);
        vivid::semantic_tag(freq, "frequency_hz");
        vivid::semantic_shape(freq, "scalar");
        vivid::semantic_unit(freq, "Hz");
        vivid::display_hint(freq, VIVID_DISPLAY_HIDDEN);
        vivid::description(freq, "Center frequency of this EQ band in Hz");

        vivid::param_group(gain, group);
        vivid::semantic_tag(gain, "gain_db");
        vivid::semantic_shape(gain, "scalar");
        vivid::semantic_unit(gain, "dB");
        vivid::display_hint(gain, VIVID_DISPLAY_HIDDEN);
        vivid::description(gain, "Boost or cut in dB (-24 to +24)");

        vivid::param_group(q, group);
        vivid::semantic_tag(q, "resonance");
        vivid::semantic_shape(q, "scalar");
        vivid::display_hint(q, VIVID_DISPLAY_HIDDEN);
        vivid::description(q, "Filter Q / bandwidth (higher = narrower)");

        vivid::param_group(type, group);
        vivid::display_hint(type, VIVID_DISPLAY_HIDDEN);
        vivid::description(type, "Filter shape: Peak, Low Shelf, High Shelf, Low Pass, or High Pass");
    };

    setup_band(freq_1, gain_1, q_1, type_1, "Band 1");
    setup_band(freq_2, gain_2, q_2, type_2, "Band 2");
    setup_band(freq_3, gain_3, q_3, type_3, "Band 3");
    setup_band(freq_4, gain_4, q_4, type_4, "Band 4");
}

void ParametricEQ::collect_params(std::vector<vivid::ParamBase*>& out) {
    out.push_back(&band_count);
    out.push_back(&freq_1); out.push_back(&gain_1); out.push_back(&q_1); out.push_back(&type_1);
    out.push_back(&freq_2); out.push_back(&gain_2); out.push_back(&q_2); out.push_back(&type_2);
    out.push_back(&freq_3); out.push_back(&gain_3); out.push_back(&q_3); out.push_back(&type_3);
    out.push_back(&freq_4); out.push_back(&gain_4); out.push_back(&q_4); out.push_back(&type_4);
}

void ParametricEQ::collect_ports(std::vector<VividPortDescriptor>& out) {
    out.push_back({"input",     VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    out.push_back({"output",    VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    out.push_back({"freq_1_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    vivid::append_analysis_ports(out);
}

static void apply_biquad(BiquadState& s, const pe::BiquadCoeffs& c,
                         float* buf, uint32_t frames) {
    for (uint32_t i = 0; i < frames; i++) {
        float x0 = buf[i];
        float y0 = c.b0 * x0 + c.b1 * s.x1 + c.b2 * s.x2
                              - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x0;
        s.y2 = s.y1; s.y1 = y0;
        buf[i] = y0;
    }
}

void ParametricEQ::process_audio(const VividAudioContext* ctx) {
    float* in  = ctx->input_buffers[0];
    float* out = ctx->output_buffers[0];
    uint32_t frames = ctx->buffer_size;
    float sr = static_cast<float>(ctx->sample_rate);

    for (uint32_t i = 0; i < frames; i++) out[i] = in[i];

    float freq_1_cv = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
    int bc = band_count.int_value();

    float freqs[] = {freq_1.value, freq_2.value, freq_3.value, freq_4.value};
    float gains[] = {gain_1.value, gain_2.value, gain_3.value, gain_4.value};
    float qs[]    = {q_1.value,    q_2.value,    q_3.value,    q_4.value};
    int   types[] = {type_1.int_value(), type_2.int_value(),
                     type_3.int_value(), type_4.int_value()};

    freqs[0] *= std::pow(2.0f, freq_1_cv / 12.0f);
    freqs[0] = std::clamp(freqs[0], pe::kMinFreqHz, pe::kMaxFreqHz);

    for (int b = 0; b < bc; b++) {
        auto c = pe::compute_coeffs(types[b], freqs[b], gains[b], qs[b], sr);
        apply_biquad(bands_[b], c, out, frames);
    }
}

VIVID_DEFINE_OP(ParametricEQ) {
}

VIVID_REGISTER(ParametricEQ)
VIVID_EDITOR(ParametricEQ)
