// Vivid Effects 2D - Noise Operator Implementation

#include <vivid/effects/noise.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/asset_loader.h>
#include <vivid/context.h>
#include <cstring>
#include <string>

namespace vivid::effects {

// Uniform buffer structure (must match shader, 16-byte aligned)
struct NoiseUniforms {
    float resolution[2]; // width, height for aspect ratio correction
    float time;
    float scale;
    float speed;
    float z;            // 3rd dimension
    float lacunarity;
    float persistence;
    float offsetX;
    float offsetY;
    int octaves;
    int noiseType;      // 0=Perlin, 1=Simplex, 2=Worley, 3=Value
    int colorNoise;     // 0=grayscale, 1=RGB independent channels
    int centerOrigin;   // 0=corner origin, 1=center origin
};

Noise::~Noise() {
    cleanup();
}

void Noise::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);
    createPipeline(ctx);
}

void Noise::createPipeline(Context& ctx) {
    // Load shader
    std::string shaderSource = AssetLoader::instance().loadShader("noise.wgsl");

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Noise Uniforms");
    bufferDesc.size = sizeof(NoiseUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Build pipeline using PipelineBuilder
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(NoiseUniforms));

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create bind group
    WGPUBindGroupEntry bindEntry = {};
    bindEntry.binding = 0;
    bindEntry.buffer = m_uniformBuffer;
    bindEntry.offset = 0;
    bindEntry.size = sizeof(NoiseUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("Noise Bind Group");
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);
}

void Noise::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Generators use their declared resolution (default 1280x720)

    // Noise is animated if speed > 0
    bool animated = (speed > 0.0f);
    if (!animated && !needsCook()) return;

    // Update uniforms
    NoiseUniforms uniforms = {};
    uniforms.resolution[0] = static_cast<float>(m_width);
    uniforms.resolution[1] = static_cast<float>(m_height);
    uniforms.time = static_cast<float>(ctx.time());
    uniforms.scale = scale;
    uniforms.speed = speed;
    uniforms.z = offset.z();
    uniforms.lacunarity = lacunarity;
    uniforms.persistence = persistence;
    uniforms.offsetX = offset.x();
    uniforms.offsetY = offset.y();
    uniforms.octaves = octaves;
    uniforms.noiseType = type.index();
    uniforms.colorNoise = colorNoise ? 1 : 0;
    uniforms.centerOrigin = centerOrigin ? 1 : 0;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Use shared command encoder for batched submission
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

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

void Noise::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
