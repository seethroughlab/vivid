#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/drum_dsp.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"

// ---------------------------------------------------------------------------
// DrumClap: 4 staggered noise bursts through SVF bandpass (stereo)
//
// Each burst has randomized timing (controlled by sloppy) and stereo panning.
// Filtered tail provides the reverberant decay.
// ---------------------------------------------------------------------------

struct DrumClap : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "DrumClap";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> decay        {"decay",        0.2f,    0.05f,   1.0f};
    vivid::Param<float> tone         {"tone",         0.5f,    0.0f,    1.0f};
    vivid::Param<float> sloppy       {"sloppy",       0.3f,    0.0f,    1.0f};
    vivid::Param<float> tail         {"tail",         0.3f,    0.0f,    1.0f};
    vivid::Param<float> stereo_width {"stereo_width", 0.5f,    0.0f,    1.0f};
    vivid::Param<float> tune         {"tune",      1500.0f,  500.0f, 5000.0f};
    vivid::Param<float> volume       {"volume",       0.7f,    0.0f,    1.0f};
    vivid::Param<int>   note         {"note",         39,      0,      127};

    static constexpr int kNumBursts = 4;
    // Base burst offsets in seconds (~15ms apart)
    static constexpr double kBurstBase[kNumBursts] = {0.0, 0.015, 0.030, 0.045};

    drum::DecayEnvelope env_;
    drum::WhiteNoise    noise_;
    drum::SVF           filter_l_;
    drum::SVF           filter_r_;
    float prev_trigger_ = 0.0f;

    // Per-trigger randomized burst timing offsets and pan positions
    double burst_offsets_[kNumBursts] = {};
    float  burst_pan_[kNumBursts]     = {};  // -1 to 1

    DrumClap() {
        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");

        vivid::semantic_tag(tune, "frequency_hz");
        vivid::semantic_shape(tune, "scalar");
        vivid::semantic_unit(tune, "Hz");

        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&decay);
        out.push_back(&tone);
        out.push_back(&sloppy);
        out.push_back(&tail);
        out.push_back(&stereo_width);
        out.push_back(&tune);
        out.push_back(&volume);
        out.push_back(&note);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_FLOAT, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SCALAR, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_in", VIVID_PORT_INPUT, VividMidiBuffer));
    }

    void randomize_bursts(float slop, float width) {
        for (int b = 0; b < kNumBursts; b++) {
            // Randomize timing: base + random offset scaled by sloppy
            float r1 = noise_.next();
            burst_offsets_[b] = kBurstBase[b] + r1 * slop * 0.01; // up to 10ms variation

            // Randomize stereo pan
            float r2 = noise_.next();
            burst_pan_[b] = r2 * width;
        }
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out_l = ctx->output_buffers[0];
        float* out_r = ctx->output_buffers[0] + ctx->buffer_size;
        double sr    = ctx->sample_rate;
        double inv_sr = 1.0 / sr;

        float dec       = decay.value;
        float tn        = tone.value;
        float slop      = sloppy.value;
        float tl        = tail.value;
        float width     = stereo_width.value;
        float center    = tune.value;
        float vol       = volume.value;

        float cutoff = center + tn * 2000.0f;

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
                randomize_bursts(slop, width);
            }

            float env = env_.value(dec);
            double t  = env_.time;

            // Sum burst contributions
            float burst_l = 0.0f;
            float burst_r = 0.0f;
            static constexpr double kBurstDuration = 0.008; // 8ms per burst

            for (int b = 0; b < kNumBursts; b++) {
                double burst_start = burst_offsets_[b];
                double burst_end   = burst_start + kBurstDuration;

                if (t >= burst_start && t < burst_end) {
                    float burst_env = 1.0f - static_cast<float>((t - burst_start) / kBurstDuration);
                    float n = noise_.next() * burst_env;

                    // Stereo pan
                    float pan = burst_pan_[b];
                    float gain_l = 0.5f * (1.0f - pan);
                    float gain_r = 0.5f * (1.0f + pan);
                    burst_l += n * gain_l;
                    burst_r += n * gain_r;
                }
            }

            // Tail: filtered noise with slow decay
            float tail_sample = 0.0f;
            if (tl > 0.0f) {
                tail_sample = noise_.next() * env * tl;
            }

            // Filter left and right
            float filt_l = filter_l_.process(burst_l + tail_sample * 0.5f, cutoff, 0.4f,
                                              static_cast<float>(sr), drum::SVF::BP);
            float filt_r = filter_r_.process(burst_r + tail_sample * 0.5f, cutoff, 0.4f,
                                              static_cast<float>(sr), drum::SVF::BP);

            out_l[i] = filt_l * env * vol * vel_scale;
            out_r[i] = filt_r * env * vol * vel_scale;

            env_.advance(inv_sr);
        }

    }
};

VIVID_REGISTER(DrumClap)
