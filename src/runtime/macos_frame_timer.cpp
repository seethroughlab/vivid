#ifdef __APPLE__
#include "runtime/macos_frame_timer.h"
#include <CoreFoundation/CoreFoundation.h>

namespace vivid {

void macos_run_frame_loop(std::function<bool()> poll_events,
                          std::function<bool()> tick) {
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

    // Register timer for all modes we need it to fire in.
    // kCFRunLoopCommonModes does NOT include tracking/modal on macOS (unlike iOS).
    CFRunLoopRef rl = CFRunLoopGetMain();
    CFRunLoopAddTimer(rl, ctx.timer, kCFRunLoopDefaultMode);
    CFRunLoopAddTimer(rl, ctx.timer, CFSTR("NSEventTrackingRunLoopMode"));
    CFRunLoopAddTimer(rl, ctx.timer, CFSTR("NSModalPanelRunLoopMode"));

    // Outer loop: poll events (may block during tracking), then let the timer fire.
    // During tracking, glfwPollEvents() blocks inside [NSApp sendEvent:], and
    // AppKit runs CFRunLoopRunInMode(NSEventTrackingRunLoopMode, ...) internally.
    // Our timer fires in that nested run loop, rendering frames continuously.
    // When tracking ends, glfwPollEvents() returns, and we resume the outer loop.
    while (!ctx.should_stop) {
        if (!poll_events()) {
            ctx.should_stop = true;
            break;
        }
        // Let the timer fire once in default mode, then loop back to poll.
        // timeout=1.0s is a ceiling — the timer fires at 1/240s, waking the loop.
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }

    CFRunLoopTimerInvalidate(ctx.timer);
    CFRelease(ctx.timer);
}

}  // namespace vivid

#endif  // __APPLE__
