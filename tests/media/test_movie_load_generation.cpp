#include "operators/shared/movie_decode/load_generation.h"
#include <cstdio>
#include "test_helpers.h"

static int g_fail = 0;

int main() {
    MovieLoadGenerationTracker t;
    uint64_t g1 = t.next();
    uint64_t g2 = t.next();

    check(g1 == 1 && g2 == 2, "generation increments");
    check(!t.should_apply(g1), "stale generation rejected");
    check(t.should_apply(g2), "latest generation accepted");

    t.mark_applied(g2);
    check(!t.should_apply(g2), "already-applied generation rejected");

    uint64_t g3 = t.next();
    check(t.should_apply(g3), "new generation accepted after apply");

    return g_fail == 0 ? 0 : 1;
}
