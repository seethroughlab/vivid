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
    f.requested_pts = pts;
    f.request_sequence = static_cast<uint64_t>(pts * 1000.0);
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

static void test_bounded_fifo_runs_multiple_pending_jobs() {
    VideoDecodeWorker w;
    w.start();

    std::atomic<int> call_count{0};
    w.submit_work([&]() { call_count++; return make_frame(1.0); }, 0, 101);
    w.submit_work([&]() { call_count++; return make_frame(2.0); }, 0, 102);
    w.submit_work([&]() { call_count++; return make_frame(3.0); }, 0, 103);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    DecodedFrame out;
    check(call_count.load() == 3, "bounded_fifo: all pending jobs executed");
    check(w.pop_best(1.0, 0, 0.1, out), "bounded_fifo: first frame available");
    check(out.pts == 1.0, "bounded_fifo: first frame pts");
    check(w.pop_best(2.0, 0, 0.1, out), "bounded_fifo: second frame available");
    check(out.pts == 2.0, "bounded_fifo: second frame pts");
    check(w.pop_best(3.0, 0, 0.1, out), "bounded_fifo: third frame available");
    check(out.pts == 3.0, "bounded_fifo: third frame pts");

    w.stop();
}

static void test_duplicate_pending_key_is_ignored() {
    VideoDecodeWorker w;

    std::atomic<int> call_count{0};
    const bool first = w.submit_work([&]() {
        call_count++;
        return make_frame(1.0);
    }, 0, 999);
    const bool duplicate = w.submit_work([&]() {
        call_count++;
        return make_frame(2.0);
    }, 0, 999);

    w.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    check(first, "dedupe: first keyed work accepted");
    check(!duplicate, "dedupe: duplicate pending key ignored");
    check(call_count.load() == 1, "dedupe: duplicate work did not execute");

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

    std::atomic<bool> release_first{false};
    w.submit_work([&]() {
        while (!release_first.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return make_frame(0.0);
    });

    std::atomic<int> destroyed{0};
    for (int i = 0; i < 20; ++i) {
        auto guard = std::shared_ptr<int>(new int(i), [&](int* value) {
            delete value;
            destroyed.fetch_add(1, std::memory_order_relaxed);
        });
        w.submit_work([guard, i]() {
            return make_frame(1.0 + static_cast<double>(i));
        }, 0, static_cast<uint64_t>(1000 + i));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(destroyed.load(std::memory_order_relaxed) >= 8,
          "resource_release: capacity eviction releases captures");
    release_first.store(true, std::memory_order_release);

    w.stop();
}

int main() {
    std::fprintf(stderr, "=== VideoDecodeWorker tests ===\n");
    test_submit_and_pop();
    test_bounded_fifo_runs_multiple_pending_jobs();
    test_duplicate_pending_key_is_ignored();
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
