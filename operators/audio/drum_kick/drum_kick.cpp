#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../drum_common/drum_dsp.h"

// ---------------------------------------------------------------------------
// DrumKick: 808-style kick drum
//
// Sine oscillator + 2nd/3rd harmonics + white noise click transient (2ms burst).
// Pitch envelope sweeps down from (pitch + pitch_env) to pitch.
// Soft clipping via tanh for warmth/drive.
// ---------------------------------------------------------------------------

struct DrumKick : vivid::OperatorBase {
    static constexpr const char* kName   = "DrumKick";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase     {"phase",      0.0f,   0.0f,   1.0f};
    vivid::Param<float> pitch     {"pitch",     60.0f,  20.0f, 200.0f};
    vivid::Param<float> pitch_env {"pitch_env",150.0f,   0.0f, 500.0f};
    vivid::Param<float> pitch_decay{"pitch_decay",0.05f, 0.01f,  0.5f};
    vivid::Param<float> decay     {"decay",      0.3f,  0.05f,   2.0f};
    vivid::Param<float> click     {"click",      0.3f,   0.0f,   1.0f};
    vivid::Param<float> drive     {"drive",      0.2f,   0.0f,   1.0f};
    vivid::Param<float> overtones {"overtones",  0.1f,   0.0f,   1.0f};
    vivid::Param<float> attack    {"attack",     0.005f, 0.0f,   0.1f};
    vivid::Param<float> volume    {"volume",     0.8f,   0.0f,   1.0f};

    drum::DecayEnvelope amp_env_;
    drum::DecayEnvelope pitch_env_;
    drum::WhiteNoise    noise_;
    double              osc_phase_ = 0.0;
    float               prev_phase_ = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&pitch);
        out.push_back(&pitch_env);
        out.push_back(&pitch_decay);
        out.push_back(&decay);
        out.push_back(&click);
        out.push_back(&drive);
        out.push_back(&overtones);
        out.push_back(&attack);
        out.push_back(&volume);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out = audio->output_buffers[0];
        double sr  = audio->sample_rate;
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
        float cur_phase = phase.value;

        // Click burst duration: 2ms in samples
        double click_dur = 0.002;

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            // Trigger detection
            if (i == 0 && drum::detect_trigger(cur_phase, prev_phase_)) {
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

            out[i] = sample * vol;

            // Advance
            osc_phase_ += freq * inv_sr;
            if (osc_phase_ >= 1.0) osc_phase_ -= 1.0;
            amp_env_.advance(inv_sr);
            pitch_env_.advance(inv_sr);
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(DrumKick)
