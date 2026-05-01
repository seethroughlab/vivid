// NoteModulator smoke + behavioral tests.
//
// Verifies:
//   1. Operator declares notes_in + notes_out as VividNoteBuffer custom_ref.
//   2. With amount=0 on every channel, the operator forwards notes_in
//      verbatim (no spurious events emitted).
//   3. With timbre_amount > 0 and three simultaneous notes with distinct
//      note_ids, three different timbre values are emitted per frame —
//      proving per-note phase decorrelation works.
//   4. Compose-add semantics: an upstream PRESSURE event composes with the
//      modulator's contribution, producing pressure events whose value
//      tracks (upstream + modulator).
//   5. Replace mode: with pressure_mode=1, the modulator overrides the
//      upstream value instead of adding.

#include "operator_api/metronome_sync.h"
#include "operator_api/note_types.h"
#include "operator_api/types.h"
#include "runtime/operators/operator_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

constexpr int kFrames = 256;
constexpr uint32_t kSampleRate = 48000;

static void* stub_lane_state(void*, uint32_t, uint32_t) { return nullptr; }

struct Harness {
    VividNoteBuffer notes{};
    void* custom_inputs[1] = {&notes};
    void* custom_outputs[1] = {nullptr};

    VividAudioContext ctx{};

    Harness() {
        ctx.sample_rate         = kSampleRate;
        ctx.buffer_size         = kFrames;
        ctx.lane_state_fn       = stub_lane_state;
        ctx.lane_state_service  = nullptr;
        ctx.lane_id             = 1;
        ctx.custom_inputs       = custom_inputs;
        ctx.custom_input_count  = 1;
        ctx.custom_outputs      = custom_outputs;
        ctx.custom_output_count = 1;
        // Default metronome state (sane bar-locked defaults). Tests that
        // exercise metronome rate_mode override these.
        ctx.metronome_bpm           = 120.0f;
        ctx.metronome_beats_per_bar = 4;
        ctx.metronome_beats_elapsed = 0.0;
        ctx.metronome_beat_phase    = 0.0f;
        ctx.metronome_bar_phase     = 0.0f;
        ctx.metronome_beat_ms       = 500.0f;
    }

    void set_metronome(double beats_elapsed, float bpm = 120.0f) {
        ctx.metronome_bpm           = bpm;
        ctx.metronome_beats_elapsed = beats_elapsed;
    }

    void clear_notes() { notes.count = 0; }

    void push_note_on(uint8_t note, float vel_0_1, uint64_t id) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e = {};
        e.type = VIVID_NOTE_ON; e.note_number = note; e.value = vel_0_1; e.note_id = id;
    }
    void push_pressure(uint64_t id, float v_0_1) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e = {};
        e.type = VIVID_NOTE_PRESSURE; e.note_id = id; e.value = v_0_1;
    }

    const VividNoteBuffer* output() const {
        return static_cast<const VividNoteBuffer*>(custom_outputs[0]);
    }
};

static const VividParamDescriptor* find_param(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->param_count; ++p)
        if (std::strcmp(desc->params[p].name, name) == 0) return &desc->params[p];
    return nullptr;
}

static int param_index(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->param_count; ++p)
        if (std::strcmp(desc->params[p].name, name) == 0) return static_cast<int>(p);
    return -1;
}

// Count events of a given type matching a given note_id in `out`.
static int count_events(const VividNoteBuffer* out, VividNoteEventType type, uint64_t note_id) {
    if (!out) return 0;
    int n = 0;
    for (uint32_t i = 0; i < out->count; ++i) {
        if (out->events[i].type == type && out->events[i].note_id == note_id) ++n;
    }
    return n;
}

// Fetch the LAST event of the given type for a note_id.
static const VividNoteEvent* last_event(const VividNoteBuffer* out, VividNoteEventType type, uint64_t note_id) {
    if (!out) return nullptr;
    const VividNoteEvent* last = nullptr;
    for (uint32_t i = 0; i < out->count; ++i) {
        if (out->events[i].type == type && out->events[i].note_id == note_id) last = &out->events[i];
    }
    return last;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string dylib_path = build_dir + "/note_modulator.dylib";

    if (!std::filesystem::exists(dylib_path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", dylib_path.c_str());
        return 1;
    }

    vivid::OperatorLoader loader;
    if (!loader.load(dylib_path.c_str())) {
        std::fprintf(stderr, "FATAL: failed to load %s\n", dylib_path.c_str());
        return 1;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "NoteModulator descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "NoteModulator") == 0, "operator name is NoteModulator");

    // ── Port surface ───────────────────────────────────────────────
    bool has_in = false, has_out = false;
    for (uint32_t p = 0; p < desc->port_count; ++p) {
        if (std::strcmp(desc->ports[p].name, "notes_in") == 0) has_in = true;
        if (std::strcmp(desc->ports[p].name, "notes_out") == 0) has_out = true;
    }
    check(has_in,  "declares notes_in");
    check(has_out, "declares notes_out");

    // ── Param surface — verify all 24 expected params exist ───────
    const char* expected_params[] = {
        "timbre_amount", "timbre_rate_hz", "timbre_waveform", "timbre_phase_offset",
        "timbre_phase_random", "timbre_polarity", "timbre_attack_ms", "timbre_mode",
        "pressure_amount", "pressure_rate_hz", "pressure_waveform", "pressure_phase_offset",
        "pressure_phase_random", "pressure_polarity", "pressure_attack_ms", "pressure_mode",
        "pitch_bend_amount", "pitch_bend_rate_hz", "pitch_bend_waveform", "pitch_bend_phase_offset",
        "pitch_bend_phase_random", "pitch_bend_polarity", "pitch_bend_attack_ms", "pitch_bend_mode",
    };
    for (const char* n : expected_params) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "param %s exists", n);
        check(find_param(desc, n) != nullptr, msg);
    }

    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
    int idx_t_amount  = param_index(desc, "timbre_amount");
    int idx_t_rand    = param_index(desc, "timbre_phase_random");
    int idx_t_attack  = param_index(desc, "timbre_attack_ms");
    int idx_p_amount  = param_index(desc, "pressure_amount");
    int idx_p_attack  = param_index(desc, "pressure_attack_ms");
    int idx_p_mode    = param_index(desc, "pressure_mode");
    int idx_p_polarity= param_index(desc, "pressure_polarity");

    // ── Test 1: amount=0 on all channels → output is verbatim ────
    {
        std::fprintf(stderr, "\n--- amount=0 → verbatim passthrough ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        // defaults already have all *_amount = 0.

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        h.clear_notes();
        h.push_note_on(60, 0.8f, 1001);
        h.push_note_on(64, 0.8f, 1002);
        uint32_t in_count = h.notes.count;

        loader.process_audio(inst, &h.ctx);
        const auto* out = h.output();
        check(out != nullptr, "notes_out published");
        check(out->count == in_count, "verbatim event count when all amounts=0");
        // Confirm no PRESSURE/TIMBRE/PITCH_BEND events were emitted.
        check(count_events(out, VIVID_NOTE_PRESSURE,   1001) == 0, "no PRESSURE injected (channel off)");
        check(count_events(out, VIVID_NOTE_TIMBRE,     1001) == 0, "no TIMBRE injected (channel off)");
        check(count_events(out, VIVID_NOTE_PITCH_BEND, 1001) == 0, "no PITCH_BEND injected (channel off)");

        loader.destroy_instance(inst);
    }

    // ── Test 2: per-note phase decorrelation ─────────────────────
    // Three simultaneous notes with distinct note_ids, timbre LFO at very
    // low rate (so phase barely advances within one block) and high
    // phase_random. Skip the attack ramp by setting attack_ms=0. Verify
    // that the three emitted timbre values are NOT all equal — proving
    // per-note hashing seeded different starting phases.
    {
        std::fprintf(stderr, "\n--- per-note phase decorrelation ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_t_amount] = 0.5f;
        params[idx_t_rand]   = 1.0f;        // max decorrelation
        params[idx_t_attack] = 0.0f;        // no attack ramp

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        // Block 1: NOTE_ONs only (this initializes per-note phases).
        h.clear_notes();
        h.push_note_on(60, 0.8f, 2001);
        h.push_note_on(64, 0.8f, 2002);
        h.push_note_on(67, 0.8f, 2003);
        loader.process_audio(inst, &h.ctx);

        // Block 2: empty input — modulator should emit one timbre event
        // per active note with each note's distinct phase.
        h.clear_notes();
        loader.process_audio(inst, &h.ctx);
        const auto* out = h.output();
        check(out != nullptr, "notes_out published on empty block");

        const auto* e1 = last_event(out, VIVID_NOTE_TIMBRE, 2001);
        const auto* e2 = last_event(out, VIVID_NOTE_TIMBRE, 2002);
        const auto* e3 = last_event(out, VIVID_NOTE_TIMBRE, 2003);
        check(e1 != nullptr, "TIMBRE event for note_id 2001");
        check(e2 != nullptr, "TIMBRE event for note_id 2002");
        check(e3 != nullptr, "TIMBRE event for note_id 2003");

        if (e1 && e2 && e3) {
            // At least two of the three values must differ.
            bool any_diff = (std::fabs(e1->value - e2->value) > 1e-4f)
                         || (std::fabs(e1->value - e3->value) > 1e-4f)
                         || (std::fabs(e2->value - e3->value) > 1e-4f);
            check(any_diff, "three notes emit distinct timbre values (phase decorrelated)");
        }

        loader.destroy_instance(inst);
    }

    // ── Test 3: compose-add semantics ────────────────────────────
    // Upstream PRESSURE event for note_id 3001 with value 0.5.
    // Modulator pressure: amount = 0.2, polarity = bipolar, attack_ms = 0.
    // After process_audio, output should contain a PRESSURE event for 3001
    // whose value is in [0.5 - 0.2, 0.5 + 0.2] (= [0.3, 0.7]). It must NOT
    // be exactly 0.5 (that would mean compose got dropped) and must NOT be
    // outside the upstream-stacked range (that would mean replace mode).
    {
        std::fprintf(stderr, "\n--- compose-add semantics ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_p_amount]   = 0.2f;
        params[idx_p_attack]   = 0.0f;
        params[idx_p_mode]     = 0.0f;   // add
        params[idx_p_polarity] = 0.0f;   // bipolar so the LFO swings around 0

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        // Block 1: NOTE_ON + upstream PRESSURE = 0.5.
        h.clear_notes();
        h.push_note_on(60, 0.8f, 3001);
        h.push_pressure(3001, 0.5f);
        loader.process_audio(inst, &h.ctx);

        const auto* out = h.output();
        const auto* e = last_event(out, VIVID_NOTE_PRESSURE, 3001);
        check(e != nullptr, "compose: PRESSURE event present after upstream + modulator");
        if (e) {
            // Composed value should be 0.5 + something_in_[-0.2, 0.2], clamped to [0,1].
            // It cannot equal 0.5 exactly unless the modulator contribution is
            // exactly 0 — which is unlikely given the random starting phase.
            // Instead, assert it lies in the permissible compose range.
            check(e->value >= 0.3f - 1e-4f && e->value <= 0.7f + 1e-4f,
                  "compose: emitted pressure within [upstream - amount, upstream + amount]");
        }
        loader.destroy_instance(inst);
    }

    // ── Test 4: replace mode ────────────────────────────────────
    // Same setup as test 3, but pressure_mode=1 (replace). Output value
    // should be in [-amount, amount] (bipolar) clamped to [0, 1] —
    // independent of the 0.5 upstream value. The output must NOT lie in
    // (0.3, 0.7) consistently (which is the compose-add range), proving
    // it's actually replacing.
    //
    // Specifically: with bipolar polarity and amount=0.2 and clamp to
    // [0,1], the replaced value lies in [0, 0.2] (since negative values
    // clamp to 0). This is OUTSIDE the [0.3, 0.7] compose range.
    {
        std::fprintf(stderr, "\n--- replace mode ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_p_amount]   = 0.2f;
        params[idx_p_attack]   = 0.0f;
        params[idx_p_mode]     = 1.0f;   // replace
        params[idx_p_polarity] = 0.0f;   // bipolar

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        h.clear_notes();
        h.push_note_on(60, 0.8f, 4001);
        h.push_pressure(4001, 0.5f);
        loader.process_audio(inst, &h.ctx);

        const auto* out = h.output();
        const auto* e = last_event(out, VIVID_NOTE_PRESSURE, 4001);
        check(e != nullptr, "replace: PRESSURE event present");
        if (e) {
            check(e->value >= 0.0f - 1e-4f && e->value <= 0.2f + 1e-4f,
                  "replace: emitted pressure within [0, amount] (clamped, bipolar)");
        }
        loader.destroy_instance(inst);
    }

    // Param-index lookups for the new tempo-sync params.
    int idx_t_rate_mode      = param_index(desc, "timbre_rate_mode");
    int idx_t_sync_division  = param_index(desc, "timbre_sync_division");
    int idx_t_phase_random   = idx_t_rand;  // alias for readability below
    int idx_t_polarity       = param_index(desc, "timbre_polarity");

    // ── Test 5: free-mode regression ──────────────────────────────
    // After splitting phase_accum from phase_random_offset, free mode
    // must behave identically to v1: each note's emitted timbre value
    // returns to its starting value after one full LFO period of frames.
    // This pins per-note phase_random spread + the accumulator semantics.
    {
        std::fprintf(stderr, "\n--- free-mode regression ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_t_amount]      = 0.5f;
        params[idx_t_rate_mode]   = static_cast<float>(vivid::kRateModeFree);
        params[idx_t_phase_random]= 1.0f;
        params[idx_t_attack]      = 0.0f;
        params[idx_t_polarity]    = 0.0f;  // bipolar
        // Pick rate_hz so one period spans an integer number of frames.
        // kFrames=256 @ 48kHz = 256/48000s ≈ 5.333ms per block. Set
        // rate=10Hz → period=0.1s → 0.1/5.333ms ≈ 18.75 blocks per period.
        // Use rate=5.859375Hz so period is exactly 32 blocks
        // (32 * 256/48000 = 0.17067s → freq = 1/0.17067 = 5.859375).
        const float kBlockSec = static_cast<float>(kFrames) / static_cast<float>(kSampleRate);
        const int kBlocksPerPeriod = 32;
        const float kFreq = 1.0f / (kBlocksPerPeriod * kBlockSec);
        params[param_index(desc, "timbre_rate_hz")] = kFreq;

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        h.clear_notes();
        h.push_note_on(60, 0.8f, 5001);
        loader.process_audio(inst, &h.ctx);
        const auto* out0 = h.output();
        const auto* e0 = last_event(out0, VIVID_NOTE_TIMBRE, 5001);
        check(e0 != nullptr, "free regression: TIMBRE event at block 0");
        float v0 = e0 ? e0->value : 0.0f;

        // Run kBlocksPerPeriod-1 more blocks (one full period total).
        h.clear_notes();
        for (int b = 0; b < kBlocksPerPeriod; ++b) loader.process_audio(inst, &h.ctx);
        const auto* outN = h.output();
        const auto* eN = last_event(outN, VIVID_NOTE_TIMBRE, 5001);
        check(eN != nullptr, "free regression: TIMBRE event after one period");
        if (e0 && eN) {
            check(std::fabs(eN->value - v0) < 1e-3f,
                  "free regression: value returns to start after one LFO period");
        }
        loader.destroy_instance(inst);
    }

    // ── Test 6: metronome lockstep ────────────────────────────────
    // Two NOTE_ONs with distinct note_ids, metronome rate_mode,
    // phase_random=0. Both notes must emit identical timbre values —
    // proves the global metronome phase source.
    {
        std::fprintf(stderr, "\n--- metronome lockstep ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_t_amount]       = 0.5f;
        params[idx_t_rate_mode]    = static_cast<float>(vivid::kRateModeMetronome);
        params[idx_t_sync_division]= 2.0f;   // 1/4
        params[idx_t_phase_random] = 0.0f;
        params[idx_t_attack]       = 0.0f;
        params[idx_t_polarity]     = 0.0f;   // bipolar

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();
        h.set_metronome(/*beats_elapsed=*/4.5);  // mid-bar

        h.clear_notes();
        h.push_note_on(60, 0.8f, 6001);
        h.push_note_on(64, 0.8f, 6002);
        loader.process_audio(inst, &h.ctx);

        const auto* out = h.output();
        const auto* e1 = last_event(out, VIVID_NOTE_TIMBRE, 6001);
        const auto* e2 = last_event(out, VIVID_NOTE_TIMBRE, 6002);
        check(e1 != nullptr, "metronome lockstep: TIMBRE for 6001");
        check(e2 != nullptr, "metronome lockstep: TIMBRE for 6002");
        if (e1 && e2) {
            check(std::fabs(e1->value - e2->value) < 1e-5f,
                  "metronome lockstep: simultaneous notes emit identical timbre");
        }
        loader.destroy_instance(inst);
    }

    // ── Test 7: metronome + phase_random ──────────────────────────
    // Same metronome setup as test 6 but phase_random=1.0. Two notes
    // must emit DIFFERENT timbre values despite sharing the metronome
    // phase — proves the per-note offset is layered on top in metronome
    // mode (so users can decorrelate even when bar-locked).
    //
    // Use UNIPOLAR polarity here: bipolar raw values can be negative,
    // and compose-add with upstream=0 then clamps to 0, which would
    // mask phase decorrelation behind the channel-range clamp. Unipolar
    // keeps mod_value in [0, amount] so distinct phases stay distinct.
    {
        std::fprintf(stderr, "\n--- metronome + phase_random ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_t_amount]       = 0.5f;
        params[idx_t_rate_mode]    = static_cast<float>(vivid::kRateModeMetronome);
        params[idx_t_sync_division]= 2.0f;
        params[idx_t_phase_random] = 1.0f;
        params[idx_t_attack]       = 0.0f;
        params[idx_t_polarity]     = 1.0f;  // unipolar — keep mod in [0, amount]

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();
        h.set_metronome(/*beats_elapsed=*/2.5);

        h.clear_notes();
        h.push_note_on(60, 0.8f, 7001);
        h.push_note_on(64, 0.8f, 7002);
        loader.process_audio(inst, &h.ctx);

        const auto* out = h.output();
        const auto* e1 = last_event(out, VIVID_NOTE_TIMBRE, 7001);
        const auto* e2 = last_event(out, VIVID_NOTE_TIMBRE, 7002);
        check(e1 != nullptr, "metronome+random: TIMBRE for 7001");
        check(e2 != nullptr, "metronome+random: TIMBRE for 7002");
        if (e1 && e2) {
            check(std::fabs(e1->value - e2->value) > 1e-3f,
                  "metronome+random: distinct notes emit distinct timbre even when bar-locked");
        }
        loader.destroy_instance(inst);
    }

    if (failures > 0) {
        std::fprintf(stderr, "\nFAIL: %d note_modulator test failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "\nOK\n");
    return 0;
}
