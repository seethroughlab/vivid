// Depth of Field Effect Implementation

#include <vivid/render3d/depth_of_field.h>
#include <vivid/render3d/renderer.h>
#include <vivid/context.h>
#include <cstring>

namespace vivid::render3d {

using namespace vivid::effects;

namespace {

const char* DOF_SHADER_SOURCE = R"(
struct Uniforms {
    focusDistance: f32,
    focusRange: f32,
    blurStrength: f32,
    showDepth: f32,  // 1.0 = show depth, 0.0 = normal DOF
    texelSize: vec2f,
    _pad: vec2f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var colorTexture: texture_2d<f32>;
@group(0) @binding(2) var depthTexture: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);
    output.position = vec4f(x, y, 0.0, 1.0);
    output.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(colorTexture, texSampler, input.uv);
    let depth = textureSample(depthTexture, texSampler, input.uv).r;

    // Calculate circle of confusion (blur amount) based on depth
    let depthDiff = abs(depth - uniforms.focusDistance);
    let coc = saturate((depthDiff - uniforms.focusRange) / (1.0 - uniforms.focusRange));
    let blurAmount = coc * uniforms.blurStrength;

    // Debug mode: show depth and focus info
    if (uniforms.showDepth > 0.5) {
        // Red = depth value, Green = focus distance, Blue = blur amount
        // This lets us see all the values at once
        return vec4f(depth, uniforms.focusDistance, blurAmount * 5.0, 1.0);
    }

    // If no blur needed, return original color
    if (blurAmount < 0.01) {
        return color;
    }

    // Simple box blur for testing
    let radius = blurAmount * 20.0;  // pixels
    var result = vec3f(0.0);
    let steps = 5;  // 5x5 = 25 samples
    var count = 0.0;

    for (var x = -steps; x <= steps; x++) {
        for (var y = -steps; y <= steps; y++) {
            let offset = vec2f(f32(x), f32(y)) * radius / f32(steps) * uniforms.texelSize;
            result += textureSample(colorTexture, texSampler, input.uv + offset).rgb;
            count += 1.0;
        }
    }

    return vec4f(result / count, 1.0);
}
)";

// WGSL uniform layout with proper alignment:
// struct Uniforms {
//     focusDistance: f32,    // offset 0
//     focusRange: f32,       // offset 4
//     blurStrength: f32,     // offset 8
//     showDepth: f32,        // offset 12
//     texelSize: vec2f,      // offset 16
//     _pad: vec2f,           // offset 24
// };
// Total size: 32 bytes
struct DOFUniforms {
    float focusDistance;     // offset 0
    float focusRange;        // offset 4
    float blurStrength;      // offset 8
    float showDepth;         // offset 12
    float texelSizeX;        // offset 16
    float texelSizeY;        // offset 20
    float _pad[2];           // offset 24-31
};

} // namespace

DepthOfField::~DepthOfField() {
    cleanup();
}

DepthOfField& DepthOfField::input(Render3D* render) {
    m_render3d = render;
    setInput(0, render);
    return *this;
}

void DepthOfField::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void DepthOfField::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(DOF_SHADER_SOURCE);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(DOFUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    // Color texture
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Depth texture
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = layoutEntries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Color target - use RGBA16Float to match the chain's HDR format
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void DepthOfField::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    checkResize(ctx);

    if (!m_render3d || !m_render3d->hasDepthOutput()) {
        // No depth output available, just pass through
        // Copy input to output
        return;
    }

    if (!needsCook()) return;

    WGPUDevice device = ctx.device();

    // Get input textures
    WGPUTextureView colorView = m_render3d->outputView();
    WGPUTextureView depthView = m_render3d->depthOutputView();

    if (!colorView || !depthView) return;

    // Update uniforms
    DOFUniforms uniforms;
    uniforms.focusDistance = static_cast<float>(m_focusDistance);
    uniforms.focusRange = static_cast<float>(m_focusRange);
    uniforms.blurStrength = static_cast<float>(m_blurStrength);
    uniforms.showDepth = m_showDepth ? 1.0f : 0.0f;
    uniforms.texelSizeX = 1.0f / static_cast<float>(m_width);
    uniforms.texelSizeY = 1.0f / static_cast<float>(m_height);
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntries[4] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(DOFUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].textureView = colorView;

    bindEntries[2].binding = 2;
    bindEntries[2].textureView = depthView;

    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render DOF
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    wgpuBindGroupRelease(bindGroup);

    didCook();
}

void DepthOfField::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
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

    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::render3d
