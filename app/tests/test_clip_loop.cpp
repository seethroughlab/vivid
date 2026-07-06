// Headless test for the in-clip loop region (M2-followup): the ClipScheduler must play
// only notes inside [loop_start, loop_end) and wrap the playhead within it.
#include "midi/midi_clip.h"
#include "test_helpers.h"
#include <vector>

using namespace vivid::session;

// Collect the ordered note-on pitches over `blocks` half-beat blocks from transport 0.
static std::vector<int> run(MidiClip& clip, int blocks) {
    ClipScheduler sched; sched.reset(&clip);
    const uint32_t frames = 12000, sr = 48000; const double bpm = 120.0;
    const double delta = frames * (bpm / 60.0) / sr;   // 0.5 beats/block
    std::vector<int> ons;
    for (int b = 0; b < blocks; ++b) {
        std::vector<NoteEvent> nev; std::vector<ExprEvent> eev;
        sched.emit(b * delta, delta, frames, nev, eev);
        for (const auto& e : nev) if (e.on) ons.push_back(e.pitch);
    }
    return ons;
}

int main() {
    // Four notes, one per beat.
    auto make = []() {
        MidiClip c; c.length = 4.0;
        const int p[4] = { 60, 62, 64, 65 };
        for (int i = 0; i < 4; ++i) { ClipNote n{}; n.pitch = p[i]; n.start = i; n.dur = 0.5; n.vel = 0.8f; c.notes.push_back(n); }
        return c;
    };

    // --- loop_lo/loop_hi helpers ---
    {
        MidiClip c = make();
        CHECK_NEAR(c.loop_lo(), 0.0, 1e-9); CHECK_NEAR(c.loop_hi(), 4.0, 1e-9);   // default = whole clip
        c.loop_start = 1.0; c.loop_end = 3.0;
        CHECK_NEAR(c.loop_lo(), 1.0, 1e-9); CHECK_NEAR(c.loop_hi(), 3.0, 1e-9);
        c.loop_start = 2.0; c.loop_end = 1.0;                                     // invalid (end<=start) -> whole clip
        CHECK_NEAR(c.loop_lo(), 0.0, 1e-9); CHECK_NEAR(c.loop_hi(), 4.0, 1e-9);
    }

    // --- no loop: all four notes over [0,4) in order ---
    {
        MidiClip c = make();
        std::vector<int> ons = run(c, 8);   // beats 0..4
        CHECK(ons.size() == 4);
        CHECK(ons[0] == 60 && ons[1] == 62 && ons[2] == 64 && ons[3] == 65);
    }

    // --- loop [1,3): only pitches 62 and 64, repeating; 60 and 65 never sound ---
    {
        MidiClip c = make(); c.loop_start = 1.0; c.loop_end = 3.0;
        std::vector<int> ons = run(c, 8);   // beats 0..4 = two loop cycles
        CHECK(ons.size() == 4);
        CHECK(ons[0] == 62 && ons[1] == 64 && ons[2] == 62 && ons[3] == 64);
        for (int p : ons) CHECK(p != 60 && p != 65);
    }

    // --- a note extending past loop_end still re-triggers and releases (never hangs) ---
    {
        MidiClip c; c.length = 4.0; c.loop_start = 1.0; c.loop_end = 3.0;
        ClipNote n{}; n.pitch = 62; n.start = 2.0; n.dur = 1.5; n.vel = 0.8f;   // ends past loop_end (3.0)
        c.notes.push_back(n);
        ClipScheduler sched; sched.reset(&c);
        const uint32_t frames = 12000, sr = 48000; const double bpm = 120.0;
        const double delta = frames * (bpm / 60.0) / sr;
        int ons = 0, offs = 0;
        for (int b = 0; b < 12; ++b) {     // 6 beats = 3 loop cycles
            std::vector<NoteEvent> nev; std::vector<ExprEvent> eev;
            sched.emit(b * delta, delta, frames, nev, eev);
            for (const auto& e : nev) { if (e.on) ++ons; else ++offs; }
        }
        CHECK(ons >= 2 && offs >= 1 && ons - offs <= 1);   // re-triggers, releases, no leak
    }

    return vivid::test::summary("test_clip_loop");
}
