// Vivid V3 - Display Implementation

#include <vivid/display.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace vivid {

// Helper to create WGPUStringView from C string
static inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Load shader from file
static std::string loadShaderFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Display::Display(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat)
    : m_device(device)
    , m_queue(queue)
    , m_surfaceFormat(surfaceFormat)
{
    m_valid = createBlitPipeline();
    // Text pipeline is optional - we'll add it later
}

Display::~Display() {
    if (m_blitPipeline) wgpuRenderPipelineRelease(m_blitPipeline);
    if (m_sampler) wgpuSamplerRelease(m_sampler);
    if (m_blitBindGroupLayout) wgpuBindGroupLayoutRelease(m_blitBindGroupLayout);
    if (m_textPipeline) wgpuRenderPipelineRelease(m_textPipeline);
    if (m_fontTexture) wgpuTextureRelease(m_fontTexture);
    if (m_fontTextureView) wgpuTextureViewRelease(m_fontTextureView);
    if (m_textBindGroupLayout) wgpuBindGroupLayoutRelease(m_textBindGroupLayout);
    if (m_textVertexBuffer) wgpuBufferRelease(m_textVertexBuffer);
}

// Get the executable directory (platform-specific)
static fs::path getExecutableDir() {
#ifdef __APPLE__
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return fs::path(path).parent_path();
    }
#elif defined(_WIN32)
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return fs::path(path).parent_path();
#else
    // Linux: /proc/self/exe
    return fs::read_symlink("/proc/self/exe").parent_path();
#endif
    return fs::current_path();
}

bool Display::createBlitPipeline() {
    // Find shader file
    std::string shaderCode;
    fs::path exeDir = getExecutableDir();

    // Try different paths for shader file
    std::vector<fs::path> searchPaths = {
        exeDir / "shaders" / "blit.wgsl",
        "shaders/blit.wgsl",
        "../shaders/blit.wgsl",
        "../../core/shaders/blit.wgsl",
    };

    for (const auto& path : searchPaths) {
        shaderCode = loadShaderFile(path.string());
        if (!shaderCode.empty()) break;
    }

    if (shaderCode.empty()) {
        std::cerr << "Failed to load blit shader" << std::endl;
        return false;
    }

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderCode.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Blit Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);
    if (!shaderModule) {
        std::cerr << "Failed to create blit shader module" << std::endl;
        return false;
    }

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.maxAnisotropy = 1;

    m_sampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);
    if (!m_sampler) {
        std::cerr << "Failed to create sampler" << std::endl;
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Create bind group layout
    WGPUBindGroupLayoutEntry entries[2] = {};

    // Sampler
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    // Texture
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.label = toStringView("Blit Bind Group Layout");
    bindGroupLayoutDesc.entryCount = 2;
    bindGroupLayoutDesc.entries = entries;

    m_blitBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &bindGroupLayoutDesc);
    if (!m_blitBindGroupLayout) {
        std::cerr << "Failed to create bind group layout" << std::endl;
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.label = toStringView("Blit Pipeline Layout");
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_blitBindGroupLayout;

    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);
    if (!pipelineLayout) {
        std::cerr << "Failed to create pipeline layout" << std::endl;
        wgpuShaderModuleRelease(shaderModule);
        return false;
    }

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Blit Pipeline");
    pipelineDesc.layout = pipelineLayout;

    // Vertex stage
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 0;  // No vertex buffers

    // Fragment stage
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = m_surfaceFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // Multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    m_blitPipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    if (!m_blitPipeline) {
        std::cerr << "Failed to create blit pipeline" << std::endl;
        return false;
    }

    std::cout << "Blit pipeline created successfully" << std::endl;
    return true;
}

bool Display::createTextPipeline() {
    // TODO: Implement bitmap font rendering
    return true;
}

void Display::blit(WGPURenderPassEncoder pass, WGPUTextureView texture) {
    if (!m_blitPipeline || !texture) return;

    // Create bind group for this texture
    WGPUBindGroupEntry entries[2] = {};

    entries[0].binding = 0;
    entries[0].sampler = m_sampler;

    entries[1].binding = 1;
    entries[1].textureView = texture;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.label = toStringView("Blit Bind Group");
    bindGroupDesc.layout = m_blitBindGroupLayout;
    bindGroupDesc.entryCount = 2;
    bindGroupDesc.entries = entries;

    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindGroupDesc);
    if (!bindGroup) return;

    wgpuRenderPassEncoderSetPipeline(pass, m_blitPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);  // 3 vertices, 1 instance

    wgpuBindGroupRelease(bindGroup);
}

void Display::renderText(WGPURenderPassEncoder pass, const std::string& text,
                         float x, float y, float scale) {
    // TODO: Implement bitmap font rendering
    // For now, this is a no-op
}

} // namespace vivid
