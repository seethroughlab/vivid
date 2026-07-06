#ifdef __APPLE__
#import <Cocoa/Cocoa.h>

#include "platform/menu_bar.h"

#include <string>
#include <vector>

// The stored callbacks (set by install_menu_bar). File-scope so the Objective-C target
// below can reach them. MRC (no ARC), matching the other .mm files.
namespace vivid { namespace platform { static MenuActions g_actions; } }

// Objective-C target for the menu items; forwards each action to the C++ callback.
@interface VividMenuTarget : NSObject
@end
@implementation VividMenuTarget
- (void)newProject:(id)sender     { (void)sender; if (vivid::platform::g_actions.new_project)     vivid::platform::g_actions.new_project(); }
- (void)openProject:(id)sender    { (void)sender; if (vivid::platform::g_actions.open_project)    vivid::platform::g_actions.open_project(); }
- (void)saveProject:(id)sender    { (void)sender; if (vivid::platform::g_actions.save_project)    vivid::platform::g_actions.save_project(); }
- (void)saveProjectAs:(id)sender  { (void)sender; if (vivid::platform::g_actions.save_project_as) vivid::platform::g_actions.save_project_as(); }
- (void)openRecent:(NSMenuItem*)sender {
    NSString* p = [sender representedObject];
    if (p && vivid::platform::g_actions.open_recent)
        vivid::platform::g_actions.open_recent(std::string([p UTF8String]));
}
@end

static VividMenuTarget* g_target = nil;      // kept alive for the app lifetime (intentional)
static NSMenu*          g_recentMenu = nil;  // the Open Recent submenu (retained by its item)

namespace vivid { namespace platform {

void install_menu_bar(const MenuActions& actions) {
    g_actions = actions;
    @autoreleasepool {
        if (!g_target) g_target = [[VividMenuTarget alloc] init];

        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu) { mainMenu = [[NSMenu alloc] initWithTitle:@""]; [NSApp setMainMenu:mainMenu]; }

        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        [fileMenu setAutoenablesItems:NO];

        NSMenuItem* it = nil;
        it = [[NSMenuItem alloc] initWithTitle:@"New" action:@selector(newProject:) keyEquivalent:@"n"];
        [it setTarget:g_target]; [fileMenu addItem:it]; [it release];
        it = [[NSMenuItem alloc] initWithTitle:@"Open…" action:@selector(openProject:) keyEquivalent:@"o"];
        [it setTarget:g_target]; [fileMenu addItem:it]; [it release];

        // Open Recent submenu (populated by set_recent_projects).
        g_recentMenu = [[NSMenu alloc] initWithTitle:@"Open Recent"];
        [g_recentMenu setAutoenablesItems:NO];
        NSMenuItem* recentItem = [[NSMenuItem alloc] initWithTitle:@"Open Recent" action:nil keyEquivalent:@""];
        [recentItem setSubmenu:g_recentMenu];
        [fileMenu addItem:recentItem];
        [recentItem release]; [g_recentMenu release];   // both retained by the menu hierarchy

        [fileMenu addItem:[NSMenuItem separatorItem]];
        it = [[NSMenuItem alloc] initWithTitle:@"Save" action:@selector(saveProject:) keyEquivalent:@"s"];
        [it setTarget:g_target]; [fileMenu addItem:it]; [it release];
        it = [[NSMenuItem alloc] initWithTitle:@"Save As…" action:@selector(saveProjectAs:) keyEquivalent:@"s"];
        [it setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagShift)];
        [it setTarget:g_target]; [fileMenu addItem:it]; [it release];

        NSMenuItem* fileItem = [[NSMenuItem alloc] initWithTitle:@"File" action:nil keyEquivalent:@""];
        [fileItem setSubmenu:fileMenu];
        NSInteger insertAt = ([mainMenu numberOfItems] > 0) ? 1 : 0;  // after the app menu
        [mainMenu insertItem:fileItem atIndex:insertAt];
        [fileItem release]; [fileMenu release];   // retained by mainMenu
    }
}

void set_recent_projects(const std::vector<std::string>& paths) {
    if (!g_recentMenu) return;
    @autoreleasepool {
        [g_recentMenu removeAllItems];
        if (paths.empty()) {
            NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"No Recent Projects" action:nil keyEquivalent:@""];
            [none setEnabled:NO];
            [g_recentMenu addItem:none]; [none release];
            return;
        }
        for (const auto& p : paths) {
            NSString* full  = [NSString stringWithUTF8String:p.c_str()];
            NSString* title = [full lastPathComponent];
            NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:@selector(openRecent:) keyEquivalent:@""];
            [it setTarget:g_target];
            [it setRepresentedObject:full];
            [g_recentMenu addItem:it]; [it release];
        }
    }
}

}}  // namespace vivid::platform

#endif  // __APPLE__
