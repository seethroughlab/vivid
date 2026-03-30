#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include <cstdint>
#include <cstring>

// DrumKit — MIDI-to-per-slot-MIDI demuxer.
//
// Takes a single MIDI stream and fans it out to per-slot MIDI outputs,
// filtering note-on/off messages by configurable note number per slot.
// Non-note messages (CC, etc.) are forwarded to all slots.
//
// Analogous to Ableton's Drum Rack: each slot maps a MIDI note to an
// output that can drive any audio operator (DrumKick, Sampler, etc.).
//
struct DrumKit : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName = "DrumKit";
    static constexpr bool kTimeDependent = false;
    static constexpr int kSlotCount = 8;

    // GM drum mapping defaults
    vivid::Param<int> note_0{"note_0", 36, 0, 127};  // Kick
    vivid::Param<int> note_1{"note_1", 38, 0, 127};  // Snare
    vivid::Param<int> note_2{"note_2", 42, 0, 127};  // Closed HH
    vivid::Param<int> note_3{"note_3", 46, 0, 127};  // Open HH
    vivid::Param<int> note_4{"note_4", 39, 0, 127};  // Clap
    vivid::Param<int> note_5{"note_5", 45, 0, 127};  // Tom
    vivid::Param<int> note_6{"note_6", 37, 0, 127};  // Cross stick
    vivid::Param<int> note_7{"note_7", 40, 0, 127};  // High tom

    VividMidiBuffer slot_bufs_[kSlotCount];

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&note_0);
        out.push_back(&note_1);
        out.push_back(&note_2);
        out.push_back(&note_3);
        out.push_back(&note_4);
        out.push_back(&note_5);
        out.push_back(&note_6);
        out.push_back(&note_7);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_in", VIVID_PORT_INPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_0", VIVID_PORT_OUTPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_1", VIVID_PORT_OUTPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_2", VIVID_PORT_OUTPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_3", VIVID_PORT_OUTPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_4", VIVID_PORT_OUTPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_5", VIVID_PORT_OUTPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_6", VIVID_PORT_OUTPUT, VividMidiBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_7", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void process_frame(const VividFrameContext* ctx) override {
        const VividMidiBuffer* midi_in = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0])
            midi_in = static_cast<const VividMidiBuffer*>(ctx->custom_inputs[0]);
        route(midi_in, ctx->param_values, ctx->custom_outputs, ctx->custom_output_count);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const VividMidiBuffer* midi_in = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0])
            midi_in = static_cast<const VividMidiBuffer*>(ctx->custom_inputs[0]);
        route(midi_in, ctx->param_values, ctx->custom_outputs, ctx->custom_output_count);
    }

    void route(const VividMidiBuffer* midi_in, const float* params,
               void** custom_outputs, uint32_t custom_output_count) {
        // Clear all slot buffers
        for (int s = 0; s < kSlotCount; ++s)
            slot_bufs_[s].count = 0;

        if (midi_in) {
            // Build note-to-slot lookup from params
            int note_map[kSlotCount];
            for (int s = 0; s < kSlotCount; ++s)
                note_map[s] = static_cast<int>(params[s]);

            for (uint32_t m = 0; m < midi_in->count; ++m) {
                const auto& msg = midi_in->messages[m];
                uint8_t status_type = msg.status & 0xF0;

                if (status_type == 0x90 || status_type == 0x80) {
                    // Note-on or note-off: route to matching slot
                    for (int s = 0; s < kSlotCount; ++s) {
                        if (msg.data1 == static_cast<uint8_t>(note_map[s])) {
                            auto& buf = slot_bufs_[s];
                            if (buf.count < VIVID_MIDI_BUFFER_CAPACITY)
                                buf.messages[buf.count++] = msg;
                            break;
                        }
                    }
                } else {
                    // Non-note messages (CC, etc.): forward to all slots
                    for (int s = 0; s < kSlotCount; ++s) {
                        auto& buf = slot_bufs_[s];
                        if (buf.count < VIVID_MIDI_BUFFER_CAPACITY)
                            buf.messages[buf.count++] = msg;
                    }
                }
            }
        }

        // Publish slot buffers
        if (custom_outputs) {
            for (int s = 0; s < kSlotCount && s < static_cast<int>(custom_output_count); ++s)
                custom_outputs[s] = &slot_bufs_[s];
        }
    }
};

VIVID_REGISTER(DrumKit)
