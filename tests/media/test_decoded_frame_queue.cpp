// Unit tests for DecodedFrameQueue.

#include "decoded_frame_queue.h"
#include "test_helpers.h"

#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

static DecodedFrame make_frame(uint32_t w, uint32_t h, double pts) {
    DecodedFrame f;
    f.width = w;
    f.height = h;
    f.pts = pts;
    f.requested_pts = pts;
    f.data.resize(static_cast<size_t>(w) * h * 4, 0xAA);
    return f;
}

static DecodedFrame make_frame(uint32_t w, uint32_t h, double pts, uint64_t loop_generation) {
    DecodedFrame f = make_frame(w, h, pts);
    f.loop_generation = loop_generation;
    f.request_sequence = static_cast<uint64_t>(pts * 1000.0) + loop_generation * 1000000ULL;
    return f;
}

static DecodedFrame make_keyed_frame(uint32_t w,
                                     uint32_t h,
                                     double pts,
                                     uint64_t loop_generation,
                                     uint64_t request_key) {
    DecodedFrame f = make_frame(w, h, pts, loop_generation);
    f.request_key = request_key;
    return f;
}

static void test_push_pop_basic() {
    DecodedFrameQueue q;
    check(q.size() == 0, "queue starts empty");

    q.push(make_frame(320, 240, 1.0));
    check(q.size() == 1, "size == 1 after push");

    DecodedFrame out;
    check(q.pop_latest(out), "pop_latest returns true");
    check(out.width == 320, "popped frame width");
    check(out.pts == 1.0, "popped frame pts");
    check(q.size() == 0, "queue empty after pop");
}

static void test_pop_latest_returns_newest() {
    DecodedFrameQueue q;
    q.push(make_frame(320, 240, 1.0));
    q.push(make_frame(320, 240, 2.0));
    q.push(make_frame(320, 240, 3.0));

    DecodedFrame out;
    check(q.pop_latest(out), "pop_latest returns true");
    check(out.pts == 3.0, "pop_latest returns newest (pts=3.0)");
    check(q.size() == 0, "queue drained after pop_latest");
}

static void test_bounded_capacity() {
    DecodedFrameQueue q;
    // Push more than kMaxFrames
    for (int i = 0; i < static_cast<int>(DecodedFrameQueue::kMaxFrames) + 4; ++i) {
        q.push(make_frame(320, 240, static_cast<double>(i)));
    }
    check(q.size() == DecodedFrameQueue::kMaxFrames, "queue bounded at kMaxFrames");

    DecodedFrame out;
    q.pop_latest(out);
    check(out.pts == static_cast<double>(DecodedFrameQueue::kMaxFrames + 3),
          "newest frame survives capacity eviction");
}

static void test_duplicate_request_keys_do_not_fill_capacity() {
    DecodedFrameQueue q;
    check(q.push(make_keyed_frame(320, 240, 0.100, 2, 4242)),
          "dedupe_ready: first keyed frame accepted");

    for (int i = 0; i < 20; ++i) {
        auto duplicate = make_keyed_frame(320, 240, 0.100, 2, 4242);
        duplicate.request_sequence = 1000 + static_cast<uint64_t>(i);
        check(!q.push(std::move(duplicate)),
              "dedupe_ready: duplicate keyed frame rejected");
    }

    check(q.size() == 1, "dedupe_ready: duplicates do not consume capacity");
}

static void test_native_frame_replaces_cpu_fallback_duplicate() {
    DecodedFrameQueue q;
    auto fallback = make_keyed_frame(320, 240, 0.200, 2, 5252);
    fallback.cpu_fallback = true;
    check(q.push(std::move(fallback)), "dedupe_replace: CPU fallback accepted");

    auto native = make_keyed_frame(320, 240, 0.200, 2, 5252);
    native.native_pixel_buffer = std::shared_ptr<void>(std::make_shared<int>(7));
    check(q.push(std::move(native)), "dedupe_replace: native frame replaces fallback");

    DecodedFrame out;
    check(q.pop_latest(out), "dedupe_replace: replacement frame available");
    check(out.has_native_pixel_buffer(), "dedupe_replace: native frame survived");
    check(!out.cpu_fallback, "dedupe_replace: CPU fallback was replaced");
}

static void test_pop_best_returns_target_frame() {
    DecodedFrameQueue q;
    q.push(make_frame(320, 240, 0.100, 2));
    q.push(make_frame(320, 240, 0.133, 2));
    q.push(make_frame(320, 240, 0.166, 2));

    DecodedFrame out;
    check(q.pop_best(0.140, 2, 1.0 / 30.0, out), "pop_best returns frame near target");
    check(out.pts == 0.133, "pop_best chooses newest frame not past target");
}

static void test_pop_best_keeps_next_loop_until_wrap() {
    DecodedFrameQueue q;
    q.push(make_frame(320, 240, 9.900, 7));
    q.push(make_frame(320, 240, 9.933, 7));
    q.push(make_frame(320, 240, 0.000, 8));
    q.push(make_frame(320, 240, 0.033, 8));

    DecodedFrame out;
    check(q.pop_best(9.940, 7, 1.0 / 30.0, out),
          "pop_best returns final pre-wrap frame even when post-wrap is queued");
    check(out.loop_generation == 7, "pop_best keeps current loop generation");
    check(out.pts == 9.933, "pop_best does not skip to first frame of next loop");

    check(q.pop_best(0.010, 8, 1.0 / 30.0, out),
          "pop_best returns post-wrap frame after generation advances");
    check(out.loop_generation == 8, "post-wrap frame generation selected");
}

static void test_post_wrap_prefetch_survives_duplicate_current_submissions() {
    DecodedFrameQueue q;
    constexpr uint64_t gen = 7;
    constexpr uint64_t next_gen = gen + 1;

    q.push(make_keyed_frame(320, 240, 9.866, gen, 7001));
    q.push(make_keyed_frame(320, 240, 9.900, gen, 7002));
    q.push(make_keyed_frame(320, 240, 9.933, gen, 7003));
    q.push(make_keyed_frame(320, 240, 9.966, gen, 7004));
    q.push(make_keyed_frame(320, 240, 0.000, next_gen, 8001));
    q.push(make_keyed_frame(320, 240, 0.033, next_gen, 8002));
    q.push(make_keyed_frame(320, 240, 0.066, next_gen, 8003));
    q.push(make_keyed_frame(320, 240, 0.099, next_gen, 8004));

    check(q.push(make_keyed_frame(320, 240, 9.800, gen, 7999)),
          "loop_window: first current primary accepted");
    for (int i = 0; i < 20; ++i) {
        auto duplicate = make_keyed_frame(320, 240, 9.800, gen, 7999);
        duplicate.request_sequence = 10000 + static_cast<uint64_t>(i);
        q.push(std::move(duplicate));
    }

    DecodedFrame out;
    check(q.pop_best(9.970, gen, 1.0 / 30.0, out),
          "loop_window: final pre-wrap frame still available");
    check(out.loop_generation == gen, "loop_window: selected current generation");
    check(out.pts >= 9.933, "loop_window: did not jump away from final frames");

    check(q.pop_best(0.010, next_gen, 1.0 / 30.0, out),
          "loop_window: post-wrap prefetch survived duplicate spam");
    check(out.loop_generation == next_gen, "loop_window: selected post-wrap generation");
}

static void test_future_generation_window_rejects_current_capacity_spam() {
    DecodedFrameQueue q;
    constexpr uint64_t gen = 4;
    constexpr uint64_t next_gen = gen + 1;

    for (size_t i = 0; i < DecodedFrameQueue::kMaxFrames; ++i) {
        q.push(make_keyed_frame(320,
                                240,
                                static_cast<double>(i) / 30.0,
                                next_gen,
                                9000 + i));
    }

    check(!q.push(make_keyed_frame(320, 240, 9.900, gen, 8001)),
          "future_window: full future-generation queue rejects older current spam");

    DecodedFrame out;
    check(q.pop_best(0.010, next_gen, 1.0 / 30.0, out),
          "future_window: first future frame remains available");
    check(out.loop_generation == next_gen,
          "future_window: future generation was preserved");
}

static void test_pop_best_drops_stale_previous_generation() {
    DecodedFrameQueue q;
    q.push(make_frame(320, 240, 9.900, 3));
    q.push(make_frame(320, 240, 0.000, 4));

    DecodedFrame out;
    check(q.pop_best(0.005, 4, 1.0 / 30.0, out),
          "pop_best ignores stale previous generation");
    check(out.loop_generation == 4, "stale generation was not selected");
    check(q.size() == 0, "stale previous-generation frame was dropped");
}

static void test_pop_empty() {
    DecodedFrameQueue q;
    DecodedFrame out;
    check(!q.pop_latest(out), "pop_latest returns false on empty queue");
}

static void test_flush() {
    DecodedFrameQueue q;
    q.push(make_frame(320, 240, 1.0));
    q.push(make_frame(320, 240, 2.0));
    q.flush();
    check(q.size() == 0, "queue empty after flush");

    DecodedFrame out;
    check(!q.pop_latest(out), "pop_latest returns false after flush");
}

static void test_compressed_frame_metadata() {
    DecodedFrameQueue q;

    DecodedFrame f;
    f.width = 640;
    f.height = 480;
    f.pts = 2.0;
    f.compressed = true;
    f.compressed_format = VideoCompressedFormat::BC3;
    f.requires_ycocg = true;
    f.data.resize(640 * 480 / 2, 0xCC);  // compressed size is smaller
    q.push(std::move(f));

    DecodedFrame out;
    check(q.pop_latest(out), "compressed: pop returns true");
    check(out.compressed, "compressed: flag preserved");
    check(out.compressed_format == VideoCompressedFormat::BC3, "compressed: format preserved");
    check(out.requires_ycocg, "compressed: ycocg flag preserved");
    check(out.width == 640, "compressed: width preserved");
    check(out.pts == 2.0, "compressed: pts preserved");
}

static void test_concurrent_push_pop() {
    DecodedFrameQueue q;
    constexpr int kPushCount = 100;

    std::thread producer([&]() {
        for (int i = 0; i < kPushCount; ++i) {
            q.push(make_frame(320, 240, static_cast<double>(i)));
        }
    });

    int pop_count = 0;
    std::thread consumer([&]() {
        DecodedFrame out;
        for (int i = 0; i < kPushCount; ++i) {
            if (q.pop_latest(out)) pop_count++;
            std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();

    // At least some pops should have succeeded (exact count depends on scheduling)
    check(pop_count >= 0, "concurrent push/pop did not crash");
    // Drain any remaining
    DecodedFrame out;
    while (q.pop_latest(out)) pop_count++;
    check(pop_count > 0, "at least one frame was consumed");
}

int main() {
    std::fprintf(stderr, "=== DecodedFrameQueue tests ===\n");
    test_push_pop_basic();
    test_pop_latest_returns_newest();
    test_bounded_capacity();
    test_duplicate_request_keys_do_not_fill_capacity();
    test_native_frame_replaces_cpu_fallback_duplicate();
    test_pop_best_returns_target_frame();
    test_pop_best_keeps_next_loop_until_wrap();
    test_post_wrap_prefetch_survives_duplicate_current_submissions();
    test_future_generation_window_rejects_current_capacity_spam();
    test_pop_best_drops_stale_previous_generation();
    test_pop_empty();
    test_flush();
    test_compressed_frame_metadata();
    test_concurrent_push_pop();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Queue tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
