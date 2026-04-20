#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "runtime/platform/macos_open_file.h"

#include <cstdio>
#include <mutex>
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

namespace vivid::platform {

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
