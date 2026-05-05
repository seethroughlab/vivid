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

VIVID_DEFINE_OP(MidiFilePlayer) {
}

VIVID_FILE_DROP(kMidiFilePlayerFileDrops)

// Optional debug-inject hook (probed by OperatorLoader via dlsym at load
// time). Lets the runtime push synthetic MIDI bytes into this operator
// alongside the file's own playback — same mechanism as MidiInput and
// Arpeggiator. Used by capture_note_response et al.
extern "C" void vivid_op_inject_midi(void* instance, const uint8_t* bytes,
                                       uint32_t count) {
    if (!instance || !bytes || count == 0) return;
    // op is the first member of _VividInstance at offset 0.
    auto* op = reinterpret_cast<MidiFilePlayer*>(instance);
    std::vector<unsigned char> msg(bytes, bytes + count);
    op->inject_events({std::move(msg)});
}
