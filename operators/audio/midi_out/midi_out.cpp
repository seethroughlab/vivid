#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "RtMidi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Sends note events to a system MIDI output port.
 *
 * Accepts the same VividNoteBuffer wire format produced by MidiClip, MidiInput,
 * and all internal sequencers, and serializes those events as MIDI 1.0 bytes to
 * a selected system MIDI output port. Enables driving external hardware synths,
 * standalone apps, or DAWs directly from a Vivid graph.
 *
 * Set `device` to a substring of the target port name (e.g. "IAC Bus 1"), leave
 * empty to use the first available port, or set to `"virtual"` to open a named
 * virtual port visible to other apps.
 *
 * @tip Wire any notes_out from MidiClip, MidiInput, or a sequencer into notes_in.
 * @see MidiClockOut, MidiInput, MidiClip
 */
struct MidiOut : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName        = "MidiOut";
    static constexpr const char* kDisplayName = "MIDI Out";
    static constexpr const char* kSummary     = "Send note events to a system MIDI output port";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::TextValue> device            {"device", ""};
    vivid::Param<int>              channel           {"channel", 1, 1, 16};
    vivid::Param<float>            velocity_scale    {"velocity_scale", 1.0f, 0.0f, 2.0f};
    vivid::Param<vivid::TextValue> virtual_port_name {"virtual_port_name", "Vivid"};

    MidiOut() {
        vivid::description(device,            "MIDI output port name (substring match); empty = first available; \"virtual\" = open a named virtual port");
        vivid::description(channel,           "MIDI channel to send on (1–16)");
        vivid::description(velocity_scale,    "Scale note velocities (1.0 = unchanged, 0 = silent, 2 = double)");
        vivid::description(virtual_port_name, "Name for the virtual port when device is set to \"virtual\"");
    }

    ~MidiOut() override {
        send_all_notes_off();
        close_port();
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&device);
        out.push_back(&channel);
        out.push_back(&velocity_scale);
        out.push_back(&virtual_port_name);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
    }

    void prepare_instance_assets() override {
        open_port(device.str_value);
    }

    void main_thread_update(double /*time*/) override {
        if (device.str_value != last_device_ ||
            (is_virtual_ && virtual_port_name.str_value != last_virtual_port_name_))
            open_port(device.str_value);
    }

    void process_audio(const VividAudioContext* ctx) override {
        if (!port_open_ && !test_capture_mode_) return;
        if (!ctx->custom_inputs || ctx->custom_input_count == 0) return;

        const auto* buf = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
        if (!buf || buf->count == 0) return;

        const int ch = std::clamp(channel.int_value(), 1, 16) - 1; // 0-based

        for (uint32_t i = 0; i < buf->count; ++i) {
            const VividNoteEvent& ev = buf->events[i];
            switch (ev.type) {
                case VIVID_NOTE_ON: {
                    float scaled = ev.value * velocity_scale.value;
                    auto vel = static_cast<uint8_t>(std::clamp(static_cast<int>(scaled * 127.0f), 1, 127));
                    send3(0x90 | ch, ev.note_number, vel);
                    has_live_notes_ = true;
                    break;
                }
                case VIVID_NOTE_OFF: {
                    send3(0x80 | ch, ev.note_number, 0);
                    break;
                }
                case VIVID_NOTE_PITCH_BEND: {
                    // semitones → 14-bit (±12 semitones maps to full range)
                    int bend14 = 8192 + static_cast<int>(ev.value / 12.0f * 8191.0f);
                    bend14 = std::clamp(bend14, 0, 16383);
                    send3(0xE0 | ch, bend14 & 0x7F, (bend14 >> 7) & 0x7F);
                    break;
                }
                case VIVID_NOTE_PRESSURE: {
                    auto pres = static_cast<uint8_t>(std::clamp(static_cast<int>(ev.value * 127.0f), 0, 127));
                    send3(0xA0 | ch, ev.note_number, pres);
                    break;
                }
                case VIVID_NOTE_TIMBRE: {
                    // CC 74 — brightness / slide
                    auto val = static_cast<uint8_t>(std::clamp(static_cast<int>(ev.value * 127.0f), 0, 127));
                    send3(0xB0 | ch, 74, val);
                    break;
                }
                default:
                    break;
            }
        }
    }

public:
    bool test_capture_mode_ = false;
    std::vector<std::vector<unsigned char>> test_captured_;

private:
    std::unique_ptr<RtMidiOut> rtmidi_out_;
    bool        port_open_             = false;
    bool        has_live_notes_        = false;
    bool        is_virtual_            = false;
    std::string last_device_;
    std::string last_virtual_port_name_;

    void send3(int status, int d1, int d2) {
        std::vector<unsigned char> msg = {
            static_cast<unsigned char>(status),
            static_cast<unsigned char>(d1),
            static_cast<unsigned char>(d2)
        };
        if (test_capture_mode_) { test_captured_.push_back(msg); return; }
        try {
            rtmidi_out_->sendMessage(&msg);
        } catch (RtMidiError& e) {
            std::fprintf(stderr, "[midi_out] sendMessage error: %s\n", e.what());
            port_open_ = false;
        }
    }

    void send_all_notes_off() {
        if (!has_live_notes_) return;
        if (!test_capture_mode_ && (!rtmidi_out_ || !port_open_)) return;
        const int ch = std::clamp(channel.int_value(), 1, 16) - 1;
        std::vector<unsigned char> msg = {
            static_cast<unsigned char>(0xB0 | ch), 123, 0
        };
        if (test_capture_mode_) { test_captured_.push_back(msg); has_live_notes_ = false; return; }
        try {
            rtmidi_out_->sendMessage(&msg);
        } catch (RtMidiError&) {}
        has_live_notes_ = false;
    }

    void open_port(const std::string& name) {
        send_all_notes_off();
        close_port();
        last_device_ = name;
        last_virtual_port_name_ = virtual_port_name.str_value;
        is_virtual_ = false;
        try {
            rtmidi_out_ = std::make_unique<RtMidiOut>();

            if (name == "virtual") {
                rtmidi_out_->openVirtualPort(virtual_port_name.str_value);
                is_virtual_ = true;
                port_open_  = true;
                std::fprintf(stderr, "[midi_out] Opened virtual port \"%s\"\n",
                    virtual_port_name.str_value.c_str());
                return;
            }

            const unsigned int count = rtmidi_out_->getPortCount();
            if (count == 0) {
                std::fprintf(stderr, "[midi_out] No MIDI output ports available\n");
                return;
            }

            unsigned int target = 0;
            if (!name.empty()) {
                bool found = false;
                for (unsigned int i = 0; i < count; ++i) {
                    if (rtmidi_out_->getPortName(i).find(name) != std::string::npos) {
                        target = i;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::fprintf(stderr, "[midi_out] Port '%s' not found. Available ports:\n", name.c_str());
                    for (unsigned int i = 0; i < count; ++i)
                        std::fprintf(stderr, "  [%u] %s\n", i, rtmidi_out_->getPortName(i).c_str());
                    return;
                }
            }

            rtmidi_out_->openPort(target);
            std::fprintf(stderr, "[midi_out] Opened port %u (%s)\n",
                target, rtmidi_out_->getPortName(target).c_str());
            port_open_ = true;
        } catch (RtMidiError& e) {
            std::fprintf(stderr, "[midi_out] Failed to open port: %s\n", e.what());
            port_open_ = false;
        }
    }

    void close_port() {
        if (rtmidi_out_ && port_open_) {
            try { rtmidi_out_->closePort(); } catch (RtMidiError&) {}
        }
        port_open_ = false;
        is_virtual_ = false;
        rtmidi_out_.reset();
    }
};
