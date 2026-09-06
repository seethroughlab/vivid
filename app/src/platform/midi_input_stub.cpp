#ifndef __APPLE__
#include "platform/midi_input.h"

// Non-macOS: no hardware MIDI input yet. (Linux ALSA / Windows WinMM would slot in here as
// additional platform backends; the byte decoding itself is already portable — midi/midi_parse.h.)
namespace vivid::platform {

MidiInput::~MidiInput() {}
bool MidiInput::start() { return false; }
void MidiInput::stop() {}
int  MidiInput::poll(MidiMsg*, int) { return 0; }
int  MidiInput::source_count() const { return 0; }
std::vector<MidiSource> MidiInput::sources() const { return {}; }
void MidiInput::select(int32_t, int) {}
int32_t  MidiInput::selected_source() const { return 0; }
int      MidiInput::selected_channel() const { return -1; }
uint64_t MidiInput::events_seen() const { return 0; }
uint64_t MidiInput::last_event_host_time() const { return 0; }

}  // namespace vivid::platform

#endif  // !__APPLE__
