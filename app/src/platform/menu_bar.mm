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
- (void)openExample:(NSMenuItem*)sender {
    NSString* p = [sender representedObject];
    if (p && vivid::platform::g_actions.open_example)
        vivid::platform::g_actions.open_example(std::string([p UTF8String]));
}
- (void)doUndo:(id)sender { (void)sender; if (vivid::platform::g_actions.undo) vivid::platform::g_actions.undo(); }
- (void)doRedo:(id)sender { (void)sender; if (vivid::platform::g_actions.redo) vivid::platform::g_actions.redo(); }
- (void)setGeminiKey:(id)sender   { (void)sender; if (vivid::platform::g_actions.set_gemini_key)  vivid::platform::g_actions.set_gemini_key(); }
- (void)evaluateOutput:(id)sender { (void)sender; if (vivid::platform::g_actions.evaluate_output) vivid::platform::g_actions.evaluate_output(); }
@end

static VividMenuTarget* g_target = nil;      // kept alive for the app lifetime (intentional)
static NSMenu*          g_recentMenu = nil;  // the Open Recent submenu (retained by its item)
static NSMenu*          g_exampleMenu = nil; // the Open Example submenu (retained by its item)
static NSMenuItem*      g_undoItem = nil;    // Edit > Undo (title updated by set_edit_labels)
static NSMenuItem*      g_redoItem = nil;    // Edit > Redo

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

        // Open Example submenu (populated by set_example_projects).
        g_exampleMenu = [[NSMenu alloc] initWithTitle:@"Open Example"];
        [g_exampleMenu setAutoenablesItems:NO];
        NSMenuItem* exampleItem = [[NSMenuItem alloc] initWithTitle:@"Open Example" action:nil keyEquivalent:@""];
        [exampleItem setSubmenu:g_exampleMenu];
        [fileMenu addItem:exampleItem];
        [exampleItem release]; [g_exampleMenu release];   // retained by the menu hierarchy

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

        // Edit menu (ADR-0017/G4). Undo/Redo are LABEL-ONLY (no ⌘Z key-equivalent): the keyboard is
        // handled in input.cpp so a focused clip editor keeps its own note-undo; a ⌘Z here would let
        // AppKit swallow the key first. Titles + enabled state are refreshed by set_edit_labels.
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
        [editMenu setAutoenablesItems:NO];
        g_undoItem = [[NSMenuItem alloc] initWithTitle:@"Undo" action:@selector(doUndo:) keyEquivalent:@""];
        [g_undoItem setTarget:g_target]; [g_undoItem setEnabled:NO]; [editMenu addItem:g_undoItem]; [g_undoItem release];
        g_redoItem = [[NSMenuItem alloc] initWithTitle:@"Redo" action:@selector(doRedo:) keyEquivalent:@""];
        [g_redoItem setTarget:g_target]; [g_redoItem setEnabled:NO]; [editMenu addItem:g_redoItem]; [g_redoItem release];
        NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
        [editItem setSubmenu:editMenu];
        [mainMenu insertItem:editItem atIndex:insertAt + 1];   // right after File
        [editItem release]; [editMenu release];   // retained by mainMenu (items retained by editMenu)

        // Eval menu (ADR-0026). "Set Gemini Key…" opens the in-app key modal; "Evaluate Output" runs
        // a one-shot Gemini evaluation of the live master and toasts the verdict. No key-equivalents.
        NSMenu* evalMenu = [[NSMenu alloc] initWithTitle:@"Eval"];
        [evalMenu setAutoenablesItems:NO];
        it = [[NSMenuItem alloc] initWithTitle:@"Set Gemini Key…" action:@selector(setGeminiKey:) keyEquivalent:@""];
        [it setTarget:g_target]; [evalMenu addItem:it]; [it release];
        it = [[NSMenuItem alloc] initWithTitle:@"Evaluate Output" action:@selector(evaluateOutput:) keyEquivalent:@""];
        [it setTarget:g_target]; [evalMenu addItem:it]; [it release];
        NSMenuItem* evalItem = [[NSMenuItem alloc] initWithTitle:@"Eval" action:nil keyEquivalent:@""];
        [evalItem setSubmenu:evalMenu];
        [mainMenu insertItem:evalItem atIndex:insertAt + 2];   // right after Edit
        [evalItem release]; [evalMenu release];   // retained by mainMenu
    }
}

void set_edit_labels(const std::string& undo_label, const std::string& redo_label,
                     bool can_undo, bool can_redo) {
    @autoreleasepool {
        if (g_undoItem) {
            NSString* t = undo_label.empty() ? @"Undo" : [NSString stringWithUTF8String:("Undo " + undo_label).c_str()];
            [g_undoItem setTitle:t]; [g_undoItem setEnabled:can_undo];
        }
        if (g_redoItem) {
            NSString* t = redo_label.empty() ? @"Redo" : [NSString stringWithUTF8String:("Redo " + redo_label).c_str()];
            [g_redoItem setTitle:t]; [g_redoItem setEnabled:can_redo];
        }
    }
}

void set_document_edited(bool edited) {
    @autoreleasepool {
        // The GLFW window is the app's main/key window; tag its close-box + proxy as modified.
        NSWindow* w = [NSApp mainWindow] ?: [NSApp keyWindow];
        if (!w) { NSArray<NSWindow*>* ws = [NSApp windows]; if (ws.count) w = ws[0]; }
        [w setDocumentEdited:edited ? YES : NO];
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

void set_example_projects(const std::vector<MenuItemEntry>& examples) {
    if (!g_exampleMenu) return;
    @autoreleasepool {
        [g_exampleMenu removeAllItems];
        if (examples.empty()) {
            NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"No Examples Found" action:nil keyEquivalent:@""];
            [none setEnabled:NO];
            [g_exampleMenu addItem:none]; [none release];
            return;
        }
        for (const auto& e : examples) {
            NSString* title = [NSString stringWithUTF8String:e.label.c_str()];
            NSString* full  = [NSString stringWithUTF8String:e.path.c_str()];
            NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:@selector(openExample:) keyEquivalent:@""];
            [it setTarget:g_target];
            [it setRepresentedObject:full];
            [g_exampleMenu addItem:it]; [it release];
        }
    }
}

}}  // namespace vivid::platform

#endif  // __APPLE__
