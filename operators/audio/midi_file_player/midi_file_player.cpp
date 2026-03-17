#include "midi_file_player.h"

static const char* kMidiFilePlayerDropExtensions[] = {
    ".mid",
    ".midi",
};

static const VividFileDropHandlerDescriptor kMidiFilePlayerFileDrops[] = {{
    "Play MIDI File",
    kMidiFilePlayerDropExtensions,
    2,
    "file",
    100,
    "Create a MidiFilePlayer node from a dropped MIDI file.",
}};

VIVID_REGISTER(MidiFilePlayer)
VIVID_FILE_DROP(kMidiFilePlayerFileDrops)
