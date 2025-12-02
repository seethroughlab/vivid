# Vivid Implementation Plan — Part 2: Runtime

This document covers the core runtime implementation including the WebGPU renderer and hot-reload system.

---

## Core Header Files

### runtime/include/vivid/types.h

```cpp
#pragma once
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <vector>
#include <memory>
#include <cstdint>

namespace vivid {

// Forward declarations
struct WGPUTextureImpl;

// Texture handle - wraps WebGPU texture
class Texture {
public:
    Texture() = default;
    Texture(std::shared_ptr<WGPUTextureImpl> impl, int width, int height);
    
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return impl_ != nullptr; }
    
    // Internal access for renderer
    std::shared_ptr<WGPUTextureImpl> impl() const { return impl_; }
    
private:
    std::shared_ptr<WGPUTextureImpl> impl_;
    int width_ = 0;
    int height_ = 0;
};

// Parameter value types
using ParamValue = std::variant<
    float, 
    int, 
    bool, 
    glm::vec2, 
    glm::vec3, 
    glm::vec4, 
    std::string
>;

// Parameter declaration for introspection
struct ParamDecl {
    std::string name;
    ParamValue defaultValue;
    ParamValue minValue;
    ParamValue maxValue;
};

// Node output types
enum class OutputKind {
    Texture,
    Value,
    ValueArray,
    Geometry
};

// Information about a node for the editor
struct NodeInfo {
    std::string id;
    int sourceLine;
    OutputKind kind;
    std::vector<ParamDecl> params;
};

} // namespace vivid
```

### runtime/include/vivid/context.h

```cpp
#pragma once
#include "types.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace vivid {

class Renderer;

class Context {
public:
    Context(Renderer& renderer, int width, int height);
    
    // Time
    float time() const { return time_; }
    float dt() const { return dt_; }
    int frame() const { return frame_; }
    
    // Resolution
    int width() const { return width_; }
    int height() const { return height_; }
    
    // Texture creation
    Texture createTexture(int width, int height);
    Texture createTexture();  // Uses default resolution
    
    // Shader execution
    // shaderName refers to a .wgsl file in the shaders directory
    void runShader(const std::string& shaderName,
                   const std::unordered_map<std::string, ParamValue>& uniforms,
                   Texture& output);
    
    // Run shader with input textures
    void runShader(const std::string& shaderName,
                   const std::unordered_map<std::string, ParamValue>& uniforms,
                   const std::vector<Texture>& inputs,
                   Texture& output);
    
    // Input/output between operators
    void setOutput(const std::string& name, const Texture& tex);
    void setOutput(const std::string& name, float value);
    void setOutput(const std::string& name, const std::vector<float>& values);
    
    Texture getInputTexture(const std::string& nodeId, const std::string& output = "out");
    float getInputValue(const std::string& nodeId, const std::string& output = "out");
    std::vector<float> getInputValues(const std::string& nodeId, const std::string& output = "out");
    
    // Access to renderer for advanced use
    Renderer& renderer() { return renderer_; }
    
    // Internal: called by runtime each frame
    void beginFrame(float time, float dt, int frame);
    void endFrame();
    
private:
    Renderer& renderer_;
    int width_;
    int height_;
    float time_ = 0;
    float dt_ = 0;
    int frame_ = 0;
    
    std::unordered_map<std::string, Texture> textureOutputs_;
    std::unordered_map<std::string, float> valueOutputs_;
    std::unordered_map<std::string, std::vector<float>> valueArrayOutputs_;
};

} // namespace vivid
```

### runtime/include/vivid/operator.h

```cpp
#pragma once
#include "context.h"
#include "types.h"
#include <memory>
#include <vector>
#include <string>

namespace vivid {

// Base class for serializable operator state (for hot-reload)
struct OperatorState {
    virtual ~OperatorState() = default;
};

// Base class for all operators
class Operator {
public:
    virtual ~Operator() = default;
    
    // Lifecycle
    virtual void init(Context& ctx) {}
    virtual void process(Context& ctx) = 0;
    virtual void cleanup() {}
    
    // Hot reload support
    virtual std::unique_ptr<OperatorState> saveState() { return nullptr; }
    virtual void loadState(std::unique_ptr<OperatorState> state) {}
    
    // Introspection for editor
    virtual std::vector<ParamDecl> params() { return {}; }
    virtual OutputKind outputKind() { return OutputKind::Texture; }
    
    // Identity (set by NODE macro)
    void setId(const std::string& id) { id_ = id; }
    void setSourceLine(int line) { sourceLine_ = line; }
    const std::string& id() const { return id_; }
    int sourceLine() const { return sourceLine_; }
    
protected:
    std::string id_;
    int sourceLine_ = 0;
};

} // namespace vivid

// Export macros for shared library
#if defined(_WIN32)
    #define VIVID_EXPORT extern "C" __declspec(dllexport)
#else
    #define VIVID_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define VIVID_OPERATOR(ClassName) \
    VIVID_EXPORT vivid::Operator* vivid_create_operator() { \
        return new ClassName(); \
    } \
    VIVID_EXPORT void vivid_destroy_operator(vivid::Operator* op) { \
        delete op; \
    } \
    VIVID_EXPORT const char* vivid_operator_name() { \
        return #ClassName; \
    }
```

### runtime/include/vivid/node_macro.h

```cpp
#pragma once
#include "operator.h"
#include <memory>

namespace vivid {

// Global registry for nodes (populated by NODE macro)
class NodeRegistry {
public:
    struct NodeEntry {
        std::string id;
        int sourceLine;
        std::unique_ptr<Operator> op;
    };
    
    static NodeRegistry& instance();
    
    void registerNode(const std::string& id, int line, std::unique_ptr<Operator> op);
    std::vector<NodeEntry>& nodes() { return nodes_; }
    void clear() { nodes_.clear(); }
    
private:
    std::vector<NodeEntry> nodes_;
};

// Helper for NODE macro
template<typename T>
T& registerNode(const char* id, int line, T* op) {
    op->setId(id);
    op->setSourceLine(line);
    NodeRegistry::instance().registerNode(id, line, std::unique_ptr<Operator>(op));
    return *op;
}

} // namespace vivid

// NODE macro: registers operator with source location
// Usage: auto noise = NODE(Noise()).scale(4.0);
#define NODE_IMPL(var, op, line) \
    vivid::registerNode(#var, line, new op)

#define NODE(op) NODE_IMPL(op, op, __LINE__)

// Named node variant for explicit naming
// Usage: auto myNoise = NODE_AS("noise", Noise()).scale(4.0);
#define NODE_AS(name, op) vivid::registerNode(name, __LINE__, new op)
```

### runtime/include/vivid/vivid.h

```cpp
#pragma once

// Main include file for Vivid projects

#include "types.h"
#include "context.h"
#include "operator.h"
#include "node_macro.h"
#include "params.h"

// User implements this function in their chain.cpp
extern void setup(vivid::Context& ctx);
```

### runtime/include/vivid/params.h

```cpp
#pragma once
#include "types.h"

namespace vivid {

// Helper to create parameter declarations
inline ParamDecl floatParam(const std::string& name, float def, float min, float max) {
    return {name, def, min, max};
}

inline ParamDecl intParam(const std::string& name, int def, int min, int max) {
    return {name, def, min, max};
}

inline ParamDecl boolParam(const std::string& name, bool def) {
    return {name, def, false, true};
}

inline ParamDecl vec2Param(const std::string& name, glm::vec2 def) {
    return {name, def, glm::vec2(0), glm::vec2(1)};
}

inline ParamDecl vec3Param(const std::string& name, glm::vec3 def) {
    return {name, def, glm::vec3(0), glm::vec3(1)};
}

inline ParamDecl colorParam(const std::string& name, glm::vec3 def) {
    return {name, def, glm::vec3(0), glm::vec3(1)};
}

} // namespace vivid
```

---

## Core Runtime Files

### runtime/src/main.cpp

```cpp
#include "app.h"
#include <iostream>
#include <string>

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " <project_path> [options]\n"
              << "\nOptions:\n"
              << "  --width <n>     Window width (default: 1280)\n"
              << "  --height <n>    Window height (default: 720)\n"
              << "  --port <n>      WebSocket port (default: 9876)\n"
              << "  --fullscreen    Start in fullscreen mode\n"
              << "  --vsync         Enable vsync (default: off)\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    vivid::AppConfig config;
    config.projectPath = argv[1];
    
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--width" && i + 1 < argc) {
            config.width = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            config.height = std::stoi(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            config.wsPort = std::stoi(argv[++i]);
        } else if (arg == "--fullscreen") {
            config.fullscreen = true;
        } else if (arg == "--vsync") {
            config.vsync = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }
    
    vivid::App app(config);
    return app.run();
}
```

### runtime/src/app.h

```cpp
#pragma once
#include <vivid/context.h>
#include <string>
#include <memory>

namespace vivid {

class Window;
class Renderer;
class Graph;
class HotLoader;
class FileWatcher;
class Compiler;
class PreviewServer;

struct AppConfig {
    std::string projectPath;
    int width = 1280;
    int height = 720;
    int wsPort = 9876;
    bool fullscreen = false;
    bool vsync = false;
};

class App {
public:
    explicit App(const AppConfig& config);
    ~App();
    
    int run();
    
private:
    void onSourceChanged(const std::string& path);
    void onShaderChanged(const std::string& path);
    void recompileAndReload();
    void sendPreviewsToEditor();
    
    AppConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Context> context_;
    std::unique_ptr<Graph> graph_;
    std::unique_ptr<HotLoader> hotloader_;
    std::unique_ptr<FileWatcher> fileWatcher_;
    std::unique_ptr<Compiler> compiler_;
    std::unique_ptr<PreviewServer> previewServer_;
    
    bool needsRecompile_ = false;
    bool running_ = true;
    float time_ = 0;
    int frame_ = 0;
};

} // namespace vivid
```

### runtime/src/app.cpp

```cpp
#include "app.h"
#include "window.h"
#include "renderer.h"
#include "graph.h"
#include "hotload.h"
#include "file_watcher.h"
#include "compiler.h"
#include "preview_server.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace vivid {

App::App(const AppConfig& config) : config_(config) {
    // Create window
    window_ = std::make_unique<Window>(config.width, config.height, "Vivid", config.fullscreen);
    
    // Create WebGPU renderer
    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->init(window_->handle(), config.width, config.height)) {
        throw std::runtime_error("Failed to initialize WebGPU renderer");
    }
    
    // Create context for operators
    context_ = std::make_unique<Context>(*renderer_, config.width, config.height);
    
    // Create operator graph
    graph_ = std::make_unique<Graph>();
    
    // Create hot-loader
    hotloader_ = std::make_unique<HotLoader>();
    
    // Create compiler
    compiler_ = std::make_unique<Compiler>(config.projectPath);
    
    // Create file watcher
    fileWatcher_ = std::make_unique<FileWatcher>();
    fileWatcher_->watch(config.projectPath, [this](const std::string& path) {
        if (path.ends_with(".cpp") || path.ends_with(".h")) {
            onSourceChanged(path);
        } else if (path.ends_with(".wgsl")) {
            onShaderChanged(path);
        }
    });
    
    // Create preview server for VS Code extension
    previewServer_ = std::make_unique<PreviewServer>(config.wsPort);
    previewServer_->start();
    
    // Initial compile and load
    recompileAndReload();
}

App::~App() {
    previewServer_->stop();
    renderer_->shutdown();
}

int App::run() {
    auto lastTime = std::chrono::high_resolution_clock::now();
    
    while (running_ && !window_->shouldClose()) {
        // Calculate delta time
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        time_ += dt;
        frame_++;
        
        // Poll events
        window_->pollEvents();
        
        // Handle pending recompile
        if (needsRecompile_) {
            recompileAndReload();
            needsRecompile_ = false;
        }
        
        // Begin frame
        renderer_->beginFrame();
        context_->beginFrame(time_, dt, frame_);
        
        // Execute operator graph
        graph_->execute(*context_);
        
        // End frame
        context_->endFrame();
        
        // Get final output and blit to screen
        Texture finalOutput = graph_->finalOutput();
        if (finalOutput.valid()) {
            renderer_->blitToScreen(finalOutput);
        }
        
        renderer_->endFrame();
        window_->swapBuffers();
        
        // Send previews to editor (throttled)
        if (frame_ % 3 == 0) {  // ~20fps preview updates
            sendPreviewsToEditor();
        }
        
        // Handle file watcher events
        fileWatcher_->poll();
    }
    
    return 0;
}

void App::onSourceChanged(const std::string& path) {
    std::cout << "[App] Source changed: " << path << "\n";
    needsRecompile_ = true;
}

void App::onShaderChanged(const std::string& path) {
    std::cout << "[App] Shader changed: " << path << "\n";
    renderer_->reloadShader(path);
}

void App::recompileAndReload() {
    std::cout << "[App] Recompiling...\n";
    
    // Save state from current operators
    auto savedStates = graph_->saveAllStates();
    
    // Compile the project
    auto result = compiler_->compile();
    if (!result.success) {
        std::cerr << "[App] Compile failed:\n" << result.errorOutput << "\n";
        previewServer_->sendCompileStatus(false, result.errorOutput);
        return;
    }
    
    // Unload old library
    hotloader_->unload();
    
    // Load new library
    if (!hotloader_->load(result.libraryPath)) {
        std::cerr << "[App] Failed to load library: " << result.libraryPath << "\n";
        previewServer_->sendCompileStatus(false, "Failed to load compiled library");
        return;
    }
    
    // Rebuild graph from loaded operators
    graph_->rebuild(hotloader_->operators());
    
    // Initialize operators
    graph_->initAll(*context_);
    
    // Restore state
    graph_->restoreAllStates(savedStates);
    
    std::cout << "[App] Reload complete\n";
    previewServer_->sendCompileStatus(true, "");
}

void App::sendPreviewsToEditor() {
    auto previews = graph_->capturePreviews(*renderer_);
    previewServer_->sendNodeUpdates(previews);
}

} // namespace vivid
```

---

## WebGPU Renderer

### runtime/src/renderer.h

```cpp
#pragma once
#include <vivid/types.h>
#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace vivid {

// Internal texture implementation
struct WGPUTextureImpl {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    int width = 0;
    int height = 0;
    
    ~WGPUTextureImpl();
};

// Cached shader pipeline
struct ShaderPipeline {
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    std::string source;
    std::string path;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    bool init(void* windowHandle, int width, int height);
    void shutdown();
    
    // Frame lifecycle
    void beginFrame();
    void endFrame();
    
    // Texture management
    Texture createTexture(int width, int height);
    void destroyTexture(Texture& texture);
    std::vector<uint8_t> readTexturePixels(const Texture& texture);
    
    // Shader management
    bool loadShader(const std::string& name, const std::string& wgslSource);
    bool loadShaderFromFile(const std::string& name, const std::string& path);
    void reloadShader(const std::string& path);
    
    // Rendering
    void runShader(const std::string& shaderName,
                   const std::unordered_map<std::string, ParamValue>& uniforms,
                   const std::vector<Texture>& inputs,
                   Texture& output);
    
    void blitToScreen(const Texture& texture);
    
    // Resize
    void resize(int width, int height);
    
    int width() const { return width_; }
    int height() const { return height_; }
    
private:
    void createSwapChain();
    WGPURenderPipeline createPipeline(const std::string& wgslSource);
    WGPUBindGroup createBindGroup(const ShaderPipeline& pipeline,
                                   const std::unordered_map<std::string, ParamValue>& uniforms,
                                   const std::vector<Texture>& inputs);
    
    WGPUInstance instance_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSwapChain swapChain_ = nullptr;
    
    WGPUTextureFormat swapChainFormat_ = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureView currentFrameView_ = nullptr;
    
    int width_ = 0;
    int height_ = 0;
    
    std::unordered_map<std::string, ShaderPipeline> shaders_;
    std::string blitShaderName_ = "_blit";
    
    // Uniform buffer for shader parameters
    WGPUBuffer uniformBuffer_ = nullptr;
    size_t uniformBufferSize_ = 256;  // Enough for typical uniforms
};

} // namespace vivid
```

### runtime/src/renderer.cpp

```cpp
#include "renderer.h"
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
    #define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
#else
    #define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

namespace vivid {

// Fullscreen triangle vertex + simple blit shader
static const char* BLIT_SHADER = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;  // Flip Y for texture sampling
    return out;
}

@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var tex: texture_2d<f32>;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(tex, texSampler, in.uv);
}
)";

WGPUTextureImpl::~WGPUTextureImpl() {
    if (view) wgpuTextureViewRelease(view);
    if (texture) wgpuTextureRelease(texture);
}

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(void* windowHandle, int width, int height) {
    width_ = width;
    height_ = height;
    
    // Create WebGPU instance
    instance_ = wgpuCreateInstance(nullptr);
    if (!instance_) {
        std::cerr << "[Renderer] Failed to create WebGPU instance\n";
        return false;
    }
    
    // Create surface from GLFW window
    GLFWwindow* window = static_cast<GLFWwindow*>(windowHandle);
    
#if defined(__APPLE__)
    {
        id metalLayer = nullptr;  // Will be set up by GLFW
        WGPUSurfaceDescriptorFromMetalLayer metalDesc = {};
        metalDesc.chain.sType = WGPUSType_SurfaceDescriptorFromMetalLayer;
        metalDesc.layer = glfwGetCocoaWindow(window);  // Actually need to get Metal layer
        
        WGPUSurfaceDescriptor surfaceDesc = {};
        surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&metalDesc);
        surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    }
#elif defined(_WIN32)
    {
        WGPUSurfaceDescriptorFromWindowsHWND windowsDesc = {};
        windowsDesc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
        windowsDesc.hwnd = glfwGetWin32Window(window);
        windowsDesc.hinstance = GetModuleHandle(nullptr);
        
        WGPUSurfaceDescriptor surfaceDesc = {};
        surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&windowsDesc);
        surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    }
#else
    {
        WGPUSurfaceDescriptorFromXlibWindow x11Desc = {};
        x11Desc.chain.sType = WGPUSType_SurfaceDescriptorFromXlibWindow;
        x11Desc.window = glfwGetX11Window(window);
        x11Desc.display = glfwGetX11Display();
        
        WGPUSurfaceDescriptor surfaceDesc = {};
        surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&x11Desc);
        surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    }
#endif
    
    if (!surface_) {
        std::cerr << "[Renderer] Failed to create surface\n";
        return false;
    }
    
    // Request adapter
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface_;
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;
    
    wgpuInstanceRequestAdapter(
        instance_, &adapterOpts,
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter, const char* message, void* userdata) {
            if (status == WGPURequestAdapterStatus_Success) {
                *static_cast<WGPUAdapter*>(userdata) = adapter;
            } else {
                std::cerr << "[Renderer] Adapter request failed: " << (message ? message : "unknown") << "\n";
            }
        },
        &adapter_
    );
    
    if (!adapter_) {
        std::cerr << "[Renderer] Failed to get adapter\n";
        return false;
    }
    
    // Request device
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = "Vivid Device";
    
    wgpuAdapterRequestDevice(
        adapter_, &deviceDesc,
        [](WGPURequestDeviceStatus status, WGPUDevice device, const char* message, void* userdata) {
            if (status == WGPURequestDeviceStatus_Success) {
                *static_cast<WGPUDevice*>(userdata) = device;
            } else {
                std::cerr << "[Renderer] Device request failed: " << (message ? message : "unknown") << "\n";
            }
        },
        &device_
    );
    
    if (!device_) {
        std::cerr << "[Renderer] Failed to get device\n";
        return false;
    }
    
    // Set error callback
    wgpuDeviceSetUncapturedErrorCallback(
        device_,
        [](WGPUErrorType type, const char* message, void* userdata) {
            std::cerr << "[WebGPU Error] " << message << "\n";
        },
        nullptr
    );
    
    // Get queue
    queue_ = wgpuDeviceGetQueue(device_);
    
    // Create swap chain
    createSwapChain();
    
    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = uniformBufferSize_;
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device_, &bufferDesc);
    
    // Load blit shader
    loadShader(blitShaderName_, BLIT_SHADER);
    
    std::cout << "[Renderer] WebGPU initialized successfully\n";
    return true;
}

void Renderer::createSwapChain() {
    if (swapChain_) {
        wgpuSwapChainRelease(swapChain_);
    }
    
    WGPUSwapChainDescriptor swapChainDesc = {};
    swapChainDesc.width = width_;
    swapChainDesc.height = height_;
    swapChainDesc.format = swapChainFormat_;
    swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
    swapChainDesc.presentMode = WGPUPresentMode_Fifo;
    
    swapChain_ = wgpuDeviceCreateSwapChain(device_, surface_, &swapChainDesc);
}

void Renderer::shutdown() {
    for (auto& [name, pipeline] : shaders_) {
        if (pipeline.pipeline) wgpuRenderPipelineRelease(pipeline.pipeline);
        if (pipeline.bindGroupLayout) wgpuBindGroupLayoutRelease(pipeline.bindGroupLayout);
    }
    shaders_.clear();
    
    if (uniformBuffer_) wgpuBufferRelease(uniformBuffer_);
    if (swapChain_) wgpuSwapChainRelease(swapChain_);
    if (queue_) wgpuQueueRelease(queue_);
    if (device_) wgpuDeviceRelease(device_);
    if (adapter_) wgpuAdapterRelease(adapter_);
    if (surface_) wgpuSurfaceRelease(surface_);
    if (instance_) wgpuInstanceRelease(instance_);
}

void Renderer::beginFrame() {
    currentFrameView_ = wgpuSwapChainGetCurrentTextureView(swapChain_);
}

void Renderer::endFrame() {
    if (currentFrameView_) {
        wgpuTextureViewRelease(currentFrameView_);
        currentFrameView_ = nullptr;
    }
    wgpuSwapChainPresent(swapChain_);
}

Texture Renderer::createTexture(int width, int height) {
    WGPUTextureDescriptor desc = {};
    desc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_TextureBinding | 
                 WGPUTextureUsage_RenderAttachment | 
                 WGPUTextureUsage_CopySrc;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    
    auto impl = std::make_shared<WGPUTextureImpl>();
    impl->texture = wgpuDeviceCreateTexture(device_, &desc);
    impl->width = width;
    impl->height = height;
    
    WGPUTextureViewDescriptor viewDesc = {};
    impl->view = wgpuTextureCreateView(impl->texture, &viewDesc);
    
    return Texture(impl, width, height);
}

void Renderer::destroyTexture(Texture& texture) {
    // Texture destructor handles cleanup via shared_ptr
    texture = Texture();
}

std::vector<uint8_t> Renderer::readTexturePixels(const Texture& texture) {
    if (!texture.valid()) return {};
    
    auto impl = texture.impl();
    size_t bufferSize = impl->width * impl->height * 4;
    
    // Create staging buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    WGPUBuffer stagingBuffer = wgpuDeviceCreateBuffer(device_, &bufferDesc);
    
    // Copy texture to buffer
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, nullptr);
    
    WGPUImageCopyTexture src = {};
    src.texture = impl->texture;
    
    WGPUImageCopyBuffer dst = {};
    dst.buffer = stagingBuffer;
    dst.layout.bytesPerRow = impl->width * 4;
    dst.layout.rowsPerImage = impl->height;
    
    WGPUExtent3D extent = {static_cast<uint32_t>(impl->width), 
                           static_cast<uint32_t>(impl->height), 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
    
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    
    // Map and read buffer
    std::vector<uint8_t> pixels(bufferSize);
    bool mapped = false;
    
    wgpuBufferMapAsync(
        stagingBuffer, WGPUMapMode_Read, 0, bufferSize,
        [](WGPUBufferMapAsyncStatus status, void* userdata) {
            *static_cast<bool*>(userdata) = (status == WGPUBufferMapAsyncStatus_Success);
        },
        &mapped
    );
    
    // Wait for mapping (in production, should be async)
    wgpuDevicePoll(device_, true, nullptr);
    
    if (mapped) {
        const void* data = wgpuBufferGetConstMappedRange(stagingBuffer, 0, bufferSize);
        std::memcpy(pixels.data(), data, bufferSize);
        wgpuBufferUnmap(stagingBuffer);
    }
    
    wgpuBufferRelease(stagingBuffer);
    return pixels;
}

bool Renderer::loadShader(const std::string& name, const std::string& wgslSource) {
    // Unload existing shader with same name
    auto it = shaders_.find(name);
    if (it != shaders_.end()) {
        if (it->second.pipeline) wgpuRenderPipelineRelease(it->second.pipeline);
        if (it->second.bindGroupLayout) wgpuBindGroupLayoutRelease(it->second.bindGroupLayout);
    }
    
    ShaderPipeline shader;
    shader.source = wgslSource;
    shader.pipeline = createPipeline(wgslSource);
    
    if (!shader.pipeline) {
        std::cerr << "[Renderer] Failed to create pipeline for shader: " << name << "\n";
        return false;
    }
    
    shader.bindGroupLayout = wgpuRenderPipelineGetBindGroupLayout(shader.pipeline, 0);
    shaders_[name] = std::move(shader);
    
    return true;
}

bool Renderer::loadShaderFromFile(const std::string& name, const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "[Renderer] Failed to open shader file: " << path << "\n";
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    bool result = loadShader(name, buffer.str());
    if (result) {
        shaders_[name].path = path;
    }
    return result;
}

void Renderer::reloadShader(const std::string& path) {
    for (auto& [name, shader] : shaders_) {
        if (shader.path == path) {
            std::cout << "[Renderer] Reloading shader: " << name << "\n";
            loadShaderFromFile(name, path);
            return;
        }
    }
}

WGPURenderPipeline Renderer::createPipeline(const std::string& wgslSource) {
    // Create shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = wgslSource.c_str();
    
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device_, &shaderDesc);
    if (!shaderModule) {
        return nullptr;
    }
    
    // Create pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    
    WGPUFragmentState fragment = {};
    fragment.module = shaderModule;
    fragment.entryPoint = "fs_main";
    
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    pipelineDesc.fragment = &fragment;
    
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    
    wgpuShaderModuleRelease(shaderModule);
    
    return pipeline;
}

void Renderer::runShader(const std::string& shaderName,
                         const std::unordered_map<std::string, ParamValue>& uniforms,
                         const std::vector<Texture>& inputs,
                         Texture& output) {
    auto it = shaders_.find(shaderName);
    if (it == shaders_.end()) {
        std::cerr << "[Renderer] Shader not found: " << shaderName << "\n";
        return;
    }
    
    const auto& shader = it->second;
    auto outputImpl = output.impl();
    
    // Create bind group with uniforms and textures
    WGPUBindGroup bindGroup = createBindGroup(shader, uniforms, inputs);
    
    // Create command encoder
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, nullptr);
    
    // Begin render pass targeting the output texture
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputImpl->view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};
    
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderSetPipeline(pass, shader.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);  // Fullscreen triangle
    wgpuRenderPassEncoderEnd(pass);
    
    // Submit
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);
    
    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
}

void Renderer::blitToScreen(const Texture& texture) {
    auto it = shaders_.find(blitShaderName_);
    if (it == shaders_.end()) return;
    
    const auto& shader = it->second;
    
    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    WGPUSampler sampler = wgpuDeviceCreateSampler(device_, &samplerDesc);
    
    // Create bind group
    WGPUBindGroupEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].sampler = sampler;
    entries[1].binding = 1;
    entries[1].textureView = texture.impl()->view;
    
    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = shader.bindGroupLayout;
    bindGroupDesc.entryCount = 2;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindGroupDesc);
    
    // Render to swap chain
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, nullptr);
    
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = currentFrameView_;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};
    
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderSetPipeline(pass, shader.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);
    
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuRenderPassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuSamplerRelease(sampler);
}

WGPUBindGroup Renderer::createBindGroup(
    const ShaderPipeline& pipeline,
    const std::unordered_map<std::string, ParamValue>& uniforms,
    const std::vector<Texture>& inputs) {
    
    // This is simplified - real implementation would parse shader to determine bindings
    // For now, assume binding 0 = uniform buffer, bindings 1+ = textures
    
    std::vector<WGPUBindGroupEntry> entries;
    
    // TODO: Pack uniforms into buffer and add as entry
    // TODO: Add texture entries
    
    WGPUBindGroupDescriptor desc = {};
    desc.layout = pipeline.bindGroupLayout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    
    return wgpuDeviceCreateBindGroup(device_, &desc);
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    createSwapChain();
}

} // namespace vivid
```

---

## Hot Loader

### runtime/src/hotload.h

```cpp
#pragma once
#include <vivid/operator.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid {

class HotLoader {
public:
    HotLoader();
    ~HotLoader();
    
    // Load a shared library containing operators
    bool load(const std::string& libraryPath);
    
    // Unload current library
    void unload();
    
    // Check if loaded
    bool isLoaded() const { return handle_ != nullptr; }
    
    // Get loaded operators
    std::vector<Operator*>& operators() { return operators_; }
    
private:
    void* handle_ = nullptr;
    std::vector<Operator*> operators_;
    
    using CreateFunc = Operator* (*)();
    using DestroyFunc = void (*)(Operator*);
    DestroyFunc destroyFunc_ = nullptr;
};

} // namespace vivid
```

### runtime/src/hotload.cpp

```cpp
#include "hotload.h"
#include <iostream>

#if defined(_WIN32)
    #include <windows.h>
    #define LOAD_LIBRARY(path) LoadLibraryA(path)
    #define GET_SYMBOL(handle, name) GetProcAddress((HMODULE)handle, name)
    #define CLOSE_LIBRARY(handle) FreeLibrary((HMODULE)handle)
    #define LIB_ERROR() "Windows error"
#else
    #include <dlfcn.h>
    #define LOAD_LIBRARY(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
    #define GET_SYMBOL(handle, name) dlsym(handle, name)
    #define CLOSE_LIBRARY(handle) dlclose(handle)
    #define LIB_ERROR() dlerror()
#endif

namespace vivid {

HotLoader::HotLoader() = default;

HotLoader::~HotLoader() {
    unload();
}

bool HotLoader::load(const std::string& libraryPath) {
    // Clear previous errors
#ifndef _WIN32
    dlerror();
#endif
    
    handle_ = LOAD_LIBRARY(libraryPath.c_str());
    if (!handle_) {
        std::cerr << "[HotLoader] Failed to load " << libraryPath << ": " << LIB_ERROR() << "\n";
        return false;
    }
    
    // Get create function
    auto createFunc = reinterpret_cast<CreateFunc>(GET_SYMBOL(handle_, "vivid_create_operator"));
    if (!createFunc) {
        std::cerr << "[HotLoader] Missing vivid_create_operator\n";
        CLOSE_LIBRARY(handle_);
        handle_ = nullptr;
        return false;
    }
    
    // Get destroy function
    destroyFunc_ = reinterpret_cast<DestroyFunc>(GET_SYMBOL(handle_, "vivid_destroy_operator"));
    if (!destroyFunc_) {
        std::cerr << "[HotLoader] Missing vivid_destroy_operator\n";
        CLOSE_LIBRARY(handle_);
        handle_ = nullptr;
        return false;
    }
    
    // Create operator instance
    Operator* op = createFunc();
    if (op) {
        operators_.push_back(op);
    }
    
    std::cout << "[HotLoader] Loaded " << libraryPath << "\n";
    return true;
}

void HotLoader::unload() {
    // Destroy operators
    if (destroyFunc_) {
        for (auto* op : operators_) {
            destroyFunc_(op);
        }
    }
    operators_.clear();
    
    // Close library
    if (handle_) {
        CLOSE_LIBRARY(handle_);
        handle_ = nullptr;
    }
    
    destroyFunc_ = nullptr;
}

} // namespace vivid
```

---

## Next Parts

- **PLAN-03-operators.md** — Operator API, built-in operators, WGSL shaders
- **PLAN-04-extension.md** — VS Code extension, WebSocket protocol, inline decorations
