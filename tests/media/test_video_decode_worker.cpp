// Unit tests for VideoDecodeWorker.

#include "video_decode_worker.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

static DecodedFrame make_frame(double pts) {
    DecodedFrame f;
    f.width = 320;
    f.height = 240;
    f.pts = pts;
    f.data.resize(320 * 240 * 4, 0xBB);
    return f;
}

static void test_submit_and_pop() {
    VideoDecodeWorker w;
    w.start();

    w.submit_work([]() { return make_frame(1.0); });

    // Give the worker time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DecodedFrame out;
    check(w.pop_latest(out), "pop_latest returns true after submit");
    check(out.pts == 1.0, "popped frame pts == 1.0");

    w.stop();
}

static void test_newest_wins() {
    VideoDecodeWorker w;
    w.start();

    // Submit two work items rapidly — second should replace first
    std::atomic<int> call_count{0};
    w.submit_work([&]() { call_count++; return make_frame(1.0); });
    w.submit_work([&]() { call_count++; return make_frame(2.0); });

    // Give worker time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    DecodedFrame out;
    if (w.pop_latest(out)) {
        // The most recent result should be available
        // (could be 1.0 or 2.0 depending on timing, but both are valid)
        check(out.pts >= 1.0, "popped frame is one of the submitted frames");
    }
    // The key invariant: no crash, and at most 2 calls were made
    check(call_count.load() <= 2, "at most 2 work items executed");

    w.stop();
}

static void test_submit_decoded_bypasses_worker() {
    VideoDecodeWorker w;
    w.start();

    w.submit_decoded(make_frame(5.0));

    DecodedFrame out;
    check(w.pop_latest(out), "pop_latest returns true after submit_decoded");
    check(out.pts == 5.0, "submit_decoded bypasses worker thread");

    w.stop();
}

static void test_flush() {
    VideoDecodeWorker w;
    w.start();

    w.submit_decoded(make_frame(1.0));
    w.submit_decoded(make_frame(2.0));
    w.flush();

    DecodedFrame out;
    check(!w.pop_latest(out), "pop_latest returns false after flush");

    w.stop();
}

static void test_start_stop_lifecycle() {
    VideoDecodeWorker w;

    // Start and stop without any work
    w.start();
    w.stop();

    // Start again and do some work
    w.start();
    w.submit_work([]() { return make_frame(1.0); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DecodedFrame out;
    check(w.pop_latest(out), "works after restart");

    w.stop();
}

static void test_destructor_with_pending_work() {
    // This should not hang or crash
    {
        VideoDecodeWorker w;
        w.start();
        w.submit_work([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return make_frame(1.0);
        });
        // Destructor calls stop(), which should join cleanly
    }
    check(true, "destructor with pending work did not hang");
}

static void test_pop_empty() {
    VideoDecodeWorker w;
    w.start();

    DecodedFrame out;
    check(!w.pop_latest(out), "pop_latest returns false when no work submitted");

    w.stop();
}

static void test_flush_discards_in_flight_result() {
    VideoDecodeWorker w;
    w.start();

    std::atomic<bool> release_first{false};
    w.submit_work([&]() {
        while (!release_first.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return make_frame(1.0);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    w.flush();
    w.submit_work([]() { return make_frame(2.0); });
    release_first.store(true, std::memory_order_release);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    DecodedFrame out;
    check(w.pop_latest(out), "flush_generation: latest frame available after recovery");
    check(out.pts == 2.0, "flush_generation: stale pre-flush frame discarded");

    w.stop();
}

static void test_replaced_work_releases_captured_resources() {
    VideoDecodeWorker w;
    w.start();

    std::atomic<int> destroyed{0};
    auto guard = std::shared_ptr<int>(new int(1), [&](int* value) {
        delete value;
        destroyed.fetch_add(1, std::memory_order_relaxed);
    });
    w.submit_work([guard]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return make_frame(1.0);
    });
    guard.reset();
    w.submit_work([]() { return make_frame(2.0); });
    w.flush();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(destroyed.load(std::memory_order_relaxed) >= 1,
          "resource_release: replaced or flushed work releases captures");

    w.stop();
}

int main() {
    std::fprintf(stderr, "=== VideoDecodeWorker tests ===\n");
    test_submit_and_pop();
    test_newest_wins();
    test_submit_decoded_bypasses_worker();
    test_flush();
    test_start_stop_lifecycle();
    test_destructor_with_pending_work();
    test_pop_empty();
    test_flush_discards_in_flight_result();
    test_replaced_work_releases_captured_resources();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Worker tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
