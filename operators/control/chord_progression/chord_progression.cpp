#include "chord_progression_core.h"
#include "control/audio_scalar_utils.h"

#include <array>

struct ChordProgression : ChordProgressionCore, vivid::AudioProcessable {
    static constexpr const char* kName        = "ChordProgression";
    static constexpr const char* kDisplayName = "Chord Progression";
    static constexpr const char* kSummary =
        "Diatonic chord changes from a key + Roman-numeral pattern";
    static constexpr std::array<const char*, 4> kKeywords = {
        "harmony", "chords", "diatonic", "roman numerals"
    };

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_mode.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
        // SCALAR outputs (note/vel/gate) are now ports [0..2] — the legacy
        // LANE_ARRAY notes/velocities/gates outputs were removed in PR3.
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_DEFINE_OP(ChordProgression) {
    name = "ChordProgression";
    display_name = "Chord Progression";
    summary = "Diatonic chord changes from a key + Roman-numeral pattern";
    keywords = {"harmony", "chords", "diatonic", "roman numerals"};
}

VIVID_THUMBNAIL(ChordProgression)
VIVID_INSPECTOR(ChordProgression)
