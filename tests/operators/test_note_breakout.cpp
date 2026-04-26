// NoteBreakout smoke tests.
//
// Verifies that:
//   1. NoteBreakout declares notes_in + voice_ids/voice_gates/voice_velocities
//      /voice_freqs ports, all four control lanes tagged advanced.
//   2. Empty notes_in → all four lanes are length 0.
//   3. Two NOTE_ONs (60, 64) with distinct ids → two-element lanes; voice_ids
//      ascending; voice_freqs[i] matches the equal-temperament frequency for
//      voice_ids[i]'s held note.
//   4. Same-pitch retrigger (60 with a fresh id) overlapping previous 60 →
//      both present, voice_ids distinct.
//   5. Per-note PITCH_BEND on a held id → voice_freqs[i] for that id reflects
//      the bend in semitones.

#include "operator_api/note_types.h"
#include "operator_api/types.h"
#include "runtime/operators/operator_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

constexpr int kFrames = 256;
constexpr uint32_t kSampleRate = 48000;

struct LaneOutBuf {
    std::vector<float> data;
    static float* resize_cb(void* h, uint32_t len) {
        auto* self = static_cast<LaneOutBuf*>(h);
        self->data.assign(len, 0.0f);
        return self->data.data();
    }
    static void commit_cb(void* /*h*/, uint32_t /*len*/) {}
};

// Stub lane-state service.
static void* stub_lane_state(void*, uint32_t, uint32_t) { return nullptr; }

struct Harness {
    LaneOutBuf voice_ids_buf;
    LaneOutBuf voice_gates_buf;
    LaneOutBuf voice_velocities_buf;
    LaneOutBuf voice_freqs_buf;
    VividLaneOutput lane_outputs[4] = {};

    VividNoteBuffer notes{};
    void* custom_inputs[1] = {&notes};

    VividAudioContext ctx{};

    Harness() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.lane_state_fn      = stub_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;

        lane_outputs[0] = {&voice_ids_buf,        LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[1] = {&voice_gates_buf,      LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[2] = {&voice_velocities_buf, LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[3] = {&voice_freqs_buf,      LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        ctx.output_lanes = lane_outputs;

        ctx.custom_inputs      = custom_inputs;
        ctx.custom_input_count = 1;
    }

    void clear_notes() { notes.count = 0; }

    void push_note_on(uint8_t note, float vel_0_1, uint64_t id) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type = VIVID_NOTE_ON; e.note_number = note; e.value = vel_0_1; e.note_id = id;
    }
    void push_note_off(uint64_t id) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type = VIVID_NOTE_OFF; e.note_id = id;
    }
    void push_pitch_bend(uint64_t id, float semis) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type = VIVID_NOTE_PITCH_BEND; e.note_id = id; e.value = semis;
    }
};

static bool has_port(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->port_count; ++p)
        if (std::strcmp(desc->ports[p].name, name) == 0) return true;
    return false;
}

static const VividPortDescriptor* find_port(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->port_count; ++p)
        if (std::strcmp(desc->ports[p].name, name) == 0) return &desc->ports[p];
    return nullptr;
}

static float midi_to_hz(float note) {
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string dylib_path = build_dir + "/note_breakout.dylib";

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
    check(desc != nullptr, "NoteBreakout descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "NoteBreakout") == 0, "operator name is NoteBreakout");
    check(has_port(desc, "notes_in"), "declares notes_in");
    check(has_port(desc, "voice_ids"), "declares voice_ids");
    check(has_port(desc, "voice_gates"), "declares voice_gates");
    check(has_port(desc, "voice_velocities"), "declares voice_velocities");
    check(has_port(desc, "voice_freqs"), "declares voice_freqs");

    // All four breakout lanes must be tagged advanced so the inspector
    // collapses them by default.
    for (const char* name : {"voice_ids", "voice_gates", "voice_velocities", "voice_freqs"}) {
        const auto* p = find_port(desc, name);
        if (p) {
            check(p->display_hint == VIVID_PORT_DISPLAY_ADVANCED,
                  (std::string(name) + " tagged ADVANCED").c_str());
        }
    }

    // ---------------------------------------------------------------------
    // Test: empty notes_in → all four lanes length 0
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- empty stream → empty lanes ---\n");
        Harness h;
        void* inst = loader.create_instance();
        loader.process_audio(inst, &h.ctx);
        check(h.voice_ids_buf.data.empty(),        "voice_ids empty");
        check(h.voice_gates_buf.data.empty(),      "voice_gates empty");
        check(h.voice_velocities_buf.data.empty(), "voice_velocities empty");
        check(h.voice_freqs_buf.data.empty(),      "voice_freqs empty");
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test: two NOTE_ONs (60 with id 100, 64 with id 200) → ascending
    // voice_ids; voice_freqs match equal-temperament for the held notes.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- two notes → sorted lanes, correct freqs ---\n");
        Harness h;
        void* inst = loader.create_instance();
        h.push_note_on(60, 0.8f, /*id=*/100);
        h.push_note_on(64, 0.6f, /*id=*/200);
        loader.process_audio(inst, &h.ctx);

        check(h.voice_ids_buf.data.size() == 2, "two voice_ids emitted");
        if (h.voice_ids_buf.data.size() == 2) {
            check_float(h.voice_ids_buf.data[0], 100.0f, 1e-3f, "voice_ids[0] = 100");
            check_float(h.voice_ids_buf.data[1], 200.0f, 1e-3f, "voice_ids[1] = 200");
            check_float(h.voice_gates_buf.data[0], 1.0f, "voice_gates[0] held");
            check_float(h.voice_gates_buf.data[1], 1.0f, "voice_gates[1] held");
            check_float(h.voice_velocities_buf.data[0], 0.8f, 1e-3f, "voice_velocities[0]");
            check_float(h.voice_velocities_buf.data[1], 0.6f, 1e-3f, "voice_velocities[1]");
            check_float(h.voice_freqs_buf.data[0], midi_to_hz(60.0f), 1e-2f,
                        "voice_freqs[0] = ~261.6 Hz (C4)");
            check_float(h.voice_freqs_buf.data[1], midi_to_hz(64.0f), 1e-2f,
                        "voice_freqs[1] = ~329.6 Hz (E4)");
        }
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test: same-pitch retrigger with a fresh id → both voices present,
    // distinct voice_ids, both voice_freqs ≈ 261.6 Hz.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- same-pitch overlap → distinct ids, identical freqs ---\n");
        Harness h;
        void* inst = loader.create_instance();
        h.push_note_on(60, 1.0f, /*id=*/300);
        h.push_note_on(60, 1.0f, /*id=*/301);
        loader.process_audio(inst, &h.ctx);

        check(h.voice_ids_buf.data.size() == 2, "two slots for same pitch");
        if (h.voice_ids_buf.data.size() == 2) {
            check_float(h.voice_ids_buf.data[0], 300.0f, 1e-3f, "voice_ids[0] = 300");
            check_float(h.voice_ids_buf.data[1], 301.0f, 1e-3f, "voice_ids[1] = 301 (sorted ascending)");
            check_float(h.voice_freqs_buf.data[0], midi_to_hz(60.0f), 1e-2f,
                        "voice_freqs[0] = C4");
            check_float(h.voice_freqs_buf.data[1], midi_to_hz(60.0f), 1e-2f,
                        "voice_freqs[1] = C4 (same pitch, fresh slot)");
        }
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test: PITCH_BEND on a held id → voice_freqs reflects the bend.
    // Bend = +12 semitones on note 60 → freq doubles to ~523.25 Hz (C5).
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- pitch_bend mutates voice_freqs ---\n");
        Harness h;
        void* inst = loader.create_instance();
        // Block 1 — hold 60 with id 400.
        h.push_note_on(60, 1.0f, /*id=*/400);
        loader.process_audio(inst, &h.ctx);
        check_float(h.voice_freqs_buf.data[0], midi_to_hz(60.0f), 1e-2f,
                    "pre-bend voice_freqs = C4");

        // Block 2 — bend up an octave.
        h.clear_notes();
        h.push_pitch_bend(/*id=*/400, /*semis=*/12.0f);
        loader.process_audio(inst, &h.ctx);
        check(h.voice_freqs_buf.data.size() == 1, "still one voice after bend");
        if (h.voice_freqs_buf.data.size() == 1) {
            check_float(h.voice_freqs_buf.data[0], midi_to_hz(72.0f), 1e-1f,
                        "voice_freqs reflects +12 semitone bend = C5 ~523.25 Hz");
        }
        loader.destroy_instance(inst);
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
