#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "runtime/macos_menu.h"

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
    // Edit
    kMenuTagDeleteSelected,
    kMenuTagEditMeta,
    // View
    kMenuTagToggleUI,
    kMenuTagToggleFullscreen,
    kMenuTagToggleBezierWires,
    kMenuTagToggleShowParamWires,
    kMenuTagToggleAnalysis,
    kMenuTagToggleSessionGrid,
    kMenuTagToggleMidiMap,
    // Insert
    kMenuTagAddNode,
};

@interface VividMenuDelegate : NSObject
@property (nonatomic, assign) vivid::MenuCallbacks callbacks;
- (void)menuAction:(NSMenuItem*)sender;
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
        case kMenuTagDeleteSelected:    if (_callbacks.on_delete_selected) _callbacks.on_delete_selected(); break;
        case kMenuTagEditMeta:          if (_callbacks.on_edit_meta) _callbacks.on_edit_meta(); break;
        case kMenuTagToggleUI:          if (_callbacks.on_toggle_ui) _callbacks.on_toggle_ui(); break;
        case kMenuTagToggleFullscreen:  if (_callbacks.on_toggle_fullscreen) _callbacks.on_toggle_fullscreen(); break;
        case kMenuTagToggleBezierWires: if (_callbacks.on_toggle_bezier_wires) _callbacks.on_toggle_bezier_wires(); break;
        case kMenuTagToggleShowParamWires: if (_callbacks.on_toggle_show_param_wires) _callbacks.on_toggle_show_param_wires(); break;
        case kMenuTagToggleAnalysis: if (_callbacks.on_toggle_analysis) _callbacks.on_toggle_analysis(); break;
        case kMenuTagToggleSessionGrid: if (_callbacks.on_toggle_session_grid) _callbacks.on_toggle_session_grid(); break;
        case kMenuTagToggleMidiMap:     if (_callbacks.on_toggle_midi_map) _callbacks.on_toggle_midi_map(); break;
        case kMenuTagAddNode:           if (_callbacks.on_add_node) _callbacks.on_add_node(); break;
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    switch (item.tag) {
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

namespace vivid {

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
                initWithTitle:@"About Vivid"
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
                    initWithTitle:@"Check for Updates..."
                           action:@selector(menuAction:)
                    keyEquivalent:@""];
                checkUpdatesItem.target = sDelegate;
                checkUpdatesItem.tag = kMenuTagCheckForUpdates;
                [appMenu insertItem:checkUpdatesItem atIndex:servicesIdx];

                NSMenuItem* autoCheckUpdatesItem = [[NSMenuItem alloc]
                    initWithTitle:@"Automatically Check for Updates"
                           action:@selector(menuAction:)
                    keyEquivalent:@""];
                autoCheckUpdatesItem.target = sDelegate;
                autoCheckUpdatesItem.tag = kMenuTagToggleAutoCheckUpdates;
                [appMenu insertItem:autoCheckUpdatesItem atIndex:servicesIdx];

                NSMenuItem* prefsItem = [[NSMenuItem alloc]
                    initWithTitle:@"Preferences..."
                           action:@selector(menuAction:)
                    keyEquivalent:@","];
                prefsItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
                prefsItem.target = sDelegate;
                prefsItem.tag = kMenuTagPreferences;
                [appMenu insertItem:prefsItem atIndex:servicesIdx];
            }
        }

        // --- Create "File" menu and insert at index 1 ---
        NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];

        NSMenuItem* newItem = [[NSMenuItem alloc]
            initWithTitle:@"New"
                   action:@selector(menuAction:)
            keyEquivalent:@"n"];
        newItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        newItem.target = sDelegate;
        newItem.tag = kMenuTagNew;
        [fileMenu addItem:newItem];

        NSMenuItem* newProjectItem = [[NSMenuItem alloc]
            initWithTitle:@"New Project..."
                   action:@selector(menuAction:)
            keyEquivalent:@"N"];
        newProjectItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        newProjectItem.target = sDelegate;
        newProjectItem.tag = kMenuTagNewProject;
        [fileMenu addItem:newProjectItem];
        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* openItem = [[NSMenuItem alloc]
            initWithTitle:@"Open..."
                   action:@selector(menuAction:)
            keyEquivalent:@"o"];
        openItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        openItem.target = sDelegate;
        openItem.tag = kMenuTagOpen;
        [fileMenu addItem:openItem];

        NSMenuItem* openExampleItem = [[NSMenuItem alloc]
            initWithTitle:@"Open Example..."
                   action:@selector(menuAction:)
            keyEquivalent:@"O"];
        openExampleItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        openExampleItem.target = sDelegate;
        openExampleItem.tag = kMenuTagOpenExample;
        [fileMenu addItem:openExampleItem];

        NSMenuItem* openGraphFolderItem = [[NSMenuItem alloc]
            initWithTitle:@"Open Graph Folder"
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        openGraphFolderItem.target = sDelegate;
        openGraphFolderItem.tag = kMenuTagOpenGraphFolder;
        [fileMenu addItem:openGraphFolderItem];

        NSMenuItem* saveItem = [[NSMenuItem alloc]
            initWithTitle:@"Save"
                   action:@selector(menuAction:)
            keyEquivalent:@"s"];
        saveItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        saveItem.target = sDelegate;
        saveItem.tag = kMenuTagSave;
        [fileMenu addItem:saveItem];

        NSMenuItem* saveAsItem = [[NSMenuItem alloc]
            initWithTitle:@"Save As..."
                   action:@selector(menuAction:)
            keyEquivalent:@"S"];
        saveAsItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        saveAsItem.target = sDelegate;
        saveAsItem.tag = kMenuTagSaveAs;
        [fileMenu addItem:saveAsItem];

        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* exportItem = [[NSMenuItem alloc]
            initWithTitle:@"Export Standalone..."
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        exportItem.target = sDelegate;
        exportItem.tag = kMenuTagExport;
        [fileMenu addItem:exportItem];

        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* browseItem = [[NSMenuItem alloc]
            initWithTitle:@"Browse Packages..."
                   action:@selector(menuAction:)
            keyEquivalent:@"P"];
        browseItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        browseItem.target = sDelegate;
        browseItem.tag = kMenuTagBrowsePackages;
        [fileMenu addItem:browseItem];

        NSMenuItem* openCatalogSiteItem = [[NSMenuItem alloc]
            initWithTitle:@"Open Package Catalog Website..."
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        openCatalogSiteItem.target = sDelegate;
        openCatalogSiteItem.tag = kMenuTagOpenPackageCatalogWebsite;
        [fileMenu addItem:openCatalogSiteItem];

        NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File"
                                                             action:nil
                                                      keyEquivalent:@""];
        [fileMenuItem setSubmenu:fileMenu];
        [mainMenu insertItem:fileMenuItem atIndex:1];

        // --- Create "Edit" menu and insert at index 2 ---
        NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];

        NSMenuItem* deleteItem = [[NSMenuItem alloc]
            initWithTitle:@"Delete Selected"
                   action:@selector(menuAction:)
            keyEquivalent:[NSString stringWithFormat:@"%C", (unichar)NSBackspaceCharacter]];
        deleteItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        deleteItem.target = sDelegate;
        deleteItem.tag = kMenuTagDeleteSelected;
        [editMenu addItem:deleteItem];

        NSMenuItem* editMetaItem = [[NSMenuItem alloc]
            initWithTitle:@"Edit Meta..."
                   action:@selector(menuAction:)
            keyEquivalent:@"e"];
        editMetaItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        editMetaItem.target = sDelegate;
        editMetaItem.tag = kMenuTagEditMeta;
        [editMenu addItem:editMetaItem];

        NSMenuItem* editMenuItem = [[NSMenuItem alloc] initWithTitle:@"Edit"
                                                              action:nil
                                                       keyEquivalent:@""];
        [editMenuItem setSubmenu:editMenu];
        [mainMenu insertItem:editMenuItem atIndex:2];

        // --- Create "View" menu and insert at index 3 ---
        NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];

        NSMenuItem* toggleUIItem = [[NSMenuItem alloc]
            initWithTitle:@"Toggle Graph UI"
                   action:@selector(menuAction:)
            keyEquivalent:@"`"];
        toggleUIItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleUIItem.target = sDelegate;
        toggleUIItem.tag = kMenuTagToggleUI;
        [viewMenu addItem:toggleUIItem];

        NSMenuItem* toggleFullscreenItem = [[NSMenuItem alloc]
            initWithTitle:@"Toggle Fullscreen"
                   action:@selector(menuAction:)
            keyEquivalent:@"f"];
        toggleFullscreenItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagControl;
        toggleFullscreenItem.target = sDelegate;
        toggleFullscreenItem.tag = kMenuTagToggleFullscreen;
        [viewMenu addItem:toggleFullscreenItem];

        NSMenuItem* toggleBezierItem = [[NSMenuItem alloc]
            initWithTitle:@"Toggle Bezier Wires"
                   action:@selector(menuAction:)
            keyEquivalent:@"b"];
        toggleBezierItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleBezierItem.target = sDelegate;
        toggleBezierItem.tag = kMenuTagToggleBezierWires;
        [viewMenu addItem:toggleBezierItem];

        NSMenuItem* toggleParamWiresItem = [[NSMenuItem alloc]
            initWithTitle:@"Show Param Wires"
                   action:@selector(menuAction:)
            keyEquivalent:@"p"];
        toggleParamWiresItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleParamWiresItem.target = sDelegate;
        toggleParamWiresItem.tag = kMenuTagToggleShowParamWires;
        [viewMenu addItem:toggleParamWiresItem];

        NSMenuItem* toggleAnalysisItem = [[NSMenuItem alloc]
            initWithTitle:@"Show Analysis"
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        toggleAnalysisItem.target = sDelegate;
        toggleAnalysisItem.tag = kMenuTagToggleAnalysis;
        [viewMenu addItem:toggleAnalysisItem];

        NSMenuItem* toggleGridItem = [[NSMenuItem alloc]
            initWithTitle:@"Toggle Session Grid"
                   action:@selector(menuAction:)
            keyEquivalent:@"g"];
        toggleGridItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        toggleGridItem.target = sDelegate;
        toggleGridItem.tag = kMenuTagToggleSessionGrid;
        [viewMenu addItem:toggleGridItem];

        NSMenuItem* toggleMidiItem = [[NSMenuItem alloc]
            initWithTitle:@"Toggle MIDI Map"
                   action:@selector(menuAction:)
            keyEquivalent:@"m"];
        toggleMidiItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
        toggleMidiItem.target = sDelegate;
        toggleMidiItem.tag = kMenuTagToggleMidiMap;
        [viewMenu addItem:toggleMidiItem];

        NSMenuItem* viewMenuItem = [[NSMenuItem alloc] initWithTitle:@"View"
                                                              action:nil
                                                       keyEquivalent:@""];
        [viewMenuItem setSubmenu:viewMenu];
        [mainMenu insertItem:viewMenuItem atIndex:3];

        // --- Create "Insert" menu and insert at index 4 ---
        NSMenu* insertMenu = [[NSMenu alloc] initWithTitle:@"Insert"];

        NSMenuItem* addNodeItem = [[NSMenuItem alloc]
            initWithTitle:@"Add Node..."
                   action:@selector(menuAction:)
            keyEquivalent:@"t"];
        addNodeItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        addNodeItem.target = sDelegate;
        addNodeItem.tag = kMenuTagAddNode;
        [insertMenu addItem:addNodeItem];

        NSMenuItem* insertMenuItem = [[NSMenuItem alloc] initWithTitle:@"Insert"
                                                                action:nil
                                                         keyEquivalent:@""];
        [insertMenuItem setSubmenu:insertMenu];
        [mainMenu insertItem:insertMenuItem atIndex:4];

        // --- Create "Help" menu and append ---
        NSMenu* helpMenu = [[NSMenu alloc] initWithTitle:@"Help"];

        NSMenuItem* reportIssueItem = [[NSMenuItem alloc]
            initWithTitle:@"Report an Issue..."
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        reportIssueItem.target = sDelegate;
        reportIssueItem.tag = kMenuTagReportIssue;
        [helpMenu addItem:reportIssueItem];

        NSMenuItem* helpMenuItem = [[NSMenuItem alloc] initWithTitle:@"Help"
                                                              action:nil
                                                       keyEquivalent:@""];
        [helpMenuItem setSubmenu:helpMenu];
        [mainMenu addItem:helpMenuItem];
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
