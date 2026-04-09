// Unit tests for DecodedFrameQueue.

#include "decoded_frame_queue.h"
#include "test_helpers.h"

#include <cstdio>
#include <thread>
#include <vector>

static DecodedFrame make_frame(uint32_t w, uint32_t h, double pts) {
    DecodedFrame f;
    f.width = w;
    f.height = h;
    f.pts = pts;
    f.data.resize(static_cast<size_t>(w) * h * 4, 0xAA);
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
    for (int i = 0; i < 5; ++i) {
        q.push(make_frame(320, 240, static_cast<double>(i)));
    }
    check(q.size() == DecodedFrameQueue::kMaxFrames, "queue bounded at kMaxFrames");

    DecodedFrame out;
    q.pop_latest(out);
    check(out.pts == 4.0, "newest frame survives (pts=4.0)");
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
    test_pop_empty();
    test_flush();
    test_compressed_frame_metadata();
    test_concurrent_push_pop();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "Queue tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
