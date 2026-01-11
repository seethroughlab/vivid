// Vivid Effects 2D - ToneMap Operator Implementation

#include <vivid/effects/tone_map.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>

namespace vivid::effects {

struct ToneMapUniforms {
    float exposure;
    int mode;
    float whitePoint;
    float _pad;
};

ToneMap::~ToneMap() {
    cleanup();
}

void ToneMap::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void ToneMap::createPipeline(Context& ctx) {
    // Fragment shader - tone mapping algorithms
    const char* fragmentShader = R"(
struct Uniforms {
    exposure: f32,
    mode: i32,
    whitePoint: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

// Reinhard tone mapping (simple and extended)
fn tonemapReinhard(color: vec3f, whitePoint: f32) -> vec3f {
    // Extended Reinhard: x * (1 + x / white^2) / (1 + x)
    let white2 = whitePoint * whitePoint;
    return color * (1.0 + color / white2) / (1.0 + color);
}

// ACES filmic tone mapping (approximation)
fn tonemapACES(color: vec3f) -> vec3f {
    // ACES fitted curve by Krzysztof Narkowicz
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), vec3f(0.0), vec3f(1.0));
}

// Uncharted 2 filmic tone mapping
fn unchartedPartial(x: vec3f) -> vec3f {
    let A = 0.15;  // Shoulder strength
    let B = 0.50;  // Linear strength
    let C = 0.10;  // Linear angle
    let D = 0.20;  // Toe strength
    let E = 0.02;  // Toe numerator
    let F = 0.30;  // Toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

fn tonemapFilmic(color: vec3f) -> vec3f {
    let exposureBias = 2.0;
    let curr = unchartedPartial(color * exposureBias);
    let W = vec3f(11.2);
    let whiteScale = vec3f(1.0) / unchartedPartial(W);
    return curr * whiteScale;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);

    // Apply exposure
    let exposed = color.rgb * uniforms.exposure;

    // Apply tone mapping based on mode
    var mapped: vec3f;
    if (uniforms.mode == 0) {
        // Reinhard
        mapped = tonemapReinhard(exposed, uniforms.whitePoint);
    } else if (uniforms.mode == 1) {
        // ACES
        mapped = tonemapACES(exposed);
    } else {
        // Filmic (Uncharted 2)
        mapped = tonemapFilmic(exposed);
    }

    return vec4f(mapped, color.a);
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder for cleaner pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(ToneMapUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(ToneMapUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void ToneMap::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    ToneMapUniforms uniforms = {};
    uniforms.exposure = exposure;
    uniforms.mode = mode;
    uniforms.whitePoint = whitePoint;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(ToneMapUniforms);
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

void ToneMap::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
