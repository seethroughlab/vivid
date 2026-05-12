#include "midi_clip_core.h"
#include "control/audio_scalar_utils.h"

struct MidiClip : MidiClipCore, vivid::AudioProcessable {
    static constexpr const char* kName = "MidiClip";

    void process_audio(const VividAudioContext* ctx) override {
        MidiClipCore::process_audio(ctx);
    }
};

VIVID_DEFINE_OP(MidiClip) {
    display_name = "MIDI Clip";
    keywords     = {"piano roll", "melody", "notes", "loop", "sequencer", "clip"};
    summary      = "Looping piano-roll clip editor that feeds note events to instruments";
}

VIVID_EDITOR(MidiClip)

static const char* kMidiClipDropExts[] = {".mid", ".midi"};
static const VividFileDropHandlerDescriptor kMidiClipFileDrops[] = {{
    "Import into MIDI Clip",
    kMidiClipDropExts, 2,
    "midi_import",
    50,  // secondary to MidiFilePlayer (priority 100)
    "Create a MidiClip node and import notes from the dropped MIDI file.",
}};
VIVID_FILE_DROP(kMidiClipFileDrops)
