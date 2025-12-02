// Windows-specific surface creation using Direct3D 12 / HWND
#include "platform_surface.h"

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <windows.h>

namespace vivid {

WGPUSurface createSurfaceForWindow(WGPUInstance instance, GLFWwindow* window) {
    if (!instance || !window) {
        return nullptr;
    }

    // Get the Win32 window handle from GLFW
    HWND hwnd = glfwGetWin32Window(window);
    if (!hwnd) {
        return nullptr;
    }

    // Get the HINSTANCE for the window
    HINSTANCE hinstance = GetModuleHandle(nullptr);

    // Create WebGPU surface from Windows HWND
    WGPUSurfaceSourceWindowsHWND windowsSource = {};
    windowsSource.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    windowsSource.chain.next = nullptr;
    windowsSource.hinstance = hinstance;
    windowsSource.hwnd = hwnd;

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&windowsSource);
    surfaceDesc.label = WGPUStringView{.data = "VividSurface", .length = 12};

    return wgpuInstanceCreateSurface(instance, &surfaceDesc);
}

} // namespace vivid
