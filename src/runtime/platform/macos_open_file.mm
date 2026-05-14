#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "runtime/platform/macos_open_file.h"

#include <cstdio>
#include <mutex>
#include <objc/runtime.h>
#include <string>
#include <vector>

namespace {

std::mutex g_queue_mutex;
std::vector<std::string> g_queue;

void enqueue_path(const char* utf8) {
    if (!utf8 || *utf8 == '\0') return;
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_queue.emplace_back(utf8);
}

} // namespace

@interface VividOpenFileHandler : NSObject
- (void)handleOpenDocumentsEvent:(NSAppleEventDescriptor*)event
                  withReplyEvent:(NSAppleEventDescriptor*)replyEvent;
@end

@implementation VividOpenFileHandler

- (void)handleOpenDocumentsEvent:(NSAppleEventDescriptor*)event
                  withReplyEvent:(NSAppleEventDescriptor*)replyEvent {
    (void)replyEvent;
    NSAppleEventDescriptor* directObject = [event paramDescriptorForKeyword:keyDirectObject];
    if (!directObject) return;

    NSInteger count = [directObject numberOfItems];
    for (NSInteger i = 1; i <= count; ++i) {
        NSAppleEventDescriptor* item = [directObject descriptorAtIndex:i];
        if (!item) continue;
        NSAppleEventDescriptor* fileURLDesc = [item coerceToDescriptorType:typeFileURL];
        if (!fileURLDesc) continue;
        NSData* data = [fileURLDesc data];
        if (!data) continue;
        NSURL* url = [NSURL URLWithDataRepresentation:data relativeToURL:nil];
        if (!url || !url.isFileURL) continue;
        NSString* path = [url path];
        if (!path) continue;
        enqueue_path([path UTF8String]);
        std::fprintf(stderr, "[vivid] OpenFile event: queued %s\n", [path UTF8String]);
    }
}

@end

// IMP for the injected application:openFile: method. NSDocumentController calls
// this before attempting to open the file itself; returning YES short-circuits it.
static BOOL vivid_app_open_file(id self, SEL cmd, NSApplication* app, NSString* filename) {
    (void)self; (void)cmd; (void)app;
    enqueue_path([filename UTF8String]);
    std::fprintf(stderr, "[vivid] application:openFile: queued %s\n",
                 [filename UTF8String]);
    return YES;
}

namespace vivid::platform {

void inject_open_file_delegate() {
    id delegate = [NSApp delegate];
    if (!delegate) {
        std::fprintf(stderr, "[vivid] inject_open_file_delegate: no delegate yet\n");
        return;
    }

    Class original = object_getClass(delegate);
    NSString* subName =
        [NSStringFromClass(original) stringByAppendingString:@"_VividFileOpen"];
    Class sub = NSClassFromString(subName);

    if (!sub) {
        sub = objc_allocateClassPair(original, [subName UTF8String], 0);
        if (sub) {
            class_addMethod(sub,
                            @selector(application:openFile:),
                            (IMP)vivid_app_open_file,
                            "c@:@@");
            objc_registerClassPair(sub);
        }
    }
    if (sub) {
        object_setClass(delegate, sub);
        std::fprintf(stderr, "[vivid] inject_open_file_delegate: injected into %s\n",
                     [NSStringFromClass(original) UTF8String]);
    }
}

void schedule_open_file_injection() {
    // Register for NSApplicationWillFinishLaunchingNotification, which fires
    // during glfwInit()'s brief [NSApp run] — AFTER GLFW sets its app delegate
    // but BEFORE applicationDidFinishLaunching where NSDocumentController
    // processes kAEOpenDocuments events. Injecting application:openFile: here
    // ensures NSDocumentController calls our handler (returning YES) instead of
    // falling back to its own document-opening logic (which shows the error).
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationWillFinishLaunchingNotification
                   object:nil
                    queue:nil
               usingBlock:^(NSNotification* note) {
        (void)note;
        inject_open_file_delegate();
    }];
}

void install_open_file_handler() {
    static VividOpenFileHandler* handler = nil;
    if (handler) return;
    handler = [[VividOpenFileHandler alloc] init];
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:handler
            andSelector:@selector(handleOpenDocumentsEvent:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEOpenDocuments];
}

std::vector<std::string> drain_pending_open_files() {
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    out.swap(g_queue);
    return out;
}

} // namespace vivid::platform

#endif // __APPLE__
