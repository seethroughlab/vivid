// Ph2 audit P2-01: the CLAP UI->audio param queue must DROP when full, never lap the ring and let the
// RT consumer read a half-overwritten {id,value}. This test covers the drop-on-full semantics single-
// threaded, and — under ThreadSanitizer (THREAD label) — races a producer against a consumer and
// asserts every popped message is internally consistent (no torn read). A regression to the old
// unconditional-advance push would both corrupt a value here and trip TSan.
#include "audio/clap_param_queue.h"
#include "test_helpers.h"

#include <atomic>
#include <thread>

using vivid::session::ClapParamQueue;
using vivid::session::ClapParamMsg;

// Encode a self-checking payload: value == id * 2.0. A torn read (id from one message, value from
// another) violates this, so any inconsistency is caught even without TSan.
static constexpr double kEncode = 2.0;

int main() {
    // ---- 1. drop-on-full: fill to N, the next push is refused, buffered entries stay intact ----
    {
        ClapParamQueue q;
        for (int i = 0; i < ClapParamQueue::N; ++i)
            CHECK(q.push(static_cast<uint32_t>(i), i * kEncode));   // all fit
        CHECK(!q.push(99999, 12345.0));                            // full -> dropped, not lapped
        // Everything pops back in FIFO order, uncorrupted, and the dropped one never appears.
        ClapParamMsg m;
        for (int i = 0; i < ClapParamQueue::N; ++i) {
            CHECK(q.pop(m));
            CHECK(m.id == static_cast<uint32_t>(i) && m.value == i * kEncode);
        }
        CHECK(!q.pop(m));   // empty
    }

    // ---- 2. interleaved push/pop keeps FIFO order and consistency ----
    {
        ClapParamQueue q;
        ClapParamMsg m;
        for (uint32_t i = 0; i < 10; ++i) CHECK(q.push(i, i * kEncode));
        for (uint32_t i = 0; i < 5; ++i) { CHECK(q.pop(m)); CHECK(m.id == i && m.value == i * kEncode); }
        for (uint32_t i = 10; i < 15; ++i) CHECK(q.push(i, i * kEncode));
        for (uint32_t i = 5; i < 15; ++i) { CHECK(q.pop(m)); CHECK(m.id == i && m.value == i * kEncode); }
        CHECK(!q.pop(m));
    }

    // ---- 3. concurrent producer/consumer (the TSan target): no torn reads under overflow pressure ----
    {
        ClapParamQueue q;
        constexpr uint32_t kMsgs = 200000;   // far exceeds N, so the producer laps into "full" repeatedly
        std::atomic<bool> bad{false};
        std::atomic<uint32_t> popped{0};

        std::thread consumer([&] {
            ClapParamMsg m;
            uint32_t seen = 0;
            while (seen < kMsgs) {
                if (q.pop(m)) {
                    if (m.value != m.id * kEncode) bad.store(true);   // torn {id,value} would fail here
                    ++seen;
                } else {
                    std::this_thread::yield();
                }
            }
            popped.store(seen);
        });

        for (uint32_t i = 0; i < kMsgs; ++i)
            while (!q.push(i, i * kEncode)) std::this_thread::yield();   // retry on full (never overwrites)

        consumer.join();
        CHECK(!bad.load());                 // every popped message was internally consistent
        CHECK(popped.load() == kMsgs);      // with producer-side retry, none are actually lost
    }

    return vivid::test::summary("test_clap_param_queue");
}
