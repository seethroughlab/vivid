// Vivid Effects 2D - Blur Operator Implementation
// Separable Gaussian blur with configurable radius

#include <vivid/effects/blur.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/context.h>
#include <cmath>
#include <string>

namespace vivid::effects {

struct BlurUniforms {
    float radius;
    float direction;  // 0 = horizontal, 1 = vertical
    float texelW;
    float texelH;
};

Blur::~Blur() {
    cleanup();
}

void Blur::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void Blur::createPipeline(Context& ctx) {
    // Fragment shader only - uses shared vertex shader from gpu_common.h
    const char* fragmentShader = R"(
struct Uniforms {
    radius: f32,
    direction: f32,
    texelW: f32,
    texelH: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

fn gaussian(x: f32, sigma: f32) -> f32 {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texel = vec2f(uniforms.texelW, uniforms.texelH);
    var dir: vec2f;
    if (uniforms.direction < 0.5) {
        dir = vec2f(1.0, 0.0);
    } else {
        dir = vec2f(0.0, 1.0);
    }

    let sigma = uniforms.radius / 3.0;
    let samples = i32(ceil(uniforms.radius));

    var color = vec4f(0.0);
    var totalWeight = 0.0;

    // Sample in both directions from center
    for (var i = -samples; i <= samples; i++) {
        let offset = dir * texel * f32(i);
        let weight = gaussian(f32(i), sigma);
        color += textureSample(inputTex, texSampler, input.uv + offset) * weight;
        totalWeight += weight;
    }

    return color / totalWeight;
}
)";

    // Combine shared vertex shader with effect-specific fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(BlurUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());

    // Create temp texture for ping-pong
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = m_width;
    texDesc.size.height = m_height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = EFFECTS_FORMAT;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    m_tempTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    m_tempView = wgpuTextureCreateView(m_tempTexture, &viewDesc);

    WGPUBindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(BlurUniforms);

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    // Use same pipeline for both passes (just change uniforms)
    m_pipelineH = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);
    m_pipelineV = m_pipelineH;
    wgpuRenderPipelineAddRef(m_pipelineV);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void Blur::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    // Update cached bind groups if input changed
    updateBindGroups(ctx, inView);

    float texelW = 1.0f / m_width;
    float texelH = 1.0f / m_height;

    for (int pass = 0; pass < passes; ++pass) {
        // Horizontal pass: input -> temp
        {
            BlurUniforms uniforms = {};
            uniforms.radius = radius;
            uniforms.direction = 0.0f;  // Horizontal
            uniforms.texelW = texelW;
            uniforms.texelH = texelH;
            wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

            // Use shared command encoder for batched submission
            WGPUCommandEncoder encoder = ctx.gpuEncoder();

            WGPURenderPassColorAttachment colorAttachment = {};
            colorAttachment.view = m_tempView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = {0, 0, 0, 1};

            WGPURenderPassDescriptor passDesc = {};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipelineH);
            // Use first-pass bind group for first iteration, subsequent for others
            WGPUBindGroup bindGroup = (pass == 0) ? m_bindGroupHFirst : m_bindGroupHSubseq;
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(renderPass);
            wgpuRenderPassEncoderRelease(renderPass);

            // Don't submit - using shared encoder from Context
        }

        // Vertical pass: temp -> output
        {
            BlurUniforms uniforms = {};
            uniforms.radius = radius;
            uniforms.direction = 1.0f;  // Vertical
            uniforms.texelW = texelW;
            uniforms.texelH = texelH;
            wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

            // Use shared command encoder for batched submission
            WGPUCommandEncoder encoder = ctx.gpuEncoder();

            WGPURenderPassEncoder renderPass;
            beginRenderPass(renderPass, encoder);
            wgpuRenderPassEncoderSetPipeline(renderPass, m_pipelineV);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 0, m_bindGroupV, 0, nullptr);
            wgpuRenderPassEncoderDraw(renderPass, 3, 1, 0, 0);
            endRenderPass(renderPass, encoder, ctx);
        }
    }

    didCook();
}

void Blur::cleanup() {
    // Release cached bind groups
    gpu::release(m_bindGroupHFirst);
    gpu::release(m_bindGroupHSubseq);
    gpu::release(m_bindGroupV);
    m_lastInputView = nullptr;

    gpu::release(m_pipelineH);
    if (m_pipelineV && m_pipelineV != m_pipelineH) { wgpuRenderPipelineRelease(m_pipelineV); }
    m_pipelineV = nullptr;
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    gpu::release(m_tempView);
    gpu::release(m_tempTexture);
    releaseOutput();
    m_initialized = false;
}

void Blur::updateBindGroups(Context& ctx, WGPUTextureView inView) {
    // Check if we need to update (input changed or first time)
    bool needsUpdate = (m_bindGroupV == nullptr) || (inView != m_lastInputView);
    if (!needsUpdate) return;

    // Release old bind groups
    gpu::release(m_bindGroupHFirst);
    gpu::release(m_bindGroupHSubseq);
    gpu::release(m_bindGroupV);

    // Create H pass bind group for first iteration (uses external input)
    {
        WGPUBindGroupEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(BlurUniforms);
        entries[1].binding = 1;
        entries[1].textureView = inView;
        entries[2].binding = 2;
        entries[2].sampler = m_sampler;

        WGPUBindGroupDescriptor desc = {};
        desc.layout = m_bindGroupLayout;
        desc.entryCount = 3;
        desc.entries = entries;
        m_bindGroupHFirst = wgpuDeviceCreateBindGroup(ctx.device(), &desc);
    }

    // Create H pass bind group for subsequent iterations (uses output texture)
    {
        WGPUBindGroupEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(BlurUniforms);
        entries[1].binding = 1;
        entries[1].textureView = m_outputView;
        entries[2].binding = 2;
        entries[2].sampler = m_sampler;

        WGPUBindGroupDescriptor desc = {};
        desc.layout = m_bindGroupLayout;
        desc.entryCount = 3;
        desc.entries = entries;
        m_bindGroupHSubseq = wgpuDeviceCreateBindGroup(ctx.device(), &desc);
    }

    // Create V pass bind group (uses internal temp texture)
    {
        WGPUBindGroupEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(BlurUniforms);
        entries[1].binding = 1;
        entries[1].textureView = m_tempView;
        entries[2].binding = 2;
        entries[2].sampler = m_sampler;

        WGPUBindGroupDescriptor desc = {};
        desc.layout = m_bindGroupLayout;
        desc.entryCount = 3;
        desc.entries = entries;
        m_bindGroupV = wgpuDeviceCreateBindGroup(ctx.device(), &desc);
    }

    m_lastInputView = inView;
}

} // namespace vivid::effects
