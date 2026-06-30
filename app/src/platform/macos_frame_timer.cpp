#ifdef __APPLE__
#include "platform/macos_frame_timer.h"
#include <CoreFoundation/CoreFoundation.h>
#include <cstdio>

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

    if (!ctx.timer) {
        std::fprintf(stderr, "[vivid] CFRunLoopTimerCreate failed\n");
        return;
    }

    // The timer drives tick() ONLY inside nested tracking/modal run-loops — the ones
    // a hosted plugin GUI enters on mouse-down, where the outer loop is blocked inside
    // glfwPollEvents(). It is intentionally NOT registered in default mode: macOS does
    // not fire run-loop timers for a *background* app, so relying on the timer made the
    // frame loop (and the control-server drain it runs) stall whenever the app wasn't
    // frontmost. Instead the outer loop ticks directly, so the loop keeps pumping
    // regardless of foreground — required for agent-driven/MCP use.
    CFRunLoopRef rl = CFRunLoopGetMain();
    CFRunLoopAddTimer(rl, ctx.timer, CFSTR("NSEventTrackingRunLoopMode"));
    CFRunLoopAddTimer(rl, ctx.timer, CFSTR("NSModalPanelRunLoopMode"));

    while (!ctx.should_stop) {
        if (!poll_events()) { ctx.should_stop = true; break; }
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0 / 120.0, true);
        if (ctx.tick && !ctx.tick()) { ctx.should_stop = true; break; }
    }

    CFRunLoopTimerInvalidate(ctx.timer);
    CFRelease(ctx.timer);
}

}  // namespace vivid

#endif  // __APPLE__
