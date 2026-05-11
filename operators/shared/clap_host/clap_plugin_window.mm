#import <Cocoa/Cocoa.h>
#include "shared/clap_host/clap_plugin_window.h"

@interface VividClapWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) BOOL closed;
@end

@implementation VividClapWindowDelegate
- (void)windowWillClose:(NSNotification*)notification {
    (void)notification;
    self.closed = YES;
}
@end

struct ClapPluginWindow {
    const clap_plugin_t*      plugin;
    const clap_plugin_gui_t*  gui_ext;
    NSWindow* __strong        ns_win;
    VividClapWindowDelegate* __strong delegate;
};

ClapPluginWindow* clap_plugin_window_open(const clap_plugin_t* plugin,
                                           const clap_plugin_gui_t* gui_ext,
                                           const char* title) {
    if (!plugin || !gui_ext) return nullptr;

    @autoreleasepool {
        if (!gui_ext->is_api_supported(plugin, CLAP_WINDOW_API_COCOA, false))
            return nullptr;

        if (!gui_ext->create(plugin, CLAP_WINDOW_API_COCOA, false))
            return nullptr;

        uint32_t w = 800, h = 600;
        gui_ext->get_size(plugin, &w, &h);

        NSRect content = NSMakeRect(0, 0, (CGFloat)w, (CGFloat)h);
        NSWindowStyleMask style = NSWindowStyleMaskTitled
                                | NSWindowStyleMaskClosable
                                | NSWindowStyleMaskMiniaturizable;
        NSWindow* win = [[NSWindow alloc] initWithContentRect:content
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        NSString* nsTitle = title ? [NSString stringWithUTF8String:title] : @"Plugin";
        [win setTitle:nsTitle];
        [win center];

        VividClapWindowDelegate* delegate = [[VividClapWindowDelegate alloc] init];
        [win setDelegate:delegate];

        clap_window_t cw;
        cw.api   = CLAP_WINDOW_API_COCOA;
        cw.cocoa = (__bridge clap_nsview)[win contentView];

        if (!gui_ext->set_parent(plugin, &cw)) {
            gui_ext->destroy(plugin);
            return nullptr;
        }

        gui_ext->set_size(plugin, w, h);
        gui_ext->show(plugin);
        [win makeKeyAndOrderFront:nil];

        ClapPluginWindow* handle = new ClapPluginWindow();
        handle->plugin   = plugin;
        handle->gui_ext  = gui_ext;
        handle->ns_win   = win;
        handle->delegate = delegate;
        return handle;
    }
}

bool clap_plugin_window_is_open(const ClapPluginWindow* win) {
    if (!win) return false;
    return !win->delegate.closed;
}

void clap_plugin_window_close(ClapPluginWindow* win) {
    if (!win) return;
    @autoreleasepool {
        win->gui_ext->hide(win->plugin);
        win->gui_ext->destroy(win->plugin);
        [win->ns_win setDelegate:nil];
        if (win->ns_win.isVisible)
            [win->ns_win close];
        win->ns_win   = nil;
        win->delegate = nil;
    }
    delete win;
}
