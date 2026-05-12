#import <Cocoa/Cocoa.h>
#include "shared/vst3_host/vst3_plugin_window.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// ---------------------------------------------------------------------------
// IPlugFrame implementation — lets the plugin request a window resize.
// ---------------------------------------------------------------------------

class Vst3PlugFrame : public IPlugFrame {
    NSWindow* __unsafe_unretained win_;
public:
    explicit Vst3PlugFrame(NSWindow* w) : win_(w) {}
    virtual ~Vst3PlugFrame() = default;

    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        if (std::memcmp(_iid, IPlugFrame::iid, sizeof(TUID)) == 0) {
            *obj = this; return kResultOk;
        }
        if (obj) *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef()  override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* newSize) override {
        if (!view || !newSize || !win_) return kInvalidArgument;
        NSRect frame = [win_ frame];
        CGFloat new_h = static_cast<CGFloat>(newSize->bottom - newSize->top);
        CGFloat new_w = static_cast<CGFloat>(newSize->right  - newSize->left);
        CGFloat delta_h = new_h - [win_ contentLayoutRect].size.height;
        frame.size.width  = new_w;
        frame.size.height += delta_h;
        frame.origin.y    -= delta_h;
        [win_ setFrame:frame display:YES animate:NO];
        view->onSize(newSize);
        return kResultOk;
    }
};

// ---------------------------------------------------------------------------
// Window delegate — tracks close events.
// ---------------------------------------------------------------------------

@interface VividVst3WindowDelegate : NSObject <NSWindowDelegate>
@property (assign) BOOL closed;
@end

@implementation VividVst3WindowDelegate
- (void)windowWillClose:(NSNotification*)n { (void)n; self.closed = YES; }
@end

// ---------------------------------------------------------------------------
// Vst3PluginWindow
// ---------------------------------------------------------------------------

struct Vst3PluginWindow {
    IPlugView*                 plug_view  = nullptr;
    Vst3PlugFrame*             plug_frame = nullptr;
    NSWindow* __strong         ns_win;
    VividVst3WindowDelegate* __strong delegate;
};

Vst3PluginWindow* vst3_plugin_window_open(IEditController* controller, const char* title) {
    if (!controller) return nullptr;

    IPlugView* view = controller->createView(ViewType::kEditor);
    if (!view) return nullptr;

    if (view->isPlatformTypeSupported(kPlatformTypeNSView) != kResultOk) {
        view->release();
        return nullptr;
    }

    @autoreleasepool {
        ViewRect rect{};
        view->getSize(&rect);
        CGFloat w = rect.right  - rect.left;
        CGFloat h = rect.bottom - rect.top;
        if (w < 100) w = 800;
        if (h < 100) h = 600;

        NSRect content = NSMakeRect(0, 0, w, h);
        NSWindowStyleMask style = NSWindowStyleMaskTitled
                                | NSWindowStyleMaskClosable
                                | NSWindowStyleMaskMiniaturizable
                                | NSWindowStyleMaskResizable;
        NSWindow* win = [[NSWindow alloc] initWithContentRect:content
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        [win setTitle:title ? [NSString stringWithUTF8String:title] : @"VST3 Plugin"];
        [win center];

        VividVst3WindowDelegate* delegate = [[VividVst3WindowDelegate alloc] init];
        [win setDelegate:delegate];

        Vst3PlugFrame* plug_frame = new Vst3PlugFrame(win);
        view->setFrame(plug_frame);

        if (view->attached((__bridge void*)[win contentView], kPlatformTypeNSView) != kResultOk) {
            view->setFrame(nullptr);
            delete plug_frame;
            view->release();
            return nullptr;
        }

        // Notify of DPI scale if supported
        IPlugViewContentScaleSupport* scale = nullptr;
        if (view->queryInterface(IPlugViewContentScaleSupport::iid, (void**)&scale) == kResultOk && scale) {
            CGFloat sf = [win backingScaleFactor];
            scale->setContentScaleFactor(static_cast<IPlugViewContentScaleSupport::ScaleFactor>(sf));
            scale->release();
        }

        [win makeKeyAndOrderFront:nil];

        auto* handle      = new Vst3PluginWindow();
        handle->plug_view  = view;
        handle->plug_frame = plug_frame;
        handle->ns_win     = win;
        handle->delegate   = delegate;
        return handle;
    }
}

bool vst3_plugin_window_is_open(const Vst3PluginWindow* win) {
    return win && !win->delegate.closed;
}

void vst3_plugin_window_close(Vst3PluginWindow* win) {
    if (!win) return;
    @autoreleasepool {
        if (win->plug_view) {
            win->plug_view->removed();
            win->plug_view->setFrame(nullptr);
            win->plug_view->release();
            win->plug_view = nullptr;
        }
        delete win->plug_frame;
        win->plug_frame = nullptr;
        [win->ns_win setDelegate:nil];
        if (win->ns_win.isVisible)
            [win->ns_win close];
        win->ns_win   = nil;
        win->delegate = nil;
    }
    delete win;
}
