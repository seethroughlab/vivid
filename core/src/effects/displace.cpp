// Vivid Effects 2D - Displace Operator Implementation

#include <vivid/effects/displace.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <string>
#include <vivid/context.h>
#include <cstring>

namespace vivid::effects {

// Uniform buffer structure (16-byte aligned)
struct DisplaceUniforms {
    float strength;
    float strengthX;
    float strengthY;
    float _pad;
};

Displace::~Displace() {
    cleanup();
}

void Displace::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Displace::createPipeline(Context& ctx) {
    // Embedded shader
    const char* fragmentShader = R"(
struct Uniforms {
    strength: f32,
    strengthX: f32,
    strengthY: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var sourceTex: texture_2d<f32>;
@group(0) @binding(2) var mapTex: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Sample displacement map
    let displacement = textureSample(mapTex, texSampler, input.uv);

    // Use R and G channels as X and Y displacement
    // Map from [0,1] to [-1,1] range
    let offsetX = (displacement.r - 0.5) * 2.0 * uniforms.strength * uniforms.strengthX;
    let offsetY = (displacement.g - 0.5) * 2.0 * uniforms.strength * uniforms.strengthY;

    // Apply displacement to UV coordinates
    let displacedUV = input.uv + vec2f(offsetX, offsetY);

    // Sample source texture at displaced coordinates
    return textureSample(sourceTex, texSampler, displacedUV);
}
)";

    // Combine shared vertex shader with effect-specific fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(DisplaceUniforms))
           .texture(1)    // source texture
           .texture(2)    // displacement map
           .sampler(3);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(DisplaceUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void Displace::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Match input resolution (from source input)
    matchInputResolution(0);

    // Get input textures
    WGPUTextureView sourceView = inputView(0);
    WGPUTextureView mapView = inputView(1);

    if (!sourceView || !mapView) {
        return;  // Need both inputs
    }

    if (!needsCook()) return;

    // Update uniforms
    DisplaceUniforms uniforms = {};
    uniforms.strength = strength;
    uniforms.strengthX = strengthX;
    uniforms.strengthY = strengthY;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group with current input textures
    WGPUBindGroupEntry bindEntries[4] = {};

    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].offset = 0;
    bindEntries[0].size = sizeof(DisplaceUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].textureView = sourceView;

    bindEntries[2].binding = 2;
    bindEntries[2].textureView = mapView;

    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("Displace Bind Group");
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

void Displace::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
