#ifdef __APPLE__
#include "runtime/macos_frame_timer.h"
#include <CoreFoundation/CoreFoundation.h>

namespace vivid {

void macos_run_frame_loop(std::function<bool()> tick) {
    struct Context {
        std::function<bool()> tick;
        CFRunLoopTimerRef timer = nullptr;
        bool should_stop = false;
    };

    Context ctx{std::move(tick)};

    CFRunLoopTimerContext timer_ctx{};
    timer_ctx.info = &ctx;

    // 1/240s interval — naturally throttled by vsync in gpu.end_frame()
    ctx.timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent(),
        1.0 / 240.0,
        0, 0,
        [](CFRunLoopTimerRef, void* info) {
            auto* c = static_cast<Context*>(info);
            if (!c->tick()) {
                c->should_stop = true;
                CFRunLoopStop(CFRunLoopGetMain());
            }
        },
        &timer_ctx
    );

    // Timer in kCFRunLoopCommonModes fires in both default mode and
    // NSEventTrackingRunLoopMode (active during window drag/resize).
    CFRunLoopAddTimer(CFRunLoopGetMain(), ctx.timer, kCFRunLoopCommonModes);

    // Use a loop around CFRunLoopRunInMode instead of CFRunLoopRun() because
    // GLFW's Cocoa backend posts a deferred [NSApp stop:] during initialization
    // that can cause a single CFRunLoopRun() to exit immediately.
    while (!ctx.should_stop) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1e10, true);
    }

    CFRunLoopTimerInvalidate(ctx.timer);
    CFRelease(ctx.timer);
}

}  // namespace vivid

#endif  // __APPLE__
