#import <Cocoa/Cocoa.h>
#include "file_dialog.h"

namespace vivid::ui {

std::string open_file_dialog() {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            return std::string([[[panel URL] path] UTF8String]);
        }
        return {};
    }
}

std::string open_directory_dialog() {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            return std::string([[[panel URL] path] UTF8String]);
        }
        return {};
    }
}

std::string save_file_dialog(const std::string& default_name) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setCanCreateDirectories:YES];
        if (!default_name.empty()) {
            [panel setNameFieldStringValue:
                [NSString stringWithUTF8String:default_name.c_str()]];
        }
        if ([panel runModal] == NSModalResponseOK) {
            return std::string([[[panel URL] path] UTF8String]);
        }
        return {};
    }
}

std::string save_directory_dialog(const std::string& default_name) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setCanCreateDirectories:YES];
        // Treat the entered name as a directory (no extension)
        [panel setExtensionHidden:YES];
        if (!default_name.empty()) {
            [panel setNameFieldStringValue:
                [NSString stringWithUTF8String:default_name.c_str()]];
        }
        if ([panel runModal] == NSModalResponseOK) {
            return std::string([[[panel URL] path] UTF8String]);
        }
        return {};
    }
}

} // namespace vivid::ui
