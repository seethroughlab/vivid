// Vivid Runtime - Main Entry Point

#include "vivid/vivid.h"
#include <iostream>
#include <chrono>

// Platform-specific defines for GLFW native access
#if defined(PLATFORM_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32 1
#elif defined(PLATFORM_LINUX)
    #define GLFW_EXPOSE_NATIVE_X11 1
#elif defined(PLATFORM_MACOS)
    #define GLFW_EXPOSE_NATIVE_COCOA 1
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// Undef Windows macros that conflict with our code
#ifdef GetObject
    #undef GetObject
#endif
#ifdef CreateWindow
    #undef CreateWindow
#endif

namespace vivid {

// Simple test render - clears screen with cycling color
void runTestMode(Context& ctx) {
    std::cout << "Vivid Runtime v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << std::endl;
    std::cout << "Running in test mode - press ESC to exit" << std::endl;

    while (!ctx.shouldClose()) {
        ctx.pollEvents();
        ctx.beginFrame();

        // Clear with a cycling color based on time
        float t = ctx.time();
        float r = 0.5f + 0.5f * std::sin(t * 0.5f);
        float g = 0.5f + 0.5f * std::sin(t * 0.7f + 2.0f);
        float b = 0.5f + 0.5f * std::sin(t * 1.1f + 4.0f);

        // The actual clear happens in beginFrame/endFrame

        ctx.endFrame();
    }
}

} // namespace vivid

int main(int argc, char** argv) {
    std::cout << "Starting Vivid..." << std::endl;

    vivid::Context ctx;

    // Initialize with default window
    if (!ctx.init(1280, 720, "Vivid")) {
        std::cerr << "Failed to initialize Vivid context" << std::endl;
        return -1;
    }

    std::cout << "Context initialized successfully" << std::endl;

    // For now, run in test mode
    // TODO: Load chain.cpp dynamically
    vivid::runTestMode(ctx);

    ctx.shutdown();

    std::cout << "Vivid shutdown complete" << std::endl;
    return 0;
}
