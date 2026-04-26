// Unit tests for the native note transport helpers.
//
// Covers:
//   - VividNoteBuffer struct shape (capacity bound, struct size sanity)
//   - note_id counter is monotonic and never returns 0
//   - note_on / note_off / per-note expression helpers populate fields correctly
//   - helpers reject note_id == 0 (the reserved global-stream id)
//   - capacity-full case appends nothing and returns false

#include "operator_api/note_types.h"
#include "note_id_counter.h"
#include "note_helpers.h"

#include <cstdio>
#include <cstring>

#include "test_helpers.h"

using namespace vivid_sequencers;

namespace {

void test_buffer_shape() {
    std::fprintf(stderr, "\n--- VividNoteBuffer shape ---\n");
    check(VIVID_NOTE_BUFFER_CAPACITY == 64,
          "VIVID_NOTE_BUFFER_CAPACITY is 64");
    check(sizeof(VividNoteEvent) == 24,
          "VividNoteEvent is 24 bytes (4-byte aligned)");
    VividNoteBuffer buf{};
    check(buf.count == 0, "fresh buffer has count == 0");
}

void test_counter_monotonic_nonzero() {
    std::fprintf(stderr, "\n--- next_note_id() is monotonic and never zero ---\n");
    uint64_t a = next_note_id();
    uint64_t b = next_note_id();
    uint64_t c = next_note_id();
    check(a != 0, "first id is non-zero");
    check(b > a, "ids are monotonic increasing");
    check(c > b, "ids are monotonic across calls");
}

void test_note_on_populates_fields() {
    std::fprintf(stderr, "\n--- note_on populates fields correctly ---\n");
    VividNoteBuffer buf{};
    uint64_t id = next_note_id();
    bool ok = note_on(buf, /*note=*/60, /*velocity=*/0.8f, id, /*offset=*/123);
    check(ok, "note_on returns true on success");
    check(buf.count == 1, "buffer count incremented to 1");
    const auto& ev = buf.events[0];
    check(ev.type == VIVID_NOTE_ON, "event type is NOTE_ON");
    check(ev.note_number == 60, "note_number = 60");
    check_float(ev.value, 0.8f, 1e-6f, "value = velocity 0.8");
    check(ev.note_id == id, "note_id matches");
    check(ev.frame_offset_samples == 123, "frame_offset matches");
}

void test_note_off_populates_fields() {
    std::fprintf(stderr, "\n--- note_off populates fields correctly ---\n");
    VividNoteBuffer buf{};
    uint64_t id = next_note_id();
    bool ok = note_off(buf, id, /*offset=*/256);
    check(ok, "note_off returns true on success");
    check(buf.count == 1, "buffer count incremented to 1");
    const auto& ev = buf.events[0];
    check(ev.type == VIVID_NOTE_OFF, "event type is NOTE_OFF");
    check(ev.note_id == id, "note_id matches");
    check(ev.frame_offset_samples == 256, "frame_offset matches");
}

void test_per_note_expression() {
    std::fprintf(stderr, "\n--- per-note expression helpers ---\n");
    VividNoteBuffer buf{};
    uint64_t id = next_note_id();

    bool ok_pb = note_pitch_bend(buf, id, /*semis=*/-2.5f, /*offset=*/10);
    check(ok_pb, "note_pitch_bend returns true");
    check(buf.events[0].type == VIVID_NOTE_PITCH_BEND, "event 0 is PITCH_BEND");
    check_float(buf.events[0].value, -2.5f, 1e-6f, "pitch_bend value = -2.5 semis");
    check(buf.events[0].note_id == id, "pitch_bend note_id matches");

    bool ok_pr = note_pressure(buf, id, /*value=*/0.6f, /*offset=*/20);
    check(ok_pr, "note_pressure returns true");
    check(buf.events[1].type == VIVID_NOTE_PRESSURE, "event 1 is PRESSURE");
    check_float(buf.events[1].value, 0.6f, 1e-6f, "pressure value = 0.6");

    bool ok_tb = note_timbre(buf, id, /*value=*/0.4f, /*offset=*/30);
    check(ok_tb, "note_timbre returns true");
    check(buf.events[2].type == VIVID_NOTE_TIMBRE, "event 2 is TIMBRE");
    check_float(buf.events[2].value, 0.4f, 1e-6f, "timbre value = 0.4");

    check(buf.count == 3, "buffer holds 3 events after 3 expression appends");
}

void test_zero_id_rejected() {
    std::fprintf(stderr, "\n--- note_id == 0 is rejected by per-note helpers ---\n");
    VividNoteBuffer buf{};
    check(!note_on(buf, 60, 1.0f, /*note_id=*/0), "note_on rejects id=0");
    check(!note_off(buf, /*note_id=*/0), "note_off rejects id=0");
    check(!note_pitch_bend(buf, /*note_id=*/0, 0.5f), "note_pitch_bend rejects id=0");
    check(!note_pressure(buf, /*note_id=*/0, 0.5f), "note_pressure rejects id=0");
    check(!note_timbre(buf, /*note_id=*/0, 0.5f), "note_timbre rejects id=0");
    check(buf.count == 0, "no events appended when ids rejected");
}

void test_capacity_full_returns_false() {
    std::fprintf(stderr, "\n--- helpers return false when buffer is full ---\n");
    VividNoteBuffer buf{};
    // Fill to capacity.
    for (int i = 0; i < VIVID_NOTE_BUFFER_CAPACITY; ++i) {
        bool ok = note_on(buf, 60, 1.0f, next_note_id(), 0);
        check(ok, "appended event below capacity");
    }
    check(buf.count == VIVID_NOTE_BUFFER_CAPACITY, "buffer is full");
    // Next append should fail.
    bool overflow = note_on(buf, 60, 1.0f, next_note_id(), 0);
    check(!overflow, "note_on returns false when buffer full");
    check(buf.count == VIVID_NOTE_BUFFER_CAPACITY,
          "count unchanged after full-buffer append attempt");
}

void test_distinct_ids_per_call() {
    std::fprintf(stderr, "\n--- distinct ids on retrigger ---\n");
    // The Phase 1 design says re-triggering the same MIDI pitch produces a
    // fresh note_id. The helper layer is identity-agnostic — emitters allocate
    // ids — but this test pins the contract that next_note_id() always
    // returns a fresh value, even across rapid calls.
    uint64_t ids[4];
    for (int i = 0; i < 4; ++i) ids[i] = next_note_id();
    check(ids[0] != ids[1] && ids[1] != ids[2] && ids[2] != ids[3],
          "consecutive next_note_id() calls produce distinct values");
}

}  // namespace

int main() {
    test_buffer_shape();
    test_counter_monotonic_nonzero();
    test_note_on_populates_fields();
    test_note_off_populates_fields();
    test_per_note_expression();
    test_zero_id_rejected();
    test_capacity_full_returns_false();
    test_distinct_ids_per_call();

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
