#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"

struct PhaseToMidiFr : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "phase_to_midi_fr";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   note    {"note",     60,  0, 127};
    vivid::Param<float> velocity{"velocity", 100.0f, 0.0f, 127.0f};

    float prev_phase_ = 0.0f;
    VividMidiBuffer midi_buf_ = {};

    PhaseToMidiFr() {
        vivid::semantic_tag(note, "midi_note");
        vivid::semantic_shape(note, "int");
        vivid::description(note, "MIDI note number to emit on each beat, 0 to 127");

        vivid::semantic_tag(velocity, "midi_velocity");
        vivid::semantic_shape(velocity, "scalar");
        vivid::description(velocity, "MIDI velocity of the emitted note, 0 to 127");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&note);
        out.push_back(&velocity);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void process_frame(const VividFrameContext* ctx) override {
        float phase = ctx->input_values[0];
        float delta = phase - prev_phase_;
        prev_phase_ = phase;

        midi_buf_.count = 0;

        if (delta < -0.5f) {
            uint8_t n = static_cast<uint8_t>(std::clamp(note.int_value(), 0, 127));
            uint8_t v = static_cast<uint8_t>(std::clamp(static_cast<int>(velocity.value), 0, 127));
            auto& msg = midi_buf_.messages[0];
            msg.status = 0x90;
            msg.data1  = n;
            msg.data2  = v;
            msg.reserved = 0;
            msg.frame_offset_samples = 0;
            midi_buf_.count = 1;
        }

        if (ctx->custom_outputs && ctx->custom_output_count > 0) {
            ctx->custom_outputs[0] = &midi_buf_;
        }
    }
};

VIVID_REGISTER(PhaseToMidiFr)
