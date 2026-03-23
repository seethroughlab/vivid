#import <Cocoa/Cocoa.h>
#include "file_dialog.h"

namespace vivid::ui {

namespace {

FileDialogTestStats g_file_dialog_test_stats;

void note_dialog_invocation(int* specific_counter) {
    ++g_file_dialog_test_stats.invocation_count;
    if (specific_counter)
        ++(*specific_counter);
}

} // namespace

std::string open_file_dialog() {
    note_dialog_invocation(&g_file_dialog_test_stats.open_file_count);
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
    note_dialog_invocation(&g_file_dialog_test_stats.open_directory_count);
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
    note_dialog_invocation(&g_file_dialog_test_stats.save_file_count);
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
    note_dialog_invocation(&g_file_dialog_test_stats.save_directory_count);
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

void reset_file_dialog_test_stats() {
    g_file_dialog_test_stats = {};
}

FileDialogTestStats file_dialog_test_stats() {
    return g_file_dialog_test_stats;
}

} // namespace vivid::ui
