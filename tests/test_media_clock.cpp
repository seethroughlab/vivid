#include "operator_api/media_clock.h"

#include <cstdio>

static int g_fail = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        g_fail++;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    vivid::MediaClockV1 c{};
    c.duration_s = 10.0f;

    c.loop_epoch = 0;
    c.local_time_s = 9.8f;
    c.monotonic_time_s = vivid::media_clock_monotonic(c.local_time_s, c.duration_s, c.loop_epoch);
    const double before_wrap = c.monotonic_time_s;

    c.loop_epoch = 1;
    c.local_time_s = 0.1f;
    c.monotonic_time_s = vivid::media_clock_monotonic(c.local_time_s, c.duration_s, c.loop_epoch);
    const double after_wrap = c.monotonic_time_s;

    check(after_wrap > before_wrap, "monotonic time increases across loop wrap");

    c.source_generation = 7;
    check(c.source_generation == 7, "source generation is tracked");

    const double no_duration = vivid::media_clock_monotonic(1.25, 0.0, 42);
    check(no_duration == 1.25, "zero duration falls back to local time");

    return g_fail == 0 ? 0 : 1;
}
