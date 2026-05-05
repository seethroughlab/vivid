#include "operator_api/operator.h"
#include "shared/drum_dsp/drum_dsp.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"

/**
 * @brief Synthesized snare with tonal body and bandpass-filtered noise.
 *
 * Combines a sine oscillator with harmonics and bandpass-filtered noise,
 * each with independent decay times. The snappy parameter controls the
 * noise character.
 *
 * @param snappy Character of the noise component — tighter vs looser rattle.
 * @param color Amount of harmonic overtones in the tonal body.
 * @see DrumKick, DrumClap, DrumKit
 */
struct DrumSnare : vivid::OperatorBase, vivid::AudioProcessable {
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
        vivid::description(tone_level, "Level of the tonal sine body in the mix");
        vivid::description(noise_level, "Level of the filtered noise in the mix");

        vivid::description(pitch, "Fundamental frequency of the tonal body in Hz");
        vivid::semantic_tag(pitch, "frequency_hz");
        vivid::semantic_shape(pitch, "scalar");
        vivid::semantic_unit(pitch, "Hz");

        vivid::description(tone_decay, "Decay time of the tonal body in seconds");
        vivid::semantic_tag(tone_decay, "time_seconds");
        vivid::semantic_shape(tone_decay, "scalar");
        vivid::semantic_unit(tone_decay, "s");

        vivid::description(noise_decay, "Decay time of the noise component in seconds");
        vivid::semantic_tag(noise_decay, "time_seconds");
        vivid::semantic_shape(noise_decay, "scalar");
        vivid::semantic_unit(noise_decay, "s");

        vivid::description(snappy, "Tightness of the noise rattle (higher = brighter, snappier)");
        vivid::description(color, "Harmonic overtone content in the tonal body");

        vivid::description(volume, "Overall output level");
        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");

        vivid::description(note, "MIDI note number that triggers this drum (0-127)");
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
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        vivid::append_analysis_ports(out);
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

        const float tone_factor  = drum::DecayEnvelope::compute_factor(t_dec, inv_sr);
        const float noise_factor = drum::DecayEnvelope::compute_factor(n_dec, inv_sr);

        for (uint32_t i = 0; i < ctx->buffer_size; i++) {
            // Wire trigger: rising-edge detection
            float tv = trig_buf ? trig_buf[i] : 0.0f;
            bool wire_trig = (tv > 0.5f && prev_trigger_ <= 0.5f);
            prev_trigger_ = tv;

            bool trig = wire_trig || ((i == 0) && midi_triggered);
            if (trig) {
                tone_env_.trigger();
                noise_env_.trigger();
                osc_phase_ = 0.0;
            }

            float t_env = tone_env_.step(tone_factor, inv_sr);
            float n_env = noise_env_.step(noise_factor, inv_sr);

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
        }

    }
};

VIVID_DEFINE_OP(DrumSnare) {
}

