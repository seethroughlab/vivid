#include "operator_api/operator.h"
#include "RtMidi.h"
#include <mutex>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdio>

struct MidiInput : vivid::OperatorBase {
    static constexpr const char* kName   = "MidiInput";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    // Params (order matters — indices used for CC learn write-back)
    vivid::Param<int>  device   {"device",    0, 0, 15};    // [0]
    vivid::Param<int>  channel  {"channel",   0, 0, 16};    // [1] 0 = omni
    vivid::Param<int>  cc_number{"cc_number", 1, 0, 127};   // [2]
    vivid::Param<bool> learn    {"learn",     false};        // [3]

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&device);
        out.push_back(&channel);
        out.push_back(&cc_number);
        out.push_back(&learn);
    }

    // Output ports: scalar note/vel/gate/trigger/pitch_bend/mod_wheel/cc_value + spread notes/velocities/gates
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"note",       VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [0]
        out.push_back({"velocity",   VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [1]
        out.push_back({"gate",       VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [2]
        out.push_back({"trigger",    VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [3]
        out.push_back({"pitch_bend", VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [4]
        out.push_back({"mod_wheel",  VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [5]
        out.push_back({"cc_value",   VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [6]
        out.push_back({"notes",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // [7]
        out.push_back({"velocities", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // [8]
        out.push_back({"gates",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // [9]
    }

    MidiInput() {
        std::memset(cc_values_, 0, sizeof(cc_values_));
    }

    void process(const VividProcessContext* ctx) override {
        ensure_midi_initialized();
        int desired_device = device.int_value();

        // Open/reopen port if device param changed
        if (midi_in_ && desired_device != current_device_) {
            if (current_device_ >= 0) {
                midi_in_->closePort();
            }
            unsigned int port_count = midi_in_->getPortCount();
            if (desired_device >= 0 && static_cast<unsigned>(desired_device) < port_count) {
                try {
                    midi_in_->openPort(static_cast<unsigned>(desired_device));
                    fprintf(stderr, "[MidiInput] Opened port %d: %s\n",
                            desired_device,
                            midi_in_->getPortName(static_cast<unsigned>(desired_device)).c_str());
                } catch (RtMidiError& e) {
                    fprintf(stderr, "[MidiInput] Error opening port %d: %s\n",
                            desired_device, e.getMessage().c_str());
                }
            }
            current_device_ = desired_device;
        }

        // Drain event buffer
        std::vector<std::vector<unsigned char>> events;
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            events.swap(event_buffer_);
        }

        int chan_filter = channel.int_value();  // 0 = omni
        bool had_note_on = false;

        for (const auto& msg : events) {
            if (msg.size() < 1) continue;

            unsigned char status = msg[0];
            unsigned char msg_type = status & 0xF0;
            int msg_chan = (status & 0x0F) + 1;  // 1-based channel

            // Channel filter (0 = omni = accept all)
            if (chan_filter != 0 && msg_chan != chan_filter) continue;

            if (msg_type == 0x90 && msg.size() >= 3) {
                // Note On
                unsigned char note = msg[1];
                unsigned char vel  = msg[2];
                if (vel > 0) {
                    last_note_ = note;
                    last_velocity_ = static_cast<float>(vel) / 127.0f;
                    had_note_on = true;
                    // Add to held buffer (update velocity if already present)
                    held_note_on(note, static_cast<float>(vel) / 127.0f);
                    fprintf(stderr, "[MidiInput] Note ON: %d vel=%d\n", note, vel);
                } else {
                    // Note On with vel=0 is Note Off
                    held_note_off(note);
                    fprintf(stderr, "[MidiInput] Note OFF: %d (vel=0)\n", note);
                }
            } else if (msg_type == 0x80 && msg.size() >= 3) {
                // Note Off
                unsigned char note = msg[1];
                held_note_off(note);
                fprintf(stderr, "[MidiInput] Note OFF: %d\n", note);
            } else if (msg_type == 0xB0 && msg.size() >= 3) {
                // Control Change
                unsigned char cc  = msg[1];
                unsigned char val = msg[2];
                cc_values_[cc] = static_cast<float>(val) / 127.0f;

                // CC Learn
                if (learn.bool_value()) {
                    ctx->param_values[2] = static_cast<float>(cc);  // cc_number
                    ctx->param_values[3] = 0.0f;                    // learn = false
                    fprintf(stderr, "[MidiInput] CC Learn: captured CC %d\n", cc);
                }
            } else if (msg_type == 0xE0 && msg.size() >= 3) {
                // Pitch Bend
                int bend_raw = (static_cast<int>(msg[2]) << 7) | static_cast<int>(msg[1]);
                pitch_bend_ = static_cast<float>(bend_raw - 8192) / 8192.0f;
            }
        }

        // Write scalar outputs
        int cc_idx = cc_number.int_value();
        if (cc_idx < 0) cc_idx = 0;
        if (cc_idx > 127) cc_idx = 127;

        ctx->output_values[0] = static_cast<float>(last_note_);        // note
        ctx->output_values[1] = last_velocity_;                         // velocity
        ctx->output_values[2] = (held_count_ > 0) ? 1.0f : 0.0f;      // gate
        ctx->output_values[3] = had_note_on ? 1.0f : 0.0f;             // trigger
        ctx->output_values[4] = pitch_bend_;                            // pitch_bend
        ctx->output_values[5] = cc_values_[1];                          // mod_wheel (CC1)
        ctx->output_values[6] = cc_values_[cc_idx];                     // cc_value

        // Write spread outputs: all currently held notes
        if (ctx->output_spreads) {
            auto& notes_sp = ctx->output_spreads[7];
            auto& vel_sp   = ctx->output_spreads[8];
            auto& gates_sp = ctx->output_spreads[9];

            uint32_t len = static_cast<uint32_t>(held_count_);
            if (notes_sp.capacity >= len) {
                notes_sp.length = len;
                vel_sp.length   = len;
                gates_sp.length = len;
                for (uint32_t i = 0; i < len; ++i) {
                    notes_sp.data[i] = static_cast<float>(held_buffer_[i].note);
                    vel_sp.data[i]   = held_buffer_[i].velocity;
                    gates_sp.data[i] = 1.0f;
                }
            }
        }
    }

private:
    static constexpr int kMaxHeld = 16;

    struct HeldNote {
        uint8_t note;
        float   velocity;
    };

    std::unique_ptr<RtMidiIn> midi_in_;
    bool midi_init_attempted_ = false;
    std::mutex event_mutex_;
    std::vector<std::vector<unsigned char>> event_buffer_;
    float cc_values_[128];
    int current_device_ = -1;
    int last_note_ = 0;
    float last_velocity_ = 0.0f;
    float pitch_bend_ = 0.0f;

    HeldNote held_buffer_[kMaxHeld] = {};
    int held_count_ = 0;

    void ensure_midi_initialized() {
        if (midi_init_attempted_) return;
        midi_init_attempted_ = true;

        try {
            midi_in_ = std::make_unique<RtMidiIn>();

            // Log available ports once on first real processing pass.
            unsigned int port_count = midi_in_->getPortCount();
            fprintf(stderr, "[MidiInput] Available MIDI ports (%u):\n", port_count);
            for (unsigned int i = 0; i < port_count; i++) {
                fprintf(stderr, "  [%u] %s\n", i, midi_in_->getPortName(i).c_str());
            }

            midi_in_->setCallback(&MidiInput::midi_callback, this);
            midi_in_->ignoreTypes(true, true, true);  // ignore sysex, timing, active sensing
        } catch (RtMidiError& e) {
            fprintf(stderr, "[MidiInput] RtMidi init error: %s\n", e.getMessage().c_str());
            midi_in_.reset();
        } catch (...) {
            fprintf(stderr, "[MidiInput] Unknown MIDI init error\n");
            midi_in_.reset();
        }
    }

    void held_note_on(uint8_t note, float velocity) {
        // Update velocity if already held
        for (int i = 0; i < held_count_; ++i) {
            if (held_buffer_[i].note == note) {
                held_buffer_[i].velocity = velocity;
                return;
            }
        }
        // Add if room
        if (held_count_ < kMaxHeld) {
            held_buffer_[held_count_].note = note;
            held_buffer_[held_count_].velocity = velocity;
            held_count_++;
        }
    }

    void held_note_off(uint8_t note) {
        for (int i = 0; i < held_count_; ++i) {
            if (held_buffer_[i].note == note) {
                // Shift remaining down
                for (int j = i; j < held_count_ - 1; ++j) {
                    held_buffer_[j] = held_buffer_[j + 1];
                }
                held_count_--;
                return;
            }
        }
    }

    static void midi_callback(double /*timestamp*/,
                               std::vector<unsigned char>* message,
                               void* user_data) {
        auto* self = static_cast<MidiInput*>(user_data);
        if (message && !message->empty()) {
            std::lock_guard<std::mutex> lock(self->event_mutex_);
            self->event_buffer_.push_back(*message);
        }
    }
};

VIVID_REGISTER(MidiInput)
