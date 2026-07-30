#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "clap_plugin_window.h"
#include "audio/clap_host.h"
#include <clap/ext/gui.h>

using vivid::session::ClapHandle;

// ---------------------------------------------------------------------------
// Window delegate — tracks close events (the Cocoa peer of clap_host_gui.closed).
// ---------------------------------------------------------------------------
@interface VividClapWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) BOOL closed;
@end

@implementation VividClapWindowDelegate
- (void)windowWillClose:(NSNotification*)n { (void)n; self.closed = YES; }
@end

// ---------------------------------------------------------------------------
// ClapPluginWindow
// ---------------------------------------------------------------------------
struct ClapPluginWindow {
    const clap_plugin_t*     plugin = nullptr;
    const clap_plugin_gui_t* gui    = nullptr;
    NSWindow* __strong                ns_win;
    VividClapWindowDelegate* __strong delegate;
    bool gui_created = false;
};

ClapPluginWindow* clap_plugin_window_open(ClapHandle* handle, const char* title) {
    if (!handle || !handle->plugin || !handle->ext_gui) return nullptr;
    const clap_plugin_t*     plugin = handle->plugin;
    const clap_plugin_gui_t* gui    = handle->ext_gui;

    // Cocoa is a floating=false embedded API (the header says: uses logical size, don't call set_scale).
    if (!gui->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, /*is_floating*/ false)) return nullptr;

    @autoreleasepool {
        if (!gui->create(plugin, CLAP_WINDOW_API_COCOA, /*is_floating*/ false)) return nullptr;

        uint32_t gw = 0, gh = 0;
        if (!gui->get_size(plugin, &gw, &gh) || gw < 100 || gh < 100) { gw = 800; gh = 600; }

        NSRect content = NSMakeRect(0, 0, static_cast<CGFloat>(gw), static_cast<CGFloat>(gh));
        NSWindowStyleMask style = NSWindowStyleMaskTitled
                                | NSWindowStyleMaskClosable
                                | NSWindowStyleMaskMiniaturizable
                                | NSWindowStyleMaskResizable;
        NSWindow* win = [[NSWindow alloc] initWithContentRect:content
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        [win setTitle:title ? [NSString stringWithUTF8String:title] : @"CLAP Plugin"];
        [win center];

        VividClapWindowDelegate* delegate = [[VividClapWindowDelegate alloc] init];
        [win setDelegate:delegate];

        // Layer-back + show before set_parent so a valid backing store exists when the plugin
        // attaches its view (mirrors the VST3 window's ordering).
        [win.contentView setWantsLayer:YES];
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];

        clap_window_t cw{};
        cw.api   = CLAP_WINDOW_API_COCOA;
        cw.cocoa = (__bridge void*)[win contentView];
        if (!gui->set_parent(plugin, &cw)) {
            gui->destroy(plugin);
            [win setDelegate:nil];
            [win close];
            return nullptr;
        }
        gui->show(plugin);

        auto* wnd = new ClapPluginWindow();
        wnd->plugin      = plugin;
        wnd->gui         = gui;
        wnd->ns_win      = win;
        wnd->delegate    = delegate;
        wnd->gui_created = true;
        return wnd;
    }
}

bool clap_plugin_window_is_open(const ClapPluginWindow* win) {
    return win && !win->delegate.closed;
}

void clap_plugin_window_close(ClapPluginWindow* win) {
    if (!win) return;
    @autoreleasepool {
        if (win->gui_created && win->gui && win->plugin) {
            win->gui->hide(win->plugin);
            win->gui->destroy(win->plugin);   // MUST precede ClapHandle::destroy()
        }
        win->gui_created = false;
        [win->ns_win setDelegate:nil];
        if (win->ns_win.isVisible)
            [win->ns_win close];
        win->ns_win   = nil;
        win->delegate = nil;
    }
    delete win;
}
#endif  // __APPLE__
