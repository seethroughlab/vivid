/**
 * @file vivid_c_window.mm
 * @brief macOS-specific window surface creation for vivid C API
 */

#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
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

        // Create a Metal layer
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        metalLayer.contentsScale = window.backingScaleFactor;

        // Attach the Metal layer to the window's content view
        NSView* contentView = [window contentView];
        [contentView setWantsLayer:YES];
        [contentView setLayer:metalLayer];

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

} // extern "C"

#endif // __APPLE__
