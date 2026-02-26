#include "runtime/system_midi.h"
#include "RtMidi.h"
#include <cstdio>

namespace vivid {

SystemMidiListener::SystemMidiListener() {
    try {
        probe_ = std::make_unique<RtMidiIn>();
    } catch (RtMidiError& e) {
        std::fprintf(stderr, "[vivid] SystemMidi: failed to create RtMidiIn: %s\n", e.what());
    }
}

SystemMidiListener::~SystemMidiListener() {
    close();
}

bool SystemMidiListener::open_all() {
    close();

    if (!probe_) return false;

    try {
        unsigned int count = probe_->getPortCount();
        if (count == 0) {
            std::fprintf(stderr, "[vivid] SystemMidi: no MIDI input ports available\n");
            return false;
        }

        for (unsigned int i = 0; i < count; ++i) {
            try {
                auto inp = std::make_unique<RtMidiIn>();
                inp->ignoreTypes(true, true, true);
                inp->setCallback(&SystemMidiListener::midi_callback, this);
                inp->openPort(i);
                std::fprintf(stderr, "[vivid] SystemMidi: opened port %u (%s)\n",
                    i, probe_->getPortName(i).c_str());
                inputs_.push_back(std::move(inp));
            } catch (RtMidiError& e) {
                std::fprintf(stderr, "[vivid] SystemMidi: failed to open port %u: %s\n",
                    i, e.what());
            }
        }

        return !inputs_.empty();
    } catch (RtMidiError& e) {
        std::fprintf(stderr, "[vivid] SystemMidi: open_all failed: %s\n", e.what());
        return false;
    }
}

void SystemMidiListener::close() {
    for (auto& inp : inputs_) {
        try {
            inp->cancelCallback();
            inp->closePort();
        } catch (RtMidiError& e) {
            std::fprintf(stderr, "[vivid] SystemMidi: close error: %s\n", e.what());
        }
    }
    inputs_.clear();
}

void SystemMidiListener::midi_callback(double /*timestamp*/,
                                       std::vector<unsigned char>* message,
                                       void* user_data) {
    if (!message || message->size() < 3) return;

    unsigned char status = (*message)[0];
    unsigned char type = status & 0xF0;
    if (type != 0xB0) return; // Only CC messages

    int channel = (status & 0x0F) + 1; // 1-16
    int cc = static_cast<int>((*message)[1]);
    float value = static_cast<float>((*message)[2]) / 127.0f;

    auto* self = static_cast<SystemMidiListener*>(user_data);
    std::lock_guard<std::mutex> lock(self->mutex_);
    self->event_buffer_.push_back({channel, cc, value});
}

std::vector<MidiCCEvent> SystemMidiListener::drain_cc_events() {
    std::vector<MidiCCEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events.swap(event_buffer_);
    }

    // Update cc_state_ on main thread
    for (const auto& ev : events) {
        if (ev.channel >= 1 && ev.channel <= 16)
            cc_state_[ev.channel - 1][ev.cc_number] = ev.value;
    }

    last_drained_ = events;
    return events;
}

float SystemMidiListener::cc_value(int channel, int cc) const {
    if (channel < 1 || channel > 16 || cc < 0 || cc > 127) return 0.0f;
    return cc_state_[channel - 1][cc];
}

std::vector<std::string> SystemMidiListener::port_names() const {
    std::vector<std::string> names;
    if (!probe_) return names;
    try {
        unsigned int count = probe_->getPortCount();
        for (unsigned int i = 0; i < count; ++i)
            names.push_back(probe_->getPortName(i));
    } catch (RtMidiError&) {}
    return names;
}

} // namespace vivid
