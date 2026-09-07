#pragma once
#include "midi/midi_parse.h"   // MidiMsg / MidiByteParser (module rank 0; platform may include it)

#include <cstdint>
#include <string>
#include <vector>

// Hardware MIDI input behind the platform seam. macOS = CoreMIDI (midi_input.mm); other OSes = a
// no-op stub (midi_input_stub.cpp).
//
// The CoreMIDI read callback runs on its own high-priority thread, so it only decodes bytes and
// pushes complete messages into a lock-free ring. The UI/main thread drains them via poll() once
// per frame — keeping all Session access on the UI thread (the rule the rest of the app follows),
// at the cost of up to one frame of monitoring latency.
//
// Sources are re-scanned on a CoreMIDI setup change, so a keyboard plugged in AFTER launch is
// picked up. Previously MIDIClientCreate was passed a null notify proc and sources were connected
// exactly once at startup, which meant hot-plugging did nothing and the only report of what was
// connected was a single fprintf — an agent driving the app had no way to see the state at all.
namespace vivid::platform {

using vivid::session::MidiKind;
using vivid::session::MidiMsg;

// One MIDI source as CoreMIDI reports it. `id` is the CoreMIDI unique ID — stable across
// replug and reboot, which is what a persisted device preference has to key on (a name is not
// unique and an index is not stable).
struct MidiSource {
    int32_t     id = 0;
    std::string name;         // "Manufacturer Model" where both are known
    bool        connected = false;   // currently receiving (i.e. passes the selection filter)
};

class MidiInput {
public:
    MidiInput() = default;
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    bool start();                             // open the client + connect matching sources
    void stop();
    int  poll(MidiMsg* out, int max);         // drain queued messages (UI thread); returns count
    int  source_count() const;                // number of CONNECTED sources (0 on stub)

    // The sources CoreMIDI currently reports, connected or not. Rebuilt on a setup change.
    std::vector<MidiSource> sources() const;

    // Restrict input to one source id (0 = accept every source, the default) and/or one channel
    // (-1 = every channel). Takes effect immediately; safe to call before start().
    void select(int32_t source_id, int channel);
    int32_t selected_source() const;
    int     selected_channel() const;

    // Observability: total messages accepted since start, and the host time of the most recent one
    // (0 = nothing yet). Lets a status surface answer "is the keyboard actually sending?" — which
    // is the question an agent could not previously ask at all.
    uint64_t events_seen() const;
    uint64_t last_event_host_time() const;

private:
    void* impl_ = nullptr;                    // opaque platform state
};

}  // namespace vivid::platform
