// Vivid Runtime - Entry Point
// Phase 4.2: Context test - texture creation and shader execution via Context

#include "window.h"
#include "renderer.h"
#include "hotload.h"
#include "file_watcher.h"
#include <vivid/context.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <chrono>
#include <fstream>
#include <thread>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [project_path] [options]\n"
              << "\nOptions:\n"
              << "  --width <n>     Window width (default: 1280)\n"
              << "  --height <n>    Window height (default: 720)\n"
              << "  --fullscreen    Start in fullscreen mode\n"
              << "  --help          Show this help message\n";
}

int main(int argc, char* argv[]) {
    // Disable output buffering for easier debugging
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

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

        // Create Context (Phase 4.2 test)
        vivid::Context ctx(renderer, width, height);
        std::cout << "Context created (" << ctx.width() << "x" << ctx.height() << ")\n";

        // Set up resize callback
        window.setResizeCallback([](int w, int h, void* userdata) {
            auto* renderer = static_cast<vivid::Renderer*>(userdata);
            renderer->resize(w, h);
        }, &renderer);

        // Test: Create texture via Context
        vivid::Texture outputTexture = ctx.createTexture(512, 512);
        if (!outputTexture.valid()) {
            std::cerr << "Failed to create output texture via Context\n";
            return 1;
        }
        std::cout << "Output texture created via Context (512x512)\n";

        // Also load shader via renderer for hot-reload test (Context caches internally)
        vivid::Shader noiseShader = renderer.loadShaderFromFile("shaders/noise.wgsl");
        if (!noiseShader.valid()) {
            std::cerr << "Failed to load noise shader\n";
            return 1;
        }
        std::cout << "Noise shader loaded for hot-reload\n";

        // HotLoader for testing (Phase 5.1)
        vivid::HotLoader hotLoader;

        // Auto-test HotLoader on startup
        {
            std::cout << "\n--- Auto-Testing HotLoader (Phase 5.1) ---\n";
            std::string libPath = "examples/hello/build/lib/liboperators.dylib";
            std::cout << "Loading library: " << libPath << "\n";
            if (hotLoader.load(libPath)) {
                std::cout << "SUCCESS: Library loaded!\n";
                std::cout << "Number of operators: " << hotLoader.operators().size() << "\n";
                for (size_t i = 0; i < hotLoader.operators().size(); ++i) {
                    auto* op = hotLoader.operators()[i];
                    std::cout << "  - Operator " << i << " (id: " << op->id() << ")\n";
                    op->init(ctx);
                    std::cout << "    Initialized.\n";
                }
                hotLoader.unload();
                std::cout << "Unloaded.\n";
            } else {
                std::cout << "FAILED to load library.\n";
            }
            std::cout << "-------------------------------------------\n\n";
        }

        // Auto-test FileWatcher (Phase 5.2)
        {
            std::cout << "\n--- Auto-Testing FileWatcher (Phase 5.2) ---\n";
            vivid::FileWatcher watcher;
            bool callbackTriggered = false;
            std::string changedFile;

            // Watch the examples/hello directory
            std::string watchDir = "examples/hello";
            watcher.watch(watchDir, [&](const std::string& path) {
                callbackTriggered = true;
                changedFile = path;
                std::cout << "[FileWatcher] Callback triggered: " << path << "\n";
            });

            if (watcher.isWatching()) {
                std::cout << "Watching directory: " << watchDir << "\n";

                // Touch a file to trigger the watcher
                std::string testFile = "examples/hello/chain.cpp";
                std::cout << "Touching file: " << testFile << "\n";
                {
                    std::ofstream ofs(testFile, std::ios::app);
                    ofs << " ";  // Append a space
                }
                {
                    // Remove the space we added
                    std::ifstream ifs(testFile);
                    std::string content((std::istreambuf_iterator<char>(ifs)),
                                       std::istreambuf_iterator<char>());
                    ifs.close();
                    if (!content.empty() && content.back() == ' ') {
                        content.pop_back();
                        std::ofstream ofs(testFile);
                        ofs << content;
                    }
                }

                // Give the watcher time to detect the change
                std::cout << "Waiting for file watcher...\n";
                for (int i = 0; i < 10 && !callbackTriggered; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    watcher.poll();
                }

                if (callbackTriggered) {
                    std::cout << "SUCCESS: FileWatcher triggered!\n";
                } else {
                    std::cout << "WARNING: FileWatcher callback not triggered within 1s\n";
                    std::cout << "(This may be OK - file system events can be delayed)\n";
                }

                watcher.stop();
            } else {
                std::cout << "FAILED: Could not start watching directory\n";
            }
            std::cout << "---------------------------------------------\n\n";
        }

        std::cout << "Entering main loop... (Press 'R' to reload shader, 'C' to test Context, 'L' to test HotLoader)\n";

        // Timing
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastFrameTime = startTime;
        int frameCount = 0;
        bool rKeyWasPressed = false;
        bool cKeyWasPressed = false;
        bool lKeyWasPressed = false;

        // Main loop
        while (!window.shouldClose()) {
            window.pollEvents();

            // Check for 'R' key to reload shader
            bool rKeyPressed = glfwGetKey(window.handle(), GLFW_KEY_R) == GLFW_PRESS;
            if (rKeyPressed && !rKeyWasPressed) {
                std::cout << "\n--- Reloading shader ---\n";
                if (renderer.reloadShader(noiseShader)) {
                    std::cout << "Shader reloaded successfully!\n";
                } else {
                    std::cout << "Shader reload FAILED. Old shader still running.\n";
                    if (renderer.hasShaderError()) {
                        std::cout << "Error:\n" << renderer.lastShaderError() << "\n";
                    }
                }
                std::cout << "------------------------\n\n";
            }
            rKeyWasPressed = rKeyPressed;

            // Check for 'C' key to test Context output storage
            bool cKeyPressed = glfwGetKey(window.handle(), GLFW_KEY_C) == GLFW_PRESS;
            if (cKeyPressed && !cKeyWasPressed) {
                std::cout << "\n--- Testing Context output storage ---\n";

                // Store a texture output
                ctx.setOutput("noise", outputTexture);
                std::cout << "Stored texture output 'noise'\n";

                // Store a value output
                ctx.setOutput("lfo", std::sin(ctx.time() * 2.0f));
                std::cout << "Stored value output 'lfo' = " << std::sin(ctx.time() * 2.0f) << "\n";

                // Retrieve them back
                vivid::Texture* retrievedTex = ctx.getInputTexture("noise");
                if (retrievedTex && retrievedTex->valid()) {
                    std::cout << "Retrieved texture 'noise': " << retrievedTex->width << "x" << retrievedTex->height << "\n";
                } else {
                    std::cout << "ERROR: Failed to retrieve texture 'noise'\n";
                }

                float retrievedVal = ctx.getInputValue("lfo", "out", -999.0f);
                std::cout << "Retrieved value 'lfo' = " << retrievedVal << "\n";

                std::cout << "Context time=" << ctx.time() << " dt=" << ctx.dt() << " frame=" << ctx.frame() << "\n";
                std::cout << "--------------------------------\n\n";
            }
            cKeyWasPressed = cKeyPressed;

            // Check for 'L' key to test HotLoader (Phase 5.1 test)
            bool lKeyPressed = glfwGetKey(window.handle(), GLFW_KEY_L) == GLFW_PRESS;
            if (lKeyPressed && !lKeyWasPressed) {
                std::cout << "\n--- Testing HotLoader (Phase 5.1) ---\n";

                // Try to load the example operator library
                std::string libPath = "examples/hello/build/lib/liboperators.dylib";
                std::cout << "Loading library: " << libPath << "\n";

                if (hotLoader.load(libPath)) {
                    std::cout << "SUCCESS: Library loaded!\n";
                    std::cout << "Number of operators: " << hotLoader.operators().size() << "\n";

                    // Test each operator
                    for (size_t i = 0; i < hotLoader.operators().size(); ++i) {
                        auto* op = hotLoader.operators()[i];
                        std::cout << "  - Operator " << i << " (id: " << op->id() << ")\n";
                        std::cout << "    Initializing...\n";
                        op->init(ctx);
                        std::cout << "    Processing...\n";
                        op->process(ctx);
                        std::cout << "    Done!\n";
                    }

                    // Check if operator stored any output
                    vivid::Texture* noiseOut = ctx.getInputTexture("noise_op", "out");
                    if (noiseOut && noiseOut->valid()) {
                        std::cout << "Operator output texture: " << noiseOut->width << "x" << noiseOut->height << "\n";
                    }

                    // Unload
                    std::cout << "Unloading library...\n";
                    hotLoader.unload();
                    std::cout << "Unloaded.\n";
                } else {
                    std::cout << "FAILED to load library!\n";
                }

                std::cout << "------------------------------------\n\n";
            }
            lKeyWasPressed = lKeyPressed;

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

            // Begin frame (both renderer and context)
            if (!renderer.beginFrame()) {
                continue;
            }
            ctx.beginFrame(time, deltaTime, frameCount);

            // Set up uniforms (using Context's time/dt/frame for consistency)
            vivid::Uniforms uniforms;
            uniforms.time = ctx.time();
            uniforms.deltaTime = ctx.dt();
            uniforms.resolutionX = static_cast<float>(outputTexture.width);
            uniforms.resolutionY = static_cast<float>(outputTexture.height);
            uniforms.frame = ctx.frame();

            // Run noise shader to output texture (still using direct renderer for now)
            renderer.runShader(noiseShader, outputTexture, nullptr, uniforms);

            // Blit result to screen
            renderer.blitToScreen(outputTexture);

            // End frame
            ctx.endFrame();
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
