// Tracker expression authoring tests (Phase 4 PR1).
//
// Verifies:
//   1. Per-cell expression fields (pitch_bend / pressure / timbre) round-trip
//      through the JSON v2 schema.
//   2. v1 patterns still load with all expression fields set to kExprEmpty
//      and expression_lane_mask = 0 (full back-compat).
//   3. tracker_expression interpolation logic — prev/next anchor lookup,
//      linear interpolation in tick space, hold-past-last-anchor, no-anchor
//      returns kExprEmpty.
//   4. Value conversion: raw int16 -> domain (pitch_bend ±48 semis matches
//      MidiInput's MPE convention; pressure/timbre clamp to 0..1).
//   5. expression_lane_mask gates serialization + interpolation visibility.

#include "tracker_core.h"
#include "tracker_data.h"
#include "tracker_expression.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace tracker;
namespace tx = vivid::tracker_expression;

namespace {
// Test access to TrackerCore's protected playback state. We need to:
//   - poke `song_` directly (skip the serialization round-trip)
//   - call `process_tick` to advance one tick at a time
//   - inspect `notes_buf_` for emitted events
struct TestTracker : public TrackerCore {
    using TrackerCore::song_;
    using TrackerCore::notes_buf_;
    using TrackerCore::current_row_;
    using TrackerCore::current_tick_;
    using TrackerCore::process_tick;
};
}  // namespace


namespace {

// Aggregate equality including expression fields.
bool cells_equal_full(const TrackerCell& a, const TrackerCell& b) {
    return a.note == b.note && a.velocity == b.velocity &&
           a.effect_type == b.effect_type && a.effect_param == b.effect_param &&
           a.pitch_bend == b.pitch_bend && a.pressure == b.pressure &&
           a.timbre == b.timbre;
}

}  // namespace

int main() {
    // -----------------------------------------------------------------
    // Test 1: cell with expression round-trips through JSON v2.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- expression cell round-trip ---\n");
        TrackerSong song{};
        song.num_patterns = 1;
        song.patterns[0].num_rows = 8;
        song.patterns[0].expression_lane_mask = kLanePb | kLanePr | kLaneTb;

        auto& c = song.patterns[0].cells[0][0];
        c.note = 60;
        c.velocity = 100;
        c.pitch_bend = 8192;   // ~+12 semis
        c.pressure   = 24576;  // ~0.75
        c.timbre     = -4096;  // negative timbre stays in cell (clamped on read)

        std::string text = serialize_song(song);
        std::fprintf(stderr, "  json size: %zu bytes\n", text.size());
        check(text.find("\"v\":2") != std::string::npos,
              "schema version is v=2");
        check(text.find("\"expr_mask\":7") != std::string::npos,
              "expr_mask serialized when nonzero");
        check(text.find("\"pb\":8192") != std::string::npos, "pb serialized");
        check(text.find("\"pr\":24576") != std::string::npos, "pr serialized");
        check(text.find("\"tb\":-4096") != std::string::npos, "tb serialized");

        TrackerSong loaded;
        check(deserialize_song(text, loaded), "deserialize succeeds");
        check(loaded.patterns[0].expression_lane_mask == 7,
              "expression_lane_mask preserved");
        check(cells_equal_full(loaded.patterns[0].cells[0][0],
                               song.patterns[0].cells[0][0]),
              "cell with expression round-trips identically");
    }

    // -----------------------------------------------------------------
    // Test 2: cells with no expression omit the fields (size discipline).
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- empty expression fields are omitted ---\n");
        TrackerSong song{};
        song.num_patterns = 1;
        song.patterns[0].num_rows = 4;
        // No expr_mask, no pb/pr/tb anywhere.
        song.patterns[0].cells[0][0] = TrackerCell{};
        song.patterns[0].cells[0][0].note = 60;
        song.patterns[0].cells[0][0].velocity = 100;

        std::string text = serialize_song(song);
        check(text.find("\"expr_mask\"") == std::string::npos,
              "expr_mask omitted when zero");
        check(text.find("\"pb\"") == std::string::npos, "pb omitted when empty");
        check(text.find("\"pr\"") == std::string::npos, "pr omitted when empty");
        check(text.find("\"tb\"") == std::string::npos, "tb omitted when empty");
    }

    // -----------------------------------------------------------------
    // Test 3: v1 patterns load with expression defaults.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- v1 backward-compat ---\n");
        const char* v1_json = R"({
            "v":1,
            "arrangement":[0],
            "patterns":[
                {"rows":4, "cells":[
                    {"row":0, "channel":0, "note":60, "velocity":100}
                ]}
            ]
        })";
        TrackerSong loaded;
        check(deserialize_song(v1_json, loaded), "v1 pattern loads");
        check(loaded.patterns[0].expression_lane_mask == 0,
              "v1 mask defaults to 0");
        const auto& c = loaded.patterns[0].cells[0][0];
        check(c.note == 60, "v1 note preserved");
        check(c.pitch_bend == kExprEmpty, "v1 pitch_bend defaults to empty");
        check(c.pressure   == kExprEmpty, "v1 pressure defaults to empty");
        check(c.timbre     == kExprEmpty, "v1 timbre defaults to empty");
    }

    // -----------------------------------------------------------------
    // Test 4: value converters match MPE convention.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- value conversion (MPE compatibility) ---\n");
        // pitch_bend: raw 32767 = +48 semis (matches midi_input.cpp:237)
        check_float(tx::pitch_bend_to_semis(32767),  48.0f, 0.01f, "pb +max -> +48 semis");
        check_float(tx::pitch_bend_to_semis(-32767), -48.0f, 0.01f, "pb -max -> -48 semis");
        check_float(tx::pitch_bend_to_semis(0),       0.0f, 0.001f, "pb 0 -> 0 semis");
        check_float(tx::pitch_bend_to_semis(8192),   12.0f, 0.05f, "pb 8192 ~ +12 semis");
        // pressure / timbre: 0..32767 -> 0..1
        check_float(tx::pressure_to_unit(32767), 1.0f, 0.001f, "pr +max -> 1.0");
        check_float(tx::pressure_to_unit(0),     0.0f, 0.001f, "pr 0 -> 0.0");
        check_float(tx::pressure_to_unit(16384), 0.5f, 0.01f, "pr 16384 ~ 0.5");
        check_float(tx::pressure_to_unit(-1000), 0.0f, 0.001f, "pr negative clamps to 0");
        check_float(tx::timbre_to_unit(32767),   1.0f, 0.001f, "tb +max -> 1.0");
    }

    // -----------------------------------------------------------------
    // Test 5: interpolation logic.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- interpolation: linear between anchors ---\n");
        TrackerPattern pat;
        pat.num_rows = 8;
        pat.expression_lane_mask = kLanePb;
        pat.cells[0][0].pitch_bend = 0;
        pat.cells[0][4].pitch_bend = 16384;  // anchor at row 4

        const int tps = 6;
        // tick 0 of row 0: at anchor -> 0
        check(tx::interpolate(pat, 0, tx::Lane::PitchBend, 0, 0, tps) == 0,
              "tick 0 of anchor row returns anchor value");
        // tick 0 of row 4: at next anchor -> 16384
        check(tx::interpolate(pat, 0, tx::Lane::PitchBend, 4, 0, tps) == 16384,
              "tick 0 of next-anchor row returns next anchor value");
        // tick 0 of row 2 (midway): linear -> ~8192
        int16_t mid = tx::interpolate(pat, 0, tx::Lane::PitchBend, 2, 0, tps);
        check(std::abs(mid - 8192) <= 1,
              "midway between anchors yields midway value");
        // After last anchor: held value
        int16_t after = tx::interpolate(pat, 0, tx::Lane::PitchBend, 6, 3, tps);
        check(after == 16384, "after last anchor returns held last-anchor value");
        // No anchor: kExprEmpty
        TrackerPattern empty;
        empty.num_rows = 8;
        check(tx::interpolate(empty, 0, tx::Lane::PitchBend, 3, 0, tps) == kExprEmpty,
              "pattern with no anchors returns kExprEmpty");
    }

    // -----------------------------------------------------------------
    // Test 6: lane visibility helper.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- lane visibility mask ---\n");
        TrackerPattern pat;
        pat.expression_lane_mask = 0;
        check(!tx::lane_visible(pat, tx::Lane::PitchBend), "pb hidden when mask=0");
        check(!tx::lane_visible(pat, tx::Lane::Pressure),  "pr hidden when mask=0");
        check(!tx::lane_visible(pat, tx::Lane::Timbre),    "tb hidden when mask=0");
        pat.expression_lane_mask = kLanePb | kLaneTb;
        check( tx::lane_visible(pat, tx::Lane::PitchBend), "pb visible when mask|=kLanePb");
        check(!tx::lane_visible(pat, tx::Lane::Pressure),  "pr stays hidden");
        check( tx::lane_visible(pat, tx::Lane::Timbre),    "tb visible when mask|=kLaneTb");
    }

    // -----------------------------------------------------------------
    // Test 7: end-to-end emission via TrackerCore::process_tick.
    // Pattern with NOTE_ON at row 0 ch 0 + pb anchor 0 at row 0,
    // pb anchor +16384 at row 4 (~ +24 semis target). Walking ticks
    // 0..23 (rows 0..3 at speed=6) should emit: a NOTE_ON at tick 0,
    // then PITCH_BEND events with monotonically-increasing semitones
    // as interpolation walks from row 0 to row 4.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- end-to-end PITCH_BEND emission ---\n");
        TestTracker t;
        t.song_.num_patterns = 1;
        t.song_.arrangement_length = 1;
        t.song_.arrangement[0] = 0;
        auto& pat = t.song_.patterns[0];
        pat.num_rows = 8;
        pat.expression_lane_mask = kLanePb;
        pat.cells[0][0] = TrackerCell{};
        pat.cells[0][0].note = 60;
        pat.cells[0][0].velocity = 100;
        pat.cells[0][0].pitch_bend = 0;        // anchor at row 0
        pat.cells[0][4] = TrackerCell{};
        pat.cells[0][4].pitch_bend = 16384;    // anchor at row 4

        const int speed = 6;
        // Walk ticks 0..23 (rows 0..3 inclusive of tick 0 of row 4 needs t=24
        // but we'll cap before that to keep things contained).
        // Tick 0: process_new_row -> NOTE_ON, then emit (interp at tick 0 = 0,
        // last_emitted=0 after note_on, so no event since they match).
        // Tick 1..23: process_tick_effects, interpolation advances pb.
        int note_on_count = 0;
        int pb_count = 0;
        float last_pb_semis = -1000.0f;
        bool monotonic = true;
        for (int i = 0; i < 24; ++i) {
            t.notes_buf_.count = 0;
            t.process_tick(speed, /*base_ch=*/0, /*ch_mode=*/0, /*mute=*/0);
            for (uint32_t e = 0; e < t.notes_buf_.count; ++e) {
                const auto& ev = t.notes_buf_.events[e];
                if (ev.type == VIVID_NOTE_ON) ++note_on_count;
                if (ev.type == VIVID_NOTE_PITCH_BEND) {
                    ++pb_count;
                    if (ev.value < last_pb_semis - 0.01f) monotonic = false;
                    last_pb_semis = ev.value;
                }
            }
        }
        check(note_on_count == 1, "exactly one NOTE_ON emitted at row 0");
        check(pb_count > 0, "PITCH_BEND events emitted during interpolation");
        check(monotonic, "PITCH_BEND values are monotonically increasing");
        // Final emitted value should be near the +24 semis target when we
        // reach row 4 tick 0 (last iteration above is row 3 tick 5 — close
        // but not at the anchor; should be roughly 23/24 of +24 ≈ 23 semis).
        check(last_pb_semis > 18.0f && last_pb_semis < 25.0f,
              "final emitted PITCH_BEND is near the row-4 target value");
        std::fprintf(stderr, "  emitted %d PITCH_BEND events; final = %.2f semis\n",
                     pb_count, last_pb_semis);
    }

    // -----------------------------------------------------------------
    // Test 8: lane mask gates emission — pb anchors but mask=0, no events.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- lane mask gates emission ---\n");
        TestTracker t;
        t.song_.num_patterns = 1;
        t.song_.arrangement_length = 1;
        t.song_.arrangement[0] = 0;
        auto& pat = t.song_.patterns[0];
        pat.num_rows = 8;
        pat.expression_lane_mask = 0;   // hidden — anchors present but ignored
        pat.cells[0][0] = TrackerCell{};
        pat.cells[0][0].note = 60;
        pat.cells[0][0].velocity = 100;
        pat.cells[0][0].pitch_bend = 0;
        pat.cells[0][4] = TrackerCell{};
        pat.cells[0][4].pitch_bend = 16384;

        int pb_count = 0;
        for (int i = 0; i < 24; ++i) {
            t.notes_buf_.count = 0;
            t.process_tick(6, 0, 0, 0);
            for (uint32_t e = 0; e < t.notes_buf_.count; ++e)
                if (t.notes_buf_.events[e].type == VIVID_NOTE_PITCH_BEND)
                    ++pb_count;
        }
        check(pb_count == 0, "no PITCH_BEND events when expression_lane_mask=0");
    }

    // -----------------------------------------------------------------
    // Test 9: schema rejection of unknown future versions.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- schema v>2 is rejected ---\n");
        const char* v3 = R"({"v":3, "patterns":[]})";
        TrackerSong loaded;
        check(!deserialize_song(v3, loaded), "v3 is rejected");
    }

    if (failures == 0)
        std::fprintf(stderr, "\nALL PASSED\n");
    else
        std::fprintf(stderr, "\n%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
