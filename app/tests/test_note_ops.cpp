// Headless test for the pure MIDI note-list transforms in midi/note_ops.h — the
// editing operations the clip editor (and later the in-UI musical tools) build on.
#include "midi/note_ops.h"
#include "test_helpers.h"
#include <vector>

using vivid::session::ClipNote;

int main() {
    using namespace vivid::session;

    // transpose_selected: only selected notes move, clamped to 0..127.
    {
        std::vector<ClipNote> n = { {60,0,1,0.8f}, {64,1,1,0.8f}, {127,2,1,0.8f} };
        std::vector<uint8_t> sel = { 1, 0, 1 };
        transpose_selected(n, sel, 5);
        CHECK(n[0].pitch == 65);       // selected -> +5
        CHECK(n[1].pitch == 64);       // unselected -> unchanged
        CHECK(n[2].pitch == 127);      // clamped at ceiling
    }

    // nudge_selected: clamps into [0, length-dur].
    {
        std::vector<ClipNote> n = { {60,0.5,1,0.8f}, {62,3.0,1,0.8f} };
        std::vector<uint8_t> sel = { 1, 1 };
        nudge_selected(n, sel, -1.0, 4.0);
        CHECK(n[0].start == 0.0);      // clamped at 0
        nudge_selected(n, sel, +10.0, 4.0);
        CHECK(n[0].start == 3.0);      // clamped at length-dur = 3
        CHECK(n[1].start == 3.0);
    }

    // quantize_selected: snaps starts to the grid.
    {
        std::vector<ClipNote> n = { {60,0.30,1,0.8f}, {62,0.65,0.5,0.8f} };
        std::vector<uint8_t> sel = { 1, 1 };
        quantize_selected(n, sel, 0.25);
        CHECK_NEAR(n[0].start, 0.25, 1e-9);   // 0.30 -> nearest 0.25 grid = 0.25
        CHECK_NEAR(n[1].start, 0.75, 1e-9);   // 0.65 -> nearest 0.25 grid = 0.75
    }

    // set_velocity_selected: clamps and only touches the selection.
    {
        std::vector<ClipNote> n = { {60,0,1,0.8f}, {62,1,1,0.3f} };
        std::vector<uint8_t> sel = { 1, 0 };
        set_velocity_selected(n, sel, 1.5f);
        CHECK_NEAR(n[0].vel, 1.0, 1e-6);      // clamped to 1
        CHECK_NEAR(n[1].vel, 0.3, 1e-6);      // unchanged
    }

    // copy_selected rebases to 0; notes_span measures the extent; paste_at offsets + clamps.
    {
        std::vector<ClipNote> n = { {60,1.0,0.5,0.8f}, {64,2.0,0.5,0.7f}, {67,0.0,1,0.9f} };
        std::vector<uint8_t> sel = { 1, 1, 0 };   // the two later notes
        auto clip = copy_selected(n, sel);
        CHECK(clip.size() == 2);
        CHECK_NEAR(clip[0].start, 0.0, 1e-9);     // earliest selected (start 1.0) rebased to 0
        CHECK_NEAR(clip[1].start, 1.0, 1e-9);     // 2.0 - 1.0
        CHECK_NEAR(notes_span(clip), 1.5, 1e-9);  // 0..(1.0+0.5)

        std::vector<ClipNote> dst = { {48,0,1,0.5f} };
        std::vector<uint8_t> dsel = { 0 };
        size_t first = 0, last = 0;
        paste_at(dst, dsel, clip, 2.0, 8.0, first, last);
        CHECK(first == 1 && last == 3);
        CHECK(dst.size() == 3 && dsel.size() == 3);
        CHECK_NEAR(dst[1].start, 2.0, 1e-9);      // 0 + 2
        CHECK_NEAR(dst[2].start, 3.0, 1e-9);      // 1 + 2
        CHECK(dsel[1] == 1 && dsel[2] == 1);      // pasted notes are selected

        // paste past the end clamps to length - dur.
        std::vector<ClipNote> d2; std::vector<uint8_t> s2;
        paste_at(d2, s2, clip, 7.9, 8.0, first, last);
        CHECK_NEAR(d2[0].start, 7.5, 1e-9);       // dur 0.5 -> max start 7.5
    }

    return vivid::test::summary("test_note_ops");
}
