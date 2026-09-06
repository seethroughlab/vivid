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
- (void)exportVideo:(id)sender    { (void)sender; if (vivid::platform::g_actions.export_video)    vivid::platform::g_actions.export_video(); }
- (void)exportAudio:(id)sender    { (void)sender; if (vivid::platform::g_actions.export_audio)    vivid::platform::g_actions.export_audio(); }
- (void)exportAv:(id)sender       { (void)sender; if (vivid::platform::g_actions.export_av)       vivid::platform::g_actions.export_av(); }
- (void)toggleReduceMotion:(id)sender { (void)sender; if (vivid::platform::g_actions.toggle_reduce_motion) vivid::platform::g_actions.toggle_reduce_motion(); }
- (void)relayoutGraph:(id)sender { (void)sender; if (vivid::platform::g_actions.relayout_graph) vivid::platform::g_actions.relayout_graph(); }
- (void)connectClaude:(id)sender  { (void)sender; if (vivid::platform::g_actions.connect_claude)  vivid::platform::g_actions.connect_claude(); }
- (void)selectAudioDevice:(NSMenuItem*)sender {
    NSString* n = [sender representedObject];   // nil/"" for the "System Default" item
    if (vivid::platform::g_actions.select_audio_device)
        vivid::platform::g_actions.select_audio_device(std::string(n ? [n UTF8String] : ""));
}
@end

static VividMenuTarget* g_target = nil;      // kept alive for the app lifetime (intentional)
static NSMenu*          g_recentMenu = nil;  // the Open Recent submenu (retained by its item)
static NSMenu*          g_exampleMenu = nil; // the Open Example submenu (retained by its item)
static NSMenuItem*      g_undoItem = nil;    // Edit > Undo (title updated by set_edit_labels)
static NSMenuItem*      g_redoItem = nil;    // Edit > Redo
static NSMenuItem*      g_exportVideoItem = nil;  // File > Export Video (title flips via set_export_video_recording)
static NSMenuItem*      g_reduceMotionItem = nil; // View > Reduce Motion (checkmark via set_reduce_motion_checked)
static NSMenu*          g_audioMenu = nil;        // View > Audio Output submenu (ADR-0032 Phase A)

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

        // Export Video — toggles a realtime AV export (label flips to "Stop Export" while recording).
        [fileMenu addItem:[NSMenuItem separatorItem]];
        g_exportVideoItem = [[NSMenuItem alloc] initWithTitle:@"Export Video…" action:@selector(exportVideo:) keyEquivalent:@""];
        [g_exportVideoItem setTarget:g_target]; [fileMenu addItem:g_exportVideoItem]; [g_exportVideoItem release];

        // Export Audio — offline master-mix bounce to a .wav (ADR-0032).
        it = [[NSMenuItem alloc] initWithTitle:@"Export Audio…" action:@selector(exportAudio:) keyEquivalent:@""];
        [it setTarget:g_target]; [fileMenu addItem:it]; [it release];
        // Export Video (Deterministic) — offline AV render locked to a synthetic clock (ADR-0032 Phase C).
        it = [[NSMenuItem alloc] initWithTitle:@"Export Video (Deterministic)…" action:@selector(exportAv:) keyEquivalent:@""];
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
        // UX Ph5 F1 / Ph1 F5: this is an EXPERIMENTAL feature (needs a Google Gemini API key; fails
        // closed with no result if unset) — labelled so it doesn't read as a finished headline.
        NSMenu* evalMenu = [[NSMenu alloc] initWithTitle:@"Eval (Experimental)"];
        [evalMenu setAutoenablesItems:NO];
        it = [[NSMenuItem alloc] initWithTitle:@"Set Gemini Key…" action:@selector(setGeminiKey:) keyEquivalent:@""];
        [it setTarget:g_target]; [evalMenu addItem:it]; [it release];
        it = [[NSMenuItem alloc] initWithTitle:@"Evaluate Output (needs Gemini key)" action:@selector(evaluateOutput:) keyEquivalent:@""];
        [it setTarget:g_target]; [evalMenu addItem:it]; [it release];
        NSMenuItem* evalItem = [[NSMenuItem alloc] initWithTitle:@"Eval (Experimental)" action:nil keyEquivalent:@""];
        [evalItem setSubmenu:evalMenu];
        [mainMenu insertItem:evalItem atIndex:insertAt + 2];   // right after Edit
        [evalItem release]; [evalMenu release];   // retained by mainMenu

        // View menu — accessibility toggles. "Reduce Motion" (UX Ph4 F1) temporally low-passes the
        // visual output so rapid full-frame flashing is damped; it carries a checkmark reflecting the
        // persisted app setting (set_reduce_motion_checked).
        NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
        [viewMenu setAutoenablesItems:NO];
        {   // Re-layout Graph (⌘L): tidy the visual + audio node graphs (was an in-graph button).
            NSMenuItem* rl = [[NSMenuItem alloc] initWithTitle:@"Re-layout Graph"
                              action:@selector(relayoutGraph:) keyEquivalent:@"l"];
            [rl setTarget:g_target]; [viewMenu addItem:rl]; [rl release];
            [viewMenu addItem:[NSMenuItem separatorItem]];
        }
        g_reduceMotionItem = [[NSMenuItem alloc] initWithTitle:@"Reduce Motion (flash limit)"
                                                        action:@selector(toggleReduceMotion:) keyEquivalent:@""];
        [g_reduceMotionItem setTarget:g_target]; [viewMenu addItem:g_reduceMotionItem]; [g_reduceMotionItem release];
        // ADR-0032 Phase A: the audio OUTPUT device picker. Populated by set_audio_devices() after launch
        // (and refreshed on a switch); each item hot-swaps the live device via select_audio_device.
        [viewMenu addItem:[NSMenuItem separatorItem]];
        g_audioMenu = [[NSMenu alloc] initWithTitle:@"Audio Output"];
        [g_audioMenu setAutoenablesItems:NO];
        NSMenuItem* audioItem = [[NSMenuItem alloc] initWithTitle:@"Audio Output" action:nil keyEquivalent:@""];
        [audioItem setSubmenu:g_audioMenu]; [viewMenu addItem:audioItem]; [audioItem release];
        NSMenuItem* viewItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
        [viewItem setSubmenu:viewMenu];
        [mainMenu insertItem:viewItem atIndex:insertAt + 3];   // right after Eval
        [viewItem release]; [viewMenu release];   // retained by mainMenu

        // ADR-0040: a downloaded build must be connectable without a repo checkout. Help > Connect
        // Claude hands the user the `claude mcp add` line for the bridge bundled in Resources/mcp.
        NSMenu* helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];
        [helpMenu setAutoenablesItems:NO];
        NSMenuItem* cc = [[NSMenuItem alloc] initWithTitle:@"Connect Claude…"
                          action:@selector(connectClaude:) keyEquivalent:@""];
        [cc setTarget:g_target]; [helpMenu addItem:cc]; [cc release];
        NSMenuItem* helpItem = [[NSMenuItem alloc] initWithTitle:@"Help" action:nil keyEquivalent:@""];
        [helpItem setSubmenu:helpMenu];
        [mainMenu insertItem:helpItem atIndex:insertAt + 4];   // right after View
        [helpItem release]; [helpMenu release];   // retained by mainMenu
    }
}

void show_copyable_message(const std::string& title, const std::string& body,
                           const std::string& copy_text) {
    @autoreleasepool {
        NSAlert* a = [[NSAlert alloc] init];
        [a setMessageText:[NSString stringWithUTF8String:title.c_str()]];
        [a setInformativeText:[NSString stringWithUTF8String:body.c_str()]];
        [a setAlertStyle:NSAlertStyleInformational];
        const bool can_copy = !copy_text.empty();
        if (can_copy) [a addButtonWithTitle:@"Copy Command"];
        [a addButtonWithTitle:@"Done"];
        const NSModalResponse r = [a runModal];
        if (can_copy && r == NSAlertFirstButtonReturn) {
            NSPasteboard* pb = [NSPasteboard generalPasteboard];
            [pb clearContents];
            [pb setString:[NSString stringWithUTF8String:copy_text.c_str()] forType:NSPasteboardTypeString];
        }
        [a release];
    }
}

void set_reduce_motion_checked(bool checked) {
    @autoreleasepool {
        if (g_reduceMotionItem)
            [g_reduceMotionItem setState:(checked ? NSControlStateValueOn : NSControlStateValueOff)];
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

void set_export_video_recording(bool recording) {
    @autoreleasepool {
        if (g_exportVideoItem)
            [g_exportVideoItem setTitle:(recording ? @"Stop Export" : @"Export Video…")];
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

void set_audio_devices(const std::vector<std::string>& names, const std::string& active_name) {
    if (!g_audioMenu) return;
    @autoreleasepool {
        [g_audioMenu removeAllItems];
        // "System Default" follows the OS default device (persists an empty requested name). It is an
        // action, not a state — the checkmark below marks whichever concrete device is actually active.
        NSMenuItem* def = [[NSMenuItem alloc] initWithTitle:@"System Default"
                                                     action:@selector(selectAudioDevice:) keyEquivalent:@""];
        [def setTarget:g_target]; [def setRepresentedObject:@""];
        [g_audioMenu addItem:def]; [def release];
        if (names.empty()) {
            NSMenuItem* none = [[NSMenuItem alloc] initWithTitle:@"No Output Devices" action:nil keyEquivalent:@""];
            [none setEnabled:NO]; [g_audioMenu addItem:none]; [none release];
            return;
        }
        [g_audioMenu addItem:[NSMenuItem separatorItem]];
        for (const auto& n : names) {
            NSString* nm = [NSString stringWithUTF8String:n.c_str()];
            NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:nm action:@selector(selectAudioDevice:) keyEquivalent:@""];
            [it setTarget:g_target]; [it setRepresentedObject:nm];
            if (n == active_name) [it setState:NSControlStateValueOn];   // the live device
            [g_audioMenu addItem:it]; [it release];
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
        // Entries arrive pre-sorted by (group, label): ungrouped items go on the Open Example menu
        // directly; each non-empty group gets its own submenu (title = group, capitalized).
        NSMutableDictionary<NSString*, NSMenu*>* groups = [NSMutableDictionary dictionary];
        for (const auto& e : examples) {
            NSString* title = [NSString stringWithUTF8String:e.label.c_str()];
            NSString* full  = [NSString stringWithUTF8String:e.path.c_str()];
            NSMenuItem* it = [[NSMenuItem alloc] initWithTitle:title action:@selector(openExample:) keyEquivalent:@""];
            [it setTarget:g_target];
            [it setRepresentedObject:full];
            if (e.group.empty()) {
                [g_exampleMenu addItem:it];
            } else {
                NSString* gkey = [NSString stringWithUTF8String:e.group.c_str()];
                NSMenu* sub = groups[gkey];
                if (!sub) {
                    sub = [[NSMenu alloc] initWithTitle:gkey];
                    [sub setAutoenablesItems:NO];
                    groups[gkey] = sub;
                    NSMenuItem* subItem = [[NSMenuItem alloc] initWithTitle:[gkey capitalizedString]
                                                                     action:nil keyEquivalent:@""];
                    [subItem setSubmenu:sub];
                    [g_exampleMenu addItem:subItem]; [subItem release]; [sub release];
                }
                [sub addItem:it];
            }
            [it release];
        }
    }
}

}}  // namespace vivid::platform

#endif  // __APPLE__
