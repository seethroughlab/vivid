// MidiOut byte-encoding correctness tests.
//
// Compiles midi_out.cpp directly to access the test_capture_mode_ seam,
// which redirects sendMessage() calls into test_captured_ without requiring
// a real MIDI port.

#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "RtMidi.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "test_helpers.h"

#include "../../operators/audio/midi_out/midi_out.cpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static VividNoteBuffer make_buf(VividNoteEvent ev) {
    VividNoteBuffer b{};
    b.count = 1;
    b.events[0] = ev;
    return b;
}

static VividAudioContext make_ctx(void** custom_inputs, uint32_t count) {
    VividAudioContext ctx{};
    ctx.buffer_size = 256;
    ctx.sample_rate = 48000;
    ctx.custom_inputs = custom_inputs;
    ctx.custom_input_count = count;
    return ctx;
}

// ---------------------------------------------------------------------------
// 1. NOTE_ON → correct status, pitch, and velocity bytes
// ---------------------------------------------------------------------------
static void test_note_on_encoding() {
    std::fprintf(stderr, "\n--- NOTE_ON encoding ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_ON;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 0.8f;  // int(0.8 * 127) = 101

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty()) {
        const auto& m = op.test_captured_[0];
        check(m.size() == 3, "NOTE_ON is 3 bytes");
        check(m[0] == 0x90, "status = 0x90 (ch 1)");
        check(m[1] == 60,   "pitch = 60");
        check(m[2] == 101,  "velocity = 101 (0.8 * 127)");
    }
}

// ---------------------------------------------------------------------------
// 2. NOTE_ON with velocity_scale = 0.5 → velocity halved
// ---------------------------------------------------------------------------
static void test_velocity_scale_half() {
    std::fprintf(stderr, "\n--- velocity_scale = 0.5 ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;
    op.velocity_scale.value = 0.5f;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_ON;
    ev.note_number = 64;
    ev.note_id     = 1;
    ev.value       = 0.8f;  // 0.8 * 0.5 = 0.4 → int(0.4 * 127) = 50

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty())
        check(op.test_captured_[0][2] == 50, "velocity = 50 (halved)");
}

// ---------------------------------------------------------------------------
// 3. NOTE_ON with velocity_scale = 2.0 → velocity clamped at 127
// ---------------------------------------------------------------------------
static void test_velocity_scale_clamp() {
    std::fprintf(stderr, "\n--- velocity_scale = 2.0 clamped at 127 ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;
    op.velocity_scale.value = 2.0f;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_ON;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 0.8f;  // 0.8 * 2.0 = 1.6 → int(1.6 * 127) = 203 → clamped to 127

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty())
        check(op.test_captured_[0][2] == 127, "velocity clamped to 127");
}

// ---------------------------------------------------------------------------
// 4. NOTE_OFF → [0x80, pitch, 0]
// ---------------------------------------------------------------------------
static void test_note_off_encoding() {
    std::fprintf(stderr, "\n--- NOTE_OFF encoding ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_OFF;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 0.0f;

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty()) {
        const auto& m = op.test_captured_[0];
        check(m[0] == 0x80, "status = 0x80 (note-off ch 1)");
        check(m[1] == 60,   "pitch = 60");
        check(m[2] == 0,    "velocity = 0");
    }
}

// ---------------------------------------------------------------------------
// 5. PITCH_BEND at +12 semitones → 14-bit max [0xE0, 0x7F, 0x7F]
// ---------------------------------------------------------------------------
static void test_pitch_bend_max() {
    std::fprintf(stderr, "\n--- PITCH_BEND +12 semitones → 14-bit max ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_PITCH_BEND;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 12.0f;  // +12 semitones → bend14 = 8192 + 8191 = 16383

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty()) {
        const auto& m = op.test_captured_[0];
        check(m[0] == 0xE0, "status = 0xE0 (pitch bend ch 1)");
        check(m[1] == 0x7F, "LSB = 0x7F (16383 & 0x7F)");
        check(m[2] == 0x7F, "MSB = 0x7F (16383 >> 7)");
    }
}

// ---------------------------------------------------------------------------
// 6. PITCH_BEND at 0 → centre 8192 [0xE0, 0x00, 0x40]
// ---------------------------------------------------------------------------
static void test_pitch_bend_center() {
    std::fprintf(stderr, "\n--- PITCH_BEND 0 semitones → centre 8192 ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_PITCH_BEND;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 0.0f;  // 0 semitones → bend14 = 8192

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty()) {
        const auto& m = op.test_captured_[0];
        check(m[0] == 0xE0, "status = 0xE0");
        check(m[1] == 0x00, "LSB = 0x00 (8192 & 0x7F)");
        check(m[2] == 0x40, "MSB = 0x40 (8192 >> 7 = 64)");
    }
}

// ---------------------------------------------------------------------------
// 7. PRESSURE → [0xA0, pitch, pressure_byte]
// ---------------------------------------------------------------------------
static void test_pressure_encoding() {
    std::fprintf(stderr, "\n--- PRESSURE encoding ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_PRESSURE;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 0.5f;  // int(0.5 * 127) = 63

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty()) {
        const auto& m = op.test_captured_[0];
        check(m[0] == 0xA0, "status = 0xA0 (poly pressure ch 1)");
        check(m[1] == 60,   "pitch = 60");
        check(m[2] == 63,   "pressure = 63 (0.5 * 127)");
    }
}

// ---------------------------------------------------------------------------
// 8. TIMBRE → [0xB0, 74, value_byte]
// ---------------------------------------------------------------------------
static void test_timbre_encoding() {
    std::fprintf(stderr, "\n--- TIMBRE encoding (CC 74) ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_TIMBRE;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 1.0f;  // int(1.0 * 127) = 127

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty()) {
        const auto& m = op.test_captured_[0];
        check(m[0] == 0xB0, "status = 0xB0 (CC ch 1)");
        check(m[1] == 74,   "CC number = 74 (brightness/slide)");
        check(m[2] == 127,  "value = 127");
    }
}

// ---------------------------------------------------------------------------
// 9. Channel 2 → status bytes carry channel bit = 1
// ---------------------------------------------------------------------------
static void test_channel_2() {
    std::fprintf(stderr, "\n--- channel 2 status bytes ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;
    op.channel.value = 2.0f;

    VividNoteEvent ev{};
    ev.type        = VIVID_NOTE_ON;
    ev.note_number = 60;
    ev.note_id     = 1;
    ev.value       = 0.5f;

    VividNoteBuffer buf = make_buf(ev);
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 1, "one message captured");
    if (!op.test_captured_.empty())
        check(op.test_captured_[0][0] == 0x91, "status = 0x91 (NOTE_ON ch 2)");
}

// ---------------------------------------------------------------------------
// 10. Empty buffer → no messages captured
// ---------------------------------------------------------------------------
static void test_empty_buffer() {
    std::fprintf(stderr, "\n--- empty buffer → no messages ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteBuffer buf{};
    buf.count = 0;
    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.empty(), "no messages on empty buffer");
}

// ---------------------------------------------------------------------------
// 11. Multiple events in one buffer → all captured in order
// ---------------------------------------------------------------------------
static void test_multiple_events() {
    std::fprintf(stderr, "\n--- multiple events in one buffer ---\n");
    MidiOut op;
    op.test_capture_mode_ = true;

    VividNoteBuffer buf{};
    buf.count = 3;
    buf.events[0] = VividNoteEvent{VIVID_NOTE_ON,  60, 0, 0, 1, 0.5f};
    buf.events[1] = VividNoteEvent{VIVID_NOTE_ON,  64, 0, 0, 2, 0.7f};
    buf.events[2] = VividNoteEvent{VIVID_NOTE_OFF, 60, 0, 0, 1, 0.0f};

    void* inputs[1] = {&buf};
    VividAudioContext ctx = make_ctx(inputs, 1);

    op.process_audio(&ctx);

    check(op.test_captured_.size() == 3, "three messages captured");
    if (op.test_captured_.size() == 3) {
        check(op.test_captured_[0][0] == 0x90, "first = NOTE_ON");
        check(op.test_captured_[0][1] == 60,   "first pitch = 60");
        check(op.test_captured_[1][0] == 0x90, "second = NOTE_ON");
        check(op.test_captured_[1][1] == 64,   "second pitch = 64");
        check(op.test_captured_[2][0] == 0x80, "third = NOTE_OFF");
        check(op.test_captured_[2][1] == 60,   "third pitch = 60");
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::fprintf(stderr, "=== MidiOut byte-encoding correctness ===\n");

    test_note_on_encoding();
    test_velocity_scale_half();
    test_velocity_scale_clamp();
    test_note_off_encoding();
    test_pitch_bend_max();
    test_pitch_bend_center();
    test_pressure_encoding();
    test_timbre_encoding();
    test_channel_2();
    test_empty_buffer();
    test_multiple_events();

    std::fprintf(stderr, "\n=== Results: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
