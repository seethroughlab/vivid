#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

#include "platform/file_dialog.h"

namespace vivid::platform {

std::string open_project_dialog() {
    @autoreleasepool {
        NSOpenPanel* p = [NSOpenPanel openPanel];
        [p setCanChooseFiles:YES];
        [p setCanChooseDirectories:YES];     // a project is a folder (legacy .json also allowed)
        [p setAllowsMultipleSelection:NO];
        [p setMessage:@"Open a Vivid project (a project folder or a .json)"];
        [p setPrompt:@"Open"];
        if ([p runModal] == NSModalResponseOK && p.URLs.count > 0)
            return std::string([[p.URLs[0] path] UTF8String]);
    }
    return {};
}

std::string open_file_dialog(const std::string& message, const std::vector<std::string>& extensions) {
    @autoreleasepool {
        NSOpenPanel* p = [NSOpenPanel openPanel];
        [p setCanChooseFiles:YES];
        [p setCanChooseDirectories:NO];
        [p setAllowsMultipleSelection:NO];
        if (!message.empty()) [p setMessage:[NSString stringWithUTF8String:message.c_str()]];
        [p setPrompt:@"Choose"];
        if (!extensions.empty()) {   // filter to the given extensions (deprecated API, but works 10.13+)
            NSMutableArray* types = [NSMutableArray arrayWithCapacity:extensions.size()];
            for (const auto& e : extensions) [types addObject:[NSString stringWithUTF8String:e.c_str()]];
            [p setAllowedFileTypes:types];
        }
        if ([p runModal] == NSModalResponseOK && p.URLs.count > 0)
            return std::string([[p.URLs[0] path] UTF8String]);
    }
    return {};
}

std::string save_project_dialog(const std::string& suggested_name) {
    @autoreleasepool {
        NSSavePanel* p = [NSSavePanel savePanel];
        [p setMessage:@"Save Vivid project"];
        [p setPrompt:@"Save"];
        if (!suggested_name.empty())
            [p setNameFieldStringValue:[NSString stringWithUTF8String:suggested_name.c_str()]];
        if ([p runModal] == NSModalResponseOK && p.URL)
            return std::string([[p.URL path] UTF8String]);
    }
    return {};
}

}  // namespace vivid::platform

#endif  // __APPLE__
