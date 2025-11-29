#include "renderer.h"
#include "platform_surface.h"
#include <webgpu/wgpu.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <vector>

namespace vivid {

// Shader wrapper - prepended to user fragment shaders
// Provides uniforms, input texture, and fullscreen vertex shader
// Note: Use individual f32 for padding instead of vec3f to avoid WGSL alignment issues
// (vec3f has 16-byte alignment which inflates struct size)
static const char* SHADER_WRAPPER_PREFIX = R"(
struct Uniforms {
    // Core uniforms
    time: f32,
    deltaTime: f32,
    resolution: vec2f,
    frame: i32,
    mode: i32,
    _pad0: f32,
    _pad1: f32,

    // Operator parameters
    param0: f32,
    param1: f32,
    param2: f32,
    param3: f32,
    param4: f32,
    param5: f32,
    param6: f32,
    param7: f32,
    vec0: vec2f,
    vec1: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var inputSampler: sampler;
@group(0) @binding(2) var inputTexture: texture_2d<f32>;
@group(0) @binding(3) var inputTexture2: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

// User shader code follows...
)";

// Fullscreen triangle blit shader
// Uses vertex index to generate positions - no vertex buffer needed
static const char* BLIT_SHADER_SOURCE = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    // Generate fullscreen triangle vertices from index
    // Triangle covers entire screen: (-1,-1), (3,-1), (-1,3)
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );

    var out: VertexOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    // Convert from clip space [-1,1] to UV space [0,1]
    out.uv = pos * 0.5 + 0.5;
    // Flip Y for texture sampling (texture origin is top-left)
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var tex: texture_2d<f32>;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(tex, texSampler, in.uv);
}
)";

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::init(GLFWwindow* window, int width, int height) {
    width_ = width;
    height_ = height;

    // Create WebGPU instance
    WGPUInstanceExtras instanceExtras = {};
    instanceExtras.chain.sType = static_cast<WGPUSType>(WGPUSType_InstanceExtras);
    instanceExtras.backends = WGPUInstanceBackend_Metal;
    instanceExtras.flags = WGPUInstanceFlag_Default;

    WGPUInstanceDescriptor instanceDesc = {};
    instanceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&instanceExtras);

    instance_ = wgpuCreateInstance(&instanceDesc);
    if (!instance_) {
        std::cerr << "[Renderer] Failed to create WebGPU instance\n";
        return false;
    }
    std::cout << "[Renderer] WebGPU instance created\n";

    // Create surface from GLFW window
    if (!createSurface(window)) {
        std::cerr << "[Renderer] Failed to create surface\n";
        return false;
    }
    std::cout << "[Renderer] Surface created\n";

    // Request adapter
    if (!requestAdapter()) {
        std::cerr << "[Renderer] Failed to get adapter\n";
        return false;
    }
    std::cout << "[Renderer] Adapter acquired\n";

    // Request device
    if (!requestDevice()) {
        std::cerr << "[Renderer] Failed to get device\n";
        return false;
    }
    std::cout << "[Renderer] Device acquired\n";

    // Get queue
    queue_ = wgpuDeviceGetQueue(device_);

    // Configure surface (replaces swap chain creation)
    configureSurface();

    // Create blit pipeline for rendering textures to screen
    if (!createBlitPipeline()) {
        std::cerr << "[Renderer] Failed to create blit pipeline\n";
        return false;
    }
    std::cout << "[Renderer] Blit pipeline created\n";

    // Create shared sampler for shader input textures
    WGPUSamplerDescriptor shaderSamplerDesc = {};
    shaderSamplerDesc.magFilter = WGPUFilterMode_Linear;
    shaderSamplerDesc.minFilter = WGPUFilterMode_Linear;
    shaderSamplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    shaderSamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    shaderSamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    shaderSamplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    shaderSamplerDesc.maxAnisotropy = 1;
    shaderSampler_ = wgpuDeviceCreateSampler(device_, &shaderSamplerDesc);

    initialized_ = true;
    std::cout << "[Renderer] WebGPU initialized successfully (" << width << "x" << height << ")\n";
    return true;
}

bool Renderer::createSurface(GLFWwindow* window) {
    surface_ = createSurfaceForWindow(instance_, window);
    return surface_ != nullptr;
}

bool Renderer::requestAdapter() {
    WGPURequestAdapterOptions options = {};
    options.compatibleSurface = surface_;
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    struct AdapterUserData {
        WGPUAdapter adapter = nullptr;
        bool done = false;
    } userData;

    WGPURequestAdapterCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                               WGPUStringView message, void* userdata1, void* userdata2) {
        auto* data = static_cast<AdapterUserData*>(userdata1);
        if (status == WGPURequestAdapterStatus_Success) {
            data->adapter = adapter;
        } else {
            std::cerr << "[Renderer] Adapter request failed: "
                      << (message.data ? std::string(message.data, message.length) : "unknown")
                      << "\n";
        }
        data->done = true;
    };
    callbackInfo.userdata1 = &userData;
    callbackInfo.userdata2 = nullptr;

    wgpuInstanceRequestAdapter(instance_, &options, callbackInfo);

    // Process events until callback fires
    while (!userData.done) {
        wgpuInstanceProcessEvents(instance_);
    }

    adapter_ = userData.adapter;
    return adapter_ != nullptr;
}

bool Renderer::requestDevice() {
    // Set up device descriptor with default limits
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = WGPUStringView{.data = "VividDevice", .length = 11};

    // Set up error callback via device descriptor
    deviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const* device, WGPUErrorType type,
                                                          WGPUStringView message, void* userdata1, void* userdata2) {
        std::cerr << "[WebGPU Error] " << (message.data ? std::string(message.data, message.length) : "unknown") << "\n";
    };

    struct DeviceUserData {
        WGPUDevice device = nullptr;
        bool done = false;
    } userData;

    WGPURequestDeviceCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                               WGPUStringView message, void* userdata1, void* userdata2) {
        auto* data = static_cast<DeviceUserData*>(userdata1);
        if (status == WGPURequestDeviceStatus_Success) {
            data->device = device;
        } else {
            std::cerr << "[Renderer] Device request failed: "
                      << (message.data ? std::string(message.data, message.length) : "unknown")
                      << "\n";
        }
        data->done = true;
    };
    callbackInfo.userdata1 = &userData;
    callbackInfo.userdata2 = nullptr;

    wgpuAdapterRequestDevice(adapter_, &deviceDesc, callbackInfo);

    // Process events until callback fires
    while (!userData.done) {
        wgpuInstanceProcessEvents(instance_);
    }

    device_ = userData.device;
    return device_ != nullptr;
}

void Renderer::configureSurface() {
    // Get surface capabilities
    WGPUSurfaceCapabilities capabilities = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &capabilities);

    // Use the first supported format, or default to BGRA8Unorm
    if (capabilities.formatCount > 0) {
        surfaceFormat_ = capabilities.formats[0];
    } else {
        surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;
    }

    // Configure the surface
    WGPUSurfaceConfiguration config = {};
    config.device = device_;
    config.format = surfaceFormat_;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = static_cast<uint32_t>(width_);
    config.height = static_cast<uint32_t>(height_);
    config.presentMode = vsync_ ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;

    wgpuSurfaceConfigure(surface_, &config);

    // Clean up capabilities
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    std::cout << "[Renderer] Surface configured\n";
}

void Renderer::destroyDepthBuffer() {
    if (depthView_) {
        wgpuTextureViewRelease(depthView_);
        depthView_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }
    depthWidth_ = 0;
    depthHeight_ = 0;
}

void Renderer::createDepthBuffer(int width, int height) {
    // Don't recreate if same size
    if (depthTexture_ && depthWidth_ == width && depthHeight_ == height) {
        return;
    }

    // Destroy existing depth buffer
    destroyDepthBuffer();

    if (width <= 0 || height <= 0) return;

    // Create depth texture
    WGPUTextureDescriptor depthDesc = {};
    depthDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    depthDesc.format = DEPTH_FORMAT;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;

    depthTexture_ = wgpuDeviceCreateTexture(device_, &depthDesc);
    if (!depthTexture_) {
        std::cerr << "[Renderer] Failed to create depth texture\n";
        return;
    }

    // Create depth texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = DEPTH_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_DepthOnly;

    depthView_ = wgpuTextureCreateView(depthTexture_, &viewDesc);
    if (!depthView_) {
        std::cerr << "[Renderer] Failed to create depth texture view\n";
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
        return;
    }

    depthWidth_ = width;
    depthHeight_ = height;
    std::cout << "[Renderer] Depth buffer created (" << width << "x" << height << ")\n";
}

void Renderer::shutdown() {
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    // Note: currentTexture_ is owned by the surface, don't release it

    // Release depth buffer
    destroyDepthBuffer();

    // Release shader sampler
    if (shaderSampler_) {
        wgpuSamplerRelease(shaderSampler_);
        shaderSampler_ = nullptr;
    }

    // Release blit pipeline resources
    if (blitSampler_) {
        wgpuSamplerRelease(blitSampler_);
        blitSampler_ = nullptr;
    }
    if (blitBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(blitBindGroupLayout_);
        blitBindGroupLayout_ = nullptr;
    }
    if (blitPipeline_) {
        wgpuRenderPipelineRelease(blitPipeline_);
        blitPipeline_ = nullptr;
    }

    if (surface_) {
        wgpuSurfaceUnconfigure(surface_);
    }

    if (queue_) {
        wgpuQueueRelease(queue_);
        queue_ = nullptr;
    }

    if (device_) {
        wgpuDeviceRelease(device_);
        device_ = nullptr;
    }

    if (adapter_) {
        wgpuAdapterRelease(adapter_);
        adapter_ = nullptr;
    }

    if (surface_) {
        wgpuSurfaceRelease(surface_);
        surface_ = nullptr;
    }

    if (instance_) {
        wgpuInstanceRelease(instance_);
        instance_ = nullptr;
    }

    initialized_ = false;
    std::cout << "[Renderer] Shutdown complete\n";
}

bool Renderer::beginFrame() {
    if (!initialized_) return false;

    // Get the current texture from the surface
    WGPUSurfaceTexture surfaceTexture = {};
    wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);

    // Check for success (either optimal or suboptimal)
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        std::cerr << "[Renderer] Failed to get current texture, status: " << surfaceTexture.status << "\n";
        return false;
    }

    currentTexture_ = surfaceTexture.texture;

    // Create a view for the texture
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = surfaceFormat_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;

    currentTextureView_ = wgpuTextureCreateView(currentTexture_, &viewDesc);

    return currentTextureView_ != nullptr;
}

void Renderer::endFrame() {
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }

    // Present the surface
    wgpuSurfacePresent(surface_);

    // Note: texture is released by surface automatically after present
    currentTexture_ = nullptr;
}

void Renderer::clear(float r, float g, float b, float a) {
    if (!currentTextureView_) return;

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDesc);

    // Set up render pass with clear color
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = currentTextureView_;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {r, g, b, a};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    // Begin and immediately end the render pass (just clears)
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    // Finish and submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);

    // Clean up
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

void Renderer::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    if (width <= 0 || height <= 0) return;

    width_ = width;
    height_ = height;

    // Reconfigure surface with new size
    configureSurface();

    // Recreate depth buffer if one exists
    if (depthTexture_) {
        createDepthBuffer(width, height);
    }
}

void Renderer::setVSync(bool enabled) {
    if (vsync_ == enabled) return;
    vsync_ = enabled;

    // Reconfigure surface with new present mode
    configureSurface();

    std::cout << "[Renderer] VSync " << (enabled ? "enabled" : "disabled") << "\n";
}

bool Renderer::createBlitPipeline() {
    // Create shader module from WGSL source
    WGPUShaderSourceWGSL wgslSource = {};
    wgslSource.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslSource.code = WGPUStringView{.data = BLIT_SHADER_SOURCE, .length = strlen(BLIT_SHADER_SOURCE)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslSource);

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device_, &shaderDesc);
    if (!shaderModule) {
        std::cerr << "[Renderer] Failed to create blit shader module\n";
        return false;
    }

    // Create bind group layout
    WGPUBindGroupLayoutEntry bindGroupEntries[2] = {};

    // Sampler at binding 0
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].visibility = WGPUShaderStage_Fragment;
    bindGroupEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    // Texture at binding 1
    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].visibility = WGPUShaderStage_Fragment;
    bindGroupEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    bindGroupEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.entryCount = 2;
    bindGroupLayoutDesc.entries = bindGroupEntries;

    blitBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindGroupLayoutDesc);
    if (!blitBindGroupLayout_) {
        std::cerr << "[Renderer] Failed to create blit bind group layout\n";
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &blitBindGroupLayout_;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDesc);

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;

    // Vertex state - no buffers, positions generated in shader
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // Fragment state
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = surfaceFormat_;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    // Multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    blitPipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);

    // Clean up intermediate objects
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    if (!blitPipeline_) {
        std::cerr << "[Renderer] Failed to create blit render pipeline\n";
        return false;
    }

    // Create sampler for texture sampling
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;  // Required to be at least 1

    blitSampler_ = wgpuDeviceCreateSampler(device_, &samplerDesc);
    if (!blitSampler_) {
        std::cerr << "[Renderer] Failed to create blit sampler\n";
        return false;
    }

    return true;
}

Texture Renderer::createTexture(int width, int height) {
    Texture tex = {};
    tex.width = width;
    tex.height = height;

    // Create internal texture data
    auto* data = new TextureData();

    WGPUTextureDescriptor texDesc = {};
    texDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding |
                    WGPUTextureUsage_RenderAttachment |
                    WGPUTextureUsage_CopyDst |
                    WGPUTextureUsage_CopySrc;  // Needed for readTexturePixels
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;

    data->texture = wgpuDeviceCreateTexture(device_, &texDesc);
    if (!data->texture) {
        std::cerr << "[Renderer] Failed to create texture\n";
        delete data;
        return tex;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;

    data->view = wgpuTextureCreateView(data->texture, &viewDesc);
    if (!data->view) {
        std::cerr << "[Renderer] Failed to create texture view\n";
        wgpuTextureRelease(data->texture);
        delete data;
        return tex;
    }

    tex.handle = data;
    return tex;
}

void Renderer::destroyTexture(Texture& texture) {
    auto* data = getTextureData(texture);
    if (data) {
        if (data->view) {
            wgpuTextureViewRelease(data->view);
        }
        if (data->texture) {
            wgpuTextureRelease(data->texture);
        }
        delete data;
    }
    texture.handle = nullptr;
    texture.width = 0;
    texture.height = 0;
}

void Renderer::blitToScreen(const Texture& texture) {
    if (!currentTextureView_ || !hasValidGPU(texture)) return;

    auto* texData = getTextureData(texture);

    // Create bind group for this texture
    WGPUBindGroupEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].sampler = blitSampler_;
    entries[1].binding = 1;
    entries[1].textureView = texData->view;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = blitBindGroupLayout_;
    bindGroupDesc.entryCount = 2;
    bindGroupDesc.entries = entries;

    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindGroupDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDesc);

    // Set up render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = currentTextureView_;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    // Render fullscreen triangle
    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderSetPipeline(renderPass, blitPipeline_);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);  // 3 vertices, 1 instance
    wgpuRenderPassEncoderEnd(renderPass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);

    // Clean up
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuRenderPassEncoderRelease(renderPass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
}

void Renderer::fillTexture(Texture& texture, float r, float g, float b, float a) {
    if (!hasValidGPU(texture)) return;

    auto* texData = getTextureData(texture);

    // Create gradient/pattern pixel data to verify texture sampling works
    std::vector<uint8_t> pixels(texture.width * texture.height * 4);

    for (int y = 0; y < texture.height; y++) {
        for (int x = 0; x < texture.width; x++) {
            int i = (y * texture.width + x) * 4;

            // Create a gradient pattern modulated by the input color
            float u = static_cast<float>(x) / texture.width;
            float v = static_cast<float>(y) / texture.height;

            // Checkerboard pattern (8x8 tiles)
            bool checker = ((x / 32) + (y / 32)) % 2 == 0;
            float checkerMod = checker ? 1.0f : 0.7f;

            // Gradient from corners + input color modulation
            pixels[i + 0] = static_cast<uint8_t>((u * r) * checkerMod * 255.0f);
            pixels[i + 1] = static_cast<uint8_t>((v * g) * checkerMod * 255.0f);
            pixels[i + 2] = static_cast<uint8_t>(((1.0f - u) * b) * checkerMod * 255.0f);
            pixels[i + 3] = static_cast<uint8_t>(a * 255.0f);
        }
    }

    // Write to texture (using new wgpu API types)
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = texData->texture;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.bytesPerRow = texture.width * 4;
    dataLayout.rowsPerImage = texture.height;

    WGPUExtent3D extent = {
        static_cast<uint32_t>(texture.width),
        static_cast<uint32_t>(texture.height),
        1
    };

    wgpuQueueWriteTexture(queue_, &destination, pixels.data(), pixels.size(), &dataLayout, &extent);
}

void Renderer::uploadTexturePixels(Texture& texture, const uint8_t* pixels,
                                    int width, int height) {
    if (!hasValidGPU(texture)) return;
    if (!pixels) return;

    // Verify dimensions match
    if (texture.width != width || texture.height != height) {
        std::cerr << "[Renderer] uploadTexturePixels: dimension mismatch ("
                  << width << "x" << height << " vs "
                  << texture.width << "x" << texture.height << ")\n";
        return;
    }

    auto* texData = getTextureData(texture);

    // Write pixel data to texture
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = texData->texture;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.bytesPerRow = width * 4;  // RGBA8
    dataLayout.rowsPerImage = height;

    WGPUExtent3D extent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1
    };

    size_t dataSize = width * height * 4;
    wgpuQueueWriteTexture(queue_, &destination, pixels, dataSize, &dataLayout, &extent);
}

std::vector<uint8_t> Renderer::readTexturePixels(const Texture& texture) {
    if (!hasValidGPU(texture)) return {};

    auto* texData = getTextureData(texture);

    size_t pixelCount = texture.width * texture.height;
    size_t bytesPerRow = texture.width * 4;
    // WebGPU requires bytesPerRow to be aligned to 256 bytes
    size_t alignedBytesPerRow = (bytesPerRow + 255) & ~255;
    size_t bufferSize = alignedBytesPerRow * texture.height;

    // Create staging buffer for readback
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDesc.mappedAtCreation = false;

    WGPUBuffer stagingBuffer = wgpuDeviceCreateBuffer(device_, &bufferDesc);
    if (!stagingBuffer) {
        std::cerr << "[Renderer] Failed to create staging buffer for readback\n";
        return {};
    }

    // Copy texture to buffer
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDesc);

    WGPUTexelCopyTextureInfo source = {};
    source.texture = texData->texture;

    WGPUTexelCopyBufferInfo destination = {};
    destination.buffer = stagingBuffer;
    destination.layout.bytesPerRow = alignedBytesPerRow;
    destination.layout.rowsPerImage = texture.height;

    WGPUExtent3D copySize = {
        static_cast<uint32_t>(texture.width),
        static_cast<uint32_t>(texture.height),
        1
    };

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Map the buffer asynchronously
    struct MapUserData {
        bool done = false;
        bool success = false;
    } mapData;

    WGPUBufferMapCallbackInfo mapCallbackInfo = {};
    mapCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    mapCallbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message,
                                   void* userdata1, void* userdata2) {
        auto* data = static_cast<MapUserData*>(userdata1);
        data->success = (status == WGPUMapAsyncStatus_Success);
        data->done = true;
    };
    mapCallbackInfo.userdata1 = &mapData;

    wgpuBufferMapAsync(stagingBuffer, WGPUMapMode_Read, 0, bufferSize, mapCallbackInfo);

    // Wait for mapping to complete using wgpu-native's device poll
    while (!mapData.done) {
        wgpuDevicePoll(device_, true, nullptr);
    }

    std::vector<uint8_t> result;
    if (mapData.success) {
        const uint8_t* mappedData = static_cast<const uint8_t*>(
            wgpuBufferGetMappedRange(stagingBuffer, 0, bufferSize));

        if (mappedData) {
            // Copy data, removing row padding if necessary
            result.resize(pixelCount * 4);
            if (alignedBytesPerRow == bytesPerRow) {
                // No padding, direct copy
                memcpy(result.data(), mappedData, result.size());
            } else {
                // Remove padding from each row
                for (int y = 0; y < texture.height; y++) {
                    memcpy(result.data() + y * bytesPerRow,
                           mappedData + y * alignedBytesPerRow,
                           bytesPerRow);
                }
            }
        }
    }

    wgpuBufferUnmap(stagingBuffer);
    wgpuBufferRelease(stagingBuffer);

    return result;
}

Shader Renderer::loadShader(const std::string& wgslSource) {
    Shader shader = {};
    lastShaderError_.clear();

    // Combine wrapper prefix with user shader
    std::string fullSource = std::string(SHADER_WRAPPER_PREFIX) + wgslSource;

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = fullSource.c_str(), .length = fullSource.size()};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    shader.module = wgpuDeviceCreateShaderModule(device_, &moduleDesc);
    if (!shader.module) {
        lastShaderError_ = "Failed to create shader module";
        std::cerr << "[Renderer] " << lastShaderError_ << "\n";
        return shader;
    }

    // NOTE: wgpuShaderModuleGetCompilationInfo is not implemented in wgpu-native v24.0.0.2
    // Shader errors will be reported via the uncaptured error callback instead

    // Create bind group layout
    // Binding 0: Uniforms
    // Binding 1: Sampler
    // Binding 2: Input texture
    // Binding 3: Input texture 2 (for compositing, etc.)
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(Uniforms);

    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = layoutEntries;

    shader.bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device_, &layoutDesc);
    if (!shader.bindGroupLayout) {
        std::cerr << "[Renderer] Failed to create shader bind group layout\n";
        wgpuShaderModuleRelease(shader.module);
        shader.module = nullptr;
        return shader;
    }

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &shader.bindGroupLayout;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDesc);

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;

    pipelineDesc.vertex.module = shader.module;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // Output to RGBA8 texture
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shader.module;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    shader.pipeline = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!shader.pipeline) {
        std::cerr << "[Renderer] Failed to create shader render pipeline\n";
        wgpuBindGroupLayoutRelease(shader.bindGroupLayout);
        wgpuShaderModuleRelease(shader.module);
        shader.bindGroupLayout = nullptr;
        shader.module = nullptr;
        return shader;
    }

    std::cout << "[Renderer] Shader loaded successfully\n";
    return shader;
}

Shader Renderer::loadShaderFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Renderer] Failed to open shader file: " << path << "\n";
        return Shader{};
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    Shader shader = loadShader(buffer.str());
    shader.path = path;
    return shader;
}

bool Renderer::reloadShader(Shader& shader) {
    if (shader.path.empty()) {
        std::cerr << "[Renderer] Cannot reload shader: no source path\n";
        return false;
    }

    std::cout << "[Renderer] Reloading shader from: " << shader.path << "\n";

    // Read updated source
    std::ifstream file(shader.path);
    if (!file.is_open()) {
        std::cerr << "[Renderer] Failed to open shader file: " << shader.path << "\n";
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // Try to compile new shader
    Shader newShader = loadShader(source);
    if (!newShader.valid()) {
        std::cerr << "[Renderer] Failed to compile updated shader\n";
        return false;
    }

    // Preserve the path
    newShader.path = shader.path;

    // Release old resources
    if (shader.pipeline) wgpuRenderPipelineRelease(shader.pipeline);
    if (shader.bindGroupLayout) wgpuBindGroupLayoutRelease(shader.bindGroupLayout);
    if (shader.module) wgpuShaderModuleRelease(shader.module);

    // Swap in new shader
    shader = newShader;

    std::cout << "[Renderer] Shader reloaded successfully\n";
    return true;
}

void Renderer::destroyShader(Shader& shader) {
    if (shader.pipeline) {
        wgpuRenderPipelineRelease(shader.pipeline);
        shader.pipeline = nullptr;
    }
    if (shader.bindGroupLayout) {
        wgpuBindGroupLayoutRelease(shader.bindGroupLayout);
        shader.bindGroupLayout = nullptr;
    }
    if (shader.module) {
        wgpuShaderModuleRelease(shader.module);
        shader.module = nullptr;
    }
    shader.path.clear();
}

void Renderer::runShader(Shader& shader, Texture& output, const Texture* input,
                         const Uniforms& uniforms) {
    // Forward to the two-texture version with nullptr for input2
    runShader(shader, output, input, nullptr, uniforms);
}

void Renderer::runShader(Shader& shader, Texture& output, const Texture* input,
                         const Texture* input2, const Uniforms& uniforms) {
    if (!shader.valid() || !hasValidGPU(output)) return;

    auto* outputData = getTextureData(output);

    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.size = sizeof(Uniforms);
    uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uniformBufferDesc);

    // Upload uniforms
    wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, &uniforms, sizeof(Uniforms));

    // Use input texture or create a dummy 1x1 texture if no input
    WGPUTextureView inputView = nullptr;
    WGPUTextureView inputView2 = nullptr;
    WGPUTexture dummyTexture = nullptr;
    WGPUTextureView dummyView = nullptr;
    WGPUTexture dummyTexture2 = nullptr;
    WGPUTextureView dummyView2 = nullptr;

    // Helper to create dummy texture
    auto createDummyTexture = [this]() -> std::pair<WGPUTexture, WGPUTextureView> {
        WGPUTextureDescriptor dummyDesc = {};
        dummyDesc.size = {1, 1, 1};
        dummyDesc.format = WGPUTextureFormat_RGBA8Unorm;
        dummyDesc.usage = WGPUTextureUsage_TextureBinding;
        dummyDesc.mipLevelCount = 1;
        dummyDesc.sampleCount = 1;
        dummyDesc.dimension = WGPUTextureDimension_2D;
        WGPUTexture tex = wgpuDeviceCreateTexture(device_, &dummyDesc);

        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        WGPUTextureView view = wgpuTextureCreateView(tex, &viewDesc);
        return {tex, view};
    };

    if (input && hasValidGPU(*input)) {
        auto* inputData = getTextureData(*input);
        inputView = inputData->view;
    } else {
        auto [tex, view] = createDummyTexture();
        dummyTexture = tex;
        dummyView = view;
        inputView = dummyView;
    }

    if (input2 && hasValidGPU(*input2)) {
        auto* inputData2 = getTextureData(*input2);
        inputView2 = inputData2->view;
    } else {
        auto [tex, view] = createDummyTexture();
        dummyTexture2 = tex;
        dummyView2 = view;
        inputView2 = dummyView2;
    }

    // Create bind group with 4 entries
    WGPUBindGroupEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].buffer = uniformBuffer;
    entries[0].size = sizeof(Uniforms);

    entries[1].binding = 1;
    entries[1].sampler = shaderSampler_;

    entries[2].binding = 2;
    entries[2].textureView = inputView;

    entries[3].binding = 3;
    entries[3].textureView = inputView2;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = shader.bindGroupLayout;
    bindGroupDesc.entryCount = 4;
    bindGroupDesc.entries = entries;

    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindGroupDesc);

    // Create command encoder and render pass
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputData->view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);
    wgpuRenderPassEncoderSetPipeline(renderPass, shader.pipeline);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(renderPass);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);

    // Clean up
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuRenderPassEncoderRelease(renderPass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuBufferRelease(uniformBuffer);

    if (dummyView) wgpuTextureViewRelease(dummyView);
    if (dummyTexture) wgpuTextureRelease(dummyTexture);
    if (dummyView2) wgpuTextureViewRelease(dummyView2);
    if (dummyTexture2) wgpuTextureRelease(dummyTexture2);
}

} // namespace vivid
