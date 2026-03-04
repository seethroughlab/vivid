#include "runtime/capture_coordinator.h"
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

int main() {
    // =====================================================================
    // Test 1: Fresh state
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Fresh state ===\n");
        vivid::CaptureCoordinator cc;
        check(!cc.has_pending(), "no pending on fresh");
        check(!cc.is_recording(), "not recording on fresh");
        check(cc.recording_frame_count() == 0, "frame_count = 0");
        check(cc.recording_duration_sec() == 0.0, "duration_sec = 0.0");
    }

    // =====================================================================
    // Test 2: Request enqueue — Frame
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Request enqueue — Frame ===\n");
        vivid::CaptureCoordinator cc;
        auto future = cc.request_capture(vivid::CaptureType::Frame);
        check(future.valid(), "future is valid");
        check(cc.has_pending(), "has_pending after request");
    }

    // =====================================================================
    // Test 3: Multiple requests
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Multiple requests ===\n");
        vivid::CaptureCoordinator cc;
        auto f1 = cc.request_capture(vivid::CaptureType::Frame);
        auto f2 = cc.request_capture(vivid::CaptureType::Audio, 2.0f);
        auto f3 = cc.request_capture(vivid::CaptureType::AV);
        check(f1.valid() && f2.valid() && f3.valid(), "all futures valid");
        check(cc.has_pending(), "has_pending with 3 requests");
    }

    // =====================================================================
    // Test 4: handle_start_recording_tap with null audio
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: handle_start_recording_tap (null audio) ===\n");
        vivid::CaptureCoordinator cc;
        // audio_ is null by default
        std::string result = cc.handle_start_recording_tap();
        check(result.find("\"ok\":false") != std::string::npos, "returns ok:false");
        check(result.find("no audio engine") != std::string::npos, "error mentions audio");
    }

    // =====================================================================
    // Test 5: handle_stop_recording_tap with null audio
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: handle_stop_recording_tap (null audio) ===\n");
        vivid::CaptureCoordinator cc;
        std::string result = cc.handle_stop_recording_tap();
        check(result.find("\"ok\":false") != std::string::npos, "returns ok:false");
        check(result.find("no audio engine") != std::string::npos, "error mentions audio");
    }

    // =====================================================================
    // Test 6: request_start_recording enqueues
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: request_start_recording enqueues ===\n");
        vivid::CaptureCoordinator cc;
        auto future = cc.request_start_recording("/tmp/test_rec.mov", 30.0);
        check(future.valid(), "future is valid");
        check(cc.has_pending(), "has_pending after start_recording");
    }

    // =====================================================================
    // Test 7: request_stop_recording enqueues
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: request_stop_recording enqueues ===\n");
        vivid::CaptureCoordinator cc;
        auto future = cc.request_stop_recording();
        check(future.valid(), "future is valid");
        check(cc.has_pending(), "has_pending after stop_recording");
    }

    // =====================================================================
    // Test 8: request_snapshot_to_file enqueues
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 8: request_snapshot_to_file enqueues ===\n");
        vivid::CaptureCoordinator cc;
        auto future = cc.request_snapshot_to_file("/tmp/test_snap.png");
        check(future.valid(), "future is valid");
        check(cc.has_pending(), "has_pending after snapshot_to_file");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
