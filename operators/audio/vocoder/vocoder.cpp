#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Vocoder — channel vocoder with SVF bandpass filter bank (mono)
// ---------------------------------------------------------------------------

static constexpr int kMaxBands = 32;

struct BandState {
    float mod_low  = 0.0f, mod_band  = 0.0f; // SVF state for modulator
    float car_low  = 0.0f, car_band  = 0.0f; // SVF state for carrier
    float envelope = 0.0f;                     // Envelope follower
};

struct Vocoder : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Vocoder";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   bands         {"bands",          16, 4, 32};
    vivid::Param<float> envelope_speed{"envelope_speed", 50.0f, 1.0f, 500.0f};
    vivid::Param<float> mix           {"mix",            1.0f, 0.0f, 1.0f};

    BandState band_states_[kMaxBands];

    Vocoder() {
        vivid::semantic_tag(bands, "count");
        vivid::semantic_shape(bands, "scalar");
        vivid::display_hint(bands, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(envelope_speed, "time_milliseconds");
        vivid::semantic_shape(envelope_speed, "scalar");
        vivid::semantic_unit(envelope_speed, "ms");
        vivid::display_hint(envelope_speed, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bands);
        out.push_back(&envelope_speed);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"modulator", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"carrier",   VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",    VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"speed_cv",  VIVID_PORT_FLOAT, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SCALAR, 0, nullptr, 0, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* mod_in = ctx->input_buffers[0];
        float* car_in = ctx->input_buffers[1];
        float* out    = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float speed_cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float speed_ms = envelope_speed.value + speed_cv;
        if (speed_ms < 1.0f)   speed_ms = 1.0f;
        if (speed_ms > 500.0f) speed_ms = 500.0f;

        int   num_bands = bands.int_value();
        float wet = mix.value;
        float dry = 1.0f - wet;
        float sr  = static_cast<float>(ctx->sample_rate);

        // Envelope follower coefficient
        float env_coeff = 1.0f - std::exp(-1.0f / (speed_ms * 0.001f * sr));

        // Compute band center frequencies (logarithmically spaced)
        float band_freqs[kMaxBands];
        for (int b = 0; b < num_bands; b++) {
            if (num_bands > 1)
                band_freqs[b] = 80.0f * std::pow(12000.0f / 80.0f, static_cast<float>(b) / (num_bands - 1));
            else
                band_freqs[b] = 1000.0f;
        }

        // Compute SVF f coefficients and Q
        float band_f[kMaxBands];
        float band_q[kMaxBands];
        for (int b = 0; b < num_bands; b++) {
            float f = 2.0f * std::sin(static_cast<float>(M_PI) * band_freqs[b] / sr);
            if (f > 0.95f) f = 0.95f;
            band_f[b] = f;
            // Q from band spacing
            if (num_bands > 2) {
                float lo = (b > 0) ? band_freqs[b - 1] : band_freqs[0] * 0.5f;
                float hi = (b < num_bands - 1) ? band_freqs[b + 1] : band_freqs[num_bands - 1] * 2.0f;
                band_q[b] = 1.0f / (band_freqs[b] / (hi - lo));
            } else {
                band_q[b] = 0.15f; // fixed Q for very few bands
            }
            if (band_q[b] < 0.05f) band_q[b] = 0.05f;
            if (band_q[b] > 0.5f)  band_q[b] = 0.5f;
        }

        float norm = 1.0f / std::sqrt(static_cast<float>(num_bands));

        for (uint32_t i = 0; i < frames; i++) {
            float mod_sample = mod_in[i];
            float car_sample = car_in[i];
            float band_sum = 0.0f;

            for (int b = 0; b < num_bands; b++) {
                BandState& bs = band_states_[b];
                float f = band_f[b];
                float q = band_q[b];

                // SVF bandpass on modulator
                bs.mod_low  += f * bs.mod_band;
                float mod_high = mod_sample - bs.mod_low - q * bs.mod_band;
                bs.mod_band += f * mod_high;
                float mod_bp = bs.mod_band;

                // Envelope follower
                float abs_mod = std::fabs(mod_bp);
                bs.envelope += (abs_mod - bs.envelope) * env_coeff;

                // SVF bandpass on carrier
                bs.car_low  += f * bs.car_band;
                float car_high = car_sample - bs.car_low - q * bs.car_band;
                bs.car_band += f * car_high;
                float car_bp = bs.car_band;

                band_sum += car_bp * bs.envelope;
            }

            float wet_sig = band_sum * norm;
            out[i] = mod_in[i] * dry + wet_sig * wet;
        }
    }
};

VIVID_REGISTER(Vocoder)
