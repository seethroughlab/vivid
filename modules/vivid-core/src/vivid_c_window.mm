/**
 * @file vivid_c_window.mm
 * @brief macOS-specific window surface creation for vivid C API
 */

#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>

#include <webgpu/webgpu.h>

extern "C" {

/**
 * Create a WGPUSurface from an NSWindow pointer
 * @param instance WGPUInstance
 * @param ns_window NSWindow* cast to void*
 * @return WGPUSurface or nullptr on failure
 */
WGPUSurface vivid_create_surface_from_nswindow(WGPUInstance instance, void* ns_window) {
    if (!instance || !ns_window) {
        return nullptr;
    }

    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)ns_window;
        NSView* contentView = [window contentView];

        // Create a Metal layer
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        metalLayer.contentsScale = window.backingScaleFactor;
        metalLayer.frame = contentView.bounds;

        // Disable vsync to avoid timing issues during video playback
        metalLayer.displaySyncEnabled = NO;

        // Allow transparency for compositing with WebView
        metalLayer.opaque = NO;

        // Don't use presentsWithTransaction - it can cause issues with wgpu
        // when the surface is acquired during Core Animation callbacks
        metalLayer.presentsWithTransaction = NO;

        // Check if this is a WebView-based app (Tauri/Electron)
        // In that case, create a dedicated view for Metal rendering behind the WebView
        if (contentView.layer != nil) {
            // Create a dedicated view for the Metal layer
            NSView* metalView = [[NSView alloc] initWithFrame:contentView.bounds];
            metalView.wantsLayer = YES;
            metalView.layer = metalLayer;
            metalView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

            // Add the Metal view behind all existing subviews (including WebView)
            [contentView addSubview:metalView positioned:NSWindowBelow relativeTo:nil];
        } else {
            // Standalone mode: use Metal layer directly as the content view's layer
            [contentView setWantsLayer:YES];
            [contentView setLayer:metalLayer];
        }

        // Create surface from the Metal layer
        WGPUSurfaceSourceMetalLayer fromMetalLayer = {};
        fromMetalLayer.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        fromMetalLayer.chain.next = nullptr;
        fromMetalLayer.layer = metalLayer;

        WGPUSurfaceDescriptor surfaceDescriptor = {};
        surfaceDescriptor.nextInChain = &fromMetalLayer.chain;
        surfaceDescriptor.label = (WGPUStringView){ nullptr, WGPU_STRLEN };

        return wgpuInstanceCreateSurface(instance, &surfaceDescriptor);
    }
}

/**
 * Get the content scale factor (for Retina displays)
 * @param ns_window NSWindow* cast to void*
 * @return Scale factor (1.0 for standard, 2.0 for Retina)
 */
float vivid_get_window_scale_factor(void* ns_window) {
    if (!ns_window) {
        return 1.0f;
    }

    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)ns_window;
        return static_cast<float>(window.backingScaleFactor);
    }
}

/**
 * Get window content size in pixels
 * @param ns_window NSWindow* cast to void*
 * @param out_width Output width in pixels
 * @param out_height Output height in pixels
 */
void vivid_get_window_size(void* ns_window, int* out_width, int* out_height) {
    if (!ns_window || !out_width || !out_height) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
        return;
    }

    @autoreleasepool {
        NSWindow* window = (__bridge NSWindow*)ns_window;
        NSRect frame = [[window contentView] frame];
        CGFloat scale = window.backingScaleFactor;

        *out_width = static_cast<int>(frame.size.width * scale);
        *out_height = static_cast<int>(frame.size.height * scale);
    }
}

/**
 * Begin a Core Animation transaction for frame presentation
 * Call before wgpuSurfacePresent when using presentsWithTransaction
 */
void vivid_begin_frame_transaction() {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
}

/**
 * Commit the Core Animation transaction after frame presentation
 * Call after wgpuSurfacePresent when using presentsWithTransaction
 */
void vivid_commit_frame_transaction() {
    [CATransaction commit];
}

} // extern "C"

#endif // __APPLE__
