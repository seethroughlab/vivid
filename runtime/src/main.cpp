// Vivid Runtime - Main Entry Point
// Uses Diligent Engine with Vulkan backend (via MoltenVK on macOS)

#include "window.h"
#include "renderer.h"
#include "diligent_renderer.h"
#include "diligent_pbr.h"
#include "hotload.h"
#include "file_watcher.h"
#include "compiler.h"
#include "graph.h"
#include <vivid/context.h>
#include <vivid/chain.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

// Get the shared assets path (shaders, fonts, etc.)
static std::string getSharedAssetsPath(const char* argv0) {
    fs::path runtimePath = fs::weakly_canonical(argv0);
    fs::path runtimeDir = runtimePath.parent_path();

    // Dev layout: build/bin/vivid-diligent
    fs::path parentDir = runtimeDir.parent_path();
    if (parentDir.filename() == "build") {
        fs::path repoRoot = parentDir.parent_path();
        fs::path devShaders = repoRoot / "shaders";
        if (fs::exists(devShaders)) {
            return repoRoot.string();
        }
    }

    // Release layout: installed alongside binary
    fs::path releaseShaders = runtimeDir.parent_path() / "shaders";
    if (fs::exists(releaseShaders)) {
        return runtimeDir.parent_path().string();
    }

    // Fallback to current directory
    if (fs::exists("shaders")) {
        return fs::current_path().string();
    }

    return "";
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <project_path> [options]\n"
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
    std::cout << "Rendering backend: Diligent Engine + Vulkan (MoltenVK)\n";

    // Parse arguments
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

    if (projectPath.empty()) {
        std::cerr << "Error: No project path specified\n";
        printUsage(argv[0]);
        return 1;
    }

    std::cout << "Project path: " << projectPath << "\n";

    try {
        // Create window
        vivid::Window window(width, height, "Vivid", fullscreen);

        // Create WebGPU renderer (still needed for textures, 2D rendering, etc.)
        vivid::Renderer renderer;
        if (!renderer.init(window.handle(), width, height)) {
            std::cerr << "Failed to initialize WebGPU renderer\n";
            return 1;
        }

        // Create Context with standard API
        vivid::Context ctx(renderer, window, width, height);

        // Set up project path for asset resolution
        std::string absoluteProjectPath = fs::canonical(projectPath).string();
        ctx.setProjectPath(absoluteProjectPath);
        ctx.setSharedAssetsPath(getSharedAssetsPath(argv[0]));

        // Remember original working directory
        std::string originalWorkingDir = fs::current_path().string();

        std::cout << "Context created (" << ctx.width() << "x" << ctx.height() << ")\n";
        std::cout << "Project path: " << ctx.projectPath() << "\n";
        std::cout << "Shared assets: " << getSharedAssetsPath(argv[0]) << "\n";

        // Diligent renderer and PBR are lazy-initialized when first used
        // They'll be created when the first mesh is created or render call is made
        std::cout << "Diligent backend will initialize on first use\n";

        // Hot-reload system
        vivid::HotLoader hotLoader;
        vivid::FileWatcher fileWatcher;
        vivid::Compiler compiler(projectPath);
        vivid::Graph graph;

        // Chain API support
        std::unique_ptr<vivid::Chain> chain;
        bool usingChainAPI = false;

        // Hot-reload flag
        bool needsRecompile = false;

        // Watch project for changes
        fileWatcher.watch(projectPath, [&](const std::string& path) {
            if (path.ends_with(".cpp") || path.ends_with(".h") || path.ends_with(".hpp")) {
                std::cout << "[FileWatcher] Source changed: " << path << "\n";
                needsRecompile = true;
            }
        });
        std::cout << "Watching project for changes...\n";

        // Initial compile
        std::cout << "\n--- Initial Compile ---\n";
        auto result = compiler.compile();
        if (result.success) {
            std::cout << "Compiled successfully: " << result.libraryPath << "\n";
            if (hotLoader.load(result.libraryPath)) {
                if (hotLoader.usesChainAPI()) {
                    // Chain API: create chain, call setup(), then init()
                    usingChainAPI = true;
                    chain = std::make_unique<vivid::Chain>();
                    // Change to project dir so setup() can use relative paths
                    fs::current_path(absoluteProjectPath);
                    hotLoader.callSetup(*chain);
                    fs::current_path(originalWorkingDir);
                    chain->init(ctx);
                    std::cout << "Chain initialized with " << chain->size() << " operator(s)\n";
                } else {
                    // Legacy API: single operator
                    usingChainAPI = false;
                    std::cout << "Loaded " << hotLoader.operators().size() << " operator(s)\n";
                    graph.rebuild(hotLoader.operators());
                    graph.initAll(ctx);
                }
            } else {
                std::cerr << "Failed to load library\n";
            }
        } else {
            std::cerr << "Compile failed:\n" << result.errorOutput << "\n";
        }
        std::cout << "-----------------------\n\n";

        std::cout << "Entering main loop... (Edit .cpp to hot-reload, close window to quit)\n";

        // Timing
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastFrameTime = startTime;
        auto lastFpsTime = startTime;
        int frameCount = 0;
        int fpsFrameCount = 0;
        float currentFps = 0.0f;

        // Main loop
        while (!window.shouldClose()) {
            window.pollEvents();
            fileWatcher.poll();

            // Hot-reload handling
            if (needsRecompile) {
                needsRecompile = false;
                std::cout << "\n--- Hot Reload ---\n";

                // Save state
                std::map<std::string, std::unique_ptr<vivid::OperatorState>> savedStates;
                if (usingChainAPI && chain) {
                    savedStates = chain->saveAllStates();
                } else {
                    savedStates = graph.saveAllStates();
                }

                // Cleanup
                if (usingChainAPI && chain) {
                    chain->cleanup();
                    chain.reset();
                } else {
                    graph.cleanupAll();
                    graph.clear();
                }
                hotLoader.unload();
                ctx.clearOutputs();
                ctx.clearShaderCache();

                // Recompile
                auto compileResult = compiler.compile();
                if (compileResult.success) {
                    std::cout << "Compiled: " << compileResult.libraryPath << "\n";
                    if (hotLoader.load(compileResult.libraryPath)) {
                        if (hotLoader.usesChainAPI()) {
                            usingChainAPI = true;
                            chain = std::make_unique<vivid::Chain>();
                            fs::current_path(absoluteProjectPath);
                            hotLoader.callSetup(*chain);
                            fs::current_path(originalWorkingDir);
                            chain->init(ctx);
                            chain->restoreAllStates(savedStates);
                            std::cout << "Chain reloaded with " << chain->size() << " operator(s)\n";
                        } else {
                            usingChainAPI = false;
                            graph.rebuild(hotLoader.operators());
                            graph.initAll(ctx);
                            graph.restoreAllStates(savedStates);
                            std::cout << "Loaded " << hotLoader.operators().size() << " operator(s)\n";
                        }
                        std::cout << "Hot reload complete!\n";
                    } else {
                        std::cerr << "Failed to load new library\n";
                    }
                } else {
                    std::cerr << "Compile failed:\n" << compileResult.errorOutput << "\n";
                }
                std::cout << "------------------\n\n";
            }

            // Handle resize
            if (window.wasResized()) {
                renderer.resize(window.width(), window.height());
                // Note: Diligent swap chain resize is handled internally
                window.clearResizedFlag();
            }

            // Calculate timing
            auto now = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(now - startTime).count();
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
            lastFrameTime = now;

            // Begin frame - Diligent handles presentation, WebGPU only for textures/compute
            ctx.beginFrame(time, deltaTime, frameCount);

            // Execute operators
            if (usingChainAPI && chain) {
                hotLoader.callUpdate(*chain, ctx);
                chain->process(ctx);
            } else {
                graph.execute(ctx);
            }

            // Present to screen via Diligent
            ctx.endFrame();
            if (ctx.diligentRenderedThisFrame()) {
                ctx.presentDiligentSwapChain();
            }
            // Note: WebGPU blit path removed - Diligent handles all presentation
            window.clearInputState();

            frameCount++;
            fpsFrameCount++;

            // FPS display
            float timeSinceFpsUpdate = std::chrono::duration<float>(now - lastFpsTime).count();
            if (timeSinceFpsUpdate >= 0.5f) {
                currentFps = fpsFrameCount / timeSinceFpsUpdate;
                fpsFrameCount = 0;
                lastFpsTime = now;

                char titleBuf[128];
                snprintf(titleBuf, sizeof(titleBuf), "Vivid - %.1f FPS", currentFps);
                window.setTitle(titleBuf);
            }
        }

        // Cleanup
        if (usingChainAPI && chain) {
            chain->cleanup();
        } else {
            graph.cleanupAll();
        }
        hotLoader.unload();
        fileWatcher.stop();

        std::cout << "Exiting after " << frameCount << " frames\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
