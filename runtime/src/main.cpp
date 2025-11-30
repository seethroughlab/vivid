// Vivid Runtime - Entry Point
// Phase 9: Preview Server Integration + Async Readback

#include "window.h"
#include "renderer.h"
#include "graph.h"
#include "hotload.h"
#include "file_watcher.h"
#include "compiler.h"
#include "preview_server.h"
#include "async_readback.h"
#include "shared_preview.h"
#include "preview_thread.h"
#include <vivid/context.h>
#include <vivid/operator.h>
#include <GLFW/glfw3.h>
#include <stb_image_write.h>
#include <iostream>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <mutex>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

// Callback for stb_image_write JPEG encoding to memory
static void jpegWriteCallback(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

// Base64 encoding
static std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char b64chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);

    size_t i = 0;
    while (i < data.size()) {
        uint32_t a = i < data.size() ? data[i++] : 0;
        uint32_t b = i < data.size() ? data[i++] : 0;
        uint32_t c = i < data.size() ? data[i++] : 0;
        uint32_t triple = (a << 16) + (b << 8) + c;

        result += b64chars[(triple >> 18) & 0x3F];
        result += b64chars[(triple >> 12) & 0x3F];
        result += (i > data.size() + 1) ? '=' : b64chars[(triple >> 6) & 0x3F];
        result += (i > data.size()) ? '=' : b64chars[triple & 0x3F];
    }
    return result;
}

// Get the shared assets path (where shaders/ directory is located)
static std::string getSharedAssetsPath(const char* argv0) {
    fs::path runtimePath = fs::weakly_canonical(argv0);
    fs::path runtimeDir = runtimePath.parent_path();

    // Check for release layout: bin/vivid-runtime with shaders/ at parent level
    fs::path releaseShaders = runtimeDir.parent_path() / "shaders";
    if (fs::exists(releaseShaders)) {
        return runtimeDir.parent_path().string();
    }

    // Check for dev layout: build/bin/vivid-runtime with shaders/ at repo root
    fs::path devShaders = runtimeDir.parent_path().parent_path() / "shaders";
    if (fs::exists(devShaders)) {
        return runtimeDir.parent_path().parent_path().string();
    }

    // Try current working directory
    if (fs::exists("shaders")) {
        return fs::current_path().string();
    }

    return "";
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <project_path> [options]\n"
              << "\nOptions:\n"
              << "  --width <n>     Window width (default: 1920)\n"
              << "  --height <n>    Window height (default: 1080)\n"
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
    int width = 1920;
    int height = 1080;
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

        // Create Context (with Window for keyboard input)
        vivid::Context ctx(renderer, window, width, height);

        // Set up project path for asset resolution
        std::string absoluteProjectPath = fs::canonical(projectPath).string();
        ctx.setProjectPath(absoluteProjectPath);
        ctx.setSharedAssetsPath(getSharedAssetsPath(argv[0]));

        std::cout << "Context created (" << ctx.width() << "x" << ctx.height() << ")\n";
        std::cout << "Project path: " << ctx.projectPath() << "\n";
        std::cout << "Shared assets: " << getSharedAssetsPath(argv[0]) << "\n";

        // Set up resize callback
        window.setResizeCallback([](int w, int h, void* userdata) {
            auto* renderer = static_cast<vivid::Renderer*>(userdata);
            renderer->resize(w, h);
        }, &renderer);

        // Async readback for non-blocking preview capture
        vivid::AsyncReadback asyncReadback;
        asyncReadback.init(renderer.device(), renderer.queue());

        // Shared memory for zero-copy preview transfer to VS Code extension
        vivid::SharedPreview sharedPreview;
        const std::string sharedMemName = "vivid_preview";
        if (!sharedPreview.create(sharedMemName)) {
            std::cerr << "Warning: Failed to create shared memory, falling back to WebSocket\n";
        }

        // Preview thread for off-main-thread thumbnail processing
        vivid::PreviewThread previewThread;
        if (sharedPreview.isOpen()) {
            previewThread.start(&sharedPreview);
        }

        // Hot-reload system
        vivid::HotLoader hotLoader;
        vivid::FileWatcher fileWatcher;
        vivid::Compiler compiler(projectPath);
        vivid::Graph graph;

        // Chain API support
        std::unique_ptr<vivid::Chain> chain;
        bool usingChainAPI = false;

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
        const float previewUpdateInterval = 0.033f;  // ~30 fps for previews

        // Fallback: buffer for WebSocket base64 previews when shared memory unavailable
        std::vector<vivid::NodePreview> fallbackPreviews;
        std::mutex fallbackMutex;
        bool useSharedMemory = sharedPreview.isOpen();

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
                if (hotLoader.usesChainAPI()) {
                    // Chain API: create chain, call setup(), then init()
                    usingChainAPI = true;
                    chain = std::make_unique<vivid::Chain>();
                    hotLoader.callSetup(*chain);
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
            std::cerr << "Initial compile failed:\n" << result.errorOutput << "\n";
        }
        std::cout << "-----------------------\n\n";

        std::cout << "Entering main loop... (Edit .cpp to hot-reload, Ctrl+C to quit)\n";

        // Timing
        auto startTime = std::chrono::high_resolution_clock::now();
        auto lastFrameTime = startTime;
        auto lastFpsUpdate = startTime;
        int frameCount = 0;
        int fpsFrameCount = 0;
        float currentFps = 0.0f;

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
                std::map<std::string, std::unique_ptr<vivid::OperatorState>> savedStates;
                if (usingChainAPI && chain) {
                    savedStates = chain->saveAllStates();
                } else {
                    savedStates = graph.saveAllStates();
                }

                // 2. Cleanup and unload old library
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

                // 3. Compile new library
                auto compileResult = compiler.compile();
                if (compileResult.success) {
                    std::cout << "Compiled: " << compileResult.libraryPath << "\n";

                    // 4. Load new library
                    if (hotLoader.load(compileResult.libraryPath)) {
                        if (hotLoader.usesChainAPI()) {
                            // Chain API reload
                            usingChainAPI = true;
                            chain = std::make_unique<vivid::Chain>();
                            hotLoader.callSetup(*chain);
                            chain->init(ctx);
                            chain->restoreAllStates(savedStates);
                            std::cout << "Chain reloaded with " << chain->size() << " operator(s)\n";
                        } else {
                            // Legacy API reload
                            usingChainAPI = false;
                            std::cout << "Loaded " << hotLoader.operators().size() << " operator(s)\n";
                            graph.rebuild(hotLoader.operators());
                            graph.initAll(ctx);
                            graph.restoreAllStates(savedStates);
                        }
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

            // Execute operators
            vivid::Texture* finalOutput = nullptr;
            if (usingChainAPI && chain) {
                // Chain API: call update() then process()
                hotLoader.callUpdate(*chain, ctx);
                chain->process(ctx);
                finalOutput = chain->getOutput(ctx);
            } else {
                // Legacy API: execute graph
                graph.execute(ctx);
                finalOutput = graph.finalOutput(ctx);
            }

            // Blit final output to screen
            if (finalOutput && finalOutput->valid()) {
                renderer.blitToScreen(*finalOutput);
            }

            // Process any completed async readbacks
            asyncReadback.processCompleted();

            // Queue new preview captures (throttled, non-blocking)
            float timeSincePreview = std::chrono::duration<float>(now - lastPreviewUpdate).count();
            if (timeSincePreview >= previewUpdateInterval && previewServer.clientCount() > 0) {
                lastPreviewUpdate = now;

                // Update operator count in shared memory
                if (sharedPreview.isOpen()) {
                    sharedPreview.setOperatorCount(static_cast<uint32_t>(graph.operators().size()));
                }

                // Queue async readbacks for each operator
                int slotIndex = 0;
                for (auto* op : graph.operators()) {
                    if (!op) continue;

                    int currentSlot = slotIndex++;
                    std::string opId = op->id();
                    int sourceLine = op->sourceLine();

                    if (op->outputKind() == vivid::OutputKind::Texture) {
                        // Get the operator's output texture
                        vivid::Texture* tex = ctx.getInputTexture(op->id(), "out");
                        if (!tex) tex = ctx.getInputTexture("out");

                        if (tex && tex->valid()) {
                            int texWidth = tex->width;
                            int texHeight = tex->height;

                            if (useSharedMemory && previewThread.isRunning()) {
                                // Queue async readback - minimal callback, heavy work on preview thread
                                asyncReadback.queueReadback(*tex, opId,
                                    [&previewThread, currentSlot, opId, sourceLine]
                                    (const std::string& id, const std::vector<uint8_t>& pixels,
                                     int width, int height) {
                                        // Queue work to preview thread (moves pixel data)
                                        vivid::PreviewWorkItem item;
                                        item.operatorId = id;
                                        item.sourceLine = sourceLine;
                                        item.slotIndex = currentSlot;
                                        item.srcWidth = width;
                                        item.srcHeight = height;
                                        item.rgbaPixels = pixels;  // Copy here, move below
                                        previewThread.queueWork(std::move(item));
                                    });
                            } else {
                                // Fallback: encode to JPEG and base64 for WebSocket (on main thread)
                                asyncReadback.queueReadback(*tex, opId,
                                    [&fallbackMutex, &fallbackPreviews, opId, sourceLine, texWidth, texHeight]
                                    (const std::string& id, const std::vector<uint8_t>& pixels,
                                     int width, int height) {
                                        // Downsample RGBA to RGB thumbnail
                                        constexpr int thumbSize = vivid::PREVIEW_THUMB_WIDTH;
                                        int dstWidth = width;
                                        int dstHeight = height;

                                        if (width > thumbSize || height > thumbSize) {
                                            float scale = std::min(
                                                static_cast<float>(thumbSize) / width,
                                                static_cast<float>(thumbSize) / height
                                            );
                                            dstWidth = std::max(1, static_cast<int>(width * scale));
                                            dstHeight = std::max(1, static_cast<int>(height * scale));
                                        }

                                        std::vector<uint8_t> rgbPixels;
                                        rgbPixels.reserve(dstWidth * dstHeight * 3);

                                        for (int y = 0; y < dstHeight; y++) {
                                            int srcY = y * height / dstHeight;
                                            for (int x = 0; x < dstWidth; x++) {
                                                int srcX = x * width / dstWidth;
                                                size_t srcIdx = (srcY * width + srcX) * 4;
                                                rgbPixels.push_back(pixels[srcIdx]);
                                                rgbPixels.push_back(pixels[srcIdx + 1]);
                                                rgbPixels.push_back(pixels[srcIdx + 2]);
                                            }
                                        }

                                        std::vector<uint8_t> jpegData;
                                        stbi_write_jpg_to_func(
                                            jpegWriteCallback,
                                            &jpegData,
                                            dstWidth,
                                            dstHeight,
                                            3,
                                            rgbPixels.data(),
                                            60
                                        );

                                        if (!jpegData.empty()) {
                                            std::string base64 = base64Encode(jpegData);
                                            std::lock_guard<std::mutex> lock(fallbackMutex);
                                            vivid::NodePreview preview;
                                            preview.id = id;
                                            preview.sourceLine = sourceLine;
                                            preview.kind = vivid::OutputKind::Texture;
                                            preview.base64Image = base64;
                                            preview.width = texWidth;
                                            preview.height = texHeight;
                                            fallbackPreviews.push_back(preview);
                                        }
                                    });
                            }
                        }
                    } else if (op->outputKind() == vivid::OutputKind::Value) {
                        // Values don't need GPU readback - update synchronously
                        float value = ctx.getInputValue(op->id(), "out", 0.0f);

                        if (useSharedMemory && sharedPreview.isOpen()) {
                            sharedPreview.updateValueSlot(currentSlot, opId, sourceLine, value);
                            // Value slots are tracked via shared memory directly
                        } else {
                            // Fallback: send via WebSocket
                            std::lock_guard<std::mutex> lock(fallbackMutex);
                            vivid::NodePreview preview;
                            preview.id = opId;
                            preview.sourceLine = sourceLine;
                            preview.kind = vivid::OutputKind::Value;
                            preview.value = value;
                            fallbackPreviews.push_back(preview);
                        }
                    }
                }
            }

            // Send preview data to WebSocket clients
            if (useSharedMemory && previewThread.isRunning()) {
                // Check for slots updated by the preview thread
                auto updatedSlots = previewThread.getUpdatedSlots();
                if (!updatedSlots.empty() && sharedPreview.isOpen()) {
                    // Build slot info from updated slots
                    std::vector<vivid::PreviewSlotInfo> slotInfo;
                    for (int slotIdx : updatedSlots) {
                        if (slotIdx >= 0 && slotIdx < vivid::PREVIEW_MAX_OPERATORS) {
                            auto& slot = sharedPreview.memory()->slots[slotIdx];
                            if (slot.ready) {
                                vivid::PreviewSlotInfo info;
                                info.id = slot.operatorId;
                                info.slot = slotIdx;
                                info.sourceLine = slot.sourceLine;
                                info.kind = vivid::OutputKind::Texture;
                                info.updated = true;
                                slotInfo.push_back(info);
                            }
                        }
                    }

                    if (!slotInfo.empty()) {
                        sharedPreview.incrementFrame();
                        previewServer.sendPreviewMetadata(
                            slotInfo,
                            sharedPreview.memory()->header.frameNumber,
                            sharedMemName
                        );
                    }
                }
            } else {
                // Fallback: send base64 images via WebSocket
                std::lock_guard<std::mutex> lock(fallbackMutex);
                if (!fallbackPreviews.empty()) {
                    previewServer.sendNodeUpdates(fallbackPreviews);
                    fallbackPreviews.clear();
                }
            }

            // End frame
            ctx.endFrame();
            renderer.endFrame();
            window.clearInputState();

            frameCount++;
            fpsFrameCount++;

            // Update FPS display every 0.5 seconds
            float timeSinceFpsUpdate = std::chrono::duration<float>(now - lastFpsUpdate).count();
            if (timeSinceFpsUpdate >= 0.5f) {
                currentFps = static_cast<float>(fpsFrameCount) / timeSinceFpsUpdate;
                fpsFrameCount = 0;
                lastFpsUpdate = now;

                // Update window title with FPS
                char titleBuf[128];
                snprintf(titleBuf, sizeof(titleBuf), "Vivid - %.1f FPS", currentFps);
                window.setTitle(titleBuf);
            }
        }

        // Cleanup
        previewThread.stop();
        sharedPreview.close();
        asyncReadback.shutdown();
        previewServer.stop();
        if (usingChainAPI && chain) {
            chain->cleanup();
            chain.reset();
        } else {
            graph.cleanupAll();
            graph.clear();
        }
        hotLoader.unload();
        fileWatcher.stop();
        ctx.clearShaderCache();

        std::cout << "Exiting after " << frameCount << " frames\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
