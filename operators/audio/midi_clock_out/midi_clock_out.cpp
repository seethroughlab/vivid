#include "operator_api/operator.h"
#include "RtMidi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Broadcasts MIDI timing clock (24 PPQ) to external devices.
 *
 * Sends 24 MIDI Clock pulses (0xF8) per quarter note at sample-accurate timing,
 * derived from Vivid's internal metronome BPM. Any MIDI Clock–capable device or
 * DAW connected to the selected port will lock its tempo to Vivid's BPM, including
 * live tempo changes.
 *
 * When `send_transport` is enabled, toggling `enabled` off→on sends MIDI Start or
 * Continue (with optional Song Position Pointer), and toggling on→off sends Stop
 * plus All Sound Off.
 *
 * Set `device` to a substring of the target MIDI output port name.
 * Leave empty to use the first available output port.
 *
 * @tip Use with MidiOut to drive external instruments and clock them simultaneously.
 * @see MidiOut, MidiInput
 */
struct MidiClockOut : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName        = "MidiClockOut";
    static constexpr const char* kDisplayName = "MIDI Clock Out";
    static constexpr const char* kSummary     = "Broadcast MIDI timing clock (24 PPQ) to external devices";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::TextValue> device         {"device",         ""};
    vivid::Param<bool>             enabled        {"enabled",        true};
    vivid::Param<bool>             send_transport {"send_transport", true};
    vivid::Param<bool>             song_position  {"song_position",  true};

    MidiClockOut() {
        vivid::description(device,         "MIDI output port name (substring match); empty = first available");
        vivid::description(enabled,        "Start/stop clock broadcast without removing the node");
        vivid::description(send_transport, "Send MIDI Start/Stop/Continue in sync with the enabled param");
        vivid::description(song_position,  "Send Song Position Pointer on Start/Continue so receivers land on the correct beat");
    }

    ~MidiClockOut() override {
        if (was_running_) send_stop();
        close_port();
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&device);
        out.push_back(&enabled);
        out.push_back(&send_transport);
        out.push_back(&song_position);
    }

    void collect_ports(std::vector<VividPortDescriptor>& /*out*/) override {
        // Pure sink — no ports
    }

    void prepare_instance_assets() override {
        open_port(device.str_value);
    }

    void main_thread_update(double /*time*/) override {
        if (device.str_value != last_device_)
            open_port(device.str_value);
    }

    void process_audio(const VividAudioContext* ctx) override {
        if (!port_open_ && !test_capture_mode_) return;

        const bool now_enabled = enabled.bool_value();

        if (now_enabled && !prev_enabled_) {
            // Transition: stopped → running
            if (send_transport.bool_value())
                send_start_or_continue(ctx);
        } else if (!now_enabled && prev_enabled_) {
            // Transition: running → stopped
            if (send_transport.bool_value())
                send_transport_stop();
            accumulator_ = 0.0;
        }
        prev_enabled_ = now_enabled;

        if (!now_enabled) {
            was_running_ = false;
            return;
        }
        was_running_ = true;

        const double bpm         = std::max(1.0f, ctx->metronome_bpm);
        const double sample_rate = static_cast<double>(std::max(1u, ctx->sample_rate));
        const double interval    = sample_rate * 60.0 / (bpm * 24.0);

        accumulator_ += static_cast<double>(ctx->buffer_size);
        while (accumulator_ >= interval) {
            send_clock();
            accumulator_ -= interval;
        }
    }

public:
    bool test_capture_mode_ = false;
    std::vector<std::vector<unsigned char>> test_captured_;

private:
    std::unique_ptr<RtMidiOut> rtmidi_out_;
    bool   port_open_    = false;
    bool   was_running_  = false;
    bool   prev_enabled_ = false;
    double accumulator_  = 0.0;
    std::string last_device_;

    void send_clock() {
        std::vector<unsigned char> msg = {0xF8};
        if (test_capture_mode_) { test_captured_.push_back(msg); return; }
        try {
            rtmidi_out_->sendMessage(&msg);
        } catch (RtMidiError& e) {
            std::fprintf(stderr, "[midi_clock_out] sendMessage error: %s\n", e.what());
            port_open_ = false;
        }
    }

    // Called from destructor / open_port — sends bare Stop, no All Sound Off
    void send_stop() {
        if (!test_capture_mode_ && (!rtmidi_out_ || !port_open_)) return;
        std::vector<unsigned char> msg = {0xFC};
        if (test_capture_mode_) { test_captured_.push_back(msg); return; }
        try {
            rtmidi_out_->sendMessage(&msg);
        } catch (RtMidiError&) {}
    }

    // Called on enabled→disabled transition when send_transport is on
    void send_transport_stop() {
        if (!test_capture_mode_ && (!rtmidi_out_ || !port_open_)) return;
        std::vector<unsigned char> stop      = {0xFC};
        std::vector<unsigned char> sound_off = {0xB0, 120, 0};
        if (test_capture_mode_) {
            test_captured_.push_back(stop);
            test_captured_.push_back(sound_off);
            return;
        }
        try {
            rtmidi_out_->sendMessage(&stop);
            rtmidi_out_->sendMessage(&sound_off);
        } catch (RtMidiError&) {}
    }

    // Called on disabled→enabled transition when send_transport is on
    void send_start_or_continue(const VividAudioContext* ctx) {
        if (!test_capture_mode_ && (!rtmidi_out_ || !port_open_)) return;

        // Song Position Pointer: 14-bit value in MIDI beats (1/16th notes per beat)
        const double beats  = ctx->metronome_beats_elapsed;
        const auto   spp    = static_cast<uint16_t>(std::min(beats * 4.0, 16383.0));

        if (song_position.bool_value()) {
            std::vector<unsigned char> spp_msg = {
                0xF2,
                static_cast<uint8_t>(spp & 0x7Fu),
                static_cast<uint8_t>((spp >> 7u) & 0x7Fu)
            };
            if (test_capture_mode_) {
                test_captured_.push_back(spp_msg);
            } else {
                try { rtmidi_out_->sendMessage(&spp_msg); } catch (RtMidiError&) {}
            }
        }

        // Start (0xFA) if at or very near position 0, else Continue (0xFB)
        const uint8_t transport_byte = (spp < 2) ? 0xFA : 0xFB;
        std::vector<unsigned char> msg = {transport_byte};
        if (test_capture_mode_) {
            test_captured_.push_back(msg);
            return;
        }
        try {
            rtmidi_out_->sendMessage(&msg);
        } catch (RtMidiError& e) {
            std::fprintf(stderr, "[midi_clock_out] transport send error: %s\n", e.what());
        }
    }

    void open_port(const std::string& name) {
        if (was_running_) send_stop();
        close_port();
        last_device_  = name;
        was_running_  = false;
        prev_enabled_ = false;
        accumulator_  = 0.0;
        try {
            rtmidi_out_ = std::make_unique<RtMidiOut>();
            const unsigned int count = rtmidi_out_->getPortCount();
            if (count == 0) {
                std::fprintf(stderr, "[midi_clock_out] No MIDI output ports available\n");
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
                    std::fprintf(stderr, "[midi_clock_out] Port '%s' not found. Available ports:\n", name.c_str());
                    for (unsigned int i = 0; i < count; ++i)
                        std::fprintf(stderr, "  [%u] %s\n", i, rtmidi_out_->getPortName(i).c_str());
                    return;
                }
            }

            rtmidi_out_->openPort(target);
            std::fprintf(stderr, "[midi_clock_out] Opened port %u (%s)\n",
                target, rtmidi_out_->getPortName(target).c_str());
            port_open_ = true;
        } catch (RtMidiError& e) {
            std::fprintf(stderr, "[midi_clock_out] Failed to open port: %s\n", e.what());
            port_open_ = false;
        }
    }

    void close_port() {
        if (rtmidi_out_ && port_open_) {
            try { rtmidi_out_->closePort(); } catch (RtMidiError&) {}
        }
        port_open_ = false;
        rtmidi_out_.reset();
    }
};
