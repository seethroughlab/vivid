// MidiInput per-note expression and MPE correctness tests.
// Tests poly_shared broadcast, MPE per-note routing, lane_id stability,
// 2-byte MIDI passthrough, and scalar expression outputs.
//
// Compiles midi_input.cpp directly to access inject_events() test seam.

#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "runtime/graph/lane_buffer.h"
#include "runtime/graph/lane_output_adapter.h"
#include "RtMidi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "test_helpers.h"

// Pull in the operator source directly so we can access the MidiInput struct
// and its inject_events() test seam.  VIVID_REGISTER will produce extern "C"
// symbols but they are harmless in a test executable.
#include "../../operators/control/midi_input/midi_input.cpp"

// ---------------------------------------------------------------------------
// Test harness — builds a VividFrameContext with output buffers
// ---------------------------------------------------------------------------

static constexpr int kNumOutputScalars = 19;  // ports [0..18], scalars at [0-6,11,12]
static constexpr int kNumLanePorts     = 19;  // lane port array sized to match output_lanes indexing
static constexpr int kMaxLanes         = 16;

struct TestLanePort {
    vivid::LaneBuffer buf{kMaxLanes};
    VividLaneOutput output{};

    TestLanePort() {
        output = vivid::make_lane_output(&buf);
    }

    // Convenience accessors for test assertions
    uint32_t length() const { return buf.committed_length; }
    const float* data() const { return buf.data.data(); }
};

struct TestFrameContext {
    float param_values[8]  = {};   // enough for 5 params + margin
    float output_values[kNumOutputScalars] = {};
    TestLanePort lane_ports[kNumLanePorts];
    VividLaneOutput output_lanes[kNumLanePorts] = {};
    void* custom_outputs[1] = {};

    VividFrameContext ctx = {};

    TestFrameContext() {
        ctx.param_values     = param_values;
        ctx.output_values    = output_values;
        for (int i = 0; i < kNumLanePorts; ++i)
            output_lanes[i] = lane_ports[i].output;
        ctx.output_lanes     = output_lanes;
        ctx.custom_outputs   = custom_outputs;
        ctx.custom_output_count = 1;
        ctx.time        = 0.0;
        ctx.delta_time  = 1.0 / 60.0;
        ctx.frame       = 0;
    }

    void reset_outputs() {
        std::memset(output_values, 0, sizeof(output_values));
        for (int i = 0; i < kNumLanePorts; ++i)
            lane_ports[i].buf.reset();
        custom_outputs[0] = nullptr;
    }
};

// Helper: sync param_values into the operator's Param<> members.
// When calling process_frame directly (not through VIVID_REGISTER dispatch),
// the param syncing step is skipped, so Param<>::value stays at defaults.
// Sync param_values into the operator's Param<> members and run process_frame.
// When calling process_frame directly (not through VIVID_REGISTER dispatch),
// the param syncing step is skipped, so Param<>::value stays at defaults.
static void run_frame(MidiInput& op, TestFrameContext& tc) {
    op.skip_midi_init();
    op.device.value    = tc.param_values[0];
    op.channel.value   = tc.param_values[1];
    op.cc_number.value = tc.param_values[2];
    op.learn.value     = tc.param_values[3];
    op.mode.value      = tc.param_values[4];
    op.process_frame(&tc.ctx);
}

// Helper: build a raw MIDI message
static std::vector<unsigned char> midi_msg(unsigned char status, unsigned char d1, unsigned char d2 = 0) {
    if ((status & 0xF0) == 0xD0 || (status & 0xF0) == 0xC0) {
        // 2-byte messages
        return {status, d1};
    }
    return {status, d1, d2};
}

// Helper: note on (channel is 1-based)
static std::vector<unsigned char> note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    return {static_cast<unsigned char>(0x90 | (ch - 1)), note, vel};
}

// Helper: note off
static std::vector<unsigned char> note_off(uint8_t ch, uint8_t note) {
    return {static_cast<unsigned char>(0x80 | (ch - 1)), note, 0};
}

// Helper: pitch bend (channel 1-based, value 0..16383, center=8192)
static std::vector<unsigned char> pitch_bend(uint8_t ch, int value) {
    return {static_cast<unsigned char>(0xE0 | (ch - 1)),
            static_cast<unsigned char>(value & 0x7F),
            static_cast<unsigned char>((value >> 7) & 0x7F)};
}

// Helper: channel pressure (aftertouch, 2-byte message)
static std::vector<unsigned char> channel_pressure(uint8_t ch, uint8_t val) {
    return {static_cast<unsigned char>(0xD0 | (ch - 1)), val};
}

// Helper: CC
static std::vector<unsigned char> cc(uint8_t ch, uint8_t cc_num, uint8_t val) {
    return {static_cast<unsigned char>(0xB0 | (ch - 1)), cc_num, val};
}

// Helper: poly key pressure
static std::vector<unsigned char> poly_pressure(uint8_t ch, uint8_t note, uint8_t val) {
    return {static_cast<unsigned char>(0xA0 | (ch - 1)), note, val};
}

// ---------------------------------------------------------------------------
// 1. Regression: poly_shared basic behavior unchanged
// ---------------------------------------------------------------------------
static void test_poly_shared_regression() {
    std::fprintf(stderr, "\n--- poly_shared regression ---\n");
    MidiInput op;
    TestFrameContext tc;

    // Set mode=0 (poly_shared), channel=0 (omni)
    tc.param_values[0] = 0.0f;  // device
    tc.param_values[1] = 0.0f;  // channel (omni)
    tc.param_values[2] = 1.0f;  // cc_number
    tc.param_values[3] = 0.0f;  // learn
    tc.param_values[4] = 0.0f;  // mode (poly_shared)

    // Send note on ch1, note 60, vel 100
    op.inject_events({note_on(1, 60, 100)});
    run_frame(op, tc);

    check_float(tc.output_values[0], 60.0f, "note = 60");
    check_float(tc.output_values[1], 100.0f / 127.0f, "velocity = 100/127");
    check_float(tc.output_values[2], 1.0f, "gate = 1 (note held)");
    check_float(tc.output_values[3], 1.0f, "trigger = 1 (note-on this frame)");

    // Check lane arrays
    check(tc.lane_ports[7].length() == 1, "notes lane len = 1");
    check_float(tc.lane_ports[7].data()[0], 60.0f, "notes[0] = 60");
    check_float(tc.lane_ports[8].data()[0], 100.0f / 127.0f, "velocities[0] = 100/127");
    check_float(tc.lane_ports[9].data()[0], 1.0f, "gates[0] = 1");

    // Second note
    tc.reset_outputs();
    op.inject_events({note_on(1, 64, 80)});
    run_frame(op, tc);
    check(tc.lane_ports[7].length() == 2, "notes lane len = 2 after second note");
    check_float(tc.lane_ports[7].data()[0], 60.0f, "notes[0] still 60");
    check_float(tc.lane_ports[7].data()[1], 64.0f, "notes[1] = 64");

    // Note off first note
    tc.reset_outputs();
    op.inject_events({note_off(1, 60)});
    run_frame(op, tc);
    check(tc.lane_ports[7].length() == 1, "notes lane len = 1 after note-off");
    check_float(tc.lane_ports[7].data()[0], 64.0f, "remaining note = 64");
    check_float(tc.output_values[2], 1.0f, "gate still 1 (one note held)");

    // Note off second
    tc.reset_outputs();
    op.inject_events({note_off(1, 64)});
    run_frame(op, tc);
    check(tc.lane_ports[7].length() == 0, "notes lane len = 0 after all off");
    check_float(tc.output_values[2], 0.0f, "gate = 0 (no notes)");
    check_float(tc.output_values[3], 0.0f, "trigger = 0 (no note-on)");
}

// ---------------------------------------------------------------------------
// 2. poly_shared broadcast: expression data broadcasts to all lanes
// ---------------------------------------------------------------------------
static void test_poly_shared_broadcast() {
    std::fprintf(stderr, "\n--- poly_shared expression broadcast ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    // Hold two notes
    op.inject_events({note_on(1, 60, 100), note_on(1, 64, 90)});
    run_frame(op, tc);

    tc.reset_outputs();
    // Send pitch bend, channel pressure, CC74 (slide), CC11 (expression)
    op.inject_events({
        pitch_bend(1, 8192 + 4096),      // +0.5
        channel_pressure(1, 64),           // 64/127 ≈ 0.5039
        cc(1, 74, 100),                    // slide = 100/127
        cc(1, 11, 50),                     // expression = 50/127
    });
    run_frame(op, tc);

    float expected_bend = 4096.0f / 8192.0f;  // 0.5
    float expected_pressure = 64.0f / 127.0f;
    float expected_slide = 100.0f / 127.0f;
    float expected_expression = 50.0f / 127.0f;

    check(tc.lane_ports[14].length() == 2, "pitch_bends lane len = 2");
    check_float(tc.lane_ports[14].data()[0], expected_bend, "pitch_bends[0] broadcast");
    check_float(tc.lane_ports[14].data()[1], expected_bend, "pitch_bends[1] broadcast");

    check(tc.lane_ports[15].length() == 2, "pressures lane len = 2");
    check_float(tc.lane_ports[15].data()[0], expected_pressure, "pressures[0] broadcast");
    check_float(tc.lane_ports[15].data()[1], expected_pressure, "pressures[1] broadcast");

    check(tc.lane_ports[16].length() == 2, "slides lane len = 2");
    check_float(tc.lane_ports[16].data()[0], expected_slide, "slides[0] broadcast");
    check_float(tc.lane_ports[16].data()[1], expected_slide, "slides[1] broadcast");

    check(tc.lane_ports[17].length() == 2, "expressions lane len = 2");
    check_float(tc.lane_ports[17].data()[0], expected_expression, "expressions[0] broadcast");
    check_float(tc.lane_ports[17].data()[1], expected_expression, "expressions[1] broadcast");

    // Scalar outputs
    check_float(tc.output_values[4], expected_bend, "scalar pitch_bend");
    check_float(tc.output_values[11], expected_pressure, "scalar aftertouch");
    check_float(tc.output_values[12], expected_expression, "scalar expression");
}

// ---------------------------------------------------------------------------
// 3. mpe_lower: per-note expression routed by channel
// ---------------------------------------------------------------------------
static void test_mpe_lower_per_note() {
    std::fprintf(stderr, "\n--- mpe_lower per-note expression ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 1.0f;  // mpe_lower

    // Notes on member channels 2 and 3
    op.inject_events({note_on(2, 60, 100), note_on(3, 67, 90)});
    run_frame(op, tc);
    check(tc.lane_ports[7].length() == 2, "2 held notes");

    tc.reset_outputs();
    // Per-channel expression: bend ch2, pressure ch3
    op.inject_events({
        pitch_bend(2, 8192 + 4096),        // ch2 bend = +0.5
        pitch_bend(3, 8192 - 2048),        // ch3 bend = -0.25
        channel_pressure(2, 127),           // ch2 pressure = 1.0
        channel_pressure(3, 0),             // ch3 pressure = 0.0
        cc(2, 74, 100),                     // ch2 slide
        cc(3, 74, 50),                      // ch3 slide
    });
    run_frame(op, tc);

    check(tc.lane_ports[14].length() == 2, "pitch_bends len=2");
    check_float(tc.lane_ports[14].data()[0], 4096.0f / 8192.0f, "ch2 bend = +0.5");
    check_float(tc.lane_ports[14].data()[1], -2048.0f / 8192.0f, "ch3 bend = -0.25");

    check_float(tc.lane_ports[15].data()[0], 1.0f, "ch2 pressure = 1.0");
    check_float(tc.lane_ports[15].data()[1], 0.0f, "ch3 pressure = 0.0");

    check_float(tc.lane_ports[16].data()[0], 100.0f / 127.0f, "ch2 slide");
    check_float(tc.lane_ports[16].data()[1], 50.0f / 127.0f, "ch3 slide");

    // Channels lane
    check_float(tc.lane_ports[18].data()[0], 2.0f, "channels[0] = 2");
    check_float(tc.lane_ports[18].data()[1], 3.0f, "channels[1] = 3");
}

// ---------------------------------------------------------------------------
// 4. mpe_upper: per-note expression (manager ch16, members 15..2)
// ---------------------------------------------------------------------------
static void test_mpe_upper_per_note() {
    std::fprintf(stderr, "\n--- mpe_upper per-note expression ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 2.0f;  // mpe_upper

    // Notes on member channels 15 and 14
    op.inject_events({note_on(15, 60, 100), note_on(14, 67, 90)});
    run_frame(op, tc);
    check(tc.lane_ports[7].length() == 2, "2 held notes");

    tc.reset_outputs();
    op.inject_events({
        pitch_bend(15, 8192 + 2048),    // ch15 bend = +0.25
        pitch_bend(14, 8192),            // ch14 bend = 0
    });
    run_frame(op, tc);

    check_float(tc.lane_ports[14].data()[0], 2048.0f / 8192.0f, "ch15 bend = +0.25");
    check_float(tc.lane_ports[14].data()[1], 0.0f, "ch14 bend = 0");
}

// ---------------------------------------------------------------------------
// 5. Duplicate pitch across channels (MPE)
// ---------------------------------------------------------------------------
static void test_duplicate_pitch_mpe() {
    std::fprintf(stderr, "\n--- MPE duplicate pitch across channels ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 1.0f;  // mpe_lower

    // Same note 60 on two different member channels
    op.inject_events({note_on(2, 60, 100), note_on(3, 60, 80)});
    run_frame(op, tc);
    check(tc.lane_ports[7].length() == 2, "2 held notes (same pitch, different channels)");
    check_float(tc.lane_ports[7].data()[0], 60.0f, "notes[0] = 60");
    check_float(tc.lane_ports[7].data()[1], 60.0f, "notes[1] = 60");
    check_float(tc.lane_ports[8].data()[0], 100.0f / 127.0f, "vel[0] from ch2");
    check_float(tc.lane_ports[8].data()[1], 80.0f / 127.0f, "vel[1] from ch3");

    // Note-off ch2 only removes the ch2 entry
    tc.reset_outputs();
    op.inject_events({note_off(2, 60)});
    run_frame(op, tc);
    check(tc.lane_ports[7].length() == 1, "1 held note after ch2 off");
    check_float(tc.lane_ports[8].data()[0], 80.0f / 127.0f, "remaining vel from ch3");
    check_float(tc.lane_ports[18].data()[0], 3.0f, "remaining channel = 3");
}

// ---------------------------------------------------------------------------
// 6. Lane ID stability across compaction
// ---------------------------------------------------------------------------
static void test_lane_id_stability() {
    std::fprintf(stderr, "\n--- lane_id stability ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    // Add 3 notes
    op.inject_events({note_on(1, 60, 100), note_on(1, 64, 90), note_on(1, 67, 80)});
    run_frame(op, tc);
    check(tc.lane_ports[13].length() == 3, "3 lane_ids");

    float id0 = tc.lane_ports[13].data()[0];
    float id1 = tc.lane_ports[13].data()[1];
    float id2 = tc.lane_ports[13].data()[2];
    check(id0 != id1 && id1 != id2 && id0 != id2, "all lane_ids unique");

    // Remove middle note (64) — remaining IDs should be unchanged
    tc.reset_outputs();
    op.inject_events({note_off(1, 64)});
    run_frame(op, tc);
    check(tc.lane_ports[13].length() == 2, "2 lane_ids after removal");
    check_float(tc.lane_ports[13].data()[0], id0, "first lane_id preserved");
    check_float(tc.lane_ports[13].data()[1], id2, "third lane_id (now second) preserved");

    // Add new note — gets a new unique ID
    tc.reset_outputs();
    op.inject_events({note_on(1, 72, 70)});
    run_frame(op, tc);
    check(tc.lane_ports[13].length() == 3, "3 lane_ids again");
    float new_id = tc.lane_ports[13].data()[2];
    check(new_id != id0 && new_id != id1 && new_id != id2, "new note gets fresh lane_id");
}

// ---------------------------------------------------------------------------
// 7. Scalar aftertouch and expression outputs
// ---------------------------------------------------------------------------
static void test_scalar_aftertouch_expression() {
    std::fprintf(stderr, "\n--- scalar aftertouch/expression ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    op.inject_events({
        channel_pressure(1, 100),   // aftertouch
        cc(1, 11, 64),              // expression
    });
    run_frame(op, tc);

    check_float(tc.output_values[11], 100.0f / 127.0f, "aftertouch scalar");
    check_float(tc.output_values[12], 64.0f / 127.0f, "expression scalar");
}

// ---------------------------------------------------------------------------
// 8. notes_out emits native NOTE_ON / NOTE_OFF events (id round-trip)
// ---------------------------------------------------------------------------
static void test_notes_out_native_events() {
    std::fprintf(stderr, "\n--- notes_out native NOTE_ON/NOTE_OFF round-trip ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 0.0f;  // poly_shared

    op.inject_events({note_on(1, 60, 100)});
    run_frame(op, tc);

    auto* buf = static_cast<VividNoteBuffer*>(tc.custom_outputs[0]);
    check(buf != nullptr, "notes_out buffer present");
    check(buf->count == 1, "exactly one event after NOTE_ON");
    uint64_t held_id = 0;
    if (buf->count >= 1) {
        check(buf->events[0].type == VIVID_NOTE_ON, "first event is NOTE_ON");
        check(buf->events[0].note_number == 60, "note 60");
        check_float(buf->events[0].value, 100.0f / 127.0f, 1e-4f,
                    "velocity 100/127 normalized");
        check(buf->events[0].note_id != 0, "note_id is non-zero");
        held_id = buf->events[0].note_id;
    }

    // Second frame: send the matching note-off; should emit NOTE_OFF carrying
    // the same id we got back from the on event.
    tc.reset_outputs();
    op.inject_events({note_off(1, 60)});
    run_frame(op, tc);
    buf = static_cast<VividNoteBuffer*>(tc.custom_outputs[0]);
    check(buf != nullptr, "notes_out buffer present on note-off frame");
    check(buf->count == 1, "exactly one event after NOTE_OFF");
    if (buf->count >= 1) {
        check(buf->events[0].type == VIVID_NOTE_OFF, "event is NOTE_OFF");
        check(buf->events[0].note_id == held_id,
              "NOTE_OFF carries the same note_id as the matching NOTE_ON");
    }
}

// ---------------------------------------------------------------------------
// 9. MPE per-note pitch_bend emits a native PITCH_BEND event keyed on the
//    member channel's active note_id
// ---------------------------------------------------------------------------
static void test_mpe_native_expression_emit() {
    std::fprintf(stderr, "\n--- MPE per-note PITCH_BEND emits native expression event ---\n");
    MidiInput op;
    TestFrameContext tc;
    tc.param_values[4] = 1.0f;  // mpe_lower

    // Hold a note on channel 2 (MPE member), then bend it.
    op.inject_events({note_on(2, 60, 100), pitch_bend(2, 8192 + 4096)});
    run_frame(op, tc);

    auto* buf = static_cast<VividNoteBuffer*>(tc.custom_outputs[0]);
    check(buf != nullptr, "notes_out buffer present");
    bool saw_on = false, saw_bend = false;
    uint64_t on_id = 0;
    for (uint32_t i = 0; i < buf->count; ++i) {
        const auto& e = buf->events[i];
        if (e.type == VIVID_NOTE_ON && e.note_number == 60) {
            saw_on = true;
            on_id = e.note_id;
        } else if (e.type == VIVID_NOTE_PITCH_BEND) {
            check(on_id != 0, "PITCH_BEND must come after the NOTE_ON in the same buffer");
            check(e.note_id == on_id, "PITCH_BEND keyed on the held note's note_id");
            saw_bend = true;
        }
    }
    check(saw_on, "NOTE_ON emitted for ch2 note 60");
    check(saw_bend, "PITCH_BEND event emitted for the held MPE note");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::fprintf(stderr, "=== MidiInput Per-Note Expression Tests ===\n");

    test_poly_shared_regression();
    test_poly_shared_broadcast();
    test_mpe_lower_per_note();
    test_mpe_upper_per_note();
    test_duplicate_pitch_mpe();
    test_lane_id_stability();
    test_scalar_aftertouch_expression();
    test_notes_out_native_events();
    test_mpe_native_expression_emit();

    std::fprintf(stderr, "\n=== Results: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
