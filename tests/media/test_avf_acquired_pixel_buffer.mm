#include "avf_decoder.h"
#include "test_helpers.h"

#import <CoreFoundation/CoreFoundation.h>
#import <CoreVideo/CoreVideo.h>

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

int main() {
    std::fprintf(stderr, "=== AVF AcquiredPixelBuffer tests ===\n");
    test_move_constructor_releases_once();
    test_move_assignment_releases_old_buffer();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "AcquiredPixelBuffer tests: %d failed\n", failures);
    std::fprintf(stderr, "========================================\n");
    return failures == 0 ? 0 : 1;
}
