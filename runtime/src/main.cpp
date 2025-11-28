// Vivid Runtime - Entry Point
// Phase 9: Preview Server Integration

#include "window.h"
#include "renderer.h"
#include "graph.h"
#include "hotload.h"
#include "file_watcher.h"
#include "compiler.h"
#include "preview_server.h"
#include <vivid/context.h>
#include <vivid/operator.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
#include <chrono>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <project_path> [options]\n"
              << "\nOptions:\n"
              << "  --width <n>     Window width (default: 1280)\n"
              << "  --height <n>    Window height (default: 720)\n"
              << "  --fullscreen    Start in fullscreen mode\n"
              << "  --port <n>      WebSocket port for preview server (default: 9876)\n"
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
    int wsPort = 9876;
    bool fullscreen = false;
    std::string projectPath;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            height = std::stoi(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            wsPort = std::stoi(argv[++i]);
        } else if (arg == "--fullscreen") {
            fullscreen = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            projectPath = arg;
        }
    }

    if (projectPath.empty()) {
        std::cerr << "Error: No project path specified\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "Project path: " << projectPath << "\n";

    try {
        // Create window
        vivid::Window window(width, height, "Vivid", fullscreen);

        // Create and initialize renderer
        vivid::Renderer renderer;
        if (!renderer.init(window.handle(), width, height)) {
            std::cerr << "Failed to initialize renderer\n";
            return 1;
        }

        // Create Context
        vivid::Context ctx(renderer, width, height);
        std::cout << "Context created (" << ctx.width() << "x" << ctx.height() << ")\n";

        // Set up resize callback
        window.setResizeCallback([](int w, int h, void* userdata) {
            auto* renderer = static_cast<vivid::Renderer*>(userdata);
            renderer->resize(w, h);
        }, &renderer);

        // Hot-reload system
        vivid::HotLoader hotLoader;
        vivid::FileWatcher fileWatcher;
        vivid::Compiler compiler(projectPath);
        vivid::Graph graph;

        // Flags for hot-reload events
        bool needsRecompile = false;
        std::string shaderToReload;

        // Preview server for VS Code extension
        vivid::PreviewServer previewServer(wsPort);
        previewServer.setCommandCallback([&](const std::string& type, const nlohmann::json& data) {
            if (type == "reload") {
                std::cout << "[PreviewServer] Reload requested\n";
                needsRecompile = true;
            } else if (type == "param_change") {
                // Future: live parameter updates
                std::cout << "[PreviewServer] Param change: " << data.dump() << "\n";
            } else if (type == "pause") {
                // Future: pause/resume
                std::cout << "[PreviewServer] Pause: " << data.dump() << "\n";
            }
        });
        previewServer.start();

        // Preview update throttling
        auto lastPreviewUpdate = std::chrono::high_resolution_clock::now();
        const float previewUpdateInterval = 0.1f;  // 10 fps for previews

        // Start watching the project directory
        fileWatcher.watch(projectPath, [&](const std::string& path) {
            if (path.ends_with(".cpp") || path.ends_with(".h") || path.ends_with(".hpp")) {
                std::cout << "[FileWatcher] Source changed: " << path << "\n";
                needsRecompile = true;
            } else if (path.ends_with(".wgsl")) {
                std::cout << "[FileWatcher] Shader changed: " << path << "\n";
                shaderToReload = path;
            }
        });
        std::cout << "Watching project for changes...\n";

        // Initial compile and load
        std::cout << "\n--- Initial Compile ---\n";
        auto result = compiler.compile();
        if (result.success) {
            std::cout << "Compiled successfully: " << result.libraryPath << "\n";
            if (hotLoader.load(result.libraryPath)) {
                std::cout << "Loaded " << hotLoader.operators().size() << " operator(s)\n";
                graph.rebuild(hotLoader.operators());
                graph.initAll(ctx);
            } else {
                std::cerr << "Failed to load library\n";
            }
        } else {
            std::cerr << "Initial compile failed:\n" << result.errorOutput << "\n";
        }
        std::cout << "-----------------------\n\n";

        std::cout << "Entering main loop... (Edit .cpp to hot-reload, Ctrl+C to quit)\n";

        // Timing
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastFrameTime = startTime;
        int frameCount = 0;

        // Main loop
        while (!window.shouldClose()) {
            window.pollEvents();

            // Poll file watcher for events
            fileWatcher.poll();

            // Handle hot-reload of source code
            if (needsRecompile) {
                needsRecompile = false;
                std::cout << "\n--- Hot Reload ---\n";

                // 1. Save state from current operators
                auto savedStates = graph.saveAllStates();

                // 2. Cleanup and unload old library
                graph.cleanupAll();
                graph.clear();
                hotLoader.unload();
                ctx.clearOutputs();

                // 3. Compile new library
                auto compileResult = compiler.compile();
                if (compileResult.success) {
                    std::cout << "Compiled: " << compileResult.libraryPath << "\n";

                    // 4. Load new library
                    if (hotLoader.load(compileResult.libraryPath)) {
                        std::cout << "Loaded " << hotLoader.operators().size() << " operator(s)\n";

                        // 5. Rebuild graph, initialize, and restore state
                        graph.rebuild(hotLoader.operators());
                        graph.initAll(ctx);
                        graph.restoreAllStates(savedStates);
                        std::cout << "Hot reload complete!\n";

                        // Notify connected clients
                        previewServer.sendCompileStatus(true, "Compiled successfully");
                    } else {
                        std::cerr << "Failed to load new library\n";
                        previewServer.sendCompileStatus(false, "Failed to load library");
                    }
                } else {
                    std::cerr << "Compile failed:\n" << compileResult.errorOutput << "\n";
                    std::cerr << "(Old operators unloaded, running without operators)\n";
                    previewServer.sendCompileStatus(false, compileResult.errorOutput);
                }
                std::cout << "------------------\n\n";
            }

            // Handle shader hot-reload
            if (!shaderToReload.empty()) {
                std::cout << "[Renderer] Reloading shader: " << shaderToReload << "\n";
                // Note: Renderer already has shader reload capability, but we'd need
                // to track which shaders are loaded to reload them properly.
                // For now, operators will reload their shaders on next process() call.
                shaderToReload.clear();
            }

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
            ctx.beginFrame(time, deltaTime, frameCount);

            // Execute operator graph
            graph.execute(ctx);

            // Get final output from graph and blit to screen
            vivid::Texture* finalOutput = graph.finalOutput(ctx);
            if (finalOutput && finalOutput->valid()) {
                renderer.blitToScreen(*finalOutput);
            }

            // Send preview updates to connected clients (throttled)
            float timeSincePreview = std::chrono::duration<float>(now - lastPreviewUpdate).count();
            if (timeSincePreview >= previewUpdateInterval && previewServer.clientCount() > 0) {
                lastPreviewUpdate = now;

                // Capture previews from graph
                auto graphPreviews = graph.capturePreviews(ctx, renderer, 128);

                // Convert to NodePreview format
                std::vector<vivid::NodePreview> nodePreviews;
                for (const auto& preview : graphPreviews) {
                    vivid::NodePreview np;
                    np.id = preview.operatorId;
                    np.sourceLine = preview.sourceLine;
                    np.kind = preview.outputKind;
                    np.base64Image = preview.base64Jpeg;
                    np.width = preview.width;
                    np.height = preview.height;
                    np.value = preview.value;
                    nodePreviews.push_back(np);
                }

                previewServer.sendNodeUpdates(nodePreviews);
            }

            // End frame
            ctx.endFrame();
            renderer.endFrame();

            frameCount++;
        }

        // Cleanup
        previewServer.stop();
        graph.cleanupAll();
        graph.clear();
        hotLoader.unload();
        fileWatcher.stop();

        std::cout << "Exiting after " << frameCount << " frames\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
