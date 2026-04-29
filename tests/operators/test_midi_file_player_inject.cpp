// Smoke test for the MidiFilePlayer MIDI-inject hook.
//
// Verifies:
//   1. MidiFilePlayer's dylib exports `vivid_op_inject_midi`, so
//      OperatorLoader probes it via dlsym and reports has_inject_midi() == true.
//   2. Injecting a NOTE_ON with no .mid file loaded still appears in the
//      notes_out custom-ref output (custom_outputs[0]) — i.e. inject works
//      even when the file path is empty.
//   3. Injecting a matched NOTE_OFF emits a NOTE_OFF event referencing the
//      same note_id that the NOTE_ON allocated.

#include "operator_api/note_types.h"
#include "operator_api/types.h"
#include "runtime/operators/operator_loader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

constexpr int kFrames = 256;
constexpr uint32_t kSampleRate = 48000;

static void* stub_lane_state(void*, uint32_t, uint32_t) { return nullptr; }

struct Harness {
    // The operator overwrites custom_outputs[0] with a pointer to its own
    // internal VividNoteBuffer each tick — so the test reads through that
    // pointer rather than a local buffer.
    void* custom_outputs[1] = {nullptr};
    std::vector<float> param_values{0.0f, 1.0f, 0.0f, 0.0f, 1.0f}; // playing=1, vel_scale=1
    VividAudioContext ctx{};

    Harness() {
        ctx.sample_rate         = kSampleRate;
        ctx.buffer_size         = kFrames;
        ctx.lane_state_fn       = stub_lane_state;
        ctx.lane_state_service  = nullptr;
        ctx.lane_id             = 1;
        ctx.param_values        = param_values.data();
        ctx.custom_outputs      = custom_outputs;
        ctx.custom_output_count = 1;
    }

    const VividNoteBuffer* notes_out() const {
        return static_cast<const VividNoteBuffer*>(custom_outputs[0]);
    }
};

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string dylib_path = build_dir + "/midi_file_player.dylib";

    if (!std::filesystem::exists(dylib_path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", dylib_path.c_str());
        return 1;
    }

    vivid::OperatorLoader loader;
    if (!loader.load(dylib_path.c_str())) {
        std::fprintf(stderr, "FATAL: failed to load %s\n", dylib_path.c_str());
        return 1;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "MidiFilePlayer descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "MidiFilePlayer") == 0,
          "operator name is MidiFilePlayer");
    check(loader.has_inject_midi(),
          "MidiFilePlayer dylib exports vivid_op_inject_midi");

    // ---------------------------------------------------------------------
    // Inject NOTE_ON with no file loaded — must appear in notes_out.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- inject note_on, no file ---\n");
        Harness h;
        void* inst = loader.create_instance();
        check(inst != nullptr, "create_instance ok");
        if (!inst) return 1;

        uint8_t note_on[3] = {0x90, 60, 100};
        loader.inject_midi(inst, note_on, 3);
        loader.process_audio(inst, &h.ctx);

        const auto* out = h.notes_out();
        check(out != nullptr, "custom_outputs[0] points at a notes buffer");
        check(out && out->count == 1,
              "notes_out has exactly one event after inject");
        if (out && out->count >= 1) {
            check(out->events[0].type == VIVID_NOTE_ON,
                  "first event is NOTE_ON");
            check(out->events[0].note_number == 60,
                  "note_number == 60");
            check(out->events[0].note_id != 0,
                  "note_id was allocated (non-zero)");
        }
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Inject matched NOTE_ON + NOTE_OFF across two ticks — verify the
    // NOTE_OFF event references the same note_id as the NOTE_ON.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- inject note_on/off pair across ticks ---\n");
        Harness h;
        void* inst = loader.create_instance();
        if (!inst) return 1;

        uint8_t note_on[3]  = {0x90, 64, 100};
        uint8_t note_off[3] = {0x80, 64, 0};
        loader.inject_midi(inst, note_on, 3);
        loader.process_audio(inst, &h.ctx);
        const auto* out1 = h.notes_out();
        check(out1 && out1->count == 1, "tick 1: one NOTE_ON event");
        uint64_t on_id = (out1 && out1->count >= 1) ? out1->events[0].note_id : 0;

        loader.inject_midi(inst, note_off, 3);
        loader.process_audio(inst, &h.ctx);
        const auto* out2 = h.notes_out();
        check(out2 && out2->count == 1, "tick 2: one NOTE_OFF event");
        if (out2 && out2->count >= 1) {
            check(out2->events[0].type == VIVID_NOTE_OFF,
                  "event is NOTE_OFF");
            check(out2->events[0].note_id == on_id,
                  "NOTE_OFF references same note_id as NOTE_ON");
        }
        loader.destroy_instance(inst);
    }

    std::fprintf(stderr, "\n%s: %d failure(s)\n",
                 failures == 0 ? "OK" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
