// DepthMask - Mask 2D effects using 3D depth buffer

#include <vivid/render3d/depth_mask.h>
#include <vivid/render3d/renderer.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <cstring>

namespace vivid::render3d {

REGISTER_OPERATOR(DepthMask, "3D Compositing", "Mask 2D effects using 3D depth", true);

using namespace vivid::effects;

namespace {

const char* DEPTH_MASK_SHADER_SOURCE = R"(
struct Uniforms {
    threshold: f32,
    softness: f32,
    mode: i32,      // 0=Object, 1=Background, 2=DepthFade
    invert: i32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
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
    let color = textureSample(inputTexture, texSampler, input.uv);
    let depth = textureSample(depthTexture, texSampler, input.uv).r;

    var mask: f32;

    if (uniforms.mode == 0) {
        // Object mode: visible where depth < threshold (where objects are)
        // Use smoothstep for soft edges
        let edge = uniforms.softness * 0.1;
        mask = 1.0 - smoothstep(uniforms.threshold - edge, uniforms.threshold + edge, depth);
    } else if (uniforms.mode == 1) {
        // Background mode: visible where depth >= threshold (empty space)
        let edge = uniforms.softness * 0.1;
        mask = smoothstep(uniforms.threshold - edge, uniforms.threshold + edge, depth);
    } else {
        // DepthFade mode: fade based on depth (closer = more visible)
        mask = 1.0 - depth;
        // Apply softness as contrast adjustment
        mask = pow(mask, 1.0 / max(uniforms.softness + 0.1, 0.01));
    }

    // Apply invert
    if (uniforms.invert != 0) {
        mask = 1.0 - mask;
    }

    // Apply mask to alpha
    return vec4f(color.rgb, color.a * mask);
}
)";

// Uniform struct matching WGSL layout (16 bytes)
struct DepthMaskUniforms {
    float threshold;   // offset 0
    float softness;    // offset 4
    int mode;          // offset 8
    int invert;        // offset 12
};

} // namespace

DepthMask::~DepthMask() {
    cleanup();
}

void DepthMask::setRender3D(Render3D* render) {
    m_render3d = render;
    setInput(1, render);
    markDirty();
}

void DepthMask::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void DepthMask::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(DEPTH_MASK_SHADER_SOURCE);

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
    bufferDesc.size = sizeof(DepthMaskUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    // Input texture (2D effect)
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

    // Color target with premultiplied alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_One;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    colorTarget.writeMask = WGPUColorWriteMask_All;
    // Don't enable blend - we output to our own texture

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

void DepthMask::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Match input resolution from the 2D effect (input 0)
    matchInputResolution(0);

    // Check if we have valid depth source
    if (!m_render3d || !m_render3d->hasDepthOutput()) {
        // No depth output - just pass through the input
        // Copy input to output
        return;
    }

    if (!needsCook()) return;

    WGPUDevice device = ctx.device();

    // Get input texture (2D effect)
    WGPUTextureView effectView = inputView(0);
    if (!effectView) return;

    // Get depth texture from Render3D
    WGPUTextureView depthView = m_render3d->depthOutputView();
    if (!depthView) return;

    // Update uniforms
    DepthMaskUniforms uniforms;
    uniforms.threshold = static_cast<float>(threshold);
    uniforms.softness = static_cast<float>(softness);
    uniforms.mode = static_cast<int>(m_mode);
    uniforms.invert = static_cast<bool>(invert) ? 1 : 0;
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntries[4] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(DepthMaskUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].textureView = effectView;

    bindEntries[2].binding = 2;
    bindEntries[2].textureView = depthView;

    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render masked effect
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 0};  // Transparent clear

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

void DepthMask::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferDestroy(m_uniformBuffer);
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }

    TextureOperator::cleanup();
}

} // namespace vivid::render3d
