#pragma once
#include <cstdint>

// Hardware MIDI input (M6.4) behind the platform seam. macOS = CoreMIDI
// (midi_input.mm); other OSes = a no-op stub (midi_input_stub.cpp).
//
// The CoreMIDI read callback runs on its own high-priority thread, so it only
// pushes note on/off transitions into a lock-free ring. The UI/main thread
// drains them via poll() once per frame and routes them to session_note_on/off
// — keeping all Session access on the UI thread (the same rule the rest of the
// app follows), at the cost of up to one frame of monitoring latency.
namespace vivid::platform {

struct MidiEvent { bool on; int pitch; float vel; };

class MidiInput {
public:
    MidiInput() = default;
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    bool start();                             // open the client + connect all sources
    void stop();
    int  poll(MidiEvent* out, int max);       // drain queued events (UI thread); returns count
    int  source_count() const;                // number of connected MIDI sources (0 on stub)

private:
    void* impl_ = nullptr;                    // opaque platform state
};

}  // namespace vivid::platform
