#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

inline bool midi_note_on(VividMidiBuffer& buf, uint8_t note, uint8_t velocity,
                         uint8_t channel = 0, uint32_t frame_offset = 0) {
    if (buf.count >= VIVID_MIDI_BUFFER_CAPACITY) return false;
    VividMidiMessage& msg = buf.messages[buf.count++];
    msg.status = static_cast<uint8_t>(0x90 | (channel & 0x0F));
    msg.data1 = note;
    msg.data2 = velocity;
    msg.reserved = 0;
    msg.frame_offset_samples = frame_offset;
    return true;
}

inline bool midi_note_off(VividMidiBuffer& buf, uint8_t note,
                          uint8_t channel = 0, uint32_t frame_offset = 0) {
    if (buf.count >= VIVID_MIDI_BUFFER_CAPACITY) return false;
    VividMidiMessage& msg = buf.messages[buf.count++];
    msg.status = static_cast<uint8_t>(0x80 | (channel & 0x0F));
    msg.data1 = note;
    msg.data2 = 0;
    msg.reserved = 0;
    msg.frame_offset_samples = frame_offset;
    return true;
}

} // anonymous namespace

struct Sequencer : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Sequencer";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> steps        {"steps",        8, 1, 128};
    vivid::Param<int> midi_channel {"midi_channel", 1, 1, 16};

    Sequencer() {
        vivid::semantic_tag(steps, "count");
        vivid::semantic_shape(steps, "int");
        vivid::semantic_intent(steps, "sequence_length");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);
        out.push_back(&midi_channel);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_FLOAT,  VIVID_PORT_INPUT});
        out.push_back({"reset",     VIVID_PORT_FLOAT,  VIVID_PORT_INPUT});
        out.push_back({"values",    VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"probs",     VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"ratchets",  VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"value",     VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});
        out.push_back({"step",      VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});
        out.push_back({"trigger",   VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void process_audio(const VividAudioContext* ctx) override {
        float phase = ctx->input_float_values[0];
        bool reset = ctx->input_float_values[1] > 0.5f;

        // Read spread inputs
        const float* val_data = nullptr;
        uint32_t val_len = 0;
        const float* prob_data = nullptr;
        uint32_t prob_len = 0;
        const float* ratch_data = nullptr;
        uint32_t ratch_len = 0;
        if (ctx->input_spreads) {
            const auto& val_sp = ctx->input_spreads[0];   // values
            val_len = val_sp.length;
            val_data = val_sp.data;
            const auto& prob_sp = ctx->input_spreads[1];  // probs
            prob_len = prob_sp.length;
            prob_data = prob_sp.data;
            const auto& ratch_sp = ctx->input_spreads[2]; // ratchets
            ratch_len = ratch_sp.length;
            ratch_data = ratch_sp.data;
        }

        // Rising-edge reset: capture current phase as offset
        if (reset && !prev_reset_)
            phase_offset_ = phase;
        prev_reset_ = reset;

        float adj_phase = std::fmod(phase - phase_offset_ + 1.0f, 1.0f);

        // Clamp steps to values spread length
        int n = std::min(steps.int_value(), static_cast<int>(val_len));
        if (n < 1) n = 1;

        float scaled = adj_phase * static_cast<float>(n);
        int step = static_cast<int>(scaled);
        step = std::clamp(step, 0, n - 1);

        bool step_changed = (step != prev_step_);
        if (step_changed) {
            float prob = (step < static_cast<int>(prob_len) && prob_data)
                ? std::clamp(prob_data[step], 0.0f, 1.0f)
                : 1.0f;
            step_active_ = (prob >= 1.0f) || (next_random() < prob);

            int raw_ratchet = (step < static_cast<int>(ratch_len) && ratch_data)
                ? static_cast<int>(std::lround(ratch_data[step]))
                : 1;
            current_ratchet_ = std::clamp(raw_ratchet, 1, 8);
            prev_ratchet_index_ = -1;
            prev_step_ = step;
        }

        float value = 0.0f;
        if (step_active_ && val_data && step < static_cast<int>(val_len)) {
            value = val_data[step];
        }

        float step_phase = std::clamp(scaled - static_cast<float>(step), 0.0f, 0.999999f);
        int ratchet_index = static_cast<int>(step_phase * static_cast<float>(current_ratchet_));
        ratchet_index = std::clamp(ratchet_index, 0, current_ratchet_ - 1);
        bool ratchet_trigger = (ratchet_index != prev_ratchet_index_);
        if (ratchet_trigger) prev_ratchet_index_ = ratchet_index;

        bool trigger = step_active_ && ratchet_trigger;
        ctx->output_float_values[0] = value;
        ctx->output_float_values[1] = static_cast<float>(step);
        ctx->output_float_values[2] = trigger ? 1.0f : 0.0f;

        // MIDI output: note-on on trigger, legato note-off before new note
        uint8_t ch = static_cast<uint8_t>(midi_channel.int_value() - 1);
        midi_buf_.count = 0;
        if (trigger) {
            uint8_t note = static_cast<uint8_t>(std::clamp(static_cast<int>(value), 0, 127));
            if (prev_midi_note_ >= 0) {
                midi_note_off(midi_buf_,
                    static_cast<uint8_t>(prev_midi_note_), ch);
            }
            midi_note_on(midi_buf_, note, 100, ch);
            prev_midi_note_ = note;
        }
        if (ctx->custom_outputs && ctx->custom_output_count > 0) {
            ctx->custom_outputs[0] = &midi_buf_;
        }
    }

private:
    float next_random() {
        rng_state_ ^= (rng_state_ << 13);
        rng_state_ ^= (rng_state_ >> 17);
        rng_state_ ^= (rng_state_ << 5);
        constexpr float kInvMax = 1.0f / 4294967295.0f;
        return static_cast<float>(rng_state_) * kInvMax;
    }

    int prev_step_ = -1;
    float phase_offset_ = 0.0f;
    bool prev_reset_ = false;
    bool step_active_ = true;
    int current_ratchet_ = 1;
    int prev_ratchet_index_ = -1;
    uint32_t rng_state_ = 0xA5C31E59u;
    int prev_midi_note_ = -1;
    VividMidiBuffer midi_buf_ = {};
};

VIVID_REGISTER(Sequencer)
