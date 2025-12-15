// Vivid Effects 2D - TimeMachine Operator Implementation

#include <vivid/effects/time_machine.h>
#include <vivid/effects/gpu_common.h>
#include <string>
#include <vivid/context.h>
#include <cstring>

namespace vivid::effects {

// Uniform buffer structure (16-byte aligned)
struct TimeMachineUniforms {
    float depth;
    float offset;
    float invert;
    float frameCount;
    float currentIndex;
    float framesAvailable;
    float _pad[2];
};

TimeMachine::~TimeMachine() {
    cleanup();
}

void TimeMachine::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void TimeMachine::createPipeline(Context& ctx) {
    // Shader using texture_2d_array for temporal sampling
    const char* fragmentShader = R"(
struct Uniforms {
    depth: f32,
    offset: f32,
    invert: f32,
    frameCount: f32,
    currentIndex: f32,
    framesAvailable: f32,
    _pad: vec2f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var cacheTexture: texture_2d_array<f32>;
@group(0) @binding(2) var dispTexture: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Sample displacement map (use luminance)
    let dispColor = textureSample(dispTexture, texSampler, input.uv);
    var displacement = dot(dispColor.rgb, vec3f(0.299, 0.587, 0.114));  // Luminance

    // Apply invert
    if (uniforms.invert > 0.5) {
        displacement = 1.0 - displacement;
    }

    // Apply depth and offset
    displacement = displacement * uniforms.depth + uniforms.offset;
    displacement = clamp(displacement, 0.0, 1.0);

    // Calculate which frame to sample
    // displacement=0 means current frame, displacement=1 means oldest frame
    let framesBack = displacement * (uniforms.framesAvailable - 1.0);

    // Calculate the actual array index (ring buffer)
    // currentIndex points to where we'll write NEXT, so currentIndex-1 is most recent
    let mostRecentIndex = (uniforms.currentIndex - 1.0 + uniforms.frameCount);
    let targetIndexFloat = mostRecentIndex - framesBack;

    // Linear interpolation between two frames for smooth temporal blending
    let lowerIndex = floor(targetIndexFloat);
    let upperIndex = ceil(targetIndexFloat);
    let blend = fract(targetIndexFloat);

    let lowerArrayIndex = u32(lowerIndex) % u32(uniforms.frameCount);
    let upperArrayIndex = u32(upperIndex) % u32(uniforms.frameCount);

    // Get texture dimensions for textureLoad
    let dims = textureDimensions(cacheTexture);
    let pixelCoord = vec2u(
        u32(input.uv.x * f32(dims.x)),
        u32(input.uv.y * f32(dims.y))
    );

    // Sample both frames and blend
    let lowerColor = textureLoad(cacheTexture, pixelCoord, lowerArrayIndex, 0);
    let upperColor = textureLoad(cacheTexture, pixelCoord, upperArrayIndex, 0);

    return mix(lowerColor, upperColor, blend);
}
)";
    // Combine shared vertex shader with effect-specific fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("TimeMachine Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("TimeMachine Uniforms");
    bufferDesc.size = sizeof(TimeMachineUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(TimeMachineUniforms);

    // Cache texture (2D array)
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2DArray;

    // Displacement texture
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("TimeMachine Bind Group Layout");
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
    pipelineDesc.label = toStringView("TimeMachine Pipeline");
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

    // Cleanup
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void TimeMachine::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Need frame cache and displacement map
    if (!m_frameCache || !m_frameCache->cacheView()) {
        return;
    }

    WGPUTextureView dispView = inputView(1);
    if (!dispView) {
        return;  // Need displacement map
    }

    // Match cache resolution
    if (m_frameCache->outputWidth() != m_width ||
        m_frameCache->outputHeight() != m_height) {
        m_width = m_frameCache->outputWidth();
        m_height = m_frameCache->outputHeight();
        releaseOutput();
        createOutput(ctx);
    }

    // Calculate available frames
    int framesWritten = m_frameCache->allocatedFrames();  // Use allocated for now
    int framesAvailable = std::min(framesWritten, m_frameCache->allocatedFrames());
    if (framesAvailable < 1) framesAvailable = 1;

    // Update uniforms
    TimeMachineUniforms uniforms = {};
    uniforms.depth = depth;
    uniforms.offset = offset;
    uniforms.invert = invert ? 1.0f : 0.0f;
    uniforms.frameCount = static_cast<float>(m_frameCache->allocatedFrames());
    uniforms.currentIndex = static_cast<float>(m_frameCache->currentIndex());
    uniforms.framesAvailable = static_cast<float>(framesAvailable);

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntries[4] = {};

    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].offset = 0;
    bindEntries[0].size = sizeof(TimeMachineUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].textureView = m_frameCache->cacheView();

    bindEntries[2].binding = 2;
    bindEntries[2].textureView = dispView;

    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("TimeMachine Bind Group");
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = bindEntries;

    // Release old bind group
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
    }
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    // Begin render pass
    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);

    // Draw
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    // End render pass
    endRenderPass(pass, encoder, ctx);
    didCook();
}

void TimeMachine::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    m_sampler = nullptr;

    releaseOutput();
    m_initialized = false;
    m_frameCache = nullptr;
}

} // namespace vivid::effects
