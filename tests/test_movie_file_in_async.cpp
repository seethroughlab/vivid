#include "operators/gpu/movie_file_in/load_generation.h"

#include <cstdio>
#include <atomic>
#include <memory>

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
    MovieLoadCoordinator c;

    uint64_t g1 = c.request_next();
    auto t1 = c.begin_active();
    check(g1 == 1, "first generation assigned");
    check(!t1->load(), "first cancel token initially false");

    uint64_t g2 = c.request_next();
    check(g2 == 2, "second generation assigned");
    check(t1->load(), "new request cancels prior in-flight token");

    auto t2 = c.begin_active();
    check(!t2->load(), "second cancel token initially false");
    check(!c.should_apply(g1), "stale generation rejected");
    check(c.should_apply(g2), "latest generation accepted");

    c.mark_applied(g2);
    check(!c.should_apply(g2), "applied generation not re-applied");

    uint64_t g3 = c.cancel_pending();
    check(g3 == 3, "cancel_pending advances generation");
    check(t2->load(), "cancel_pending cancels active token");
    check(c.should_apply(g3), "new generation after cancel is accepted");

    auto t3 = c.begin_active();
    check(!t3->load(), "third token initially false");
    c.cancel_all();
    check(t3->load(), "cancel_all cancels active token");

    std::shared_ptr<std::atomic<bool>> dtor_token;
    {
        MovieLoadCoordinator scoped;
        dtor_token = scoped.begin_active();
        check(!dtor_token->load(), "scoped token initially false");
    }
    check(dtor_token->load(), "destructor cancels active token");

    return g_fail == 0 ? 0 : 1;
}
