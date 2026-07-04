// Headless test for M3 per-note expression: the ExprCurve sampler and the ClipScheduler
// emitting note-expression events across blocks (the "verify without UI first" step).
#include "midi/midi_clip.h"
#include "test_helpers.h"
#include <vector>

using namespace vivid::session;

int main() {
    // --- ExprCurve::sample: triangle {0,0},{0.5,2},{1,0} ---
    {
        ExprCurve c; c.bp = { {0.f,0.f}, {0.5f,2.f}, {1.f,0.f} };
        CHECK_NEAR(c.sample(0.f), 0.0, 1e-6);
        CHECK_NEAR(c.sample(0.25f), 1.0, 1e-6);   // halfway up the first leg
        CHECK_NEAR(c.sample(0.5f), 2.0, 1e-6);    // peak
        CHECK_NEAR(c.sample(0.75f), 1.0, 1e-6);   // halfway down
        CHECK_NEAR(c.sample(1.f), 0.0, 1e-6);
        CHECK_NEAR(c.sample(-1.f), 0.0, 1e-6);    // clamp below
        CHECK_NEAR(c.sample(2.f), 0.0, 1e-6);     // clamp above
        ExprCurve e; CHECK_NEAR(e.sample(0.5f), 0.0, 1e-9);   // empty = flat 0
    }

    // --- Scheduler: one long note with a bend curve, driven across 8 half-beat blocks ---
    {
        MidiClip clip; clip.length = 4.0;
        ClipNote n{}; n.pitch = 60; n.start = 0.0; n.dur = 4.0; n.vel = 0.8f;
        n.expr[AXIS_BEND].bp = { {0.f,0.f}, {0.5f,2.f}, {1.f,0.f} };
        clip.notes.push_back(n);

        ClipScheduler sched; sched.reset(&clip);
        const uint32_t frames = 12000, sr = 48000; const double bpm = 120.0;
        const double delta = frames * (bpm / 60.0) / sr;   // 0.5 beats/block

        int note_ons = 0; float on_tuning = -99.f;
        std::vector<float> bend_seq;   // block-start bend values (axis BEND)
        for (int b = 0; b < 8; ++b) {
            std::vector<NoteEvent> nev; std::vector<ExprEvent> eev;
            sched.emit(b * delta, delta, frames, nev, eev);
            for (const auto& e : nev) if (e.on) { ++note_ons; on_tuning = e.tuning; }
            for (const auto& x : eev) if (x.axis == AXIS_BEND) {
                CHECK(x.note_id != 0);
                CHECK(x.pitch == 60);
                bend_seq.push_back(x.value);
            }
        }
        CHECK(note_ons == 1);                 // one note-on for the whole loop
        CHECK_NEAR(on_tuning, 0.0, 1e-6);     // noteOn.tuning seeded from curve at t=0
        // Expect one bend point per block: 0, .5, 1, 1.5, 2(peak), 1.5, 1, .5
        CHECK(bend_seq.size() == 8u);
        if (bend_seq.size() == 8) {
            CHECK_NEAR(bend_seq[0], 0.0, 1e-5);
            CHECK_NEAR(bend_seq[4], 2.0, 1e-5);   // peak mid-note
            CHECK(bend_seq[3] < bend_seq[4] && bend_seq[4] > bend_seq[5]);  // rise then fall
        }
    }

    // --- Dedup: a flat curve emits once (at note-on), not every block ---
    {
        MidiClip clip; clip.length = 4.0;
        ClipNote n{}; n.pitch = 64; n.start = 0.0; n.dur = 4.0; n.vel = 0.8f;
        n.expr[AXIS_TIMBRE].bp = { {0.f, 0.7f} };   // single point = constant 0.7
        clip.notes.push_back(n);

        ClipScheduler sched; sched.reset(&clip);
        const uint32_t frames = 12000, sr = 48000; const double bpm = 120.0;
        const double delta = frames * (bpm / 60.0) / sr;
        int timbre_events = 0; float first_val = -1.f;
        for (int b = 0; b < 8; ++b) {
            std::vector<NoteEvent> nev; std::vector<ExprEvent> eev;
            sched.emit(b * delta, delta, frames, nev, eev);
            for (const auto& x : eev) if (x.axis == AXIS_TIMBRE) { ++timbre_events; if (first_val < 0) first_val = x.value; }
        }
        CHECK(timbre_events == 1);            // constant curve -> emitted once, then deduped
        CHECK_NEAR(first_val, 0.7, 1e-6);
    }

    // --- noteOn.tuning reflects a non-zero bend start ---
    {
        MidiClip clip; clip.length = 4.0;
        ClipNote n{}; n.pitch = 67; n.start = 0.0; n.dur = 2.0; n.vel = 0.8f;
        n.expr[AXIS_BEND].bp = { {0.f, 5.f}, {1.f, 0.f} };   // starts +5 semis
        clip.notes.push_back(n);
        ClipScheduler sched; sched.reset(&clip);
        std::vector<NoteEvent> nev; std::vector<ExprEvent> eev;
        sched.emit(0.0, 0.5, 12000, nev, eev);
        float tuning = -99.f;
        for (const auto& e : nev) if (e.on) tuning = e.tuning;
        CHECK_NEAR(tuning, 5.0, 1e-6);
    }

    return vivid::test::summary("test_expr_curve");
}
