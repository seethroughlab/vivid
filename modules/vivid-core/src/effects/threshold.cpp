// Vivid Effects 2D - Threshold Operator Implementation

#include <vivid/effects/threshold.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>

namespace vivid::effects {

struct ThresholdUniforms {
    float threshold;
    float softness;
    float invert;
    float _pad;
};

Threshold::~Threshold() {
    cleanup();
}

void Threshold::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void Threshold::createPipeline(Context& ctx) {
    // Fragment shader - threshold based on luminance
    const char* fragmentShader = R"(
struct Uniforms {
    threshold: f32,
    softness: f32,
    invert: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);

    // Calculate luminance (Rec. 709 coefficients)
    let luminance = dot(color.rgb, vec3f(0.2126, 0.7152, 0.0722));

    // Apply threshold with softness
    // softness of 0 = hard edge, softness of 1 = very gradual transition
    let halfSoft = uniforms.softness * 0.5;
    let low = uniforms.threshold - halfSoft;
    let high = uniforms.threshold + halfSoft;

    var result: f32;
    if (uniforms.softness < 0.001) {
        // Hard threshold (avoid division by zero)
        result = select(0.0, 1.0, luminance >= uniforms.threshold);
    } else {
        // Soft threshold with smoothstep
        result = smoothstep(low, high, luminance);
    }

    // Apply invert
    result = mix(result, 1.0 - result, uniforms.invert);

    return vec4f(vec3f(result), color.a);
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder for cleaner pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(ThresholdUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(ThresholdUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void Threshold::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    ThresholdUniforms uniforms = {};
    uniforms.threshold = threshold;
    uniforms.softness = softness;
    uniforms.invert = invert;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(ThresholdUniforms);
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

void Threshold::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
