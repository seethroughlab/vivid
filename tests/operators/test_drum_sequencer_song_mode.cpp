// Runtime tests for DrumSequencer song mode (auto-advance A→B→C→D).
// Drives DrumSequencerCore::compute() across pattern wraps and inspects
// the `current_pattern` scalar output and which trigger lane fires.

#include "drum_sequencer_core.h"
#include "drum_sequencer_layout.h"

#include <cstdio>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace layout = ::vivid_sequencers::drum_layout;

namespace {

struct TestDrum : DrumSequencerCore {
    void reseed(std::uint32_t s) { rng_.seed(s); }
    int  song_pos() const { return song_pos_; }
    void force_song_pos(int p) { song_pos_ = p; }
};

// Param buffer covers every index the operator reads (max = song_mode at 780).
constexpr std::size_t kParamCount = 800;

std::vector<float> make_default_params() {
    std::vector<float> p(kParamCount, 0.0f);
    p[0] = 16.0f;        // num_steps default
    p[3] = 1.0f;         // midi_channel
    for (std::size_t d = 0; d < layout::kDrumCount; ++d)
        p[layout::note_param_index(d)] = 36.0f + static_cast<float>(d);
    for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
        for (int s = 0; s < static_cast<int>(layout::kStepCount); ++s) {
            p[layout::mod_a_param_index(d, s)] = 0.5f;
            p[layout::mod_b_param_index(d, s)] = 0.5f;
            p[layout::prob_param_index(d, s)]  = 1.0f;
            p[layout::roll_param_index(d, s)]  = 1.0f;
        }
    }
    return p;
}

// One compute() call. Returns:
//   .current_pattern  — output_values[1] (the playing pattern 0..3)
//   .midi_count       — number of MIDI messages on the merged buffer
//   .first_note       — first note number emitted, or -1 if none
struct StepResult {
    int   current_pattern;
    uint32_t midi_count;
    int   first_note;
};
StepResult drive(TestDrum& d, std::vector<float>& params, float phase,
                 float reset_in = 0.0f) {
    VividNoteBuffer bufs[7]{};
    void* custom[7] = {&bufs[0], &bufs[1], &bufs[2], &bufs[3],
                       &bufs[4], &bufs[5], &bufs[6]};
    float output[2] = {};
    VividLaneOutput lane_dummy{};
    d.compute(phase, reset_in, 0.0, 4, params.data(),
              output, &lane_dummy, custom, 7);
    StepResult r{};
    r.current_pattern = static_cast<int>(output[1]);
    auto* merged = static_cast<VividNoteBuffer*>(custom[0]);
    r.midi_count = merged->count;
    r.first_note = (merged->count > 0u)
        ? static_cast<int>(merged->events[0].note_number) : -1;
    return r;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: DrumSequencer song mode ===\n\n");

    // --- Manual mode: current_pattern always equals active_pattern; only
    //     the active pattern's triggers fire.
    {
        TestDrum drum;
        drum.reseed(42);
        auto params = make_default_params();
        // Trigger on step 0 of pattern C only.
        params[layout::trig_c_param_index(0, 0)] = 1.0f;
        drum.song_mode.value = 0.0f;          // manual
        drum.active_pattern.value = 2.0f;     // C
        drum.steps.value = 4.0f;              // compute reads steps via .int_value()

        // Drive pattern across two loops.
        for (int loop = 0; loop < 2; ++loop) {
            for (int s = 0; s < 4; ++s) {
                const float phase = (static_cast<float>(s) + 0.5f) / 4.0f;
                auto r = drive(drum, params, phase);
                check(r.current_pattern == 2,
                      "manual mode: current_pattern always = active_pattern (C)");
                if (s == 0) {
                    check(r.first_note == 36,
                          "manual mode: pattern C step 0 fires kick");
                }
            }
        }
    }

    // --- Song mode: pattern wraps advance song_pos_ 0→1→2→3→0 ---
    {
        TestDrum drum;
        drum.reseed(7);
        auto params = make_default_params();
        drum.steps.value = 4.0f;
        // Distinct trigger on step 0 of each pattern so we can see which is
        // playing by which note fires (kick=36 / snare=37 / hat=38 / oh=39).
        params[layout::trigger_param_index(0, 0)] = 1.0f;  // pattern A: kick
        params[layout::trig_b_param_index(1, 0)]  = 1.0f;  // pattern B: snare
        params[layout::trig_c_param_index(2, 0)]  = 1.0f;  // pattern C: hat
        params[layout::trig_d_param_index(3, 0)]  = 1.0f;  // pattern D: oh
        drum.song_mode.value = 1.0f;

        // Cold-start frame at step 0: must NOT advance (prev_step_ was -1).
        auto r0 = drive(drum, params, 0.5f / 4.0f);
        check(r0.current_pattern == 0, "song first frame: starts on A");
        check(r0.first_note == 36,    "song first frame: pattern A's kick fires");

        // Drive the rest of pattern A's loop (steps 1..3).
        for (int s = 1; s < 4; ++s) {
            const float phase = (static_cast<float>(s) + 0.5f) / 4.0f;
            auto r = drive(drum, params, phase);
            check(r.current_pattern == 0,
                  "song mode: still on pattern A through the first loop");
        }

        // Wrap to step 0 → song advances to B.
        auto rB = drive(drum, params, 0.5f / 4.0f);
        check(rB.current_pattern == 1, "song wrap 1: A → B");
        check(rB.first_note == 37,    "pattern B fires snare on its step 0");

        // Drive the rest of pattern B then wrap to C.
        for (int s = 1; s < 4; ++s) {
            const float phase = (static_cast<float>(s) + 0.5f) / 4.0f;
            drive(drum, params, phase);
        }
        auto rC = drive(drum, params, 0.5f / 4.0f);
        check(rC.current_pattern == 2, "song wrap 2: B → C");
        check(rC.first_note == 38,    "pattern C fires hat");

        // Drive C and wrap to D.
        for (int s = 1; s < 4; ++s) {
            const float phase = (static_cast<float>(s) + 0.5f) / 4.0f;
            drive(drum, params, phase);
        }
        auto rD = drive(drum, params, 0.5f / 4.0f);
        check(rD.current_pattern == 3, "song wrap 3: C → D");
        check(rD.first_note == 39,    "pattern D fires oh");

        // Drive D and wrap back to A.
        for (int s = 1; s < 4; ++s) {
            const float phase = (static_cast<float>(s) + 0.5f) / 4.0f;
            drive(drum, params, phase);
        }
        auto rA = drive(drum, params, 0.5f / 4.0f);
        check(rA.current_pattern == 0, "song wrap 4: D → A (cycles)");
    }

    // --- Reset port returns song to A ---
    {
        TestDrum drum;
        drum.reseed(0);
        auto params = make_default_params();
        drum.steps.value = 4.0f;
        drum.song_mode.value = 1.0f;
        // Burn one frame so prev_song_mode_ = 1 (so the manual→song edge
        // doesn't clobber the forced song_pos_ on the next compute).
        drive(drum, params, 0.5f / 4.0f);
        drum.force_song_pos(2);   // pretend we're on C

        // Rising edge on the reset port should rewind to A.
        auto r = drive(drum, params, 0.5f / 4.0f, /*reset_in=*/1.0f);
        check(r.current_pattern == 0,
              "reset port (rising edge) rewinds song_pos_ to A");
    }

    // --- Clock-source change returns song to A ---
    {
        TestDrum drum;
        drum.reseed(0);
        auto params = make_default_params();
        drum.steps.value = 4.0f;
        drum.song_mode.value = 1.0f;
        // Start on the external clock so the switch below is a genuine change
        // (the param default is metronome, so setting it to metronome is a no-op).
        drum.clock_source.value = 0.0f;  // external
        drive(drum, params, 0.5f / 4.0f);  // settle prev_clock_source_ = external
        drum.force_song_pos(3);     // pretend we're on D
        drum.clock_source.value = 1.0f;  // → metronome: the change triggers the reset

        auto r = drive(drum, params, 0.5f / 4.0f);
        check(r.current_pattern == 0,
              "clock-source change rewinds song_pos_ to A");
    }

    // --- Manual → song toggle resets song_pos_ ---
    {
        TestDrum drum;
        drum.reseed(0);
        auto params = make_default_params();
        drum.steps.value = 4.0f;

        // Run a frame in manual mode to set prev_song_mode_ = 0.
        drum.song_mode.value = 0.0f;
        drive(drum, params, 0.1f);

        // Pretend song_pos_ is 2 (would be left over from a prior song).
        drum.force_song_pos(2);

        // Toggle song mode on. The manual→song edge should rewind to A.
        drum.song_mode.value = 1.0f;
        auto r = drive(drum, params, 0.2f);
        check(r.current_pattern == 0,
              "manual→song edge rewinds song_pos_ to A");
    }

    // --- Edit cursor independence: active_pattern picks the displayed
    //     pattern; playback follows song_pos_.
    {
        TestDrum drum;
        drum.reseed(0);
        auto params = make_default_params();
        drum.steps.value = 4.0f;
        // Trigger only on pattern C step 0. If active_pattern controlled
        // playback we'd expect NO fires; if song_pos_ does, we expect a fire
        // when song_pos_ lands on C.
        params[layout::trig_c_param_index(0, 0)] = 1.0f;
        drum.song_mode.value = 1.0f;
        drum.active_pattern.value = 0.0f;  // user is editing pattern A
        // First compute settles prev_song_mode_ to 1 (and song_pos_ to 0).
        drive(drum, params, 0.5f / 4.0f);
        // Now force song to be playing C without retriggering the edge.
        drum.force_song_pos(2);

        // First cold compute captured phase_offset_ = 0.125, so step centers
        // map to phases 0.125, 0.375, 0.625, 0.875 (mod 1).
        auto r = drive(drum, params, 0.375f);  // step 1 — past trigger row
        check(r.current_pattern == 2,
              "edit cursor independence: current_pattern follows song_pos_, not active_pattern");
        // Walk through the rest of the loop and wrap back to step 0.
        drive(drum, params, 0.625f);  // step 2
        drive(drum, params, 0.875f);  // step 3
        auto rWrap = drive(drum, params, 0.125f);  // step 0 — wraps song_pos_ → 3
        check(rWrap.current_pattern == 3,
              "edit cursor independence: wrap advances from C to D, ignoring active_pattern");
    }

    // --- Steps shrink mid-pattern: no false advance ---
    {
        TestDrum drum;
        drum.reseed(0);
        auto params = make_default_params();
        drum.song_mode.value = 1.0f;

        // Walk through steps 0..8 at n=16 (no wrap).
        drum.steps.value = 16.0f;
        for (int s = 0; s <= 8; ++s) {
            const float phase = (static_cast<float>(s) + 0.5f) / 16.0f;
            drive(drum, params, phase);
        }
        check(drum.song_pos() == 0,
              "steps=16, no wrap yet → song_pos_ still A");

        // Shrink to steps=4. prev_step_ was 8, n=4 → next step lands in [0..3]
        // but `prev_step_ == n-1 (=3)` is false, so no advance.
        drum.steps.value = 4.0f;
        const float phase = 0.5f / 4.0f;  // step 0 under n=4
        auto r = drive(drum, params, phase);
        check(r.current_pattern == 0,
              "steps shrink mid-pattern: song does not falsely advance");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
