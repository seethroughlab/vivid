#pragma once
#include <vivid/types.h>
#include <webgpu/webgpu.h>
#include <string>
#include <memory>
#include <cstdint>
#include <vector>

// Forward declare GLFW types
struct GLFWwindow;

namespace vivid {

// Internal texture data - stored in Texture::handle
struct TextureData {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
};

// Helper to get TextureData from public Texture
inline TextureData* getTextureData(const Texture& tex) {
    return static_cast<TextureData*>(tex.handle);
}

// Helper to check if texture has valid GPU resources
inline bool hasValidGPU(const Texture& tex) {
    auto* data = getTextureData(tex);
    return data && data->texture && data->view;
}

// Depth texture format used for 3D rendering
constexpr WGPUTextureFormat DEPTH_FORMAT = WGPUTextureFormat_Depth24Plus;

// Standard uniforms passed to all shaders
// Layout must match WGSL struct exactly (32 bytes base + 48 bytes params = 80 bytes)
struct Uniforms {
    // Core uniforms (32 bytes)
    float time = 0.0f;          // Time in seconds
    float deltaTime = 0.016f;   // Time since last frame
    float resolutionX = 0.0f;   // Output width
    float resolutionY = 0.0f;   // Output height
    int32_t frame = 0;          // Frame counter
    int32_t mode = 0;           // Generic mode/type selector
    float _pad0 = 0.0f;
    float _pad1 = 0.0f;

    // Operator parameters (48 bytes)
    float param0 = 0.0f;        // Generic float params
    float param1 = 0.0f;
    float param2 = 0.0f;
    float param3 = 0.0f;
    float param4 = 0.0f;
    float param5 = 0.0f;
    float param6 = 0.0f;
    float param7 = 0.0f;
    float vec0X = 0.0f;         // Generic vec2 params
    float vec0Y = 0.0f;
    float vec1X = 0.0f;
    float vec1Y = 0.0f;
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

    // Upload pixel data to a texture (RGBA8 format, width*height*4 bytes)
    void uploadTexturePixels(Texture& texture, const uint8_t* pixels,
                             int width, int height);

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

    // Shader error info (empty if last compile succeeded)
    const std::string& lastShaderError() const { return lastShaderError_; }
    bool hasShaderError() const { return !lastShaderError_.empty(); }

    // Run a shader to render to a texture
    // input can be nullptr if shader doesn't need an input texture
    void runShader(Shader& shader, Texture& output, const Texture* input,
                   const Uniforms& uniforms);

    // Run a shader with two input textures (for compositing, etc.)
    // input2 can be nullptr if shader only needs one texture
    void runShader(Shader& shader, Texture& output, const Texture* input,
                   const Texture* input2, const Uniforms& uniforms);

    // Resize handling
    void resize(int width, int height);

    // Depth buffer management for 3D rendering
    // Creates or recreates a depth buffer at the specified size
    void createDepthBuffer(int width, int height);
    void destroyDepthBuffer();
    WGPUTextureView depthView() const { return depthView_; }
    bool hasDepthBuffer() const { return depthTexture_ != nullptr; }

    // VSync control
    void setVSync(bool enabled);
    bool vsyncEnabled() const { return vsync_; }

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

    // Depth buffer for 3D rendering
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    int depthWidth_ = 0;
    int depthHeight_ = 0;

    // Current frame state
    WGPUTexture currentTexture_ = nullptr;
    WGPUTextureView currentTextureView_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;

    int width_ = 0;
    int height_ = 0;
    bool initialized_ = false;
    bool vsync_ = true;  // VSync enabled by default

    // Last shader compilation error
    std::string lastShaderError_;
};

} // namespace vivid
