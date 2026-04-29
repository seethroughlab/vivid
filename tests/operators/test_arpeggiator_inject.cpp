// Smoke test for the Arpeggiator MIDI-inject hook.
//
// Verifies:
//   1. Arpeggiator's dylib exports `vivid_op_inject_midi`, so OperatorLoader
//      probes it via dlsym and reports has_inject_midi() == true.
//   2. Calling loader.inject_midi() with a 3-byte NOTE_ON and then ticking
//      process_audio() does not crash, regardless of whether notes_in is
//      null or empty.
//   3. Injected events do not crash the merge path when notes_in is also
//      providing live events.
//
// End-to-end "does an injected note actually arpeggiate" coverage lives in
// the headless capture-smoke test, not here.

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
constexpr int kArpOutputCount = 4;

// Stub lane-state service.
static void* stub_lane_state(void*, uint32_t, uint32_t) { return nullptr; }

struct Harness {
    std::vector<float> output_buffers[kArpOutputCount];
    float* output_ptrs[kArpOutputCount] = {};
    VividNoteBuffer notes{};
    void* custom_inputs[1] = {&notes};
    // Arpeggiator's compute() reads from params[..~41] for modulation lanes
    // when held_count_ > 0. Allocate a generous zeroed array so the inject
    // path can fire steps without null-deref.
    std::array<float, 96> param_values{};
    VividAudioContext ctx{};

    explicit Harness(bool with_notes_in) {
        for (int i = 0; i < kArpOutputCount; ++i) {
            output_buffers[i].assign(kFrames, 0.0f);
            output_ptrs[i] = output_buffers[i].data();
        }
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.lane_state_fn      = stub_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;
        ctx.output_buffers     = output_ptrs;
        ctx.param_values       = param_values.data();
        if (with_notes_in) {
            ctx.custom_inputs      = custom_inputs;
            ctx.custom_input_count = 1;
        }
    }
};

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string dylib_path = build_dir + "/arpeggiator.dylib";

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
    check(desc != nullptr, "Arpeggiator descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "Arpeggiator") == 0,
          "operator name is Arpeggiator");
    check(loader.has_inject_midi(),
          "Arpeggiator dylib exports vivid_op_inject_midi");

    // ---------------------------------------------------------------------
    // Inject + tick with notes_in null — must not crash.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- inject + tick, no notes_in ---\n");
        Harness h(/*with_notes_in=*/false);
        void* inst = loader.create_instance();
        check(inst != nullptr, "create_instance ok");
        if (!inst) return 1;

        uint8_t note_on[3]  = {0x90, 60, 100};   // ch1 NOTE_ON C4 vel=100
        uint8_t note_off[3] = {0x80, 60, 0};     // ch1 NOTE_OFF C4
        loader.process_audio(inst, &h.ctx);  // bare tick — no inject
        loader.inject_midi(inst, note_on, 3);
        loader.process_audio(inst, &h.ctx);  // arp sees the held note
        loader.inject_midi(inst, note_off, 3);
        loader.process_audio(inst, &h.ctx);
        check(true, "inject + tick (no notes_in) did not crash");
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Inject + tick alongside an empty notes_in — must not crash and must
    // not corrupt the upstream buffer.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- inject + tick, empty notes_in ---\n");
        Harness h(/*with_notes_in=*/true);
        void* inst = loader.create_instance();
        if (!inst) return 1;

        h.notes.count = 0;
        uint8_t note_on[3] = {0x90, 67, 110};
        loader.inject_midi(inst, note_on, 3);
        loader.process_audio(inst, &h.ctx);
        check(h.notes.count == 0,
              "upstream notes_in buffer untouched by inject");
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Inject + tick alongside a populated notes_in — verify both streams
    // are accepted and merged without crash.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- inject + tick, populated notes_in ---\n");
        Harness h(/*with_notes_in=*/true);
        void* inst = loader.create_instance();
        if (!inst) return 1;

        // notes_in pre-populated with one NOTE_ON.
        h.notes.count = 1;
        h.notes.events[0].type = VIVID_NOTE_ON;
        h.notes.events[0].note_number = 64;
        h.notes.events[0].value = 0.7f;
        h.notes.events[0].note_id = 0xDEADBEEF;
        h.notes.events[0].frame_offset_samples = 0;

        uint8_t note_on[3] = {0x90, 71, 90};
        loader.inject_midi(inst, note_on, 3);
        loader.process_audio(inst, &h.ctx);
        // upstream buffer survives untouched (we copy out, don't write back)
        check(h.notes.count == 1, "notes_in remained 1 event");
        check(h.notes.events[0].note_number == 64, "notes_in event preserved");
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Tick with no inject and no notes_in — sanity check the no-op path.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- bare tick, no inject, no notes_in ---\n");
        Harness h(/*with_notes_in=*/false);
        void* inst = loader.create_instance();
        if (!inst) return 1;
        loader.process_audio(inst, &h.ctx);
        check(true, "bare tick did not crash");
        loader.destroy_instance(inst);
    }

    std::fprintf(stderr, "\n%s: %d failure(s)\n",
                 failures == 0 ? "OK" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
