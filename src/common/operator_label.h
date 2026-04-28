#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace vivid {

// Convert a stable operator id like "ChordProgression" into a default
// human-facing display name. Splitting rules:
//   1. '_', '-', or whitespace -> word boundary (collapsed to single space).
//   2. lower -> upper transition: split (e.g. "ChordProgression" -> "Chord Progression").
//   3. UPPER run followed by Upper+lower: split before the trailing upper
//      (e.g. "FFTAnalyzer" -> "FFT Analyzer", "MIDIIn" -> "MIDI In").
//   4. digit -> upper transition: split (e.g. "Render2D" -> "Render2 D").
//
// First char of each word is uppercased; the rest is preserved verbatim so
// internal capitalization stays intact (acronyms like "LFO" stay together by
// rule 3 not firing). Author overrides via kDisplayName for cases the rules
// can't disambiguate from the id alone — "FmSynth" -> "Fm Synth" by rule 2,
// override to "FM Synth"; "Render2D" -> "Render2 D" by rule 4, override to
// "Render 2D".
inline std::string default_display_name(std::string_view stable_id) {
    auto is_sep = [](unsigned char c) {
        return c == '_' || c == '-' || std::isspace(c);
    };
    auto is_upper = [](unsigned char c) { return std::isupper(c) != 0; };
    auto is_lower = [](unsigned char c) { return std::islower(c) != 0; };
    auto is_digit = [](unsigned char c) { return std::isdigit(c) != 0; };

    std::string out;
    out.reserve(stable_id.size() + 4);
    bool at_word_start = true;

    for (std::size_t i = 0; i < stable_id.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(stable_id[i]);

        if (is_sep(c)) {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            at_word_start = true;
            continue;
        }

        // Detect a split point relative to the previous *non-separator* char.
        // (`out.back() != ' '` ensures we only consider real adjacency, not
        // anything across an already-emitted boundary.)
        if (!out.empty() && out.back() != ' ') {
            unsigned char prev = static_cast<unsigned char>(stable_id[i - 1]);
            unsigned char next = (i + 1 < stable_id.size())
                ? static_cast<unsigned char>(stable_id[i + 1]) : 0;
            const bool rule_lower_upper = is_lower(prev) && is_upper(c);
            const bool rule_acronym_tail =
                is_upper(prev) && is_upper(c) && next != 0 && is_lower(next);
            const bool rule_digit_upper = is_digit(prev) && is_upper(c);
            if (rule_lower_upper || rule_acronym_tail || rule_digit_upper) {
                out.push_back(' ');
                at_word_start = true;
            }
        }

        if (at_word_start)
            out.push_back(static_cast<char>(std::toupper(c)));
        else
            out.push_back(static_cast<char>(c));
        at_word_start = false;
    }

    // Trim any trailing whitespace produced by a separator at the end.
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Lowercase + collapse non-alphanumeric runs to single spaces. Used to
// normalize both the chooser query and the per-operator search haystack so
// that "Chord Progression", "chord-progression", and "chord_progression" all
// reduce to the same canonical form. Idempotent.
inline std::string normalize_for_search(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool last_was_space = true;  // suppresses leading whitespace
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            last_was_space = false;
        } else if (!last_was_space) {
            out.push_back(' ');
            last_was_space = true;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

} // namespace vivid
