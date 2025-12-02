// Linux-specific surface creation using X11/Wayland
#include "platform_surface.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Try X11 first, fall back to Wayland if needed
#if defined(GLFW_EXPOSE_NATIVE_X11)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#endif

#include <iostream>

namespace vivid {

WGPUSurface createSurfaceForWindow(WGPUInstance instance, GLFWwindow* window) {
    if (!instance || !window) {
        return nullptr;
    }

#if defined(GLFW_EXPOSE_NATIVE_X11)
    // Try X11 first
    Display* display = glfwGetX11Display();
    Window x11Window = glfwGetX11Window(window);

    if (display && x11Window) {
        WGPUSurfaceSourceXlibWindow x11Source = {};
        x11Source.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
        x11Source.chain.next = nullptr;
        x11Source.display = display;
        x11Source.window = x11Window;

        WGPUSurfaceDescriptor surfaceDesc = {};
        surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&x11Source);
        surfaceDesc.label = WGPUStringView{.data = "VividSurface", .length = 12};

        return wgpuInstanceCreateSurface(instance, &surfaceDesc);
    }
#endif

    std::cerr << "[platform_surface_linux] No supported windowing system found\n";
    return nullptr;
}

} // namespace vivid
