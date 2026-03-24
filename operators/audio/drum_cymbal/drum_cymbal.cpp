#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/drum_dsp.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"

// ---------------------------------------------------------------------------
// DrumCymbal: 12 inharmonic ring oscillators + noise sizzle
//
// Square waves at inharmonically-related frequencies create metallic timbre.
// SVF highpass for brightness. Shimmer LFO amplitude modulation.
// Long decay times: 1-2s = ride, 3+ = crash.
// ---------------------------------------------------------------------------

struct DrumCymbal : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "DrumCymbal";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> decay   {"decay",   3.0f,  0.5f, 10.0f};
    vivid::Param<float> tone    {"tone",    0.5f,  0.0f,  1.0f};
    vivid::Param<float> pitch   {"pitch",   1.0f,  0.5f,  2.0f};
    vivid::Param<float> shimmer {"shimmer", 0.3f,  0.0f,  1.0f};
    vivid::Param<float> sizzle  {"sizzle",  0.3f,  0.0f,  1.0f};
    vivid::Param<float> volume  {"volume",  0.6f,  0.0f,  1.0f};
    vivid::Param<int>   note    {"note",    49,    0,    127};

    // 12 inharmonic frequencies for metallic cymbal timbre
    static constexpr int kNumOsc = 12;
    static constexpr float kOscFreqs[kNumOsc] = {
        205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f,
        1043.f, 1185.f, 1380.f, 1672.f, 1960.f, 2418.f
    };

    drum::DecayEnvelope env_;
    drum::WhiteNoise    noise_;
    drum::SVF           hp_filter_;
    double              ring_phases_[kNumOsc] = {};
    double              lfo_phase_ = 0.0;
    float               prev_trigger_ = 0.0f;

    DrumCymbal() {
        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");

        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&decay);
        out.push_back(&tone);
        out.push_back(&pitch);
        out.push_back(&shimmer);
        out.push_back(&sizzle);
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

        float dec      = decay.value;
        float tn       = tone.value;
        float p_mult   = pitch.value;
        float shm      = shimmer.value;
        float szl      = sizzle.value;
        float vol      = volume.value;

        float cutoff = 3000.0f + tn * 9000.0f;

        // Check for float trigger
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
                env_.trigger();
                for (int r = 0; r < kNumOsc; r++) ring_phases_[r] = 0.0;
                lfo_phase_ = 0.0;
            }

            float env = env_.value(dec);

            // Ring oscillators: square waves
            float ring_sum = audio_dsp::ring_osc_bank(ring_phases_, kOscFreqs, kNumOsc, p_mult, inv_sr);

            // Noise sizzle
            float noise_sample = noise_.next() * szl;

            // Mix ring + noise
            float raw = ring_sum + noise_sample;

            // Highpass filter
            float filtered = hp_filter_.process(raw, cutoff, 0.2f,
                                                 static_cast<float>(sr), drum::SVF::HP);

            // Shimmer LFO: amplitude modulation
            float lfo_mod = 1.0f;
            if (shm > 0.0f) {
                double lfo_freq = 4.0 + shm * 8.0;
                float lfo = static_cast<float>(std::sin(lfo_phase_ * 2.0 * M_PI));
                lfo_mod = 1.0f - shm * 0.5f * (1.0f - lfo); // modulation depth
                lfo_phase_ += lfo_freq * inv_sr;
                if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;
            }

            out[i] = filtered * env * lfo_mod * vol * vel_scale;

            env_.advance(inv_sr);
        }

    }
};

VIVID_REGISTER(DrumCymbal)
