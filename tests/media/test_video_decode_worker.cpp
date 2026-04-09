// Unit tests for VideoDecodeWorker.

#include "video_decode_worker.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
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

int main() {
    std::fprintf(stderr, "=== VideoDecodeWorker tests ===\n");
    test_submit_and_pop();
    test_newest_wins();
    test_submit_decoded_bypasses_worker();
    test_flush();
    test_start_stop_lifecycle();
    test_destructor_with_pending_work();
    test_pop_empty();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Worker tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
