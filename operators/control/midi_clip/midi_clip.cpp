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
VIVID_THUMBNAIL(MidiClip)

static const char* kMidiClipDropExts[] = {".mid", ".midi"};
static const VividFileDropHandlerDescriptor kMidiClipFileDrops[] = {{
    "Play/Edit MIDI Clip",
    kMidiClipDropExts, 2,
    "file",
    150,
    "Create a MidiClip node from a dropped MIDI file.",
}};
VIVID_FILE_DROP(kMidiClipFileDrops)

extern "C" void vivid_op_inject_midi(void* instance, const uint8_t* bytes,
                                      uint32_t count) {
    if (!instance || !bytes || count == 0) return;
    auto* op = reinterpret_cast<MidiClip*>(instance);
    std::vector<unsigned char> msg(bytes, bytes + count);
    op->inject_events({std::move(msg)});
}
