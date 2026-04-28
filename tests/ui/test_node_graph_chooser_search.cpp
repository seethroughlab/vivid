// Tests for the v2 chooser scorer (score_match_v2 in node_graph_util.h).
// These verify that the original failing case ("Chord Progression" -> the
// operator id "ChordProgression") and the related natural-language queries
// all produce positive scores in the expected ranking band.

#include "ui/graph/graph_snapshot.h"
#include "ui/graph/node_graph_util.h"
#include "common/operator_label.h"
#include "test_helpers.h"

#include <string>
#include <vector>

using vivid::ui::OperatorInfo;
using vivid::ui::build_search_haystack;
using vivid::ui::score_match_v2;
using vivid::normalize_for_search;

static OperatorInfo make_op(const std::string& id,
                            const std::string& display_name,
                            std::vector<std::string> keywords = {},
                            std::string summary = "") {
    OperatorInfo info;
    info.name = id;
    info.display_name = display_name;
    info.keywords = std::move(keywords);
    info.summary = std::move(summary);
    build_search_haystack(info);
    return info;
}

int main() {
    std::fprintf(stderr, "--- test_node_graph_chooser_search ---\n");

    auto chord = make_op("ChordProgression", "Chord Progression",
                         {"harmony", "chords", "diatonic"},
                         "Diatonic chord changes from a key + Roman-numeral pattern.");
    auto reverb = make_op("Reverb", "Reverb", {"space", "decay"},
                          "Algorithmic reverb tail.");
    auto fft    = make_op("FFTAnalyzer", "FFT Analyzer", {"spectrum"},
                          "Frequency-domain spectrum analysis.");
    auto audio  = make_op("AudioOut", "Audio Out", {}, "Output audio to a device.");

    // ---- Original failing case --------------------------------------------
    {
        std::fprintf(stderr, "\n=== Original failing case ===\n");
        int s = score_match_v2(chord.search, normalize_for_search("Chord Progression"));
        check(s == 1000, "exact 'Chord Progression' -> tier 1 (1000) for ChordProgression");
        // Other operators must NOT match this query
        check(score_match_v2(reverb.search, normalize_for_search("Chord Progression")) < 0,
              "Reverb does not match 'Chord Progression'");
    }

    // ---- Partial / natural-language queries -------------------------------
    {
        std::fprintf(stderr, "\n=== Partial queries ===\n");
        int s_prog = score_match_v2(chord.search, normalize_for_search("chord prog"));
        check(s_prog > 0, "'chord prog' surfaces ChordProgression");
        check(s_prog == 700,
              "'chord prog' is a prefix on display_name -> tier 2 (700)");

        int s_lower = score_match_v2(chord.search, normalize_for_search("chord"));
        check(s_lower == 700, "'chord' prefix on display_name");

        int s_camel = score_match_v2(chord.search, normalize_for_search("ChordProg"));
        check(s_camel >= 400,
              "'ChordProg' (no space) hits via id_norm tier (>= tier 4)");
    }

    // ---- Keyword tier -----------------------------------------------------
    {
        std::fprintf(stderr, "\n=== Keyword tier ===\n");
        int s = score_match_v2(chord.search, normalize_for_search("harmony"));
        check(s == 250, "'harmony' surfaces ChordProgression via keywords (tier 5)");
        check(score_match_v2(reverb.search, normalize_for_search("harmony")) < 0,
              "Reverb does not match 'harmony'");
    }

    // ---- Initials tier ----------------------------------------------------
    {
        std::fprintf(stderr, "\n=== Initials tier ===\n");
        int s = score_match_v2(chord.search, normalize_for_search("cp"));
        check(s == 200, "'cp' initials match 'Chord Progression' (tier 6)");
        // FFT Analyzer: "fa" should hit
        check(score_match_v2(fft.search, normalize_for_search("fa")) == 200,
              "'fa' initials match 'FFT Analyzer'");
    }

    // ---- Summary tier -----------------------------------------------------
    {
        std::fprintf(stderr, "\n=== Summary tier ===\n");
        int s = score_match_v2(chord.search, normalize_for_search("roman"));
        check(s >= 50 && s <= 99,
              "'roman' surfaces ChordProgression via summary (tier 7)");
    }

    // ---- Empty query ------------------------------------------------------
    {
        std::fprintf(stderr, "\n=== Empty query ===\n");
        check(score_match_v2(chord.search, "") == 0, "empty query returns 0");
        check(score_match_v2(reverb.search, "") == 0, "empty query returns 0 for any op");
    }

    // ---- Ranking — display_name should outrank keyword for shared terms ---
    {
        std::fprintf(stderr, "\n=== Ranking ===\n");
        // Simulate two operators where one has "audio" in its display_name
        // and another only has "audio" as a keyword.
        auto audio_filter = make_op("AudioFilter", "Audio Filter", {}, "");
        auto compressor   = make_op("Compressor", "Compressor", {"audio", "dynamics"}, "");
        int s_filter = score_match_v2(audio_filter.search, normalize_for_search("audio"));
        int s_comp   = score_match_v2(compressor.search,   normalize_for_search("audio"));
        check(s_filter > 0 && s_comp > 0, "both match 'audio'");
        check(s_filter > s_comp,
              "name-tier (Audio Filter) outranks keyword-tier (Compressor)");
    }

    // ---- Unrelated query --------------------------------------------------
    {
        std::fprintf(stderr, "\n=== Unrelated ===\n");
        check(score_match_v2(chord.search, normalize_for_search("xyzzy")) < 0,
              "unrelated query returns -1");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
