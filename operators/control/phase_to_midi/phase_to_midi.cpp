#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "control/audio_scalar_utils.h"
#include "note_helpers.h"
#include "note_id_counter.h"

struct PhaseToMidi : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "PhaseToMidi";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   note    {"note",     60,  0, 127};
    vivid::Param<float> velocity{"velocity", 100.0f, 0.0f, 127.0f};
    vivid::Param<int>   clock_mode{"clock_mode", vivid::kClockModeSyncedMetronome, vivid::clock_mode_synced_labels()};

    float prev_phase_ = 0.0f;
    VividNoteBuffer notes_buf_ = {};

    PhaseToMidi() {
        vivid::semantic_tag(note, "midi_note");
        vivid::semantic_shape(note, "int");
        vivid::description(note, "MIDI note number to emit on each beat, 0 to 127");

        vivid::semantic_tag(velocity, "midi_velocity");
        vivid::semantic_shape(velocity, "scalar");
        vivid::description(velocity, "MIDI velocity of the emitted note, 0 to 127");
        vivid::description(clock_mode, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&note);
        out.push_back(&velocity);
        out.push_back(&clock_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "beat_phase"});
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    void process_audio(const VividAudioContext* ctx) override {
        notes_buf_.count = 0;
        const vivid::MetronomeTransport metronome = vivid::metronome_transport(ctx);

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            vivid::MetronomeTransport sample_metronome =
                vivid::metronome_transport_sample(metronome, i, ctx->sample_rate);
            float phase = vivid::resolve_clock_phase(
                clock_mode.int_value(), vivid::audio_scalar_sample(ctx, 0, i), sample_metronome);
            float delta = phase - prev_phase_;
            prev_phase_ = phase;

            if (delta < -0.5f) {
                uint8_t n = static_cast<uint8_t>(std::clamp(note.int_value(), 0, 127));
                float v = std::clamp(velocity.value / 127.0f, 0.0f, 1.0f);
                vivid_sequencers::note_on(notes_buf_, n, v,
                                          vivid_sequencers::next_note_id(), i);
            }
        }

        if (ctx->custom_outputs && ctx->custom_output_count > 0) {
            ctx->custom_outputs[0] = &notes_buf_;
        }
    }
};

VIVID_DEFINE_OP(PhaseToMidi) {
}

