#include "operator_api/operator.h"
#include "shared/drum_dsp/drum_dsp.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"

/**
 * @brief Synthesized hi-hat from 6 metallic oscillators and noise.
 *
 * Blends 6 ring oscillators at metallic frequency ratios with filtered
 * noise. The ring parameter controls the oscillator-to-noise ratio.
 * Short decay for closed hat, longer for open.
 *
 * @param ring Balance between metallic oscillators (1) and noise (0).
 * @see DrumCymbal, DrumClap, DrumKit
 */
struct DrumHiHat : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "DrumHiHat";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> decay  {"decay",   0.08f, 0.01f, 2.0f};
    vivid::Param<float> tone   {"tone",    0.5f,  0.0f, 1.0f};
    vivid::Param<float> ring   {"ring",    0.5f,  0.0f, 1.0f};
    vivid::Param<float> pitch  {"pitch",   1.0f,  0.5f, 2.0f};
    vivid::Param<float> attack {"attack",  0.002f, 0.0f, 0.05f};
    vivid::Param<float> volume {"volume",  0.7f,  0.0f, 1.0f};
    vivid::Param<int>   note   {"note",    42,    0,   127};

    // 808-style metallic ring frequencies
    static constexpr float kRingFreqs[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

    drum::DecayEnvelope env_;
    drum::WhiteNoise    noise_;
    drum::SVF           hp_filter_;
    double              ring_phases_[6] = {};
    float               prev_trigger_ = 0.0f;

    DrumHiHat() {
        vivid::description(decay, "Amplitude decay time in seconds (short = closed, long = open)");
        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");

        vivid::description(tone, "Highpass filter brightness (higher = more sizzle)");
        vivid::description(ring, "Balance between metallic oscillators and noise (0 = noise, 1 = ring)");
        vivid::description(pitch, "Pitch multiplier for the metallic oscillator bank");

        vivid::description(attack, "Amplitude ramp-up time in seconds (0 = instant)");
        vivid::semantic_tag(attack, "time_seconds");
        vivid::semantic_shape(attack, "scalar");
        vivid::semantic_unit(attack, "s");

        vivid::description(volume, "Overall output level");
        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");

        vivid::description(note, "MIDI note number that triggers this drum (0-127)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&decay);
        out.push_back(&tone);
        out.push_back(&ring);
        out.push_back(&pitch);
        out.push_back(&attack);
        out.push_back(&volume);
        out.push_back(&note);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        double sr  = ctx->sample_rate;
        double inv_sr = 1.0 / sr;

        float dec      = decay.value;
        float tn       = tone.value;
        float rng      = ring.value;
        float p_mult   = pitch.value;
        float atk      = attack.value;
        float vol      = volume.value;

        float cutoff = 4000.0f + tn * 8000.0f;

        // Check for note trigger. Drum synths fire on NOTE_ON of the target
        // note number; per-note id and expression are ignored (single-shot).
        bool midi_triggered = false;
        float midi_vel_scale = 1.0f;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0]) {
            auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
            uint8_t target_note = static_cast<uint8_t>(note.int_value());
            for (uint32_t m = 0; m < notes->count; ++m) {
                const auto& ev = notes->events[m];
                if (ev.type == VIVID_NOTE_ON && ev.note_number == target_note) {
                    midi_triggered = true;
                    midi_vel_scale = ev.value;
                    break;
                }
            }
        }

        float vel_scale = midi_vel_scale;
        const float* trig_buf = ctx->input_buffers[0];

        const float env_factor = drum::DecayEnvelope::compute_factor(dec, inv_sr);

        for (uint32_t i = 0; i < ctx->buffer_size; i++) {
            // Wire trigger: rising-edge detection
            float tv = trig_buf ? trig_buf[i] : 0.0f;
            bool wire_trig = (tv > 0.5f && prev_trigger_ <= 0.5f);
            prev_trigger_ = tv;

            bool trig = wire_trig || ((i == 0) && midi_triggered);
            if (trig) {
                env_.trigger();
                for (int r = 0; r < 6; r++) ring_phases_[r] = 0.0;
            }

            const double env_time = env_.time;
            float env = env_.step(env_factor, inv_sr);

            // Attack shaping — compare against the pre-step time.
            if (atk > 0.0f && env_time < atk) {
                env *= static_cast<float>(env_time / atk);
            }

            // Ring oscillators: square waves
            float ring_sum = audio_dsp::ring_osc_bank(ring_phases_, kRingFreqs, 6, p_mult, inv_sr);

            // Noise component
            float noise_sample = noise_.next();

            // Blend ring vs noise
            float raw = ring_sum * rng + noise_sample * (1.0f - rng);

            // Highpass filter
            float filtered = hp_filter_.process(raw, cutoff, 0.3f,
                                                 static_cast<float>(sr), drum::SVF::HP);

            out[i] = filtered * env * vol * vel_scale;
        }

    }
};

VIVID_DEFINE_OP(DrumHiHat) {
}

VIVID_REGISTER(DrumHiHat)
