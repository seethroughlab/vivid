#pragma once

#include <array>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

class RtMidiIn;

namespace vivid {

struct MidiCCEvent {
    int channel = 0;   // 1-16
    int cc_number = 0;  // 0-127
    float value = 0.0f; // 0.0-1.0 (raw CC / 127)
};

class SystemMidiListener {
public:
    SystemMidiListener();
    ~SystemMidiListener();

    // Open all available MIDI input ports.
    bool open_all();
    void close();
    bool is_open() const { return !inputs_.empty(); }

    // Call once per frame from main thread. Drains buffered events,
    // updates cc_state_, and returns the events for UI relay.
    std::vector<MidiCCEvent> drain_cc_events();

    // Read latest CC value (main-thread only, no lock needed after drain).
    // channel: 1-16. Returns 0.0 if never received.
    float cc_value(int channel, int cc) const;

    // Returns most recent drain (for snapshot relay to UI).
    // Main-thread only. Reference valid until next drain_cc_events() call.
    const std::vector<MidiCCEvent>& last_drained_events() const { return last_drained_; }

    // Enumerate available MIDI input ports.
    std::vector<std::string> port_names() const;

    // Count of incoming CC events dropped because the buffer hit its cap before
    // the next drain (only reachable under pathological flooding). (audit 05-F10)
    uint64_t dropped_cc_events() const { return dropped_cc_events_; }

private:
    // Hard cap on the callback buffer. drain_cc_events() runs every frame
    // (~60 Hz) and swaps the buffer empty, so this is only reached if the frame
    // thread stalls while CC floods — at which point we drop new events rather
    // than grow without bound.
    static constexpr size_t kMaxBufferedCcEvents = 4096;

    static void midi_callback(double timestamp, std::vector<unsigned char>* message, void* user_data);

    // One RtMidiIn per open port (RtMidi limitation: one port per instance)
    std::vector<std::unique_ptr<RtMidiIn>> inputs_;

    // Probe instance (never opened, used for port enumeration)
    std::unique_ptr<RtMidiIn> probe_;

    // Callback thread pushes here (shared across all inputs)
    std::mutex mutex_;
    std::vector<MidiCCEvent> event_buffer_;
    uint64_t dropped_cc_events_ = 0;  // bumped when event_buffer_ hits the cap

    // Main-thread state (no lock needed after drain)
    std::array<std::array<float, 128>, 16> cc_state_{};
    std::vector<MidiCCEvent> last_drained_;
};

} // namespace vivid
