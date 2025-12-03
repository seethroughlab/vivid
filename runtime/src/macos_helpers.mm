#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

extern "C" {

void* getContentViewFromWindow(void* nsWindow) {
    if (!nsWindow) return nullptr;

    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)nsWindow;
        NSView* contentView = [window contentView];

        if (!contentView) return nullptr;

        // Make the view layer-backed and set a CAMetalLayer
        [contentView setWantsLayer:YES];

        // Check if layer is already CAMetalLayer
        if (![contentView.layer isKindOfClass:[CAMetalLayer class]]) {
            // Create and set a CAMetalLayer
            CAMetalLayer* metalLayer = [CAMetalLayer layer];
            metalLayer.contentsScale = [[window screen] backingScaleFactor];
            metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            metalLayer.framebufferOnly = YES;

            // Replace the layer
            [contentView setLayer:metalLayer];
        }

        return (__bridge void*)contentView;
    }
}

} // extern "C"

#endif // __APPLE__
