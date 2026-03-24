#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/drum_dsp.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"

// ---------------------------------------------------------------------------
// DrumKick: 808-style kick drum
//
// Sine oscillator + 2nd/3rd harmonics + white noise click transient (2ms burst).
// Pitch envelope sweeps down from (pitch + pitch_env) to pitch.
// Soft clipping via tanh for warmth/drive.
// ---------------------------------------------------------------------------

struct DrumKick : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "DrumKick";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> pitch     {"pitch",     60.0f,  20.0f, 200.0f};
    vivid::Param<float> pitch_env {"pitch_env",150.0f,   0.0f, 500.0f};
    vivid::Param<float> pitch_decay{"pitch_decay",0.05f, 0.01f,  0.5f};
    vivid::Param<float> decay     {"decay",      0.3f,  0.05f,   2.0f};
    vivid::Param<float> click     {"click",      0.3f,   0.0f,   1.0f};
    vivid::Param<float> drive     {"drive",      0.2f,   0.0f,   1.0f};
    vivid::Param<float> overtones {"overtones",  0.1f,   0.0f,   1.0f};
    vivid::Param<float> attack    {"attack",     0.005f, 0.0f,   0.1f};
    vivid::Param<float> volume    {"volume",     0.8f,   0.0f,   1.0f};
    vivid::Param<int>   note      {"note",       36,     0,     127};

    drum::DecayEnvelope amp_env_;
    drum::DecayEnvelope pitch_env_;
    drum::WhiteNoise    noise_;
    double              osc_phase_ = 0.0;
    float               prev_trigger_ = 0.0f;

    DrumKick() {
        vivid::semantic_tag(pitch, "frequency_hz");
        vivid::semantic_shape(pitch, "scalar");
        vivid::semantic_unit(pitch, "Hz");

        vivid::semantic_tag(pitch_decay, "time_seconds");
        vivid::semantic_shape(pitch_decay, "scalar");
        vivid::semantic_unit(pitch_decay, "s");

        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");

        vivid::semantic_tag(attack, "time_seconds");
        vivid::semantic_shape(attack, "scalar");
        vivid::semantic_unit(attack, "s");

        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&pitch);
        out.push_back(&pitch_env);
        out.push_back(&pitch_decay);
        out.push_back(&decay);
        out.push_back(&click);
        out.push_back(&drive);
        out.push_back(&overtones);
        out.push_back(&attack);
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

        float p_base   = pitch.value;
        float p_sweep  = pitch_env.value;
        float p_decay  = pitch_decay.value;
        float a_decay  = decay.value;
        float clk      = click.value;
        float drv      = drive.value;
        float ovt      = overtones.value;
        float atk      = attack.value;
        float vol      = volume.value;

        // Click burst duration: 2ms in samples
        double click_dur = 0.002;

        // Check for float gate trigger (rising edge)
        bool float_triggered = false;
        float float_vel_scale = 1.0f;
        if (ctx->input_float_values) {
            float trig = ctx->input_float_values[0];
            if (trig > 0.5f && prev_trigger_ <= 0.5f) {
                float_triggered = true;
                float_vel_scale = trig;  // use trigger value as velocity
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

        // MIDI takes priority if both fire
        bool triggered = midi_triggered || float_triggered;
        float vel_scale = midi_triggered ? midi_vel_scale : float_vel_scale;

        for (uint32_t i = 0; i < ctx->buffer_size; i++) {
            bool trig = (i == 0) && triggered;
            if (trig) {
                amp_env_.trigger();
                pitch_env_.trigger();
                osc_phase_ = 0.0;
            }

            // Envelopes
            float amp  = amp_env_.value(a_decay);
            float penv = pitch_env_.value(p_decay);

            // Attack shaping: ramp up over attack time
            if (atk > 0.0f && amp_env_.time < atk) {
                float att = static_cast<float>(amp_env_.time / atk);
                amp *= att;
            }

            // Pitch: base + sweep * pitch_envelope
            double freq = p_base + p_sweep * penv;

            // Sine oscillator + harmonics (2nd and 3rd)
            double harm = audio_dsp::harmonics_3(osc_phase_, ovt);

            // Noise click transient (2ms burst)
            float click_sample = 0.0f;
            if (clk > 0.0f && amp_env_.time < click_dur) {
                float click_env = static_cast<float>(1.0 - amp_env_.time / click_dur);
                click_sample = noise_.next() * clk * click_env;
            }

            // Mix and soft clip
            float sample = static_cast<float>(harm) * amp + click_sample;
            if (drv > 0.0f)
                sample = drum::soft_clip(sample, drv);

            out[i] = sample * vol * vel_scale;

            // Advance
            osc_phase_ += freq * inv_sr;
            if (osc_phase_ >= 1.0) osc_phase_ -= 1.0;
            amp_env_.advance(inv_sr);
            pitch_env_.advance(inv_sr);
        }

    }
};

VIVID_REGISTER(DrumKick)
