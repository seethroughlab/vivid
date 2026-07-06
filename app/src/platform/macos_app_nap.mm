#ifdef __APPLE__
#import <Foundation/Foundation.h>

#include "platform/app_nap.h"
#include <cstdio>

// Defeat App Nap. macOS throttles/suspends timers (and thus our CFRunLoopTimer-driven
// frame loop, which drains the MCP control-server queue each tick) when an app is in
// the background. NSActivityUserInitiated tells the OS this process is doing work the
// user cares about, so it keeps scheduling us — the control server then responds even
// when the app isn't frontmost. We hold the returned activity token for the whole run.
namespace vivid {

static id g_app_nap_activity = nil;  // retained for the process lifetime

void disable_app_nap(const char* reason) {
    if (g_app_nap_activity) return;  // idempotent
    @autoreleasepool {
        NSActivityOptions opts =
            NSActivityUserInitiated | NSActivityLatencyCritical;
        NSString* r = [NSString stringWithUTF8String:(reason ? reason : "vivid control server")];
        id activity = [[NSProcessInfo processInfo] beginActivityWithOptions:opts reason:r];
        g_app_nap_activity = [activity retain];  // no ARC in this target
        std::fprintf(stderr, "[vivid] App Nap disabled (control server pumps in background)\n");
    }
}

}  // namespace vivid

#endif  // __APPLE__
