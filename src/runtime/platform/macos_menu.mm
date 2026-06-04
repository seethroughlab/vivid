#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "runtime/platform/macos_menu.h"
#include "ui/style/i18n.h"
#include <cstdio>

// Tag values for menu items
enum MenuTag : NSInteger {
    kMenuTagAbout = 1,
    kMenuTagNew,
    kMenuTagNewProject,
    kMenuTagOpen,
    kMenuTagOpenExample,
    kMenuTagOpenGraphFolder,
    kMenuTagSave,
    kMenuTagSaveAs,
    kMenuTagPreferences,
    kMenuTagExport,
    kMenuTagBrowsePackages,
    kMenuTagOpenPackageCatalogWebsite,
    kMenuTagCheckForUpdates,
    kMenuTagToggleAutoCheckUpdates,
    kMenuTagReportIssue,
    kMenuTagCheckSystemRequirements,
    // Edit
    kMenuTagUndo,
    kMenuTagRedo,
    kMenuTagDeleteSelected,
    kMenuTagEditMeta,
    // View
    kMenuTagToggleUI,
    kMenuTagToggleFullscreen,
    kMenuTagToggleBezierWires,
    kMenuTagToggleShowParamWires,
    kMenuTagToggleAnalysis,
    kMenuTagToggleSessionGrid,
    kMenuTagToggleBuildConsole,
    kMenuTagToggleMidiMap,
    // Insert
    kMenuTagAddNode,
};

@interface VividMenuDelegate : NSObject
@property (nonatomic, assign) vivid::MenuCallbacks callbacks;
- (void)menuAction:(NSMenuItem*)sender;
- (void)recentFileAction:(NSMenuItem*)sender;
- (void)clearRecentAction:(NSMenuItem*)sender;
- (BOOL)validateMenuItem:(NSMenuItem*)item;
@end

@implementation VividMenuDelegate

- (void)menuAction:(NSMenuItem*)sender {
    switch (sender.tag) {
        case kMenuTagAbout:             if (_callbacks.on_about) _callbacks.on_about(); break;
        case kMenuTagNew:               if (_callbacks.on_new) _callbacks.on_new(); break;
        case kMenuTagNewProject:        if (_callbacks.on_new_project) _callbacks.on_new_project(); break;
        case kMenuTagOpen:              if (_callbacks.on_open) _callbacks.on_open(); break;
        case kMenuTagOpenExample:       if (_callbacks.on_open_example) _callbacks.on_open_example(); break;
        case kMenuTagOpenGraphFolder:  if (_callbacks.on_open_graph_folder) _callbacks.on_open_graph_folder(); break;
        case kMenuTagSave:              if (_callbacks.on_save) _callbacks.on_save(); break;
        case kMenuTagSaveAs:            if (_callbacks.on_save_as) _callbacks.on_save_as(); break;
        case kMenuTagPreferences:       if (_callbacks.on_preferences) _callbacks.on_preferences(); break;
        case kMenuTagExport:            if (_callbacks.on_export) _callbacks.on_export(); break;
        case kMenuTagBrowsePackages:    if (_callbacks.on_browse_packages) _callbacks.on_browse_packages(); break;
        case kMenuTagOpenPackageCatalogWebsite:
            if (_callbacks.on_open_package_catalog_website) _callbacks.on_open_package_catalog_website();
            break;
        case kMenuTagCheckForUpdates:
            if (_callbacks.on_check_for_updates) _callbacks.on_check_for_updates();
            break;
        case kMenuTagToggleAutoCheckUpdates:
            if (_callbacks.on_toggle_auto_check_updates) _callbacks.on_toggle_auto_check_updates();
            break;
        case kMenuTagReportIssue:
            if (_callbacks.on_report_issue) _callbacks.on_report_issue();
            break;
        case kMenuTagCheckSystemRequirements:
            if (_callbacks.on_check_system_requirements) _callbacks.on_check_system_requirements();
            break;
        case kMenuTagUndo:              if (_callbacks.on_undo) _callbacks.on_undo(); break;
        case kMenuTagRedo:              if (_callbacks.on_redo) _callbacks.on_redo(); break;
        case kMenuTagDeleteSelected:    if (_callbacks.on_delete_selected) _callbacks.on_delete_selected(); break;
        case kMenuTagEditMeta:          if (_callbacks.on_edit_meta) _callbacks.on_edit_meta(); break;
        case kMenuTagToggleUI:          if (_callbacks.on_toggle_ui) _callbacks.on_toggle_ui(); break;
        case kMenuTagToggleFullscreen:  if (_callbacks.on_toggle_fullscreen) _callbacks.on_toggle_fullscreen(); break;
        case kMenuTagToggleBezierWires: if (_callbacks.on_toggle_bezier_wires) _callbacks.on_toggle_bezier_wires(); break;
        case kMenuTagToggleShowParamWires: if (_callbacks.on_toggle_show_param_wires) _callbacks.on_toggle_show_param_wires(); break;
        case kMenuTagToggleAnalysis: if (_callbacks.on_toggle_analysis) _callbacks.on_toggle_analysis(); break;
        case kMenuTagToggleSessionGrid: if (_callbacks.on_toggle_session_grid) _callbacks.on_toggle_session_grid(); break;
        case kMenuTagToggleBuildConsole: if (_callbacks.on_toggle_build_console) _callbacks.on_toggle_build_console(); break;
        case kMenuTagToggleMidiMap:     if (_callbacks.on_toggle_midi_map) _callbacks.on_toggle_midi_map(); break;
        case kMenuTagAddNode:           if (_callbacks.on_add_node) _callbacks.on_add_node(); break;
    }
}

- (void)recentFileAction:(NSMenuItem*)sender {
    NSString* path = [sender representedObject];
    if (path && _callbacks.on_open_recent) {
        _callbacks.on_open_recent([path UTF8String]);
    }
}

- (void)clearRecentAction:(NSMenuItem*)sender {
    if (_callbacks.on_clear_recent) _callbacks.on_clear_recent();
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    switch (item.tag) {
        case kMenuTagUndo: {
            // Title reflects the next undoable action, e.g. "Undo Clear pattern".
            NSString* base = [NSString stringWithUTF8String:
                vivid::ui::I18n::instance().get("menu_undo", "Undo")];
            std::string label = _callbacks.undo_label ? _callbacks.undo_label() : std::string();
            item.title = label.empty()
                ? base
                : [NSString stringWithFormat:@"%@ %s", base, label.c_str()];
            return (_callbacks.can_undo && _callbacks.can_undo()) ? YES : NO;
        }
        case kMenuTagRedo: {
            NSString* base = [NSString stringWithUTF8String:
                vivid::ui::I18n::instance().get("menu_redo", "Redo")];
            std::string label = _callbacks.redo_label ? _callbacks.redo_label() : std::string();
            item.title = label.empty()
                ? base
                : [NSString stringWithFormat:@"%@ %s", base, label.c_str()];
            return (_callbacks.can_redo && _callbacks.can_redo()) ? YES : NO;
        }
        case kMenuTagOpenGraphFolder:
            return _callbacks.has_graph_path ? _callbacks.has_graph_path() : NO;
        case kMenuTagDeleteSelected:
            return _callbacks.has_selection ? _callbacks.has_selection() : NO;
        case kMenuTagEditMeta:
            return _callbacks.can_edit_meta ? _callbacks.can_edit_meta() : NO;

        case kMenuTagToggleUI:
            item.state = (_callbacks.is_ui_visible && _callbacks.is_ui_visible()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;

        case kMenuTagToggleFullscreen:
            item.state = (_callbacks.is_fullscreen && _callbacks.is_fullscreen()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;

        case kMenuTagToggleBezierWires:
            item.state = (_callbacks.is_bezier_wires && _callbacks.is_bezier_wires()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;

        case kMenuTagToggleShowParamWires:
            item.state = (_callbacks.is_show_param_wires && _callbacks.is_show_param_wires()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;

        case kMenuTagToggleAnalysis:
            item.state = (_callbacks.is_analysis_enabled && _callbacks.is_analysis_enabled()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;

        case kMenuTagToggleSessionGrid:
            item.state = (_callbacks.is_session_grid_open && _callbacks.is_session_grid_open()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;

        case kMenuTagToggleBuildConsole:
            item.state = (_callbacks.is_build_console_open && _callbacks.is_build_console_open()) ? NSControlStateValueOn : NSControlStateValueOff;
            return (_callbacks.is_ui_visible && _callbacks.is_ui_visible()) ? YES : NO;

        case kMenuTagToggleMidiMap:
            item.state = (_callbacks.is_midi_map_mode && _callbacks.is_midi_map_mode()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;
        case kMenuTagToggleAutoCheckUpdates:
            item.state = (_callbacks.is_auto_check_updates && _callbacks.is_auto_check_updates()) ? NSControlStateValueOn : NSControlStateValueOff;
            return YES;

        default:
            return YES;
    }
}

@end

// Must be kept alive for the lifetime of the app (menus reference it as target).
static VividMenuDelegate* sDelegate = nil;
static NSMenu* sRecentMenu = nil;

namespace vivid {

namespace {

NSString* ns_localized(const char* key, const char* fallback) {
    return [NSString stringWithUTF8String:ui::I18n::instance().get(key, fallback)];
}

NSString* ns_localized_vivid(const char* key, const char* fallback) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), ui::I18n::instance().get(key, fallback), "Vivid");
    return [NSString stringWithUTF8String:buf];
}

} // namespace

void macos_setup_menu(const MenuCallbacks& callbacks) {
    @autoreleasepool {
        sDelegate = [[VividMenuDelegate alloc] init];
        sDelegate.callbacks = callbacks;

        NSMenu* mainMenu = [NSApp mainMenu];
        if (!mainMenu) return;

        // --- Insert "Preferences..." into the app menu (index 0) ---
        NSMenuItem* appMenuItem = [mainMenu itemAtIndex:0];
        NSMenu* appMenu = [appMenuItem submenu];
        if (appMenu) {
            // Insert "About Vivid" as the first item (standard macOS convention)
            NSMenuItem* aboutItem = [[NSMenuItem alloc]
                initWithTitle:ns_localized_vivid("menu_about_vivid", "About %s")
                       action:@selector(menuAction:)
                keyEquivalent:@""];
            aboutItem.target = sDelegate;
            aboutItem.tag = kMenuTagAbout;
            [appMenu insertItem:aboutItem atIndex:0];
            [appMenu insertItem:[NSMenuItem separatorItem] atIndex:1];

            // Find the "Services" item to insert before it
            NSInteger servicesIdx = -1;
            for (NSInteger i = 0; i < [appMenu numberOfItems]; i++) {
                NSMenuItem* item = [appMenu itemAtIndex:i];
                if ([item submenu] && [[item title] isEqualToString:@"Services"]) {
                    servicesIdx = i;
                    break;
                }
            }

            if (servicesIdx >= 0) {
                // Insert separator + update actions + Preferences before Services
                [appMenu insertItem:[NSMenuItem separatorItem] atIndex:servicesIdx];

                NSMenuItem* checkUpdatesItem = [[NSMenuItem alloc]
                    initWithTitle:ns_localized("menu_check_for_updates", "Check for Updates...")
                           action:@selector(menuAction:)
                    keyEquivalent:@""];
                checkUpdatesItem.target = sDelegate;
                checkUpdatesItem.tag = kMenuTagCheckForUpdates;
                [appMenu insertItem:checkUpdatesItem atIndex:servicesIdx];

                NSMenuItem* autoCheckUpdatesItem = [[NSMenuItem alloc]
                    initWithTitle:ns_localized("menu_auto_check_updates", "Automatically Check for Updates")
                           action:@selector(menuAction:)
                    keyEquivalent:@""];
                autoCheckUpdatesItem.target = sDelegate;
                autoCheckUpdatesItem.tag = kMenuTagToggleAutoCheckUpdates;
                [appMenu insertItem:autoCheckUpdatesItem atIndex:servicesIdx];

                NSMenuItem* prefsItem = [[NSMenuItem alloc]
                    initWithTitle:ns_localized("menu_preferences", "Preferences...")
                           action:@selector(menuAction:)
                    keyEquivalent:@","];
                prefsItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
                prefsItem.target = sDelegate;
                prefsItem.tag = kMenuTagPreferences;
                [appMenu insertItem:prefsItem atIndex:servicesIdx];
            }
        }

        // --- Create "File" menu and insert at index 1 ---
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:ns_localized("menu_file", "File")];

        NSMenuItem* newItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_new", "New")
                   action:@selector(menuAction:)
            keyEquivalent:@"n"];
        newItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        newItem.target = sDelegate;
        newItem.tag = kMenuTagNew;
        [fileMenu addItem:newItem];

        NSMenuItem* newProjectItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_new_project", "New Project...")
                   action:@selector(menuAction:)
            keyEquivalent:@"N"];
        newProjectItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        newProjectItem.target = sDelegate;
        newProjectItem.tag = kMenuTagNewProject;
        [fileMenu addItem:newProjectItem];
        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* openItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_open", "Open...")
                   action:@selector(menuAction:)
            keyEquivalent:@"o"];
        openItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        openItem.target = sDelegate;
        openItem.tag = kMenuTagOpen;
        [fileMenu addItem:openItem];

        NSMenuItem* openExampleItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_open_example", "Open Example...")
                   action:@selector(menuAction:)
            keyEquivalent:@"O"];
        openExampleItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        openExampleItem.target = sDelegate;
        openExampleItem.tag = kMenuTagOpenExample;
        [fileMenu addItem:openExampleItem];

        NSMenuItem* openGraphFolderItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_open_graph_folder", "Open Graph Folder")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        openGraphFolderItem.target = sDelegate;
        openGraphFolderItem.tag = kMenuTagOpenGraphFolder;
        [fileMenu addItem:openGraphFolderItem];

        // Open Recent submenu
        sRecentMenu = [[NSMenu alloc] initWithTitle:ns_localized("menu_open_recent", "Open Recent")];
        NSMenuItem* recentMenuItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_open_recent", "Open Recent")
                   action:nil
            keyEquivalent:@""];
        [recentMenuItem setSubmenu:sRecentMenu];
        [fileMenu addItem:recentMenuItem];

        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* saveItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("save", "Save")
                   action:@selector(menuAction:)
            keyEquivalent:@"s"];
        saveItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        saveItem.target = sDelegate;
        saveItem.tag = kMenuTagSave;
        [fileMenu addItem:saveItem];

        NSMenuItem* saveAsItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_save_as", "Save As...")
                   action:@selector(menuAction:)
            keyEquivalent:@"S"];
        saveAsItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        saveAsItem.target = sDelegate;
        saveAsItem.tag = kMenuTagSaveAs;
        [fileMenu addItem:saveAsItem];

        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* exportItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_export_standalone", "Export Standalone...")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        exportItem.target = sDelegate;
        exportItem.tag = kMenuTagExport;
        [fileMenu addItem:exportItem];

        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* browseItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_browse_packages", "Browse Packages...")
                   action:@selector(menuAction:)
            keyEquivalent:@"P"];
        browseItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        browseItem.target = sDelegate;
        browseItem.tag = kMenuTagBrowsePackages;
        [fileMenu addItem:browseItem];

        NSMenuItem* openCatalogSiteItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_open_package_catalog_website", "Open Package Catalog Website...")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        openCatalogSiteItem.target = sDelegate;
        openCatalogSiteItem.tag = kMenuTagOpenPackageCatalogWebsite;
        [fileMenu addItem:openCatalogSiteItem];

        NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:ns_localized("menu_file", "File")
                                                             action:nil
                                                      keyEquivalent:@""];
        [fileMenuItem setSubmenu:fileMenu];
        [mainMenu insertItem:fileMenuItem atIndex:1];

        // --- Create "Edit" menu and insert at index 2 ---
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:ns_localized("menu_edit", "Edit")];

        // Undo / Redo are deliberately click-only (no Cmd+Z key equivalent): a native
        // key equivalent would intercept Cmd+Z globally and break the app's contextual
        // undo (sticky notes, editor windows) handled in node_graph_input. Titles are
        // updated dynamically in validateMenuItem ("Undo Clear pattern").
        NSMenuItem* undoItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_undo", "Undo")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        undoItem.target = sDelegate;
        undoItem.tag = kMenuTagUndo;
        [editMenu addItem:undoItem];

        NSMenuItem* redoItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_redo", "Redo")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        redoItem.target = sDelegate;
        redoItem.tag = kMenuTagRedo;
        [editMenu addItem:redoItem];

        [editMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* deleteItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_delete_selected", "Delete Selected")
                   action:@selector(menuAction:)
            keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSBackspaceCharacter]];
        deleteItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        deleteItem.target = sDelegate;
        deleteItem.tag = kMenuTagDeleteSelected;
        [editMenu addItem:deleteItem];

        NSMenuItem* editMetaItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_edit_meta", "Edit Meta...")
                   action:@selector(menuAction:)
            keyEquivalent:@"i"];
        editMetaItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        editMetaItem.target = sDelegate;
        editMetaItem.tag = kMenuTagEditMeta;
        [editMenu addItem:editMetaItem];

        NSMenuItem* editMenuItem = [[NSMenuItem alloc] initWithTitle:ns_localized("menu_edit", "Edit")
                                                              action:nil
                                                       keyEquivalent:@""];
        [editMenuItem setSubmenu:editMenu];
        [mainMenu insertItem:editMenuItem atIndex:2];

        // --- Create "View" menu and insert at index 3 ---
        NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:ns_localized("menu_view", "View")];

        NSMenuItem* toggleUIItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_toggle_graph_ui", "Toggle Graph UI")
                   action:@selector(menuAction:)
            keyEquivalent:@"`"];
        toggleUIItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleUIItem.target = sDelegate;
        toggleUIItem.tag = kMenuTagToggleUI;
        [viewMenu addItem:toggleUIItem];

        NSMenuItem* toggleFullscreenItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_toggle_fullscreen", "Toggle Fullscreen")
                   action:@selector(menuAction:)
            keyEquivalent:@"f"];
        toggleFullscreenItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        toggleFullscreenItem.target = sDelegate;
        toggleFullscreenItem.tag = kMenuTagToggleFullscreen;
        [viewMenu addItem:toggleFullscreenItem];

        NSMenuItem* toggleBezierItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_toggle_bezier_wires", "Toggle Bezier Wires")
                   action:@selector(menuAction:)
            keyEquivalent:@"b"];
        toggleBezierItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleBezierItem.target = sDelegate;
        toggleBezierItem.tag = kMenuTagToggleBezierWires;
        [viewMenu addItem:toggleBezierItem];

        NSMenuItem* toggleParamWiresItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_show_param_wires", "Show Param Wires")
                   action:@selector(menuAction:)
            keyEquivalent:@"p"];
        toggleParamWiresItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleParamWiresItem.target = sDelegate;
        toggleParamWiresItem.tag = kMenuTagToggleShowParamWires;
        [viewMenu addItem:toggleParamWiresItem];

        NSMenuItem* toggleAnalysisItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_enable_analysis", "Enable Analysis")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        toggleAnalysisItem.target = sDelegate;
        toggleAnalysisItem.tag = kMenuTagToggleAnalysis;
        [viewMenu addItem:toggleAnalysisItem];

        NSMenuItem* toggleGridItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_toggle_session_grid", "Toggle Session Grid")
                   action:@selector(menuAction:)
            keyEquivalent:@"g"];
        toggleGridItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleGridItem.target = sDelegate;
        toggleGridItem.tag = kMenuTagToggleSessionGrid;
        [viewMenu addItem:toggleGridItem];

        NSMenuItem* toggleBuildConsoleItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("build_console", "Build Console")
                   action:@selector(menuAction:)
            keyEquivalent:@"B"];
        toggleBuildConsoleItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        toggleBuildConsoleItem.target = sDelegate;
        toggleBuildConsoleItem.tag = kMenuTagToggleBuildConsole;
        [viewMenu addItem:toggleBuildConsoleItem];

        NSMenuItem* toggleMidiItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_toggle_midi_map", "Toggle MIDI Map")
                   action:@selector(menuAction:)
            keyEquivalent:@"m"];
        toggleMidiItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        toggleMidiItem.target = sDelegate;
        toggleMidiItem.tag = kMenuTagToggleMidiMap;
        [viewMenu addItem:toggleMidiItem];

        NSMenuItem* viewMenuItem = [[NSMenuItem alloc] initWithTitle:ns_localized("menu_view", "View")
                                                              action:nil
                                                       keyEquivalent:@""];
        [viewMenuItem setSubmenu:viewMenu];
        [mainMenu insertItem:viewMenuItem atIndex:3];

        // --- Create "Insert" menu and insert at index 4 ---
        NSMenu* insertMenu = [[NSMenu alloc] initWithTitle:ns_localized("menu_insert", "Insert")];

        NSMenuItem* addNodeItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_add_node", "Add Node...")
                   action:@selector(menuAction:)
            keyEquivalent:@"t"];
        addNodeItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        addNodeItem.target = sDelegate;
        addNodeItem.tag = kMenuTagAddNode;
        [insertMenu addItem:addNodeItem];

        NSMenuItem* insertMenuItem = [[NSMenuItem alloc] initWithTitle:ns_localized("menu_insert", "Insert")
                                                                action:nil
                                                         keyEquivalent:@""];
        [insertMenuItem setSubmenu:insertMenu];
        [mainMenu insertItem:insertMenuItem atIndex:4];

        // --- Create "Help" menu and append ---
        NSMenu* helpMenu = [[NSMenu alloc] initWithTitle:ns_localized("menu_help", "Help")];

        NSMenuItem* checkRequirementsItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_check_system_requirements",
                                        "Check System Requirements...")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        checkRequirementsItem.target = sDelegate;
        checkRequirementsItem.tag = kMenuTagCheckSystemRequirements;
        [helpMenu addItem:checkRequirementsItem];

        [helpMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* reportIssueItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_report_issue", "Report an Issue...")
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        reportIssueItem.target = sDelegate;
        reportIssueItem.tag = kMenuTagReportIssue;
        [helpMenu addItem:reportIssueItem];

        NSMenuItem* helpMenuItem = [[NSMenuItem alloc] initWithTitle:ns_localized("menu_help", "Help")
                                                              action:nil
                                                       keyEquivalent:@""];
        [helpMenuItem setSubmenu:helpMenu];
        [mainMenu addItem:helpMenuItem];
    }
}

void macos_update_recent_files_menu(const std::vector<std::string>& paths) {
    @autoreleasepool {
        if (!sRecentMenu) return;
        [sRecentMenu removeAllItems];
        for (const auto& p : paths) {
            NSString* fullPath = [NSString stringWithUTF8String:p.c_str()];
            NSString* filename = [fullPath lastPathComponent];
            NSMenuItem* item = [[NSMenuItem alloc]
                initWithTitle:filename
                       action:@selector(recentFileAction:)
                keyEquivalent:@""];
            item.target = sDelegate;
            item.representedObject = fullPath;
            item.toolTip = fullPath;
            [sRecentMenu addItem:item];
        }
        if (paths.empty()) {
            NSMenuItem* noneItem = [[NSMenuItem alloc]
                initWithTitle:ns_localized("menu_no_recent_files", "No Recent Files") action:nil keyEquivalent:@""];
            [noneItem setEnabled:NO];
            [sRecentMenu addItem:noneItem];
        }
        [sRecentMenu addItem:[NSMenuItem separatorItem]];
        NSMenuItem* clearItem = [[NSMenuItem alloc]
            initWithTitle:ns_localized("menu_clear_recent", "Clear Recent")
                   action:@selector(clearRecentAction:)
            keyEquivalent:@""];
        clearItem.target = sDelegate;
        [sRecentMenu addItem:clearItem];
    }
}

void macos_set_presentation_fullscreen(bool enabled) {
    @autoreleasepool {
        if (!NSApp) return;
        static NSApplicationPresentationOptions s_saved_options = NSApplicationPresentationDefault;
        static bool s_saved = false;
        if (enabled) {
            if (!s_saved) {
                s_saved_options = [NSApp presentationOptions];
                s_saved = true;
            }
            NSApplicationPresentationOptions opts =
                NSApplicationPresentationHideDock |
                NSApplicationPresentationHideMenuBar;
            [NSApp setPresentationOptions:opts];
        } else {
            if (s_saved) {
                [NSApp setPresentationOptions:s_saved_options];
                s_saved = false;
            } else {
                [NSApp setPresentationOptions:NSApplicationPresentationDefault];
            }
        }
    }
}

void macos_set_document_edited(bool edited) {
    @autoreleasepool {
        if (!NSApp) return;
        NSWindow* window = [NSApp keyWindow];
        if (!window) window = [NSApp mainWindow];
        if (!window) return;
        [window setDocumentEdited:(edited ? YES : NO)];
    }
}

}  // namespace vivid

#endif  // __APPLE__
