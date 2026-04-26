// Tracker pattern_data serialization tests.
//
// Verifies the JSON schema migration (2026-04):
//   - serialize_song writes JSON
//   - deserialize_song dispatches by first non-whitespace char
//   - both paths produce equivalent TrackerSong state
//   - schema v > 2 is rejected
//   - sparse patterns round-trip without spurious cells
//   - note-off (255) round-trips
//   - empty / malformed input degrades gracefully

#include "tracker_data.h"
#include "test_helpers.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace tracker;

namespace {

// Build a representative TrackerSong with mixed content: notes, velocities,
// effects, note-off, and empty cells across multiple patterns + arrangement.
TrackerSong make_fixture_song() {
    TrackerSong s{};
    s.num_patterns = 2;
    s.arrangement_length = 4;
    s.arrangement[0] = 0;
    s.arrangement[1] = 1;
    s.arrangement[2] = 0;
    s.arrangement[3] = 1;

    auto set_cell = [&](int p, int ch, int row, uint8_t note, uint8_t vel,
                        uint8_t fxt = 0, uint8_t fxp = 0) {
        s.patterns[p].cells[ch][row] = {note, vel, fxt, fxp};
    };

    s.patterns[0].num_rows = 16;
    set_cell(0, 0,  0, 66, 100);            // F#4 vel 100
    set_cell(0, 0,  3, 69,  85);            // A4  vel 85
    set_cell(0, 0,  5, 73,  90);            // C#5 vel 90
    set_cell(0, 1,  8, 60,  80, 0x01, 32);  // C4  vel 80, FX 01 20
    set_cell(0, 0, 12, NOTE_OFF, 0);         // note-off
    set_cell(0, 7, 15, 36,  64);            // ch7 last row (boundary)

    s.patterns[1].num_rows = 8;
    set_cell(1, 0, 0, 30, 127);             // F#1 max velocity (drone)

    return s;
}

bool cells_equal(const TrackerCell& a, const TrackerCell& b) {
    return a.note == b.note && a.velocity == b.velocity &&
           a.effect_type == b.effect_type && a.effect_param == b.effect_param;
}

bool songs_equal(const TrackerSong& a, const TrackerSong& b) {
    if (a.num_patterns != b.num_patterns) return false;
    if (a.arrangement_length != b.arrangement_length) return false;
    for (int i = 0; i < a.arrangement_length; ++i)
        if (a.arrangement[i] != b.arrangement[i]) return false;
    for (int p = 0; p < a.num_patterns; ++p) {
        if (a.patterns[p].num_rows != b.patterns[p].num_rows) return false;
        for (int r = 0; r < a.patterns[p].num_rows; ++r) {
            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                if (!cells_equal(a.patterns[p].cells[ch][r],
                                 b.patterns[p].cells[ch][r])) {
                    std::fprintf(stderr,
                        "  cell mismatch at pat=%d ch=%d row=%d: "
                        "(%u,%u,%u,%u) vs (%u,%u,%u,%u)\n",
                        p, ch, r,
                        a.patterns[p].cells[ch][r].note,
                        a.patterns[p].cells[ch][r].velocity,
                        a.patterns[p].cells[ch][r].effect_type,
                        a.patterns[p].cells[ch][r].effect_param,
                        b.patterns[p].cells[ch][r].note,
                        b.patterns[p].cells[ch][r].velocity,
                        b.patterns[p].cells[ch][r].effect_type,
                        b.patterns[p].cells[ch][r].effect_param);
                    return false;
                }
            }
        }
    }
    return true;
}

int count_non_empty_cells(const TrackerSong& s) {
    int n = 0;
    for (int p = 0; p < s.num_patterns; ++p)
        for (int r = 0; r < s.patterns[p].num_rows; ++r)
            for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
                const auto& c = s.patterns[p].cells[ch][r];
                if (c.note != NOTE_EMPTY || c.velocity != 0 ||
                    c.effect_type != 0 || c.effect_param != 0) ++n;
            }
    return n;
}

} // namespace

int main() {
    // -----------------------------------------------------------------
    // Test 1: serialize_song writes JSON (starts with '{', has "v":2).
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- serialize_song writes JSON ---\n");
        TrackerSong song = make_fixture_song();
        std::string out = serialize_song(song);
        check(!out.empty(), "serialized output non-empty");
        check(out[0] == '{', "first char is '{' (JSON)");
        check(out.find("\"v\":2") != std::string::npos ||
              out.find("\"v\": 2") != std::string::npos,
              "contains schema version v=2");
        check(out.find("\"arrangement\"") != std::string::npos,
              "contains arrangement key");
        check(out.find("\"patterns\"") != std::string::npos,
              "contains patterns key");
    }

    // -----------------------------------------------------------------
    // Test 2: JSON round-trip preserves the song exactly.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- JSON round-trip ---\n");
        TrackerSong original = make_fixture_song();
        std::string s = serialize_song(original);
        TrackerSong restored;
        bool ok = deserialize_song(s, restored);
        check(ok, "deserialize_song ok on JSON");
        check(songs_equal(original, restored), "JSON round-trip preserves song");
    }

    // -----------------------------------------------------------------
    // Test 3: legacy text format still loads, and a legacy → JSON →
    //         legacy → JSON migration preserves the cell content.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- legacy → JSON migration ---\n");
        TrackerSong original = make_fixture_song();
        std::string legacy_text = serialize_song_legacy(original);
        check(legacy_text.compare(0, 12, "arrangement:") == 0,
              "legacy serializer produces expected prefix");

        TrackerSong from_legacy;
        bool ok1 = deserialize_song(legacy_text, from_legacy);
        check(ok1, "deserialize_song ok on legacy text");
        check(songs_equal(original, from_legacy),
              "legacy round-trip preserves song");

        // Now save as JSON and reload — simulates "load old graph, re-save".
        std::string json_text = serialize_song(from_legacy);
        TrackerSong from_json;
        bool ok2 = deserialize_song(json_text, from_json);
        check(ok2, "deserialize_song ok on migrated JSON");
        check(songs_equal(original, from_json),
              "legacy → JSON migration preserves song");
    }

    // -----------------------------------------------------------------
    // Test 4: schema v > kSongSchemaVersion is rejected (loads as empty).
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- schema v>%d rejected ---\n", kSongSchemaVersion);
        const std::string future_text =
            "{\"v\":99,\"arrangement\":[0],\"patterns\":[{\"rows\":16,\"cells\":[]}]}";
        TrackerSong song;
        bool ok = deserialize_song(future_text, song);
        check(!ok, "deserialize_song returns false on future schema version");
    }

    // -----------------------------------------------------------------
    // Test 5: empty input → false, no crash.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- empty input ---\n");
        TrackerSong song;
        check(!deserialize_song("", song), "empty string returns false");
        check(!deserialize_song("   \n\t  ", song), "whitespace-only returns false");
    }

    // -----------------------------------------------------------------
    // Test 6: sparse patterns don't bloat to a full 64×8 dense grid.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- sparse pattern preservation ---\n");
        TrackerSong sparse{};
        sparse.num_patterns = 1;
        sparse.arrangement_length = 1;
        sparse.arrangement[0] = 0;
        sparse.patterns[0].num_rows = 16;
        sparse.patterns[0].cells[0][0] = {66, 100, 0, 0};   // one cell only

        std::string s = serialize_song(sparse);
        // Expect exactly one cell entry in the output.
        size_t cell_count = 0;
        size_t pos = 0;
        while ((pos = s.find("\"row\"", pos)) != std::string::npos) {
            ++cell_count;
            ++pos;
        }
        check(cell_count == 1, "single-cell pattern serializes one cell");

        TrackerSong restored;
        check(deserialize_song(s, restored), "sparse JSON round-trips");
        check(count_non_empty_cells(restored) == 1,
              "no spurious cells reappear on restore");
        check(restored.patterns[0].cells[0][0].note == 66,
              "sparse cell content preserved");
    }

    // -----------------------------------------------------------------
    // Test 7: note-off (255) round-trips correctly.
    // -----------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- note-off round-trip ---\n");
        TrackerSong song{};
        song.num_patterns = 1;
        song.arrangement_length = 1;
        song.patterns[0].num_rows = 16;
        song.patterns[0].cells[0][4] = {NOTE_OFF, 0, 0, 0};

        std::string s = serialize_song(song);
        TrackerSong restored;
        check(deserialize_song(s, restored), "note-off round-trip deserialize ok");
        check(restored.patterns[0].cells[0][4].note == NOTE_OFF,
              "note-off (255) preserved");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
