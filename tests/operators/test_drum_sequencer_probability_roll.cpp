// Runtime tests for DrumSequencer probability + roll + pattern A/B routing.
// Drives DrumSequencerCore::compute() with crafted param arrays over
// well-defined phase progressions and inspects the per-tick MIDI buffer.

#include "drum_sequencer_core.h"
#include "drum_sequencer_layout.h"

#include <cstdio>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace layout = ::vivid_sequencers::drum_layout;

namespace {

// Subclass to expose the protected rng_ seed for determinism.
struct TestDrum : DrumSequencerCore {
    void reseed(std::uint32_t s) { rng_.seed(s); }
};

// Param buffer sized to cover every index the operator reads. The last
// trigger param is tom_d_15 at index 779 (kTrigDParamBases[5] + 15) so
// 800 leaves headroom.
constexpr std::size_t kParamCount = 800;

std::vector<float> make_default_params() {
    std::vector<float> p(kParamCount, 0.0f);
    p[0] = 16.0f;                 // num_steps
    p[3] = 1.0f;                  // midi_channel (1..16)
    // MIDI notes per drum.
    for (std::size_t d = 0; d < layout::kDrumCount; ++d)
        p[layout::note_param_index(d)] = 36.0f + static_cast<float>(d);
    // Velocity default 0.5, prob default 1.0, roll default 1.
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

// Helper: drive compute() one frame at a given phase and return how many
// MIDI note-ons appeared for drum `d` (matches by note number = 36 + d).
int compute_frame_and_count(TestDrum& drum, const std::vector<float>& params,
                            float phase, std::size_t d) {
    VividMidiBuffer buf{};
    void* custom_outs[1] = {&buf};
    float output[1] = {};
    VividLaneOutput lane_dummy{};
    auto* mutable_params = const_cast<float*>(params.data());
    // compute() expects the custom_outputs slot to point to the operator's
    // own midi_buf_; the core overwrites that slot with its own buffer ptr.
    drum.compute(phase, 0.0f, 0.0, 4,
                 mutable_params, output, &lane_dummy,
                 custom_outs, 1);
    auto* out_buf = static_cast<VividMidiBuffer*>(custom_outs[0]);
    const uint8_t note = static_cast<uint8_t>(36 + d);
    int count = 0;
    for (uint32_t i = 0; i < out_buf->count; ++i) {
        if ((out_buf->messages[i].status & 0xF0) == 0x90 &&
            out_buf->messages[i].data1 == note) {
            ++count;
        }
    }
    return count;
}

// Count note-ons emitted across N full passes of the pattern, where each
// pass advances phase from 0 → ~1 at one frame per step. Returns the
// number of times drum `d` fired on step `step`.
int fire_count_over_passes(TestDrum& drum, std::vector<float>& params,
                           std::size_t d, int step, int passes) {
    const int n = 16;
    int total = 0;
    for (int pass = 0; pass < passes; ++pass) {
        for (int s = 0; s < n; ++s) {
            const float phase = (static_cast<float>(s) + 0.1f) / n;
            const int hits = compute_frame_and_count(drum, params, phase, d);
            if (s == step) total += hits;
        }
    }
    return total;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: DrumSequencer probability + roll + pattern A/B ===\n\n");

    // --- probability = 1.0 always fires; probability = 0.0 never fires ---
    {
        TestDrum drum;
        drum.reseed(42);
        auto params = make_default_params();
        // Kick triggers on step 0 only.
        params[layout::trigger_param_index(0, 0)] = 1.0f;
        params[layout::prob_param_index(0, 0)]   = 1.0f;

        int fires_always = fire_count_over_passes(drum, params, 0, 0, 10);
        check(fires_always == 10, "prob=1.0 fires on every pass (10/10)");

        // Reset and try prob = 0.0
        TestDrum drum_zero;
        drum_zero.reseed(42);
        params[layout::prob_param_index(0, 0)] = 0.0f;
        int fires_never = fire_count_over_passes(drum_zero, params, 0, 0, 20);
        check(fires_never == 0, "prob=0.0 never fires (0/20)");
    }

    // --- probability = 0.5 with a fixed seed produces a deterministic sequence ---
    {
        TestDrum a, b;
        a.reseed(1234);
        b.reseed(1234);
        auto params = make_default_params();
        params[layout::trigger_param_index(0, 0)] = 1.0f;
        params[layout::prob_param_index(0, 0)]   = 0.5f;

        int hits_a = fire_count_over_passes(a, params, 0, 0, 100);
        int hits_b = fire_count_over_passes(b, params, 0, 0, 100);
        check(hits_a == hits_b,
              "same seed → same fire count under prob=0.5");
        // Rough sanity — not asserting a tight range since sequence depends on
        // the implementation, but 100 trials should land somewhere in the
        // open interval (0, 100). Both ends would indicate the gate is broken.
        check(hits_a > 0 && hits_a < 100,
              "prob=0.5 produces a non-degenerate firing count over 100 passes");
    }

    // --- roll count: 1 fires once per step, 2 twice, 4 four times ---
    //
    // Roll schedules N sub-step emissions evenly across the step's
    // duration. To observe all N we need to sample phase densely within
    // one step's range [0, 1/n) — picking 32 samples across step 0
    // easily straddles every sub-step boundary for N up to 4.
    {
        for (int roll : {1, 2, 3, 4}) {
            TestDrum drum;
            drum.reseed(7);
            auto params = make_default_params();
            params[layout::trigger_param_index(0, 0)] = 1.0f;
            params[layout::roll_param_index(0, 0)]   = static_cast<float>(roll);

            const int n = 16;
            const int samples_per_step = 32;
            int total_hits = 0;
            for (int i = 0; i < samples_per_step; ++i) {
                const float phase = (static_cast<float>(i) + 0.5f) /
                    (static_cast<float>(samples_per_step) * static_cast<float>(n));
                total_hits += compute_frame_and_count(drum, params, phase, 0);
            }
            check(total_hits == roll,
                  (std::string("roll=") + std::to_string(roll) +
                   " emits " + std::to_string(roll) + " notes per step").c_str());
        }
    }

    // --- active_pattern routes to trig_a/b/c/d; dynamics stay shared ---
    //
    // active_pattern is a Param<int> read through `.int_value()` in
    // compute(), not through the params array, so this test pokes the
    // member directly — mirrors what the runtime does on set_param.
    {
        TestDrum drum;
        drum.reseed(55);
        auto params = make_default_params();
        params[layout::trigger_param_index(0, 0)] = 0.0f;
        params[layout::trig_b_param_index(0, 0)]  = 1.0f;

        drum.active_pattern.value = 0.0f;
        int fires_a = fire_count_over_passes(drum, params, 0, 0, 5);
        check(fires_a == 0,
              "active_pattern=0 with trigger A=0 and B=1 → no fires");

        TestDrum drum_b;
        drum_b.reseed(55);
        drum_b.active_pattern.value = 1.0f;
        int fires_b = fire_count_over_passes(drum_b, params, 0, 0, 5);
        check(fires_b == 5,
              "active_pattern=1 with trigger B=1 → fires every pass (5/5)");
    }

    // --- active_pattern=2 routes to trig_c; =3 routes to trig_d ---
    {
        TestDrum drum_c;
        drum_c.reseed(55);
        auto params = make_default_params();
        params[layout::trigger_param_index(0, 0)] = 0.0f;
        params[layout::trig_b_param_index(0, 0)]  = 0.0f;
        params[layout::trig_c_param_index(0, 0)]  = 1.0f;

        drum_c.active_pattern.value = 2.0f;
        int fires_c = fire_count_over_passes(drum_c, params, 0, 0, 5);
        check(fires_c == 5,
              "active_pattern=2 with trigger C=1 → fires every pass (5/5)");

        // Same params, but pattern=0/1/3 should not fire (no trigger on those banks).
        for (int p : {0, 1, 3}) {
            TestDrum d;
            d.reseed(55);
            d.active_pattern.value = static_cast<float>(p);
            int fires = fire_count_over_passes(d, params, 0, 0, 5);
            check(fires == 0,
                  (std::string("active_pattern=") + std::to_string(p) +
                   " with only trigger C=1 → no fires").c_str());
        }
    }
    {
        TestDrum drum_d;
        drum_d.reseed(55);
        auto params = make_default_params();
        params[layout::trigger_param_index(0, 0)] = 0.0f;
        params[layout::trig_d_param_index(0, 0)]  = 1.0f;

        drum_d.active_pattern.value = 3.0f;
        int fires_d = fire_count_over_passes(drum_d, params, 0, 0, 5);
        check(fires_d == 5,
              "active_pattern=3 with trigger D=1 → fires every pass (5/5)");
    }

    // --- velocity / probability stay shared across all four patterns ---
    //
    // Same probability and velocity values must apply regardless of which
    // pattern bank's trigger fires.
    {
        auto params = make_default_params();
        params[layout::prob_param_index(0, 0)]    = 0.0f;   // probability 0 → never fires
        params[layout::trigger_param_index(0, 0)] = 1.0f;
        params[layout::trig_b_param_index(0, 0)]  = 1.0f;
        params[layout::trig_c_param_index(0, 0)]  = 1.0f;
        params[layout::trig_d_param_index(0, 0)]  = 1.0f;

        for (int p = 0; p < 4; ++p) {
            TestDrum d;
            d.reseed(55);
            d.active_pattern.value = static_cast<float>(p);
            int fires = fire_count_over_passes(d, params, 0, 0, 5);
            check(fires == 0,
                  (std::string("probability=0 suppresses pattern ") +
                   static_cast<char>('A' + p)).c_str());
        }
    }

    // --- velocity from mod_a drives MIDI velocity byte ---
    {
        TestDrum drum;
        drum.reseed(0);
        auto params = make_default_params();
        params[layout::trigger_param_index(0, 0)] = 1.0f;
        params[layout::mod_a_param_index(0, 0)]   = 1.0f;  // max

        VividMidiBuffer buf{};
        void* custom[1] = {&buf};
        float out[1] = {};
        VividLaneOutput lane_dummy{};
        drum.compute(0.02f, 0.0f, 0.0, 4,
                     params.data(), out, &lane_dummy, custom, 1);

        auto* emitted = static_cast<VividMidiBuffer*>(custom[0]);
        check(emitted->count >= 1u, "mod_a=1.0 → at least one note-on");
        if (emitted->count >= 1u) {
            check(emitted->messages[0].data2 == 127,
                  "mod_a=1.0 clamps to MIDI velocity 127");
        }
    }

    // --- per-drum custom outputs: each port carries only its own drum ---
    //
    // Fire kick on step 0 and snare on step 4 with probability 1.0. Drive two
    // frames and check that kick_out[1] only holds the kick note (36) and
    // snare_out[2] only holds the snare note (37); the merged midi_out[0]
    // must carry whichever drum fired on that tick.
    {
        TestDrum drum;
        drum.reseed(0);
        auto params = make_default_params();
        params[layout::trigger_param_index(0, 0)] = 1.0f;  // kick @ step 0
        params[layout::trigger_param_index(1, 4)] = 1.0f;  // snare @ step 4

        VividMidiBuffer bufs[7]{};
        void* custom[7] = {&bufs[0], &bufs[1], &bufs[2], &bufs[3],
                           &bufs[4], &bufs[5], &bufs[6]};
        float out[1] = {};
        VividLaneOutput lane_dummy{};

        // Step 0 tick — kick fires. Start at phase 0 so the first-frame
        // clock_source reset lands phase_offset_ at 0 and later phases map
        // to the intended steps without drift.
        drum.compute(0.0f, 0.0f, 0.0, 4,
                     params.data(), out, &lane_dummy, custom, 7);
        const auto* merged = static_cast<VividMidiBuffer*>(custom[0]);
        const auto* kick_out = static_cast<VividMidiBuffer*>(custom[1]);
        const auto* snare_out = static_cast<VividMidiBuffer*>(custom[2]);
        check(merged->count == 1u && merged->messages[0].data1 == 36,
              "merged midi_out carries kick note on step 0");
        check(kick_out->count == 1u && kick_out->messages[0].data1 == 36,
              "kick_out carries kick note on step 0");
        check(snare_out->count == 0u,
              "snare_out is empty on step 0 (kick-only)");

        // Step 4 tick — snare fires; kick_out must now be empty (cleared).
        drum.compute(4.1f / 16.0f, 0.0f, 0.0, 4,
                     params.data(), out, &lane_dummy, custom, 7);
        merged = static_cast<VividMidiBuffer*>(custom[0]);
        kick_out = static_cast<VividMidiBuffer*>(custom[1]);
        snare_out = static_cast<VividMidiBuffer*>(custom[2]);
        check(merged->count == 1u && merged->messages[0].data1 == 37,
              "merged midi_out carries snare note on step 4");
        check(kick_out->count == 0u,
              "kick_out cleared on step 4 (snare-only)");
        check(snare_out->count == 1u && snare_out->messages[0].data1 == 37,
              "snare_out carries snare note on step 4");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
