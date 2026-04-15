#include "avf_decoder.h"
#include "decoded_frame_queue.h"
#include "test_helpers.h"

#import <CoreFoundation/CoreFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <Metal/Metal.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

static CVPixelBufferRef make_pixel_buffer() {
    CVPixelBufferRef buffer = nullptr;
    CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault,
                                          4,
                                          4,
                                          kCVPixelFormatType_32BGRA,
                                          nullptr,
                                          &buffer);
    check(status == kCVReturnSuccess && buffer != nullptr,
          "acquired_pixel_buffer: pixel buffer created");
    return buffer;
}

static void test_move_constructor_releases_once() {
    CVPixelBufferRef buffer = make_pixel_buffer();
    CFRetain(buffer);
    CFIndex before = CFGetRetainCount(buffer);

    {
        AcquiredPixelBuffer acquired;
        acquired.buffer = buffer;
        acquired.pts = 1.0;
        acquired.status = DecodeStatus::NewFrame;

        {
            AcquiredPixelBuffer moved(std::move(acquired));
            check(!acquired.valid(), "move_ctor: source invalidated");
            check(moved.valid(), "move_ctor: destination owns buffer");
            check(CFGetRetainCount(buffer) == before,
                  "move_ctor: move itself does not alter retain count");
        }

        check(CFGetRetainCount(buffer) == before - 1,
              "move_ctor: destructor releases transferred retain");
    }

    CFRelease(buffer);
}

static void test_move_assignment_releases_old_buffer() {
    CVPixelBufferRef first = make_pixel_buffer();
    CVPixelBufferRef second = make_pixel_buffer();
    CFRetain(first);
    CFRetain(second);

    {
        AcquiredPixelBuffer lhs;
        lhs.buffer = first;
        lhs.status = DecodeStatus::NewFrame;

        AcquiredPixelBuffer rhs;
        rhs.buffer = second;
        rhs.status = DecodeStatus::NewFrame;

        lhs = std::move(rhs);

        check(!rhs.valid(), "move_assign: source invalidated");
        check(lhs.buffer == second, "move_assign: destination now owns second buffer");
        check(CFGetRetainCount(first) == 1,
              "move_assign: old destination buffer released immediately");
        check(CFGetRetainCount(second) == 2,
              "move_assign: new buffer retain preserved until destruction");
    }

    check(CFGetRetainCount(second) == 1,
          "move_assign: destructor releases new destination buffer");
    CFRelease(first);
    CFRelease(second);
}

static std::filesystem::path find_h264_fixture() {
    auto cwd = std::filesystem::current_path();
    std::filesystem::path candidates[] = {
        cwd / "../assets/sync/sync-test-h264.mp4",
        cwd / "assets/sync/sync-test-h264.mp4",
        cwd.parent_path() / "assets/sync/sync-test-h264.mp4",
    };
    for (const auto& path : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) return std::filesystem::weakly_canonical(path, ec);
    }
    return {};
}

static std::filesystem::path find_hevc_fixture() {
    auto cwd = std::filesystem::current_path();
    std::filesystem::path candidates[] = {
        cwd / "../assets/sync/sync-test-hevc.mp4",
        cwd / "assets/sync/sync-test-hevc.mp4",
        cwd.parent_path() / "assets/sync/sync-test-hevc.mp4",
    };
    for (const auto& path : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) return std::filesystem::weakly_canonical(path, ec);
    }
    return {};
}

static bool can_import_pixel_buffer_with_metal(CVPixelBufferRef pixel) {
    if (!pixel) return false;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) return false;

    CVMetalTextureCacheRef cache = nullptr;
    CVReturn status = CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, device, nullptr, &cache);
    if (status != kCVReturnSuccess || !cache) return false;

    CVMetalTextureRef tex = nullptr;
    status = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                       cache,
                                                       pixel,
                                                       nullptr,
                                                       MTLPixelFormatBGRA8Unorm,
                                                       CVPixelBufferGetWidth(pixel),
                                                       CVPixelBufferGetHeight(pixel),
                                                       0,
                                                       &tex);
    const bool ok = status == kCVReturnSuccess && tex && CVMetalTextureGetTexture(tex);
    if (tex) CFRelease(tex);
    CFRelease(cache);
    return ok;
}

static void pump_main_run_loop(double seconds) {
    auto until = std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(seconds));
    while (std::chrono::steady_clock::now() < until) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static void test_native_looper_replaces_manual_loop_seek() {
    auto fixture = find_h264_fixture();
    if (fixture.empty()) {
        std::fprintf(stderr, "  SKIP: sync-test-h264.mp4 fixture not found\n");
        return;
    }

    AVFDecoder decoder;
    check(decoder.open(fixture.string()), "avf_looper: opens non-HAP fixture");
    check(decoder.native_looping_enabled(), "avf_looper: loop mode uses native looper after open");
    check(decoder.manual_loop_seek_count() == 0,
          "avf_looper: open does not use manual loop seek");

    decoder.set_speed(1.0f);
    pump_main_run_loop(0.15);
    decoder.set_loop(false);
    check(!decoder.native_looping_enabled(),
          "avf_looper: set_loop(false) tears down native looper");

    decoder.set_loop(true);
    check(decoder.native_looping_enabled(),
          "avf_looper: set_loop(true) rebuilds native looper");
    check(decoder.manual_loop_seek_count() == 0,
          "avf_looper: loop toggles do not use manual loop seek");
}

static void test_target_time_acquire_across_wrap() {
    auto fixture = find_h264_fixture();
    if (fixture.empty()) {
        std::fprintf(stderr, "  SKIP: sync-test-h264.mp4 fixture not found\n");
        return;
    }

    AVFDecoder decoder;
    check(decoder.open(fixture.string()), "avf_wrap_acquire: opens non-HAP fixture");
    decoder.set_loop(true);
    decoder.set_speed(1.0f);
    pump_main_run_loop(0.25);

    const double duration = decoder.duration();
    const double frame = 1.0 / std::max(1.0f, decoder.frame_rate());
    check(decoder.seek(std::max(0.0, duration - 6.0 * frame)),
          "avf_wrap_acquire: explicit test seek near loop boundary succeeds");
    pump_main_run_loop(0.25);

    int valid_end = 0;
    int valid_start = 0;
    for (int i = 4; i >= 1; --i) {
        const double t = std::max(0.0, duration - static_cast<double>(i) * frame);
        auto acquired = decoder.acquire_pixel_buffer_at_time(t);
        if (acquired.valid()) {
            check(std::abs(acquired.pts - t) <= frame * 1.5,
                  "avf_wrap_acquire: native target-time frame reports a matching timestamp");
            valid_end++;
        } else if (!decoder.copy_frame_at_time(t).empty()) {
            valid_end++;
        }
    }
    for (int i = 0; i < 4; ++i) {
        const double t = static_cast<double>(i) * frame;
        auto acquired = decoder.acquire_pixel_buffer_at_time(t);
        if (acquired.valid()) {
            check(std::abs(acquired.pts - t) <= frame * 1.5,
                  "avf_wrap_acquire: native post-wrap frame reports a matching timestamp");
            valid_start++;
        } else if (!decoder.copy_frame_at_time(t).empty()) {
            valid_start++;
        }
    }

    check(decoder.native_looping_enabled(),
          "avf_wrap_acquire: native looper remains active while acquiring loop-edge frames");
    check(decoder.manual_loop_seek_count() == 0,
          "avf_wrap_acquire: target-time acquisition across wrap does not use manual loop seek");
    check(valid_end > 0,
          "avf_wrap_acquire: AVFoundation can answer at least one final pre-wrap target frame");
    check(valid_start > 0,
          "avf_wrap_acquire: AVFoundation can answer at least one post-wrap target frame");
}

static void test_target_time_acquire_across_two_loop_boundaries() {
    auto fixture = find_h264_fixture();
    if (fixture.empty()) {
        std::fprintf(stderr, "  SKIP: sync-test-h264.mp4 fixture not found\n");
        return;
    }

    AVFDecoder decoder;
    check(decoder.open(fixture.string()), "avf_two_wraps: opens non-HAP fixture");
    decoder.set_loop(true);
    decoder.set_speed(1.0f);
    pump_main_run_loop(0.25);

    const double duration = decoder.duration();
    const double frame = 1.0 / std::max(1.0f, decoder.frame_rate());
    int valid_end = 0;
    int valid_start = 0;

    for (int boundary = 0; boundary < 2; ++boundary) {
        check(decoder.seek(std::max(0.0, duration - 6.0 * frame)),
              "avf_two_wraps: explicit test seek near loop boundary succeeds");
        pump_main_run_loop(0.20);

        for (int i = 4; i >= 1; --i) {
            const double t = std::max(0.0, duration - static_cast<double>(i) * frame);
            auto acquired = decoder.acquire_pixel_buffer_at_time(t);
            if (acquired.valid()) {
                check(std::abs(acquired.pts - t) <= frame * 1.5,
                      "avf_two_wraps: native pre-wrap frame reports a matching timestamp");
                valid_end++;
            } else if (!decoder.copy_frame_at_time(t).empty()) {
                valid_end++;
            }
        }
        for (int i = 0; i < 4; ++i) {
            const double t = static_cast<double>(i) * frame;
            auto acquired = decoder.acquire_pixel_buffer_at_time(t);
            if (acquired.valid()) {
                check(std::abs(acquired.pts - t) <= frame * 1.5,
                      "avf_two_wraps: native post-wrap frame reports a matching timestamp");
                valid_start++;
            } else if (!decoder.copy_frame_at_time(t).empty()) {
                valid_start++;
            }
        }

        pump_main_run_loop(8.0 * frame);
    }

    check(decoder.native_looping_enabled(),
          "avf_two_wraps: native looper remains active across repeated loop-edge acquisition");
    check(decoder.manual_loop_seek_count() == 0,
          "avf_two_wraps: repeated target-time acquisition does not use manual loop seek");
    check(valid_end > 0,
          "avf_two_wraps: valid frames available before wrap");
    check(valid_start > 0,
          "avf_two_wraps: valid frames available after wrap");
}

static void test_acquired_frames_are_metal_importable(const std::filesystem::path& fixture,
                                                      const char* label) {
    if (fixture.empty()) {
        std::fprintf(stderr, "  SKIP: %s fixture not found\n", label);
        return;
    }
    if (!MTLCreateSystemDefaultDevice()) {
        std::fprintf(stderr, "  SKIP: %s no default Metal device\n", label);
        return;
    }

    AVFDecoder decoder;
    check(decoder.open(fixture.string()), "avf_metal_import: opens fixture");
    decoder.set_loop(true);
    decoder.set_speed(1.0f);
    pump_main_run_loop(0.25);

    int importable = 0;
    for (int i = 0; i < 16; ++i) {
        pump_main_run_loop(0.04);
        auto acquired = decoder.acquire_pixel_buffer();
        if (acquired.valid() && can_import_pixel_buffer_with_metal(acquired.buffer)) {
            importable++;
        }
    }
    check(importable > 0, "avf_metal_import: at least one acquired frame imports as Metal texture");
}

int main() {
    std::fprintf(stderr, "=== AVF AcquiredPixelBuffer tests ===\n");
    test_move_constructor_releases_once();
    test_move_assignment_releases_old_buffer();
    test_native_looper_replaces_manual_loop_seek();
    test_target_time_acquire_across_wrap();
    test_target_time_acquire_across_two_loop_boundaries();
    test_acquired_frames_are_metal_importable(find_h264_fixture(), "sync-test-h264.mp4");
    test_acquired_frames_are_metal_importable(find_hevc_fixture(), "sync-test-hevc.mp4");

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "AcquiredPixelBuffer tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
