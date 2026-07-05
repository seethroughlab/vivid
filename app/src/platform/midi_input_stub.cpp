#ifndef __APPLE__
#include "platform/midi_input.h"

// Non-macOS: no hardware MIDI input yet. (Linux ALSA / Windows WinMM would slot
// in here as additional platform backends.)
namespace vivid::platform {

MidiInput::~MidiInput() {}
bool MidiInput::start() { return false; }
void MidiInput::stop() {}
int  MidiInput::poll(MidiEvent*, int) { return 0; }
int  MidiInput::source_count() const { return 0; }

}  // namespace vivid::platform

#endif  // !__APPLE__
