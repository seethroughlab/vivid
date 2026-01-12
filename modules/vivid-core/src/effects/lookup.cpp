// Vivid Effects 2D - Lookup Operator Implementation

#include <vivid/effects/lookup.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>

namespace vivid::effects {

struct LookupUniforms {
    float intensity;
    int32_t mode;  // 0=Luminance, 1=Red, 2=Green, 3=Blue
    float _pad1;
    float _pad2;
};

Lookup::~Lookup() {
    cleanup();
}

void Lookup::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void Lookup::createPipeline(Context& ctx) {
    // Fragment shader - lookup/colorize
    const char* fragmentShader = R"(
struct Uniforms {
    intensity: f32,
    mode: i32,
    _pad1: f32,
    _pad2: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var sourceTex: texture_2d<f32>;
@group(0) @binding(2) var lutTex: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let sourceColor = textureSample(sourceTex, texSampler, input.uv);

    // Calculate lookup coordinate based on mode
    var lookupCoord: f32;
    switch (uniforms.mode) {
        case 0: {
            // Luminance mode - use perceptual luminance
            lookupCoord = dot(sourceColor.rgb, vec3f(0.299, 0.587, 0.114));
        }
        case 1: {
            // Red channel
            lookupCoord = sourceColor.r;
        }
        case 2: {
            // Green channel
            lookupCoord = sourceColor.g;
        }
        case 3: {
            // Blue channel
            lookupCoord = sourceColor.b;
        }
        default: {
            lookupCoord = dot(sourceColor.rgb, vec3f(0.299, 0.587, 0.114));
        }
    }

    // Sample LUT horizontally (U = lookupCoord, V = 0.5)
    let lutUV = vec2f(lookupCoord, 0.5);
    let lutColor = textureSample(lutTex, texSampler, lutUV);

    // Blend between source and LUT result based on intensity
    let result = mix(sourceColor.rgb, lutColor.rgb, uniforms.intensity);

    return vec4f(result, sourceColor.a);
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder for cleaner pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(LookupUniforms))
           .texture(1)   // source
           .texture(2)   // LUT
           .sampler(3);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(LookupUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void Lookup::updateBindGroup(Context& ctx) {
    WGPUTextureView sourceView = inputView(0);
    WGPUTextureView lutView = inputView(1);

    if (!sourceView || !lutView) return;

    // Only recreate if inputs changed
    if (sourceView == m_lastSourceView && lutView == m_lastLutView && m_bindGroup) {
        return;
    }

    // Release old bind group
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }

    WGPUBindGroupEntry bindEntries[4] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(LookupUniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].textureView = sourceView;
    bindEntries[2].binding = 2;
    bindEntries[2].textureView = lutView;
    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = bindEntries;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    m_lastSourceView = sourceView;
    m_lastLutView = lutView;
}

void Lookup::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Match input resolution (from source texture)
    matchInputResolution(0);

    WGPUTextureView sourceView = inputView(0);
    WGPUTextureView lutView = inputView(1);

    if (!sourceView) return;
    if (!lutView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    // Update uniforms
    LookupUniforms uniforms = {};
    uniforms.intensity = intensity;
    uniforms.mode = static_cast<int32_t>(static_cast<LookupMode>(mode));

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Update bind group if inputs changed
    updateBindGroup(ctx);

    if (!m_bindGroup) return;

    // Use shared command encoder for batched submission
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);

    didCook();
}

void Lookup::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    m_lastSourceView = nullptr;
    m_lastLutView = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
