// UX Ph4 F3: the pure decision logic behind keyboard editing (app/kbd_edit_logic.h) — the audio
// wire-kind inference (kept in lock-step with the mouse path in input_graph.cpp) and the arrow-key
// spatial "nearest node in direction". Pure header, no app/GLFW link.
#include "app/kbd_edit_logic.h"
#include "test_helpers.h"

using namespace vivid::input;

int main() {
    // --- audio_can_source: only instrument(0)/effect(1)/note-fx(4)/modulator(5) start a wire.
    CHECK(audio_can_source(0) && audio_can_source(1) && audio_can_source(4) && audio_can_source(5));
    CHECK(!audio_can_source(2) && !audio_can_source(3) && !audio_can_source(6)
          && !audio_can_source(7) && !audio_can_source(8));

    // --- audio_wire_kind: signal inferred from source+target kinds; -1 = illegal.
    // Audio source (instrument 0 / effect 1): audio edge (0) to anything that isn't a source (0/3/4).
    CHECK(audio_wire_kind(1, 1) == 0);    // fx -> fx : audio
    CHECK(audio_wire_kind(1, 2) == 0);    // fx -> output : audio
    CHECK(audio_wire_kind(0, 1) == 0);    // instrument -> fx : audio
    CHECK(audio_wire_kind(1, 0) == -1);   // -> instrument (a source) : illegal
    CHECK(audio_wire_kind(1, 3) == -1);   // -> MIDI-in (a source) : illegal
    CHECK(audio_wire_kind(1, 4) == -1);   // -> note fx (a source) : illegal
    // Note source (MIDI-in 3 / note fx 4): note edge (1) onto an instrument (0) or note fx (4) only.
    CHECK(audio_wire_kind(4, 0) == 1);    // note fx -> instrument : note
    CHECK(audio_wire_kind(3, 4) == 1);    // MIDI-in -> note fx : note
    CHECK(audio_wire_kind(4, 1) == -1);   // note -> plain fx : illegal
    CHECK(audio_wire_kind(3, 2) == -1);   // note -> output : illegal
    // Modulator source (5) is param-port wiring — never a kind-connect.
    CHECK(audio_wire_kind(5, 0) == -1 && audio_wire_kind(5, 1) == -1);

    // --- nearest_in_dir: from node 0 at origin, pick the nearest neighbour in each screen direction
    // (y grows DOWN). Layout: 1=right, 2=left, 3=down, 4=up, 5=far-right (should lose to 1).
    const std::vector<float> xs = { 0,  10, -10,  0,   0,  40 };
    const std::vector<float> ys = { 0,   0,   0, 10, -10,   1 };
    CHECK(nearest_in_dir(xs, ys, 0, +1.f, 0.f) == 1);   // right → node 1 (nearer than far-right 5)
    CHECK(nearest_in_dir(xs, ys, 0, -1.f, 0.f) == 2);   // left  → node 2
    CHECK(nearest_in_dir(xs, ys, 0, 0.f, +1.f) == 3);   // down  → node 3
    CHECK(nearest_in_dir(xs, ys, 0, 0.f, -1.f) == 4);   // up    → node 4
    // Nothing to the left of the leftmost node.
    CHECK(nearest_in_dir(xs, ys, 2, -1.f, 0.f) == -1);
    // Degenerate: out-of-range `from`.
    CHECK(nearest_in_dir(xs, ys, 99, 1.f, 0.f) == -1);

    return vivid::test::summary("test_kbd_edit_logic");
}
