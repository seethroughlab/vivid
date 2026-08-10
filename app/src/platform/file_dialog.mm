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

std::string save_video_dialog(const std::string& suggested_name) {
    @autoreleasepool {
        NSSavePanel* p = [NSSavePanel savePanel];
        [p setMessage:@"Export video"];
        [p setPrompt:@"Export"];
        [p setNameFieldStringValue:[NSString stringWithUTF8String:
                                        (suggested_name.empty() ? "vivid-export.mp4" : suggested_name.c_str())]];
        if ([p runModal] == NSModalResponseOK && p.URL)
            return std::string([[p.URL path] UTF8String]);
    }
    return {};
}

std::string save_audio_dialog(const std::string& suggested_name) {
    @autoreleasepool {
        NSSavePanel* p = [NSSavePanel savePanel];
        [p setMessage:@"Export audio"];
        [p setPrompt:@"Export"];
        [p setNameFieldStringValue:[NSString stringWithUTF8String:
                                        (suggested_name.empty() ? "vivid-export.wav" : suggested_name.c_str())]];
        if ([p runModal] == NSModalResponseOK && p.URL)
            return std::string([[p.URL path] UTF8String]);
    }
    return {};
}

DiscardChoice confirm_discard_changes() {
    @autoreleasepool {
        NSAlert* a = [[NSAlert alloc] init];
        [a setMessageText:@"Do you want to save the changes to this project?"];
        [a setInformativeText:@"Your changes will be lost if you don't save them."];
        [a setAlertStyle:NSAlertStyleWarning];
        [a addButtonWithTitle:@"Save"];         // NSAlertFirstButtonReturn
        [a addButtonWithTitle:@"Don't Save"];   // NSAlertSecondButtonReturn
        [a addButtonWithTitle:@"Cancel"];       // NSAlertThirdButtonReturn
        const NSModalResponse r = [a runModal];
        if (r == NSAlertFirstButtonReturn)  return DiscardChoice::Save;
        if (r == NSAlertSecondButtonReturn) return DiscardChoice::Discard;
        return DiscardChoice::Cancel;
    }
}

bool confirm_recover_autosave(const std::string& detail) {
    @autoreleasepool {
        NSAlert* a = [[NSAlert alloc] init];
        [a setMessageText:@"Recover unsaved changes?"];
        [a setInformativeText:[NSString stringWithUTF8String:detail.c_str()]];
        [a setAlertStyle:NSAlertStyleInformational];
        [a addButtonWithTitle:@"Recover"];   // NSAlertFirstButtonReturn
        [a addButtonWithTitle:@"Discard"];   // NSAlertSecondButtonReturn
        return [a runModal] == NSAlertFirstButtonReturn;
    }
}

void show_alert(const std::string& title, const std::string& message) {
    @autoreleasepool {
        NSAlert* a = [[NSAlert alloc] init];
        [a setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [a setInformativeText:[NSString stringWithUTF8String:message.c_str()]];
        [a setAlertStyle:NSAlertStyleCritical];
        [a addButtonWithTitle:@"OK"];
        [a runModal];
    }
}

}  // namespace vivid::platform

#endif  // __APPLE__
