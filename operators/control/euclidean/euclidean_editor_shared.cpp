#include "euclidean_editor_shared.h"

#include <algorithm>

namespace vivid::euclidean_editor {

namespace {

void rotate_in_place(int* pattern, int n, int rot) {
    rot = rot % n;
    if (rot <= 0) return;
    int tmp[kMaxSteps];
    for (int i = 0; i < n; ++i) tmp[i] = pattern[(i + rot) % n];
    for (int i = 0; i < n; ++i) pattern[i] = tmp[i];
}

} // namespace

void compute_pattern(int hits, int steps, int rotation, int* out_pattern) {
    if (!out_pattern) return;
    for (int i = 0; i < kMaxSteps; ++i) out_pattern[i] = 0;
    if (steps <= 0) return;
    hits = std::clamp(hits, 0, steps);
    if (hits == 0) return;

    if (hits == steps) {
        for (int i = 0; i < steps; ++i) out_pattern[i] = 1;
        if (rotation > 0) rotate_in_place(out_pattern, steps, rotation);
        return;
    }

    // Bjorklund — classical Euclidean rhythm via pair folding.
    int seqs[kMaxSteps][kMaxSteps];
    int slen[kMaxSteps];
    for (int i = 0; i < hits;            ++i) { seqs[i][0] = 1; slen[i] = 1; }
    for (int i = hits; i < steps;        ++i) { seqs[i][0] = 0; slen[i] = 1; }

    int left = hits;
    int right = steps - hits;

    while (right > 1) {
        int pairs = std::min(left, right);
        for (int i = 0; i < pairs; ++i) {
            int src = left + i;
            for (int j = 0; j < slen[src]; ++j)
                seqs[i][slen[i] + j] = seqs[src][j];
            slen[i] += slen[src];
        }
        if (left > right) {
            right = left - pairs;
            left  = pairs;
        } else {
            int extra_start = left + pairs;
            int extra_count = right - pairs;
            for (int i = 0; i < extra_count; ++i) {
                int src = extra_start + i;
                int dst = pairs + i;
                for (int j = 0; j < slen[src]; ++j) seqs[dst][j] = seqs[src][j];
                slen[dst] = slen[src];
            }
            right = right - pairs;
            left  = pairs;
        }
    }

    int pos = 0;
    int total = left + right;
    for (int i = 0; i < total && pos < kMaxSteps; ++i)
        for (int j = 0; j < slen[i] && pos < kMaxSteps; ++j)
            out_pattern[pos++] = seqs[i][j];

    if (rotation > 0) rotate_in_place(out_pattern, steps, rotation);
}

} // namespace vivid::euclidean_editor
