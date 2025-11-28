// macOS-specific surface creation using Metal
#include "platform_surface.h"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <QuartzCore/CAMetalLayer.h>
#import <Cocoa/Cocoa.h>

namespace vivid {

WGPUSurface createSurfaceForWindow(WGPUInstance instance, GLFWwindow* window) {
    if (!instance || !window) {
        return nullptr;
    }

    // Get the Cocoa window from GLFW
    NSWindow* nsWindow = glfwGetCocoaWindow(window);
    if (!nsWindow) {
        return nullptr;
    }

    // Get or create Metal layer
    NSView* view = [nsWindow contentView];

    // GLFW with GLFW_NO_API should have already set up a Metal layer
    // but we need to ensure the view uses layer backing
    [view setWantsLayer:YES];

    // Get or create a CAMetalLayer
    CALayer* layer = [view layer];
    if (![layer isKindOfClass:[CAMetalLayer class]]) {
        // Create a new Metal layer if one doesn't exist
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        [view setLayer:metalLayer];
        layer = metalLayer;
    }

    // Create WebGPU surface from Metal layer
    WGPUSurfaceSourceMetalLayer metalSource = {};
    metalSource.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metalSource.chain.next = nullptr;
    metalSource.layer = layer;

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&metalSource);
    surfaceDesc.label = WGPUStringView{.data = "VividSurface", .length = 12};

    return wgpuInstanceCreateSurface(instance, &surfaceDesc);
}

} // namespace vivid
