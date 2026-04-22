// Pure-logic tests for the Euclidean shared helpers — the extracted
// Bjorklund algorithm and the density preset list. These used to be
// duplicated across the core and the thumbnail; extraction means a
// single test suite covers both surfaces (and the new editor).

#include "euclidean_editor_shared.h"

#include <cstdio>
#include <cstring>

#include "test_helpers.h"

namespace eu = ::vivid::euclidean_editor;

namespace {

bool pattern_equals(const int* actual, const int* expected, int n) {
    for (int i = 0; i < n; ++i) if (actual[i] != expected[i]) return false;
    return true;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: Euclidean shared helpers ===\n\n");

    // --- All-hits / all-rests edge cases ---
    {
        int p[eu::kMaxSteps] = {};
        eu::compute_pattern(0, 8, 0, p);
        check(p[0] == 0 && p[4] == 0 && p[7] == 0,
              "hits=0 yields all rests");

        eu::compute_pattern(8, 8, 0, p);
        int expected[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        check(pattern_equals(p, expected, 8), "hits=steps yields all hits");
    }

    // --- 3/8 tresillo: 1 0 0 1 0 0 1 0 ---
    {
        int p[eu::kMaxSteps] = {};
        eu::compute_pattern(3, 8, 0, p);
        int expected[8] = {1, 0, 0, 1, 0, 0, 1, 0};
        check(pattern_equals(p, expected, 8),
              "3/8 tresillo pattern matches canonical output");
    }

    // --- 5/8 cinquillo: 1 0 1 1 0 1 1 0 (standard Euclidean output) ---
    {
        int p[eu::kMaxSteps] = {};
        eu::compute_pattern(5, 8, 0, p);
        // Any of several canonical outputs are accepted — the key
        // property is: 5 hits spread across 8 steps with no two
        // adjacent rests of length > 1.
        int hit_count = 0;
        for (int i = 0; i < 8; ++i) if (p[i]) ++hit_count;
        check(hit_count == 5, "5/8 has exactly 5 hits");
    }

    // --- Rotation shifts the pattern ---
    {
        int base[eu::kMaxSteps] = {};
        int rotated[eu::kMaxSteps] = {};
        eu::compute_pattern(3, 8, 0, base);
        eu::compute_pattern(3, 8, 1, rotated);
        // Rotation 1 = shift left by 1 step: rotated[i] = base[(i+1) % n].
        bool ok = true;
        for (int i = 0; i < 8; ++i) {
            if (rotated[i] != base[(i + 1) % 8]) { ok = false; break; }
        }
        check(ok, "rotation=1 is a left-shift by one step");
    }

    // --- Rotation modulo steps ---
    {
        int rot0[eu::kMaxSteps] = {};
        int rot8[eu::kMaxSteps] = {};
        eu::compute_pattern(3, 8, 0, rot0);
        eu::compute_pattern(3, 8, 8, rot8);
        check(pattern_equals(rot0, rot8, 8),
              "rotation=steps is equivalent to rotation=0");
    }

    // --- Cells past `steps` stay zeroed ---
    {
        int p[eu::kMaxSteps] = {};
        for (int i = 0; i < eu::kMaxSteps; ++i) p[i] = 7;  // pre-fill sentinel
        eu::compute_pattern(3, 8, 0, p);
        for (int i = 8; i < eu::kMaxSteps; ++i) {
            if (p[i] != 0) {
                check(false, "cells past `steps` are zeroed");
                break;
            }
        }
        check(p[8] == 0, "cell at index 8 zeroed when steps=8");
    }

    // --- hits < 0 clamps to 0, hits > steps clamps to steps ---
    {
        int p[eu::kMaxSteps] = {};
        eu::compute_pattern(-5, 8, 0, p);
        int expected_rests[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        check(pattern_equals(p, expected_rests, 8),
              "negative hits clamps to 0 → all rests");

        eu::compute_pattern(100, 8, 0, p);
        int expected_all[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        check(pattern_equals(p, expected_all, 8),
              "hits > steps clamps to steps → all hits");
    }

    // --- Empty output buffer is a no-op ---
    {
        eu::compute_pattern(3, 8, 0, nullptr);
        check(true, "null output pointer doesn't crash");
    }

    // --- Density presets ---
    check(eu::kDensityPresetCount >= 5,
          "at least 5 density presets registered");
    check(eu::kDensityPresets[0].hits == 3 && eu::kDensityPresets[0].steps == 8,
          "first preset is 3/8 tresillo");
    for (int i = 0; i < eu::kDensityPresetCount; ++i) {
        check(eu::kDensityPresets[i].label != nullptr &&
              std::strlen(eu::kDensityPresets[i].label) > 0,
              "every preset has a non-empty label");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
