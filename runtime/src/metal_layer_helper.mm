// Metal layer helper for Diligent Engine
// Extracts NSView from GLFW window for Vulkan/Metal initialization

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

// Get Metal layer from GLFW window (for WebGPU surface creation)
extern "C" void* getMetalLayerFromGLFW(GLFWwindow* window) {
    if (!window) {
        return nullptr;
    }

    // Get the Cocoa window from GLFW
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow) {
        return nullptr;
    }

    // Get or create Metal layer
    NSView* view = [nsWindow contentView];
    [view setWantsLayer:YES];

    CALayer* layer = [view layer];
    if (![layer isKindOfClass:[CAMetalLayer class]]) {
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        [view setLayer:metalLayer];
        layer = metalLayer;
    }

    return (__bridge void*)layer;
}

// Get NSView from GLFW window (for Diligent's MacOSNativeWindow.pNSView)
// This is what Diligent expects - it will get the CAMetalLayer internally
extern "C" void* getNSViewFromGLFW(GLFWwindow* window) {
    if (!window) {
        return nullptr;
    }

    // Get the Cocoa window from GLFW
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow) {
        return nullptr;
    }

    // Get content view and ensure it uses layer backing with Metal layer
    NSView* view = [nsWindow contentView];
    [view setWantsLayer:YES];

    // Ensure it's a CAMetalLayer
    CALayer* layer = [view layer];
    if (![layer isKindOfClass:[CAMetalLayer class]]) {
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        [view setLayer:metalLayer];
    }

    return (__bridge void*)view;
}
