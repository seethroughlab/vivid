#include "arpeggiator_core.h"
#include "control/audio_scalar_utils.h"
#include "shared/sequencer/note_helpers.h"

#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

struct Arpeggiator : ArpeggiatorCore, vivid::AudioProcessable {
    static constexpr const char* kName = "Arpeggiator";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[4] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        const VividNoteBuffer* notes_in = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0)
            notes_in = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);

        // Drain any synthetic MIDI bytes pushed by the runtime debug hook
        // (vivid_op_inject_midi). Translate to native NOTE_ON/OFF events,
        // merge with the wire's notes_in (if any), and feed the unioned
        // buffer to compute(). This keeps a single point of truth for note
        // handling.
        VividNoteBuffer merged{};
        const VividNoteBuffer* effective_in = notes_in;
        if (drain_inject_into_buffer(merged, notes_in)) {
            effective_in = &merged;
        }

        compute(beat_phase, ctx->param_values, effective_in,
                local_out,
                ctx->custom_outputs, ctx->custom_output_count);
        // SCALAR outputs (note/vel/gate/step) are now ports [0..3] — the
        // legacy LANE_ARRAY note/vel/gate outputs were removed in PR3.
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }

    // Test/debug seam — pushed via the optional vivid_op_inject_midi symbol
    // (probed by the runtime via dlsym; see operator_loader.cpp).
    void inject_events(const std::vector<std::vector<unsigned char>>& messages) {
        std::lock_guard<std::mutex> lock(inject_mutex_);
        for (const auto& m : messages)
            inject_buffer_.push_back(m);
    }

private:
    // Returns true iff at least one injected event was merged into `out`.
    // When existing non-null notes_in events are present, they are copied
    // first so caller-emitted events still take effect.
    bool drain_inject_into_buffer(VividNoteBuffer& out,
                                   const VividNoteBuffer* in) {
        std::vector<std::vector<unsigned char>> drained;
        {
            std::lock_guard<std::mutex> lock(inject_mutex_);
            drained.swap(inject_buffer_);
        }
        if (drained.empty()) return false;

        out.count = 0;
        if (in) {
            uint32_t n = in->count;
            if (n > VIVID_NOTE_BUFFER_CAPACITY) n = VIVID_NOTE_BUFFER_CAPACITY;
            for (uint32_t i = 0; i < n; ++i) out.events[out.count++] = in->events[i];
        }

        for (const auto& msg : drained) {
            if (msg.size() < 1) continue;
            uint8_t status = msg[0];
            uint8_t type = status & 0xF0;
            uint8_t chan = status & 0x0F;  // 0-based for keying

            if (type == 0x90 && msg.size() >= 3 && msg[2] > 0) {
                uint8_t note = msg[1];
                float vel = static_cast<float>(msg[2]) / 127.0f;
                uint64_t id = ++inject_note_counter_;
                inject_held_id_[chan_note_key(chan, note)] = id;
                vivid_sequencers::note_on(out, note, vel, id);
            } else if ((type == 0x80 && msg.size() >= 3) ||
                       (type == 0x90 && msg.size() >= 3 && msg[2] == 0)) {
                uint8_t note = msg[1];
                auto key = chan_note_key(chan, note);
                auto it = inject_held_id_.find(key);
                if (it != inject_held_id_.end()) {
                    vivid_sequencers::note_off(out, it->second);
                    inject_held_id_.erase(it);
                }
            }
            // CC / pitch-bend / aftertouch are out of scope for inject —
            // the debug-tool surface only needs note triggering.
        }
        return true;
    }

    static uint16_t chan_note_key(uint8_t chan, uint8_t note) {
        return static_cast<uint16_t>(chan) << 8 | note;
    }

    std::mutex inject_mutex_;
    std::vector<std::vector<unsigned char>> inject_buffer_;
    // Reserved high bit so injected ids never collide with caller-allocated
    // note_ids on notes_in.
    uint64_t inject_note_counter_ = 0x8000'0000'0000'0000ULL;
    std::unordered_map<uint16_t, uint64_t> inject_held_id_;
};

VIVID_DEFINE_OP(Arpeggiator) {
}

VIVID_THUMBNAIL(Arpeggiator)
VIVID_EDITOR(Arpeggiator)

// Optional debug-inject hook (probed by OperatorLoader via dlsym at load
// time). Lets the runtime push synthetic MIDI bytes — same mechanism as
// MidiInput — for use by capture_note_response et al.
extern "C" void vivid_op_inject_midi(void* instance, const uint8_t* bytes,
                                       uint32_t count) {
    if (!instance || !bytes || count == 0) return;
    // op is the first member of _VividInstance at offset 0.
    auto* op = reinterpret_cast<Arpeggiator*>(instance);
    std::vector<unsigned char> msg(bytes, bytes + count);
    op->inject_events({std::move(msg)});
}
