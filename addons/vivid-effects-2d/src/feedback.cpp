// Vivid Effects 2D - Feedback Operator Implementation

#include <vivid/effects/feedback.h>
#include <vivid/context.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace vivid::effects {

// Uniform buffer structure (must match shader)
struct FeedbackUniforms {
    float resolution[2];
    float decay;
    float mix_amount;
    float offsetX;
    float offsetY;
    float zoom;
    float rotate;
};

// Find shader file relative to executable or source
static std::string findShaderPath(const std::string& name) {
    fs::path devPath = fs::path("addons/vivid-effects-2d/shaders") / name;
    if (fs::exists(devPath)) {
        return devPath.string();
    }

#ifdef __APPLE__
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        fs::path exePath = fs::path(pathBuf).parent_path();
        fs::path shaderPath = exePath / "shaders" / name;
        if (fs::exists(shaderPath)) {
            return shaderPath.string();
        }
        shaderPath = exePath / ".." / ".." / "addons" / "vivid-effects-2d" / "shaders" / name;
        if (fs::exists(shaderPath)) {
            return shaderPath.string();
        }
    }
#endif

    return "";
}

static std::string loadShaderSource(const std::string& name) {
    std::string path = findShaderPath(name);
    if (path.empty()) {
        return "";
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Feedback::~Feedback() {
    cleanup();
}

void Feedback::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createBufferTexture(ctx);
    createPipeline(ctx);

    m_initialized = true;
    m_firstFrame = true;
}

void Feedback::createBufferTexture(Context& ctx) {
    // Create feedback buffer texture (same size as output)
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Feedback Buffer");
    texDesc.size = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), 1};
    texDesc.format = EFFECTS_FORMAT;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;

    m_buffer = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;

    m_bufferView = wgpuTextureCreateView(m_buffer, &viewDesc);
}

void Feedback::createPipeline(Context& ctx) {
    std::string shaderSource = loadShaderSource("feedback.wgsl");
    if (shaderSource.empty()) {
        // Fallback embedded shader
        shaderSource = R"(
struct Uniforms {
    resolution: vec2f,
    decay: f32,
    mix_amount: f32,
    offsetX: f32,
    offsetY: f32,
    zoom: f32,
    rotate: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTexture: texture_2d<f32>;
@group(0) @binding(3) var bufferTexture: texture_2d<f32>;

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

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let input_color = textureSample(inputTexture, texSampler, in.uv);
    var feedback_uv = in.uv;
    let pixel_offset = vec2f(u.offsetX, u.offsetY) / u.resolution;
    feedback_uv = feedback_uv - pixel_offset;
    let center = vec2f(0.5, 0.5);
    feedback_uv = (feedback_uv - center) * u.zoom + center;
    let rotated = feedback_uv - center;
    let cos_r = cos(u.rotate);
    let sin_r = sin(u.rotate);
    feedback_uv = vec2f(
        rotated.x * cos_r - rotated.y * sin_r,
        rotated.x * sin_r + rotated.y * cos_r
    ) + center;
    let feedback_color = textureSample(bufferTexture, texSampler, feedback_uv) * u.decay;
    let result = mix(feedback_color, input_color, u.mix_amount);
    return result;
}
)";
    }

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Feedback Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Feedback Uniforms");
    bufferDesc.size = sizeof(FeedbackUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(FeedbackUniforms);

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
    layoutDesc.label = toStringView("Feedback Bind Group Layout");
    layoutDesc.entryCount = 4;
    layoutDesc.entries = layoutEntries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    // Create render pipeline
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Feedback Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void Feedback::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Match input resolution
    matchInputResolution(0);

    // Feedback is stateful - always cooks

    // Get input texture view
    WGPUTextureView inView = TextureOperator::inputView(0);
    if (!inView) {
        // No input - just pass through black
        return;
    }

    // Update uniforms
    FeedbackUniforms uniforms = {};
    uniforms.resolution[0] = static_cast<float>(m_width);
    uniforms.resolution[1] = static_cast<float>(m_height);
    uniforms.decay = m_decay;
    uniforms.mix_amount = m_firstFrame ? 1.0f : m_mix;  // First frame: 100% input
    uniforms.offsetX = m_offset.x();
    uniforms.offsetY = m_offset.y();
    uniforms.zoom = m_zoom;
    uniforms.rotate = m_rotate;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group with current textures
    WGPUBindGroupEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = sizeof(FeedbackUniforms);

    entries[1].binding = 1;
    entries[1].sampler = m_sampler;

    entries[2].binding = 2;
    entries[2].textureView = inView;

    entries[3].binding = 3;
    entries[3].textureView = m_bufferView;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    // Render to output
    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);

    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    endRenderPass(pass, encoder, ctx);

    // Copy output to buffer for next frame
    WGPUTexelCopyTextureInfo srcInfo = {};
    srcInfo.texture = m_output;

    WGPUTexelCopyTextureInfo dstInfo = {};
    dstInfo.texture = m_buffer;

    WGPUExtent3D copySize = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), 1};

    WGPUCommandEncoder copyEncoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);
    wgpuCommandEncoderCopyTextureToTexture(copyEncoder, &srcInfo, &dstInfo, &copySize);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(copyEncoder, &cmdBufferDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(copyEncoder);
    wgpuBindGroupRelease(bindGroup);

    m_firstFrame = false;
    didCook();
}

std::unique_ptr<OperatorState> Feedback::saveState() {
    // Save the buffer texture contents to CPU memory
    auto state = std::make_unique<TextureState>();
    state->width = m_width;
    state->height = m_height;

    if (!m_buffer || m_firstFrame) {
        return state;  // No buffer to save yet
    }

    // We need to read back the buffer texture
    // For now, just mark that we have state (actual readback is complex)
    // In a full implementation, we'd use wgpuBufferMapAsync to read pixels

    // Simplified: just preserve the flag that we're not on first frame
    state->pixels.resize(1);  // Non-empty means we had state
    state->pixels[0] = m_firstFrame ? 0 : 1;

    return state;
}

void Feedback::loadState(std::unique_ptr<OperatorState> state) {
    auto* texState = dynamic_cast<TextureState*>(state.get());
    if (texState && texState->hasData()) {
        // We had previous state - don't reset to first frame
        m_firstFrame = (texState->pixels[0] == 0);
    }
}

void Feedback::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
    if (m_bufferView) {
        wgpuTextureViewRelease(m_bufferView);
        m_bufferView = nullptr;
    }
    if (m_buffer) {
        wgpuTextureRelease(m_buffer);
        m_buffer = nullptr;
    }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
