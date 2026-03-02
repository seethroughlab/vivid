#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "runtime/macos_menu.h"

// Tag values for menu items
enum MenuTag : NSInteger {
    kMenuTagOpen = 1,
    kMenuTagSave,
    kMenuTagPreferences,
    kMenuTagExport,
};

@interface VividMenuDelegate : NSObject
@property (nonatomic, assign) vivid::MenuCallbacks callbacks;
- (void)menuAction:(NSMenuItem*)sender;
@end

@implementation VividMenuDelegate

- (void)menuAction:(NSMenuItem*)sender {
    switch (sender.tag) {
        case kMenuTagOpen:        if (_callbacks.on_open)        _callbacks.on_open();        break;
        case kMenuTagSave:        if (_callbacks.on_save)        _callbacks.on_save();        break;
        case kMenuTagPreferences: if (_callbacks.on_preferences) _callbacks.on_preferences(); break;
        case kMenuTagExport:      if (_callbacks.on_export)      _callbacks.on_export();      break;
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
                // Insert separator + Preferences before Services
                [appMenu insertItem:[NSMenuItem separatorItem] atIndex:servicesIdx];
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

        NSMenuItem* openItem = [[NSMenuItem alloc]
            initWithTitle:@"Open..."
                   action:@selector(menuAction:)
            keyEquivalent:@"o"];
        openItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        openItem.target = sDelegate;
        openItem.tag = kMenuTagOpen;
        [fileMenu addItem:openItem];

        NSMenuItem* saveItem = [[NSMenuItem alloc]
            initWithTitle:@"Save"
                   action:@selector(menuAction:)
            keyEquivalent:@"s"];
        saveItem.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        saveItem.target = sDelegate;
        saveItem.tag = kMenuTagSave;
        [fileMenu addItem:saveItem];

        [fileMenu addItem:[NSMenuItem separatorItem]];

        NSMenuItem* exportItem = [[NSMenuItem alloc]
            initWithTitle:@"Export Standalone..."
                   action:@selector(menuAction:)
            keyEquivalent:@""];
        exportItem.target = sDelegate;
        exportItem.tag = kMenuTagExport;
        [fileMenu addItem:exportItem];

        NSMenuItem* fileMenuItem = [[NSMenuItem alloc] initWithTitle:@"File"
                                                             action:nil
                                                      keyEquivalent:@""];
        [fileMenuItem setSubmenu:fileMenu];
        [mainMenu insertItem:fileMenuItem atIndex:1];
    }
}

}  // namespace vivid

#endif  // __APPLE__
