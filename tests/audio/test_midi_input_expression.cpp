// MidiInput per-note expression and MPE correctness tests.
//
// After Phase 5 PR2, MidiInput has no LANE_ARRAY outputs — every
// per-note signal lives on the native `notes_out` (VividNoteBuffer)
// stream as NOTE_ON / NOTE_OFF / PITCH_BEND / PRESSURE / TIMBRE events
// keyed by stable note_id. These tests inspect the event stream
// directly, mirroring the pattern in tests/operators/test_tracker_expression.cpp.
//
// Compiles midi_input.cpp directly to access inject_events() test seam.

#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "RtMidi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "test_helpers.h"

// Pull in the operator source directly so we can access the MidiInput struct
// and its inject_events() test seam.
#include "../../operators/control/midi_input/midi_input.cpp"

// ---------------------------------------------------------------------------
// Test harness — minimal VividFrameContext with custom_outputs slot for notes_out
// ---------------------------------------------------------------------------

// MidiInput's surface after Phase 5 PR2: 7 scalar outputs + notes_out (custom_ref)
// + aftertouch/expression scalars = 10 output ports total.
static constexpr int kNumOutputScalars = 10;

struct TestFrameContext {
    float param_values[8]  = {};
    float output_values[kNumOutputScalars] = {};
    void* custom_outputs[1] = {};

    VividFrameContext ctx = {};

    TestFrameContext() {
        ctx.param_values     = param_values;
        ctx.output_values    = output_values;
        ctx.output_lanes     = nullptr;       // no lane outputs anymore
        ctx.custom_outputs   = custom_outputs;
        ctx.custom_output_count = 1;
        ctx.time        = 0.0;
        ctx.delta_time  = 1.0 / 60.0;
        ctx.frame       = 0;
    }

    void reset_outputs() {
        std::memset(output_values, 0, sizeof(output_values));
        custom_outputs[0] = nullptr;
    }

    const VividNoteBuffer* notes_out() const {
        return static_cast<const VividNoteBuffer*>(custom_outputs[0]);
    }
};

// Helper: sync param_values into the operator's Param<> members and run
// process_frame. Direct invocation (not through normal operator dispatch)
// skips the runtime's param-sync step, so we do it manually.
static void run_frame(MidiInput& op, TestFrameContext& tc) {
    op.skip_midi_init();
    op.device.value    = tc.param_values[0];
    op.channel.value   = tc.param_values[1];
    op.cc_number.value = tc.param_values[2];
    op.learn.value     = tc.param_values[3];
    op.mode.value      = tc.param_values[4];
    op.process_frame(&tc.ctx);
}

static std::vector<unsigned char> note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    return {static_cast<unsigned char>(0x90 | (ch - 1)), note, vel};
}

static std::vector<unsigned char> note_off(uint8_t ch, uint8_t note) {
    return {static_cast<unsigned char>(0x80 | (ch - 1)), note, 0};
}

static std::vector<unsigned char> pitch_bend(uint8_t ch, int value) {
    return {static_cast<unsigned char>(0xE0 | (ch - 1)),
            static_cast<unsigned char>(value & 0x7F),
            static_cast<unsigned char>((value >> 7) & 0x7F)};
}

static std::vector<unsigned char> channel_pressure(uint8_t ch, uint8_t val) {
    return {static_cast<unsigned char>(0xD0 | (ch - 1)), val};
}

static std::vector<unsigned char> cc(uint8_t ch, uint8_t cc_num, uint8_t val) {
    return {static_cast<unsigned char>(0xB0 | (ch - 1)), cc_num, val};
}

static std::vector<unsigned char> poly_pressure(uint8_t ch, uint8_t note, uint8_t val) {
    return {static_cast<unsigned char>(0xA0 | (ch - 1)), note, val};
}

// ---------------------------------------------------------------------------
// Event-stream walker helpers
// ---------------------------------------------------------------------------

static int count_events(const VividNoteBuffer* buf, VividNoteEventType type) {
    if (!buf) return 0;
    int n = 0;
    for (uint32_t i = 0; i < buf->count; ++i)
        if (buf->events[i].type == type) ++n;
    return n;
}

// Find the first event of `type` carrying `note_id`. Returns nullptr if none.
static const VividNoteEvent* find_event(const VividNoteBuffer* buf,
                                        VividNoteEventType type,
                                        uint64_t note_id) {
    if (!buf) return nullptr;
    for (uint32_t i = 0; i < buf->count; ++i) {
        const auto& e = buf->events[i];
        if (e.type == type && e.note_id == note_id) return &e;
    }
    return nullptr;
}

// Find the first NOTE_ON for the given pitch. Useful when the test cares
// about the latest emitted on for a known MIDI note.
static const VividNoteEvent* find_note_on(const VividNoteBuffer* buf,
                                          uint8_t note_number) {
    if (!buf) return nullptr;
    for (uint32_t i = 0; i < buf->count; ++i) {
        const auto& e = buf->events[i];
        if (e.type == VIVID_NOTE_ON && e.note_number == note_number) return &e;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 1. poly_shared NOTE_ON/NOTE_OFF round-trip + scalar gate/trigger
// ---------------------------------------------------------------------------
static void test_poly_shared_note_lifecycle() {
    std::fprintf(stderr, "\n--- poly_shared NOTE_ON/NOTE_OFF lifecycle ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    op.inject_events({note_on(1, 60, 100)});
    run_frame(op, tc);

    auto* buf = tc.notes_out();
    check(buf != nullptr, "notes_out present");
    check(count_events(buf, VIVID_NOTE_ON) == 1, "exactly one NOTE_ON");
    check_float(tc.output_values[0], 60.0f, "scalar note = 60");
    check_float(tc.output_values[1], 100.0f / 127.0f, "scalar velocity = 100/127");
    check_float(tc.output_values[2], 1.0f, "scalar gate = 1");
    check_float(tc.output_values[3], 1.0f, "scalar trigger = 1");

    uint64_t held_id = 0;
    if (auto* e = find_note_on(buf, 60)) {
        check(e->note_id != 0, "note_id is non-zero");
        check_float(e->value, 100.0f / 127.0f, 1e-4f, "NOTE_ON value = velocity normalized");
        held_id = e->note_id;
    }

    // Second note → second NOTE_ON
    tc.reset_outputs();
    op.inject_events({note_on(1, 64, 80)});
    run_frame(op, tc);
    buf = tc.notes_out();
    check(count_events(buf, VIVID_NOTE_ON) == 1, "second frame: one NOTE_ON for note 64");
    check(find_note_on(buf, 64) != nullptr, "NOTE_ON for note 64 present");

    // Note-off the first → matching NOTE_OFF carrying the original id
    tc.reset_outputs();
    op.inject_events({note_off(1, 60)});
    run_frame(op, tc);
    buf = tc.notes_out();
    check(count_events(buf, VIVID_NOTE_OFF) == 1, "one NOTE_OFF emitted");
    check(find_event(buf, VIVID_NOTE_OFF, held_id) != nullptr,
          "NOTE_OFF carries the original note_id");
    check_float(tc.output_values[2], 1.0f, "gate still 1 (one note remains)");

    // Final note-off → gate falls
    tc.reset_outputs();
    op.inject_events({note_off(1, 64)});
    run_frame(op, tc);
    check_float(tc.output_values[2], 0.0f, "gate = 0 after all notes released");
    check_float(tc.output_values[3], 0.0f, "trigger = 0 (no note-on)");
}

// ---------------------------------------------------------------------------
// 2. poly_shared expression broadcast: PRESSURE / TIMBRE land on every
//    held note's id when the input is on a non-MPE channel
// ---------------------------------------------------------------------------
static void test_poly_shared_expression_broadcast() {
    std::fprintf(stderr, "\n--- poly_shared expression broadcast ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    op.inject_events({note_on(1, 60, 100), note_on(1, 64, 90)});
    run_frame(op, tc);

    // Snapshot the two note_ids
    auto* buf = tc.notes_out();
    uint64_t id_60 = 0, id_64 = 0;
    if (auto* e = find_note_on(buf, 60)) id_60 = e->note_id;
    if (auto* e = find_note_on(buf, 64)) id_64 = e->note_id;
    check(id_60 != 0 && id_64 != 0 && id_60 != id_64, "two distinct note_ids");

    // Push expression. In poly_shared, MidiInput stores the values on the
    // held buffer but does NOT emit per-note events for poly_shared — that
    // mode broadcasts to scalars only. Verify the scalar surface.
    tc.reset_outputs();
    op.inject_events({
        pitch_bend(1, 8192 + 4096),      // +0.5 normalized
        channel_pressure(1, 64),           // 64/127
        cc(1, 11, 50),                     // expression CC11
    });
    run_frame(op, tc);

    check_float(tc.output_values[4], 0.5f, "scalar pitch_bend = 0.5");
    check_float(tc.output_values[8], 64.0f / 127.0f, "scalar aftertouch (port 8)");
    check_float(tc.output_values[9], 50.0f / 127.0f, "scalar expression (port 9)");
}

// ---------------------------------------------------------------------------
// 3. mpe_lower per-note expression emits PITCH_BEND / PRESSURE / TIMBRE
//    keyed on the member channel's active note_id
// ---------------------------------------------------------------------------
static void test_mpe_lower_per_note() {
    std::fprintf(stderr, "\n--- mpe_lower per-note PITCH_BEND/PRESSURE/TIMBRE ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 1.0f;  // mpe_lower

    // Hold one note on member channel 2 and one on channel 3.
    op.inject_events({note_on(2, 60, 100), note_on(3, 67, 90)});
    run_frame(op, tc);
    auto* buf = tc.notes_out();
    uint64_t id_ch2 = find_note_on(buf, 60) ? find_note_on(buf, 60)->note_id : 0;
    uint64_t id_ch3 = find_note_on(buf, 67) ? find_note_on(buf, 67)->note_id : 0;
    check(id_ch2 != 0 && id_ch3 != 0 && id_ch2 != id_ch3, "two distinct MPE note_ids");

    // Bend ch2, pressure ch3, slide (CC74 → TIMBRE) on ch2.
    tc.reset_outputs();
    op.inject_events({
        pitch_bend(2, 8192 + 4096),        // ch2 → +24 semis (0.5 * 48)
        channel_pressure(3, 127),           // ch3 → 1.0
        cc(2, 74, 100),                     // ch2 slide → TIMBRE 100/127
    });
    run_frame(op, tc);
    buf = tc.notes_out();

    if (auto* e = find_event(buf, VIVID_NOTE_PITCH_BEND, id_ch2)) {
        check_float(e->value, 24.0f, 1e-4f, "ch2 PITCH_BEND = +24 semis");
    } else {
        check(false, "expected PITCH_BEND on ch2 note");
    }
    if (auto* e = find_event(buf, VIVID_NOTE_PRESSURE, id_ch3)) {
        check_float(e->value, 1.0f, 1e-4f, "ch3 PRESSURE = 1.0");
    } else {
        check(false, "expected PRESSURE on ch3 note");
    }
    if (auto* e = find_event(buf, VIVID_NOTE_TIMBRE, id_ch2)) {
        check_float(e->value, 100.0f / 127.0f, 1e-4f, "ch2 TIMBRE = 100/127");
    } else {
        check(false, "expected TIMBRE on ch2 note");
    }
    // Cross-channel events did not bleed
    check(find_event(buf, VIVID_NOTE_PITCH_BEND, id_ch3) == nullptr,
          "ch3 didn't receive ch2's bend");
    check(find_event(buf, VIVID_NOTE_PRESSURE, id_ch2) == nullptr,
          "ch2 didn't receive ch3's pressure");
}

// ---------------------------------------------------------------------------
// 4. mpe_upper variant — PITCH_BEND on member channel 15
// ---------------------------------------------------------------------------
static void test_mpe_upper_per_note() {
    std::fprintf(stderr, "\n--- mpe_upper per-note PITCH_BEND ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 2.0f;  // mpe_upper

    op.inject_events({note_on(15, 60, 100)});
    run_frame(op, tc);
    auto* buf = tc.notes_out();
    uint64_t id = find_note_on(buf, 60) ? find_note_on(buf, 60)->note_id : 0;
    check(id != 0, "MPE upper member note_id present");

    tc.reset_outputs();
    op.inject_events({pitch_bend(15, 8192 + 2048)});  // +0.25 normalized → +12 semis
    run_frame(op, tc);
    buf = tc.notes_out();
    if (auto* e = find_event(buf, VIVID_NOTE_PITCH_BEND, id)) {
        check_float(e->value, 12.0f, 1e-4f, "ch15 PITCH_BEND = +12 semis");
    } else {
        check(false, "expected PITCH_BEND on ch15");
    }
}

// ---------------------------------------------------------------------------
// 5. Same MIDI pitch on different MPE channels → independent note_ids,
//    independent PRESSURE routing
// ---------------------------------------------------------------------------
static void test_duplicate_pitch_mpe() {
    std::fprintf(stderr, "\n--- MPE duplicate pitch across channels ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 1.0f;  // mpe_lower

    op.inject_events({note_on(2, 60, 100), note_on(3, 60, 80)});
    run_frame(op, tc);
    auto* buf = tc.notes_out();
    check(count_events(buf, VIVID_NOTE_ON) == 2, "two NOTE_ON events for same pitch");
    uint64_t id_a = 0, id_b = 0;
    int seen = 0;
    for (uint32_t i = 0; i < buf->count; ++i) {
        const auto& e = buf->events[i];
        if (e.type == VIVID_NOTE_ON && e.note_number == 60) {
            if (seen == 0) id_a = e.note_id;
            else            id_b = e.note_id;
            ++seen;
        }
    }
    check(id_a != 0 && id_b != 0 && id_a != id_b,
          "same-pitch MPE notes get distinct note_ids");

    // Pressure on ch2 only updates id_a
    tc.reset_outputs();
    op.inject_events({channel_pressure(2, 127)});
    run_frame(op, tc);
    buf = tc.notes_out();
    check(find_event(buf, VIVID_NOTE_PRESSURE, id_a) != nullptr,
          "PRESSURE on ch2 note (id_a)");
    check(find_event(buf, VIVID_NOTE_PRESSURE, id_b) == nullptr,
          "PRESSURE did not bleed to id_b");

    // Note-off ch2 only releases id_a
    tc.reset_outputs();
    op.inject_events({note_off(2, 60)});
    run_frame(op, tc);
    buf = tc.notes_out();
    check(find_event(buf, VIVID_NOTE_OFF, id_a) != nullptr,
          "NOTE_OFF for ch2 carried id_a");
    check(find_event(buf, VIVID_NOTE_OFF, id_b) == nullptr,
          "id_b not released");
}

// ---------------------------------------------------------------------------
// 6. Same-pitch overlap on poly_shared: distinct note_ids, FIFO release
// ---------------------------------------------------------------------------
static void test_poly_shared_same_pitch_overlap() {
    std::fprintf(stderr, "\n--- poly_shared same-pitch overlap (FIFO release) ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    op.inject_events({note_on(1, 60, 100), note_on(1, 60, 80)});
    run_frame(op, tc);
    auto* buf = tc.notes_out();
    check(count_events(buf, VIVID_NOTE_ON) == 2, "two NOTE_ON for same pitch");
    uint64_t first_id = 0, second_id = 0;
    int seen = 0;
    for (uint32_t i = 0; i < buf->count; ++i) {
        const auto& e = buf->events[i];
        if (e.type == VIVID_NOTE_ON && e.note_number == 60) {
            if (seen == 0) first_id = e.note_id;
            else            second_id = e.note_id;
            ++seen;
        }
    }
    check(first_id != 0 && second_id != 0 && first_id != second_id,
          "distinct note_ids for same pitch");

    // First note-off releases the older note (FIFO)
    tc.reset_outputs();
    op.inject_events({note_off(1, 60)});
    run_frame(op, tc);
    buf = tc.notes_out();
    check(find_event(buf, VIVID_NOTE_OFF, first_id) != nullptr,
          "first note-off released the oldest note (FIFO)");

    tc.reset_outputs();
    op.inject_events({note_off(1, 60)});
    run_frame(op, tc);
    buf = tc.notes_out();
    check(find_event(buf, VIVID_NOTE_OFF, second_id) != nullptr,
          "second note-off released the newer note");
}

// ---------------------------------------------------------------------------
// 7. Polyphonic key pressure (0xA0) → PRESSURE event regardless of mode
// ---------------------------------------------------------------------------
static void test_poly_key_pressure_native_emit() {
    std::fprintf(stderr, "\n--- poly key pressure → native PRESSURE event ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    op.inject_events({note_on(1, 60, 100)});
    run_frame(op, tc);
    auto* buf = tc.notes_out();
    uint64_t id = find_note_on(buf, 60) ? find_note_on(buf, 60)->note_id : 0;
    check(id != 0, "NOTE_ON id captured");

    tc.reset_outputs();
    op.inject_events({poly_pressure(1, 60, 90)});
    run_frame(op, tc);
    buf = tc.notes_out();
    if (auto* e = find_event(buf, VIVID_NOTE_PRESSURE, id)) {
        check_float(e->value, 90.0f / 127.0f, 1e-4f,
                    "PRESSURE value = 90/127");
    } else {
        check(false, "expected PRESSURE event from poly key pressure");
    }
}

// ---------------------------------------------------------------------------
// 8. Scalar aftertouch / expression outputs (port indices 8 / 9 post-PR2)
// ---------------------------------------------------------------------------
static void test_scalar_aftertouch_expression() {
    std::fprintf(stderr, "\n--- scalar aftertouch/expression on ports [8]/[9] ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    op.inject_events({
        channel_pressure(1, 100),
        cc(1, 11, 64),
    });
    run_frame(op, tc);

    check_float(tc.output_values[8], 100.0f / 127.0f, "aftertouch scalar (port 8)");
    check_float(tc.output_values[9], 64.0f / 127.0f, "expression scalar (port 9)");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::fprintf(stderr, "=== MidiInput per-note expression (post-PR2 native event stream) ===\n");

    test_poly_shared_note_lifecycle();
    test_poly_shared_expression_broadcast();
    test_mpe_lower_per_note();
    test_mpe_upper_per_note();
    test_duplicate_pitch_mpe();
    test_poly_shared_same_pitch_overlap();
    test_poly_key_pressure_native_emit();
    test_scalar_aftertouch_expression();

    std::fprintf(stderr, "\n=== Results: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
