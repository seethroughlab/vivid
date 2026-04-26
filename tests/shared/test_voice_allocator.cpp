// Unit tests for vivid::VoiceAllocator (operator_api/voice_allocator.h).
//
// Native note transport (post-2026-04 migration). The allocator now keys
// slots by `note_id` instead of MIDI note number, so:
//   - same-pitch overlapping notes allocate distinct slots
//   - legato retrigger (note-off then note-on at the same pitch) yields a
//     fresh slot, not a reuse of the previous one
//   - per-note expression events (pitch_bend, pressure, timbre) mutate the
//     matching slot's state
//
// Covers: note_on/note_off basics, distinct-slot allocation for
// overlapping same-pitch notes, oldest-voice stealing, all_notes_off,
// process_note_buffer with on_on/on_off/on_expression callbacks, and
// per-note expression routing.

#include "operator_api/voice_allocator.h"

#include <cstdio>
#include <cstring>

#include "test_helpers.h"

using vivid::VoiceAllocator;
using vivid::VoiceSlot;

namespace {

// Build a VividNoteBuffer in-place from a list of events.
struct Ev {
    VividNoteEventType type;
    uint8_t            note_number;
    float              value;
    uint64_t           note_id;
    uint32_t           offset;
};

VividNoteBuffer make_buffer(std::initializer_list<Ev> events) {
    VividNoteBuffer buf{};
    for (const auto& e : events) {
        if (buf.count >= VIVID_NOTE_BUFFER_CAPACITY) break;
        auto& slot = buf.events[buf.count++];
        slot.type                 = static_cast<uint8_t>(e.type);
        slot.note_number          = e.note_number;
        slot.value                = e.value;
        slot.note_id              = e.note_id;
        slot.frame_offset_samples = e.offset;
    }
    return buf;
}

void test_note_on_off_basic() {
    std::fprintf(stderr, "\n--- VoiceAllocator: note_on/note_off basics ---\n");
    VoiceAllocator<8> alloc;
    check(alloc.active_count() == 0, "fresh allocator has no active voices");

    int idx = alloc.note_on(60, 1.0f, /*note_id=*/100, /*frame=*/0);
    check(idx == 0, "first note_on takes slot 0");
    check(alloc.slots[0].active, "slot 0 active");
    check(alloc.slots[0].gate, "slot 0 gate held");
    check(alloc.slots[0].note == 60, "slot 0 holds note 60");
    check(alloc.slots[0].note_id == 100, "slot 0 records note_id");
    check_float(alloc.slots[0].velocity, 1.0f, "slot 0 velocity 1.0");
    check(alloc.active_count() == 1, "active_count == 1");

    int idx2 = alloc.note_on(64, 0.5f, /*note_id=*/101, /*frame=*/100);
    check(idx2 == 1, "second note (different id) takes slot 1");
    check(alloc.slots[1].note_id == 101, "slot 1 records its own note_id");

    int released = alloc.note_off(/*note_id=*/100);
    check(released == 0, "note_off(id=100) releases slot 0");
    check(alloc.slots[0].active, "slot still active after note_off (release tail)");
    check(!alloc.slots[0].gate, "slot 0 gate released");

    int absent = alloc.note_off(/*note_id=*/9999);
    check(absent == -1, "note_off on absent id returns -1");
}

void test_same_pitch_overlap_distinct_slots() {
    std::fprintf(stderr, "\n--- VoiceAllocator: same-pitch overlap allocates distinct slots ---\n");
    VoiceAllocator<4> alloc;
    int a = alloc.note_on(60, 0.8f, /*note_id=*/200, /*frame=*/10);
    int b = alloc.note_on(60, 1.0f, /*note_id=*/201, /*frame=*/50);
    check(a != b, "two same-pitch note-ons with distinct ids → distinct slots");
    check(alloc.active_count() == 2, "both voices active");
    check(alloc.slots[a].note_id == 200, "slot a keeps id 200");
    check(alloc.slots[b].note_id == 201, "slot b keeps id 201");
    // Note-off on the first id must release the first slot, not the second.
    alloc.note_off(/*note_id=*/200);
    check(!alloc.slots[a].gate, "first voice released");
    check(alloc.slots[b].gate, "second voice still held");
}

void test_oldest_stealing() {
    std::fprintf(stderr, "\n--- VoiceAllocator: oldest-stealing when full ---\n");
    VoiceAllocator<4> alloc;
    alloc.note_on(60, 1.0f, /*id=*/300, /*frame=*/100);
    alloc.note_on(62, 1.0f, /*id=*/301, /*frame=*/200);
    alloc.note_on(64, 1.0f, /*id=*/302, /*frame=*/300);
    alloc.note_on(67, 1.0f, /*id=*/303, /*frame=*/400);
    check(alloc.active_count() == 4, "all slots full");

    int stolen = alloc.note_on(72, 1.0f, /*id=*/304, /*frame=*/500);
    check(stolen == 0, "oldest slot (frame 100) is stolen first");
    check(alloc.slots[0].note == 72, "stolen slot now holds new note");
    check(alloc.slots[0].note_id == 304, "stolen slot now holds new note_id");
    check(alloc.slots[0].start_frame == 500, "stolen slot has new start_frame");
}

void test_all_notes_off() {
    std::fprintf(stderr, "\n--- VoiceAllocator: all_notes_off clears every slot ---\n");
    VoiceAllocator<4> alloc;
    alloc.note_on(60, 1.0f, /*id=*/400, 0);
    alloc.note_on(64, 1.0f, /*id=*/401, 1);
    alloc.note_on(67, 1.0f, /*id=*/402, 2);
    check(alloc.active_count() == 3, "3 voices active before panic");
    alloc.all_notes_off();
    check(alloc.active_count() == 0, "no voices active after panic");
    for (int i = 0; i < 4; ++i)
        check(!alloc.slots[i].gate, "all gates cleared");
}

void test_apply_expression() {
    std::fprintf(stderr, "\n--- VoiceAllocator: per-note expression mutates correct slot ---\n");
    VoiceAllocator<4> alloc;
    alloc.note_on(60, 1.0f, /*id=*/500, 0);
    alloc.note_on(64, 1.0f, /*id=*/501, 0);

    alloc.apply_pitch_bend(/*id=*/500, /*semis=*/-1.5f);
    alloc.apply_pressure(/*id=*/501, /*v=*/0.7f);
    alloc.apply_timbre(/*id=*/500, /*v=*/0.3f);

    check_float(alloc.slots[0].pitch_bend_semis, -1.5f, 1e-6f,
                "id 500 pitch_bend stored on its slot");
    check_float(alloc.slots[1].pressure, 0.7f, 1e-6f,
                "id 501 pressure stored on its slot");
    check_float(alloc.slots[0].timbre, 0.3f, 1e-6f,
                "id 500 timbre stored on its slot");
    check_float(alloc.slots[1].pitch_bend_semis, 0.0f, 1e-6f,
                "id 501 pitch_bend untouched (default)");

    int miss = alloc.apply_pitch_bend(/*id=*/9999, 0.5f);
    check(miss == -1, "expression on absent id returns -1");
}

void test_process_note_buffer() {
    std::fprintf(stderr, "\n--- VoiceAllocator: process_note_buffer drives slots ---\n");
    VoiceAllocator<4> alloc;
    auto buf = make_buffer({
        {VIVID_NOTE_ON,         60, 0.8f, /*id=*/600, /*offset=*/0},
        {VIVID_NOTE_ON,         64, 0.6f, /*id=*/601, /*offset=*/16},
        {VIVID_NOTE_PITCH_BEND,  0, 2.0f, /*id=*/600, /*offset=*/24},
        {VIVID_NOTE_OFF,         0, 0.0f, /*id=*/600, /*offset=*/32},
        {VIVID_NOTE_OFF,         0, 0.0f, /*id=*/601, /*offset=*/48},
    });

    int n_on = 0, n_off = 0, n_expr = 0;
    alloc.process_note_buffer(&buf, /*base_frame=*/1000,
        [&](int slot, int note, float vel, uint32_t offset, uint64_t note_id) {
            (void)offset;
            ++n_on;
            check(slot >= 0 && slot < 4, "on callback slot in range");
            check(note == 60 || note == 64, "on callback note 60 or 64");
            check(note_id == 600 || note_id == 601, "on callback note_id 600 or 601");
            check_float(vel, note == 60 ? 0.8f : 0.6f, 1e-4f, "on callback velocity matches");
        },
        [&](int slot, int note, uint64_t note_id) {
            (void)note;
            ++n_off;
            check(slot >= 0 && slot < 4, "off callback slot in range");
            check(note_id == 600 || note_id == 601, "off callback note_id matches");
        },
        [&](int slot, VividNoteEventType kind, float value) {
            ++n_expr;
            check(slot >= 0 && slot < 4, "expression callback slot in range");
            check(kind == VIVID_NOTE_PITCH_BEND, "expression kind is pitch_bend");
            check_float(value, 2.0f, 1e-6f, "expression value is 2.0 semis");
        });

    check(n_on == 2, "two note-on callbacks fired");
    check(n_off == 2, "two note-off callbacks fired");
    check(n_expr == 1, "one expression callback fired");
    check(alloc.active_count() == 2, "both slots still active (release tail)");
    check(!alloc.slots[0].gate, "first voice gate released");
    check(!alloc.slots[1].gate, "second voice gate released");
    check(alloc.slots[0].start_frame == 1000, "start_frame includes base + offset");
    check(alloc.slots[1].start_frame == 1016, "second voice start_frame");
    check_float(alloc.slots[0].pitch_bend_semis, 2.0f, 1e-6f,
                "pitch_bend persisted on slot 0");
}

void test_null_buffer_safe() {
    std::fprintf(stderr, "\n--- VoiceAllocator: null note buffer is a no-op ---\n");
    VoiceAllocator<4> alloc;
    int n_on = 0, n_off = 0, n_expr = 0;
    alloc.process_note_buffer(nullptr, 0,
        [&](int, int, float, uint32_t, uint64_t) { ++n_on; },
        [&](int, int, uint64_t) { ++n_off; },
        [&](int, VividNoteEventType, float) { ++n_expr; });
    check(n_on == 0 && n_off == 0 && n_expr == 0,
          "null buffer triggers no callbacks");
    check(alloc.active_count() == 0, "null buffer leaves allocator empty");
}

void test_global_id_ignored() {
    std::fprintf(stderr, "\n--- VoiceAllocator: events with note_id=0 are ignored ---\n");
    VoiceAllocator<4> alloc;
    auto buf = make_buffer({
        {VIVID_NOTE_ON, 60, 1.0f, /*id=*/0, 0},  // global stream — synth ignores
    });
    int n_on = 0;
    alloc.process_note_buffer(&buf, 0,
        [&](int, int, float, uint32_t, uint64_t) { ++n_on; },
        [](int, int, uint64_t) {},
        [](int, VividNoteEventType, float) {});
    check(n_on == 0, "no on callback for note_id=0");
    check(alloc.active_count() == 0, "no slot allocated for note_id=0");
}

} // namespace

int main() {
    test_note_on_off_basic();
    test_same_pitch_overlap_distinct_slots();
    test_oldest_stealing();
    test_all_notes_off();
    test_apply_expression();
    test_process_note_buffer();
    test_null_buffer_safe();
    test_global_id_ignored();

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
