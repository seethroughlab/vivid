#include "common/operator_label.h"
#include "test_helpers.h"
#include <string>

int main() {
    using vivid::default_display_name;
    using vivid::normalize_for_search;

    // =================================================================
    // Test 1: default_display_name — CamelCase split
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: CamelCase ===\n");
        check(default_display_name("ChordProgression") == "Chord Progression",
              "ChordProgression -> Chord Progression");
        check(default_display_name("AudioOut") == "Audio Out", "AudioOut -> Audio Out");
        check(default_display_name("Reverb") == "Reverb", "single word capitalized");
    }

    // =================================================================
    // Test 2: snake_case / kebab-case
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: snake/kebab ===\n");
        check(default_display_name("audio_out") == "Audio Out", "audio_out -> Audio Out");
        check(default_display_name("audio-out") == "Audio Out", "audio-out -> Audio Out");
        check(default_display_name("low_pass_filter") == "Low Pass Filter", "three-word snake");
        check(default_display_name("audio") == "Audio", "single lower word");
    }

    // =================================================================
    // Test 3: acronyms — all-caps stays together
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: acronyms ===\n");
        check(default_display_name("LFO") == "LFO", "LFO stays whole");
        check(default_display_name("FFT") == "FFT", "FFT stays whole");
        check(default_display_name("ADSR") == "ADSR", "ADSR stays whole");
    }

    // =================================================================
    // Test 4: acronym + suffix — UPPER->Upper+lower split
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: acronym + suffix ===\n");
        check(default_display_name("FFTAnalyzer") == "FFT Analyzer", "FFTAnalyzer");
        check(default_display_name("MIDIIn") == "MIDI In", "MIDIIn");
        check(default_display_name("XYPad") == "XY Pad", "XYPad");
    }

    // =================================================================
    // Test 5: digit boundaries — Render2D documented as "Render2 D"
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: digits ===\n");
        check(default_display_name("Render2D") == "Render2 D",
              "Render2D documented degenerate (override expected)");
        check(default_display_name("16Step") == "16 Step", "leading digits");
        check(default_display_name("Beat3") == "Beat3", "trailing digit no split");
    }

    // =================================================================
    // Test 6: edge cases — empty, leading/trailing seps, idempotence
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: edges ===\n");
        check(default_display_name("") == "", "empty stays empty");
        check(default_display_name("_audio_") == "Audio",
              "leading + trailing separator trimmed");
        check(default_display_name("Chord Progression") == "Chord Progression",
              "already-spaced input idempotent");
        check(default_display_name(default_display_name("ChordProgression")) ==
              default_display_name("ChordProgression"),
              "idempotent under double-application");
    }

    // =================================================================
    // Test 7: normalize_for_search basics
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: normalize_for_search ===\n");
        check(normalize_for_search("Chord Progression") == "chord progression",
              "spaces preserved as single space, lowercased");
        check(normalize_for_search("chord_progression") == "chord progression",
              "underscore -> space");
        check(normalize_for_search("chord-progression") == "chord progression",
              "hyphen -> space");
        check(normalize_for_search("ChordProgression") == "chordprogression",
              "CamelCase NOT split here (matcher handles via id_norm haystack)");
        check(normalize_for_search("  Chord  Progression  ") == "chord progression",
              "collapse runs + trim");
        check(normalize_for_search("CP-3") == "cp 3", "alphanumeric preserved");
        check(normalize_for_search("") == "", "empty stays empty");
        check(normalize_for_search(normalize_for_search("Chord-Progression!")) ==
              normalize_for_search("Chord-Progression!"),
              "idempotent under double-application");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
