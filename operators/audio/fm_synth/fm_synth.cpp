#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "operator_api/voice_table.h"
#include "voice_breakouts.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @brief Two-operator polyphonic FM synthesizer with ADSR envelope.
 *
 * Classic FM synthesis with a carrier and modulator oscillator. The
 * modulator frequency is set as a ratio of the carrier, and the modulation
 * index controls harmonic richness. Drive it directly with `notes_in` from
 * any note source (Tracker, NotePattern, Sequencer, ChordProgression, …) for
 * polyphonic playback up to 8 voices. For advanced per-voice graph control,
 * pair the same note stream with `NoteBreakout` and route `voice_gates`,
 * `voice_ids`, and `voice_freqs` into envelopes and filters downstream.
 *
 * @tip Integer mod_ratio values produce harmonic timbres; non-integer values create bell-like inharmonic sounds.
 * @param mod_ratio Modulator frequency as a multiple of the carrier.
 * @param mod_index Depth of frequency modulation. Higher = more harmonics.
 * @see Oscillator, Filter, SpreadADSR
 */
struct FmSynth : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "FmSynth";
    static constexpr bool kTimeDependent = true;
    static constexpr int  kMaxVoices     = 8;
    // A polyphonic synth sums its voices into one audio `output`, so that port
    // is a lane reduction (scalar provenance) — this lets multiple synths meet
    // at a pointwise Mixer. The per-voice voice_*/voices_out breakouts keep
    // their own lane set (handled per-port in the compiler's lane-set pass).
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_REDUCTION;

    vivid::Param<float> carrier_freq{"carrier_freq", 440.0f, 20.0f, 20000.0f};
    vivid::Param<float> mod_ratio   {"mod_ratio",    2.0f,   0.0f,  16.0f};
    vivid::Param<float> mod_index   {"mod_index",    1.0f,   0.0f,  20.0f};
    vivid::Param<float> attack      {"attack",       0.01f,  0.001f, 5.0f};
    vivid::Param<float> decay       {"decay",        0.2f,   0.001f, 5.0f};
    vivid::Param<float> sustain     {"sustain",      0.7f,   0.0f,   1.0f};
    vivid::Param<float> release     {"release",      0.3f,   0.001f, 5.0f};
    vivid::Param<float> amplitude   {"amplitude",    0.5f,   0.0f,   1.0f};
    // Per-note expression depth (Phase 5). Pressure scales per-voice
    // amplitude by (1 + depth * slot.pressure); timbre scales mod_index
    // by (1 + depth * slot.timbre). Defaults respond audibly to a fresh
    // pattern; dial to 0 to flow expression through breakouts only.
    vivid::Param<float> pressure_to_amp      {"pressure_to_amp",      0.5f,  0.0f, 1.0f};
    vivid::Param<float> timbre_to_mod_index  {"timbre_to_mod_index",  0.5f, -1.0f, 1.0f};

    // Per-voice oscillator + envelope state. Indexed by VoiceTable slot.
    struct VoiceState {
        double carrier_phase = 0.0;
        double mod_phase     = 0.0;
        vivid::adsr::State envelope;
    };
    VoiceState voices_[kMaxVoices];
    vivid::VoiceTable<kMaxVoices> allocator_;
    uint64_t frame_counter_ = 0;

    // Legacy mono scalar gate path retained as an internal fallback.
    double mono_carrier_phase_ = 0.0;
    double mono_mod_phase_     = 0.0;
    vivid::adsr::State mono_env_;
    float prev_gate_ = 0.0f;

    FmSynth() {
        vivid::semantic_tag(carrier_freq, "frequency_hz");
        vivid::semantic_shape(carrier_freq, "scalar");
        vivid::semantic_unit(carrier_freq, "Hz");
        vivid::display_hint(carrier_freq, VIVID_DISPLAY_KNOB);
        vivid::description(carrier_freq, "Base frequency of the carrier oscillator in Hz");

        vivid::semantic_shape(mod_ratio, "scalar");
        vivid::display_hint(mod_ratio, VIVID_DISPLAY_KNOB);
        vivid::description(mod_ratio, "Modulator frequency as a multiple of the carrier (integer = harmonic)");

        vivid::semantic_tag(mod_index, "amplitude_linear");
        vivid::semantic_shape(mod_index, "scalar");
        vivid::display_hint(mod_index, VIVID_DISPLAY_KNOB);
        vivid::description(mod_index, "Depth of frequency modulation, higher values add more harmonics");

        vivid::semantic_tag(attack, "time_seconds");
        vivid::semantic_shape(attack, "scalar");
        vivid::semantic_unit(attack, "s");
        vivid::display_hint(attack, VIVID_DISPLAY_ADSR);
        vivid::description(attack, "Time to reach full volume after a note-on, in seconds");

        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");
        vivid::display_hint(decay, VIVID_DISPLAY_ADSR);
        vivid::description(decay, "Time to fall from peak to sustain level, in seconds");

        vivid::semantic_tag(sustain, "probability_01");
        vivid::semantic_shape(sustain, "scalar");
        vivid::display_hint(sustain, VIVID_DISPLAY_ADSR);
        vivid::description(sustain, "Held volume level while the note is sustained (0-1)");

        vivid::semantic_tag(release, "time_seconds");
        vivid::semantic_shape(release, "scalar");
        vivid::semantic_unit(release, "s");
        vivid::display_hint(release, VIVID_DISPLAY_ADSR);
        vivid::description(release, "Fade-out time after a note-off, in seconds");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::display_hint(amplitude, VIVID_DISPLAY_KNOB);
        vivid::description(amplitude, "Master output volume of the synth");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&carrier_freq);
        out.push_back(&mod_ratio);
        out.push_back(&mod_index);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&amplitude);
        out.push_back(&pressure_to_amp);
        out.push_back(&timbre_to_mod_index);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output",       VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"freq_cv",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"mod_index_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"gate_cv",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        // Canonical native note input — drive directly from Tracker/NotePattern/etc.
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        // Per-voice advanced breakouts. voices_out is a multichannel audio
        // buffer (one channel per voice slot, kMaxVoices total) populated
        // in active-note order sorted by note_id. The four control lanes
        // share the same ordering. All five are advanced — collapsed in
        // the inspector unless connected.
        out.push_back({"voices_out",       VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr,
                       8, 0.0f}); // kMaxVoices channels
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_ids",        .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_gates",      .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_velocities", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_freqs",      .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        const float mod_index_cv = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        float mi = mod_index.value + mod_index_cv;
        if (mi < 0.0f) mi = 0.0f;
        if (mi > 20.0f) mi = 20.0f;

        const float amp = amplitude.value;
        const double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);
        const float dt = static_cast<float>(inv_sr);

        // --- Source selection ---------------------------------------------
        // Priority (highest first):
        //   1. notes_in connected → polyphonic MIDI path
        //   2. scalar gate_cv path (legacy mono)
        const bool midi_driven = ctx->custom_inputs &&
                                 ctx->custom_input_count > 0 &&
                                 ctx->custom_inputs[0] != nullptr;

        if (midi_driven) {
            const auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
            allocator_.process_note_buffer(notes, frame_counter_,
                [this](int slot, int /*note*/, float /*vel*/, uint32_t /*offset*/, uint64_t /*note_id*/) {
                    voices_[slot].carrier_phase = 0.0;
                    voices_[slot].mod_phase     = 0.0;
                    vivid::adsr::gate_on(voices_[slot].envelope);
                },
                [this](int slot, int /*note*/, uint64_t /*note_id*/) {
                    vivid::adsr::gate_off(voices_[slot].envelope);
                },
                [](int /*slot*/, VividNoteEventType /*kind*/, float /*value*/) {
                    // Per-note expression updates are stored on the slot by
                    // the allocator (pitch_bend_semis, pressure, timbre). The
                    // render loop reads them below.
                });

            // Build the slot → voices_out channel mapping by sorting active
            // slots by note_id. Voices that aren't active map to -1 and are
            // skipped in the render loop. This is the cross-cutting #4
            // contract: breakout alignment by active-note order sorted by
            // note_id, identical to what emit_voice_breakouts will publish.
            int slot_to_pos[kMaxVoices];
            int sorted[kMaxVoices];
            int active_count = 0;
            for (int v = 0; v < kMaxVoices; ++v) {
                slot_to_pos[v] = -1;
                if (allocator_.slots[v].active) sorted[active_count++] = v;
            }
            std::sort(sorted, sorted + active_count,
                      [this](int a, int b) {
                          return allocator_.slots[a].note_id <
                                 allocator_.slots[b].note_id;
                      });
            for (int i = 0; i < active_count; ++i) slot_to_pos[sorted[i]] = i;

            // Zero voices_out (port 1) so unused channels are silent.
            float* voices_out_buf = (ctx->output_buffers && ctx->output_buffers[1])
                                    ? ctx->output_buffers[1] : nullptr;
            if (voices_out_buf) {
                std::memset(voices_out_buf, 0,
                            static_cast<size_t>(kMaxVoices) * frames * sizeof(float));
            }

            const float p_amp_depth = pressure_to_amp.value;
            const float t_mi_depth  = timbre_to_mod_index.value;
            for (uint32_t i = 0; i < frames; ++i) {
                float sample = 0.0f;
                for (int v = 0; v < kMaxVoices; ++v) {
                    auto& slot = allocator_.slots[v];
                    if (!slot.active) continue;
                    auto& vs = voices_[v];

                    vivid::adsr::advance(vs.envelope, dt, attack.value, decay.value,
                                         sustain.value, release.value);

                    const float voice_freq = 440.0f *
                        std::pow(2.0f, (static_cast<float>(slot.note) - 69.0f
                                        + slot.pitch_bend_semis) / 12.0f);
                    const float mod_freq = voice_freq * mod_ratio.value;

                    // Per-note expression: pressure scales amplitude,
                    // timbre scales modulation index (harmonic richness).
                    const float pressure_scale = 1.0f + p_amp_depth * slot.pressure;
                    const float voice_mi       = mi * (1.0f + t_mi_depth * slot.timbre);

                    const float mod_signal = voice_mi * std::sin(2.0 * M_PI * vs.mod_phase);
                    const float voice_sample =
                        std::sin(2.0 * M_PI * vs.carrier_phase + mod_signal)
                        * vs.envelope.env_value * slot.velocity * pressure_scale;
                    sample += voice_sample;

                    // Mirror the voice into the voices_out breakout channel
                    // at this slot's note_id-sorted rank. Apply the master
                    // amplitude here too so breakout audio matches the mono
                    // mix's per-voice level.
                    if (voices_out_buf && slot_to_pos[v] >= 0) {
                        const int pos = slot_to_pos[v];
                        voices_out_buf[pos * frames + i] = voice_sample * amp;
                    }

                    vs.carrier_phase += static_cast<double>(voice_freq) * inv_sr;
                    if (vs.carrier_phase >= 1.0) vs.carrier_phase -= 1.0;
                    vs.mod_phase += static_cast<double>(mod_freq) * inv_sr;
                    if (vs.mod_phase >= 1.0) vs.mod_phase -= 1.0;

                    if (vs.envelope.stage == vivid::adsr::IDLE) slot.active = false;
                }
                out[i] = sample * amp;
                ++frame_counter_;
            }

            // Emit voice_*/voices_out aligned to active-note-by-note_id order.
            // ctx->value_outputs[] is indexed by overall OUTPUT port position.
            // Output port order: output(0), voices_out(1), voice_ids(2),
            // voice_gates(3), voice_velocities(4), voice_freqs(5). Value-API
            // equivalent of vivid_sequencers::emit_voice_breakouts_from_sorted:
            // resize each lane to active_count, fill in note_id-sorted order,
            // commit. (The shared helper is VividLaneOutput-based; this inlines
            // the same logic over value_outputs without changing behavior.)
            if (ctx->value_outputs) {
                const uint32_t n = static_cast<uint32_t>(active_count);
                auto emit_value_lane = [&](int port, auto value_for_slot) {
                    float* buf = vivid_value_output_floats(
                        &ctx->value_outputs[port], n);
                    if (buf) {
                        for (int i = 0; i < active_count; ++i) {
                            const vivid::VoiceSlot& s =
                                allocator_.slots[sorted[i]];
                            buf[i] = value_for_slot(s);
                        }
                    }
                    vivid_value_output_commit(&ctx->value_outputs[port], n);
                };
                emit_value_lane(2, [](const vivid::VoiceSlot& s) {
                    return static_cast<float>(s.note_id);
                });
                emit_value_lane(3, [](const vivid::VoiceSlot& s) {
                    return s.gate ? 1.0f : 0.0f;
                });
                emit_value_lane(4, [](const vivid::VoiceSlot& s) {
                    return s.velocity;
                });
                emit_value_lane(5, [](const vivid::VoiceSlot& s) {
                    return vivid_sequencers::voice_freq_hz(s);
                });
            }
            return;
        }

        // --- Legacy monophonic scalar-CV path -----------------------------
        float freq_cv = ctx->input_buffers[0] ? ctx->input_buffers[0][0] : 0.0f;
        float gate_cv = ctx->input_buffers[2] ? ctx->input_buffers[2][0] : 0.0f;
        float vel_scale = 1.0f;

        float freq = carrier_freq.value * std::pow(2.0f, freq_cv / 12.0f);
        if (freq < 20.0f)    freq = 20.0f;
        if (freq > 20000.0f) freq = 20000.0f;
        const float mod_freq = freq * mod_ratio.value;

        const bool gate_on  = gate_cv > 0.5f;
        const bool prev_on  = prev_gate_ > 0.5f;
        if (gate_on && !prev_on) vivid::adsr::gate_on(mono_env_);
        if (!gate_on && prev_on) vivid::adsr::gate_off(mono_env_);
        prev_gate_ = gate_cv;

        for (uint32_t i = 0; i < frames; ++i) {
            vivid::adsr::advance(mono_env_, dt, attack.value, decay.value,
                                 sustain.value, release.value);

            const float mod_signal = mi * std::sin(2.0 * M_PI * mono_mod_phase_);
            const float sample = std::sin(2.0 * M_PI * mono_carrier_phase_ + mod_signal);
            out[i] = sample * mono_env_.env_value * amp * vel_scale;

            mono_carrier_phase_ += static_cast<double>(freq) * inv_sr;
            if (mono_carrier_phase_ >= 1.0) mono_carrier_phase_ -= 1.0;
            mono_mod_phase_ += static_cast<double>(mod_freq) * inv_sr;
            if (mono_mod_phase_ >= 1.0) mono_mod_phase_ -= 1.0;
            ++frame_counter_;
        }
    }
};

VIVID_DEFINE_OP(FmSynth) {
}

