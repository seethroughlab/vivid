#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import <objc/message.h>

#include "runtime/sparkle_bridge.h"

namespace vivid {

static void ensure_sparkle_loaded() {
    if (NSClassFromString(@"SPUStandardUpdaterController") != nil) return;
    NSBundle* main_bundle = [NSBundle mainBundle];
    if (!main_bundle) return;
    NSString* fw_path = [[main_bundle bundlePath] stringByAppendingPathComponent:@"Contents/Frameworks/Sparkle.framework"];
    NSBundle* sparkle_bundle = [NSBundle bundleWithPath:fw_path];
    if (sparkle_bundle && ![sparkle_bundle isLoaded]) {
        [sparkle_bundle load];
    }
}

bool SparkleBridge::available() {
    ensure_sparkle_loaded();
    return NSClassFromString(@"SPUStandardUpdaterController") != nil;
}

bool SparkleBridge::check_for_updates(std::string* error) {
    ensure_sparkle_loaded();
    Class controller_cls = NSClassFromString(@"SPUStandardUpdaterController");
    if (!controller_cls) {
        if (error) *error = "Sparkle framework unavailable";
        return false;
    }

    SEL alloc_sel = sel_registerName("alloc");
    SEL init_sel = sel_registerName("initWithStartingUpdater:updaterDelegate:userDriverDelegate:");
    SEL updater_sel = sel_registerName("updater");
    SEL check_sel = sel_registerName("checkForUpdates");

    id (*msg_alloc)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    id (*msg_init)(id, SEL, BOOL, id, id) = (id (*)(id, SEL, BOOL, id, id))objc_msgSend;
    id (*msg_obj)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    void (*msg_void)(id, SEL) = (void (*)(id, SEL))objc_msgSend;

    id controller = msg_alloc((id)controller_cls, alloc_sel);
    if (!controller) {
        if (error) *error = "Sparkle controller alloc failed";
        return false;
    }

    controller = msg_init(controller, init_sel, YES, nil, nil);
    if (!controller) {
        if (error) *error = "Sparkle controller init failed";
        return false;
    }

    id updater = msg_obj(controller, updater_sel);
    if (!updater) {
        if (error) *error = "Sparkle updater unavailable";
        return false;
    }
    msg_void(updater, check_sel);
    return true;
}

}  // namespace vivid

#endif  // __APPLE__
