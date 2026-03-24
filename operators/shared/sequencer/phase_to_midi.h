#pragma once

#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"

// ---------------------------------------------------------------------------
// PhaseToMidi: Converts a [0,1) beat_phase signal into MIDI note-on messages.
//
// Detects phase wraps (same as Clock -> Sequencer convention) and emits
// a note-on for the configured note/velocity on each wrap. This bridges
// the Clock Phase layer to the MIDI layer for operators that only accept
// midi_in (e.g., drum operators after the unified triggering convention).
// ---------------------------------------------------------------------------

struct PhaseToMidi : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "PhaseToMidi";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   note    {"note",     60,  0, 127};
    vivid::Param<float> velocity{"velocity", 100.0f, 0.0f, 127.0f};

    float prev_phase_ = 0.0f;
    VividMidiBuffer midi_buf_ = {};

    PhaseToMidi() {
        vivid::semantic_tag(note, "midi_note");
        vivid::semantic_shape(note, "int");

        vivid::semantic_tag(velocity, "midi_velocity");
        vivid::semantic_shape(velocity, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&note);
        out.push_back(&velocity);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void process(const VividProcessContext* ctx) override {
        float phase = ctx->input_values[0];
        float delta = phase - prev_phase_;
        prev_phase_ = phase;

        midi_buf_.count = 0;

        // Phase wrap detection (same convention as audio_dsp::detect_trigger)
        if (delta < -0.5f) {
            uint8_t n = static_cast<uint8_t>(std::clamp(note.int_value(), 0, 127));
            uint8_t v = static_cast<uint8_t>(std::clamp(static_cast<int>(velocity.value), 0, 127));
            auto& msg = midi_buf_.messages[0];
            msg.status = 0x90;  // Note On, channel 1
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
