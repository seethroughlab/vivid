#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/drum_dsp.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"

// ---------------------------------------------------------------------------
// DrumSnare: Sine body + harmonics + SVF-filtered white noise
//
// Independent tone/noise decay times for snappy control.
// SVF bandpass on noise controlled by snappy param.
// ---------------------------------------------------------------------------

struct DrumSnare : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "DrumSnare";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> tone_level {"tone",        0.5f,   0.0f,  1.0f};
    vivid::Param<float> noise_level{"noise",       0.5f,   0.0f,  1.0f};
    vivid::Param<float> pitch      {"pitch",     200.0f, 100.0f, 400.0f};
    vivid::Param<float> tone_decay {"tone_decay",  0.1f,  0.01f,  0.5f};
    vivid::Param<float> noise_decay{"noise_decay", 0.2f,  0.05f,  0.5f};
    vivid::Param<float> snappy     {"snappy",      0.5f,   0.0f,  1.0f};
    vivid::Param<float> color      {"color",       0.5f,   0.0f,  1.0f};
    vivid::Param<float> volume     {"volume",      0.8f,   0.0f,  1.0f};
    vivid::Param<int>   note       {"note",        38,     0,    127};

    drum::DecayEnvelope tone_env_;
    drum::DecayEnvelope noise_env_;
    drum::WhiteNoise    noise_;
    drum::SVF           noise_filter_;
    double              osc_phase_ = 0.0;
    float               prev_trigger_ = 0.0f;

    DrumSnare() {
        vivid::semantic_tag(pitch, "frequency_hz");
        vivid::semantic_shape(pitch, "scalar");
        vivid::semantic_unit(pitch, "Hz");

        vivid::semantic_tag(tone_decay, "time_seconds");
        vivid::semantic_shape(tone_decay, "scalar");
        vivid::semantic_unit(tone_decay, "s");

        vivid::semantic_tag(noise_decay, "time_seconds");
        vivid::semantic_shape(noise_decay, "scalar");
        vivid::semantic_unit(noise_decay, "s");

        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&tone_level);
        out.push_back(&noise_level);
        out.push_back(&pitch);
        out.push_back(&tone_decay);
        out.push_back(&noise_decay);
        out.push_back(&snappy);
        out.push_back(&color);
        out.push_back(&volume);
        out.push_back(&note);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_FLOAT, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SCALAR, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_in", VIVID_PORT_INPUT, VividMidiBuffer));
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        double sr  = ctx->sample_rate;
        double inv_sr = 1.0 / sr;

        float p       = pitch.value;
        float t_level = tone_level.value;
        float n_level = noise_level.value;
        float t_dec   = tone_decay.value;
        float n_dec   = noise_decay.value;
        float snap    = snappy.value;
        float col     = color.value;
        float vol     = volume.value;

        float cutoff = 2000.0f + snap * 4000.0f;

        // Check for float gate trigger (rising edge)
        bool float_triggered = false;
        float float_vel_scale = 1.0f;
        if (ctx->input_float_values) {
            float trig = ctx->input_float_values[0];
            if (trig > 0.5f && prev_trigger_ <= 0.5f) {
                float_triggered = true;
                float_vel_scale = trig;
            }
            prev_trigger_ = trig;
        }

        // Check for MIDI trigger
        bool midi_triggered = false;
        float midi_vel_scale = 1.0f;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0]) {
            auto* midi = static_cast<const VividMidiBuffer*>(ctx->custom_inputs[0]);
            uint8_t target_note = static_cast<uint8_t>(note.int_value());
            for (uint32_t m = 0; m < midi->count; ++m) {
                const auto& msg = midi->messages[m];
                if ((msg.status & 0xF0) == 0x90 && msg.data2 > 0 && msg.data1 == target_note) {
                    midi_triggered = true;
                    midi_vel_scale = msg.data2 / 127.0f;
                    break;
                }
            }
        }

        bool triggered = midi_triggered || float_triggered;
        float vel_scale = midi_triggered ? midi_vel_scale : float_vel_scale;

        for (uint32_t i = 0; i < ctx->buffer_size; i++) {
            bool trig = (i == 0) && triggered;
            if (trig) {
                tone_env_.trigger();
                noise_env_.trigger();
                osc_phase_ = 0.0;
            }

            float t_env = tone_env_.value(t_dec);
            float n_env = noise_env_.value(n_dec);

            // Tone body: sine + harmonics controlled by color
            double body = audio_dsp::harmonics_3(osc_phase_, col);

            // Filtered noise
            float raw_noise = noise_.next();
            float filt_noise = noise_filter_.process(raw_noise, cutoff, 0.3f,
                                                      static_cast<float>(sr), drum::SVF::BP);

            // Mix
            float sample = static_cast<float>(body) * t_env * t_level
                         + filt_noise * n_env * n_level;

            out[i] = sample * vol * vel_scale;

            osc_phase_ += p * inv_sr;
            if (osc_phase_ >= 1.0) osc_phase_ -= 1.0;
            tone_env_.advance(inv_sr);
            noise_env_.advance(inv_sr);
        }

    }
};

VIVID_REGISTER(DrumSnare)
