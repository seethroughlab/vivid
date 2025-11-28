// Vivid Runtime - Entry Point
// Phase 3.3: Shader System test

#include "window.h"
#include "renderer.h"
#include <iostream>
#include <cmath>
#include <chrono>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [project_path] [options]\n"
              << "\nOptions:\n"
              << "  --width <n>     Window width (default: 1280)\n"
              << "  --height <n>    Window height (default: 720)\n"
              << "  --fullscreen    Start in fullscreen mode\n"
              << "  --help          Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::cout << "Vivid Runtime v0.1.0\n";

    // Parse command line arguments
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    std::string projectPath;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::stoi(argv[++i]);
        } else if (arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            projectPath = arg;
        }
    }

    if (!projectPath.empty()) {
        std::cout << "Project path: " << projectPath << "\n";
    }

    try {
        // Create window
        vivid::Window window(width, height, "Vivid", fullscreen);

        // Create and initialize renderer
        vivid::Renderer renderer;
        if (!renderer.init(window.handle(), width, height)) {
            std::cerr << "Failed to initialize renderer\n";
            return 1;
        }

        // Set up resize callback
        window.setResizeCallback([](int w, int h, void* userdata) {
            auto* renderer = static_cast<vivid::Renderer*>(userdata);
            renderer->resize(w, h);
        }, &renderer);

        // Create output texture
        vivid::Texture outputTexture = renderer.createTexture(512, 512);
        if (!outputTexture.valid()) {
            std::cerr << "Failed to create output texture\n";
            return 1;
        }
        std::cout << "Output texture created (512x512)\n";

        // Load noise shader from file
        vivid::Shader noiseShader = renderer.loadShaderFromFile("shaders/noise.wgsl");
        if (!noiseShader.valid()) {
            std::cerr << "Failed to load noise shader\n";
            return 1;
        }
        std::cout << "Noise shader loaded\n";

        std::cout << "Entering main loop...\n";

        // Timing
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastFrameTime = startTime;
        int frameCount = 0;

        // Main loop
        while (!window.shouldClose()) {
            window.pollEvents();

            // Handle resize
            if (window.wasResized()) {
                renderer.resize(window.width(), window.height());
                window.clearResizedFlag();
            }

            // Calculate timing
            auto now = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(now - startTime).count();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;

            // Begin frame
            if (!renderer.beginFrame()) {
                continue;
            }

            // Set up uniforms
            vivid::Uniforms uniforms;
            uniforms.time = time;
            uniforms.deltaTime = deltaTime;
            uniforms.resolutionX = static_cast<float>(outputTexture.width);
            uniforms.resolutionY = static_cast<float>(outputTexture.height);
            uniforms.frame = frameCount;

            // Run noise shader to output texture
            renderer.runShader(noiseShader, outputTexture, nullptr, uniforms);

            // Blit result to screen
            renderer.blitToScreen(outputTexture);

            // End frame
            renderer.endFrame();

            frameCount++;
        }

        // Clean up
        renderer.destroyShader(noiseShader);
        renderer.destroyTexture(outputTexture);

        std::cout << "Exiting after " << frameCount << " frames\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
