#pragma once
#include <webgpu/webgpu.h>
#include <string>
#include <memory>
#include <cstdint>
#include <vector>

// Forward declare GLFW types
struct GLFWwindow;

namespace vivid {

// Simple texture handle for the renderer
struct Texture {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    int width = 0;
    int height = 0;

    bool valid() const { return texture != nullptr && view != nullptr; }
};

// Standard uniforms passed to all shaders
struct Uniforms {
    float time = 0.0f;          // Time in seconds
    float deltaTime = 0.016f;   // Time since last frame
    float resolutionX = 0.0f;   // Output width
    float resolutionY = 0.0f;   // Output height
    int32_t frame = 0;          // Frame counter
    float _padding[3] = {};     // Padding to align to 16 bytes
};

// Shader handle for custom WGSL shaders
struct Shader {
    WGPUShaderModule module = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    std::string path;  // Source file path (for hot-reload)

    bool valid() const { return pipeline != nullptr; }
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    // Non-copyable
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize WebGPU with a GLFW window
    bool init(GLFWwindow* window, int width, int height);
    void shutdown();

    // Frame lifecycle
    bool beginFrame();
    void endFrame();

    // Clear the current frame with a solid color
    void clear(float r, float g, float b, float a = 1.0f);

    // Texture management
    Texture createTexture(int width, int height);
    void destroyTexture(Texture& texture);

    // Blit a texture to the screen
    void blitToScreen(const Texture& texture);

    // Fill a texture with a solid color (for testing)
    void fillTexture(Texture& texture, float r, float g, float b, float a = 1.0f);

    // Read pixel data from a texture (for preview capture)
    // Returns RGBA8 pixel data, or empty vector on failure
    std::vector<uint8_t> readTexturePixels(const Texture& texture);

    // Shader management
    Shader loadShader(const std::string& wgslSource);
    Shader loadShaderFromFile(const std::string& path);
    bool reloadShader(Shader& shader);  // Reload from original file path
    void destroyShader(Shader& shader);

    // Run a shader to render to a texture
    // input can be nullptr if shader doesn't need an input texture
    void runShader(Shader& shader, Texture& output, const Texture* input,
                   const Uniforms& uniforms);

    // Resize handling
    void resize(int width, int height);

    // Accessors
    int width() const { return width_; }
    int height() const { return height_; }
    bool isInitialized() const { return initialized_; }
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }

private:
    bool createSurface(GLFWwindow* window);
    bool requestAdapter();
    bool requestDevice();
    void configureSurface();
    bool createBlitPipeline();

    // WebGPU objects
    WGPUInstance instance_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Blit pipeline
    WGPURenderPipeline blitPipeline_ = nullptr;
    WGPUBindGroupLayout blitBindGroupLayout_ = nullptr;
    WGPUSampler blitSampler_ = nullptr;

    // Shared sampler for shader input textures
    WGPUSampler shaderSampler_ = nullptr;

    // Current frame state
    WGPUTexture currentTexture_ = nullptr;
    WGPUTextureView currentTextureView_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;

    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
};

} // namespace vivid
