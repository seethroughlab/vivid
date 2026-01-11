// Vivid Effects 2D - Level Operator Implementation

#include <vivid/effects/level.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>

namespace vivid::effects {

struct LevelUniforms {
    float inBlack;
    float inWhite;
    float gamma;
    float outBlack;
    float outWhite;
    float _pad1;
    float _pad2;
    float _pad3;
};

Level::~Level() {
    cleanup();
}

void Level::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void Level::createPipeline(Context& ctx) {
    // Fragment shader - level adjustment
    const char* fragmentShader = R"(
struct Uniforms {
    inBlack: f32,
    inWhite: f32,
    gamma: f32,
    outBlack: f32,
    outWhite: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);

    // Calculate input range (avoid division by zero)
    let inRange = max(uniforms.inWhite - uniforms.inBlack, 0.001);

    // Normalize to input range
    let normalized = (color.rgb - uniforms.inBlack) / inRange;

    // Clamp and apply gamma correction
    let clamped = clamp(normalized, vec3f(0.0), vec3f(1.0));
    let gammaCorrected = pow(clamped, vec3f(1.0 / uniforms.gamma));

    // Map to output range
    let outRange = uniforms.outWhite - uniforms.outBlack;
    let result = gammaCorrected * outRange + uniforms.outBlack;

    return vec4f(result, color.a);
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder for cleaner pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(LevelUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(LevelUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void Level::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    LevelUniforms uniforms = {};
    uniforms.inBlack = inBlack;
    uniforms.inWhite = inWhite;
    uniforms.gamma = gamma;
    uniforms.outBlack = outBlack;
    uniforms.outWhite = outWhite;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(LevelUniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].textureView = inView;
    bindEntries[2].binding = 2;
    bindEntries[2].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 3;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Use shared command encoder for batched submission
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);

    wgpuBindGroupRelease(bindGroup);

    didCook();
}

void Level::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
