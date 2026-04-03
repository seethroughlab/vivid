#include "arpeggiator_patterns.h"
#include <array>
#include <cstdio>

template <size_t N>
static bool matches(const std::array<int, N>& expected, int mode, int pool_count) {
    for (size_t i = 0; i < N; ++i) {
        int got = vivid_sequencers::arp_pattern_index(mode, static_cast<int>(i), pool_count);
        if (got != expected[i]) {
            std::fprintf(stderr, "mode=%d step=%zu expected=%d got=%d\n",
                         mode, i, expected[i], got);
            return false;
        }
    }
    return true;
}

int main() {
    // UpDown over 4 notes: 0,1,2,3,2,1,...
    if (!matches(std::array<int, 8>{0, 1, 2, 3, 2, 1, 0, 1}, 2, 4)) {
        return 1;
    }

    // DownUp over 4 notes: 3,2,1,0,1,2,...
    if (!matches(std::array<int, 8>{3, 2, 1, 0, 1, 2, 3, 2}, 3, 4)) {
        return 1;
    }

    // Converge over 5 notes: low, high, ...
    if (!matches(std::array<int, 5>{0, 4, 1, 3, 2}, 6, 5)) {
        return 1;
    }

    // Diverge over 5 notes (center-out)
    if (!matches(std::array<int, 5>{2, 3, 1, 4, 0}, 7, 5)) {
        return 1;
    }

    // New OrderDown mode: reverse input-order traversal
    if (!matches(std::array<int, 8>{3, 2, 1, 0, 3, 2, 1, 0}, 9, 4)) {
        return 1;
    }

    std::printf("arpeggiator pattern tests passed\n");
    return 0;
}
