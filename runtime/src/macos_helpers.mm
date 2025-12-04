// macOS-specific helpers for Vivid

#define GLFW_EXPOSE_NATIVE_COCOA 1

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

// Get NSView with CAMetalLayer attached
// MoltenVK implements Vulkan on top of Metal, so we need CAMetalLayer
void* GetNSWindowView(GLFWwindow* wnd) {
    id window = glfwGetCocoaWindow(wnd);
    id view = [window contentView];

    // Load QuartzCore framework dynamically
    NSBundle* bundle = [NSBundle bundleWithPath:@"/System/Library/Frameworks/QuartzCore.framework"];
    if (!bundle) {
        return nullptr;
    }

    // Create a CAMetalLayer
    id layer = [[bundle classNamed:@"CAMetalLayer"] layer];
    if (!layer) {
        return nullptr;
    }

    // Attach the layer to the view
    [view setLayer:layer];
    [view setWantsLayer:YES];

    // Handle retina display scaling
    CGSize viewScale = [view convertSizeToBacking:CGSizeMake(1.0, 1.0)];
    [layer setContentsScale:MIN(viewScale.width, viewScale.height)];

    return view;
}
