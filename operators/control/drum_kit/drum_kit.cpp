#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include <cstdint>
#include <cstring>

struct DrumKit : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "DrumKit";
    static constexpr bool kTimeDependent = false;
    static constexpr int kSlotCount = 8;
    static constexpr int kMaxActive = 64;

    vivid::Param<int> note_0{"note_0", 36, 0, 127};
    vivid::Param<int> note_1{"note_1", 38, 0, 127};
    vivid::Param<int> note_2{"note_2", 42, 0, 127};
    vivid::Param<int> note_3{"note_3", 46, 0, 127};
    vivid::Param<int> note_4{"note_4", 39, 0, 127};
    vivid::Param<int> note_5{"note_5", 45, 0, 127};
    vivid::Param<int> note_6{"note_6", 37, 0, 127};
    vivid::Param<int> note_7{"note_7", 40, 0, 127};

    VividNoteBuffer slot_bufs_[kSlotCount];

    // Active id → slot routing table. Persists across buffers so expression
    // events that arrive after the originating NOTE_ON reach the same slot.
    struct ActiveId { uint64_t note_id; int slot; };
    ActiveId active_[kMaxActive] = {};
    int active_count_ = 0;

    DrumKit() {
        vivid::description(note_0, "MIDI note number for slot 0 (default: kick)");
        vivid::description(note_1, "MIDI note number for slot 1 (default: snare)");
        vivid::description(note_2, "MIDI note number for slot 2 (default: closed hi-hat)");
        vivid::description(note_3, "MIDI note number for slot 3 (default: open hi-hat)");
        vivid::description(note_4, "MIDI note number for slot 4 (default: clap)");
        vivid::description(note_5, "MIDI note number for slot 5 (default: tom)");
        vivid::description(note_6, "MIDI note number for slot 6 (default: cross stick)");
        vivid::description(note_7, "MIDI note number for slot 7 (default: high tom)");
    }

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
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_0", VIVID_PORT_OUTPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_1", VIVID_PORT_OUTPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_2", VIVID_PORT_OUTPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_3", VIVID_PORT_OUTPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_4", VIVID_PORT_OUTPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_5", VIVID_PORT_OUTPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_6", VIVID_PORT_OUTPUT, VividNoteBuffer));
        out.push_back(VIVID_CUSTOM_REF_PORT("slot_7", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    void process_audio(const VividAudioContext* ctx) override {
        const VividNoteBuffer* notes_in = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0])
            notes_in = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
        route(notes_in, ctx->param_values, ctx->custom_outputs, ctx->custom_output_count);
    }

private:
    void emit_to_slot(int slot, const VividNoteEvent& ev) {
        auto& buf = slot_bufs_[slot];
        if (buf.count < VIVID_NOTE_BUFFER_CAPACITY)
            buf.events[buf.count++] = ev;
    }

    int slot_for_id(uint64_t note_id) const {
        for (int i = 0; i < active_count_; ++i)
            if (active_[i].note_id == note_id) return active_[i].slot;
        return -1;
    }

    void track_id(uint64_t note_id, int slot) {
        // Replace existing entry if present.
        for (int i = 0; i < active_count_; ++i) {
            if (active_[i].note_id == note_id) {
                active_[i].slot = slot;
                return;
            }
        }
        if (active_count_ < kMaxActive)
            active_[active_count_++] = {note_id, slot};
    }

    void untrack_id(uint64_t note_id) {
        for (int i = 0; i < active_count_; ++i) {
            if (active_[i].note_id == note_id) {
                active_[i] = active_[--active_count_];
                return;
            }
        }
    }

    void route(const VividNoteBuffer* notes_in, const float* params,
               void** custom_outputs, uint32_t custom_output_count) {
        for (int s = 0; s < kSlotCount; ++s)
            slot_bufs_[s].count = 0;

        if (notes_in) {
            int note_map[kSlotCount];
            for (int s = 0; s < kSlotCount; ++s)
                note_map[s] = static_cast<int>(params[s]);

            for (uint32_t m = 0; m < notes_in->count; ++m) {
                const auto& ev = notes_in->events[m];
                if (ev.note_id == 0) continue;  // global stream — drums ignore

                if (ev.type == VIVID_NOTE_ON) {
                    for (int s = 0; s < kSlotCount; ++s) {
                        if (ev.note_number == static_cast<uint8_t>(note_map[s])) {
                            emit_to_slot(s, ev);
                            track_id(ev.note_id, s);
                            break;
                        }
                    }
                } else if (ev.type == VIVID_NOTE_OFF) {
                    int s = slot_for_id(ev.note_id);
                    if (s >= 0) {
                        emit_to_slot(s, ev);
                        untrack_id(ev.note_id);
                    }
                } else {
                    // Expression event — route to the slot that owns the id.
                    int s = slot_for_id(ev.note_id);
                    if (s >= 0)
                        emit_to_slot(s, ev);
                }
            }
        }

        if (custom_outputs) {
            for (int s = 0; s < kSlotCount && s < static_cast<int>(custom_output_count); ++s)
                custom_outputs[s] = &slot_bufs_[s];
        }
    }
};

VIVID_REGISTER(DrumKit)
