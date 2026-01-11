// Vivid Effects 2D - NormalMap Operator Implementation

#include <vivid/effects/normal_map.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>

namespace vivid::effects {

struct NormalMapUniforms {
    float strength;
    float flipY;
    float texelWidth;
    float texelHeight;
};

NormalMap::~NormalMap() {
    cleanup();
}

void NormalMap::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void NormalMap::createPipeline(Context& ctx) {
    // Fragment shader - Sobel-based normal map generation
    const char* fragmentShader = R"(
struct Uniforms {
    strength: f32,
    flipY: f32,
    texelWidth: f32,
    texelHeight: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

// Get height from texture (use luminance for grayscale conversion)
fn getHeight(uv: vec2f) -> f32 {
    let color = textureSample(inputTex, texSampler, uv);
    return dot(color.rgb, vec3f(0.2126, 0.7152, 0.0722));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let dx = uniforms.texelWidth;
    let dy = uniforms.texelHeight;

    // Sample 3x3 neighborhood for Sobel filter
    let tl = getHeight(uv + vec2f(-dx, -dy));  // top-left
    let t  = getHeight(uv + vec2f(0.0, -dy));  // top
    let tr = getHeight(uv + vec2f( dx, -dy));  // top-right
    let l  = getHeight(uv + vec2f(-dx, 0.0));  // left
    let r  = getHeight(uv + vec2f( dx, 0.0));  // right
    let bl = getHeight(uv + vec2f(-dx,  dy));  // bottom-left
    let b  = getHeight(uv + vec2f(0.0,  dy));  // bottom
    let br = getHeight(uv + vec2f( dx,  dy));  // bottom-right

    // Sobel filter for gradients
    // Gx = right - left (horizontal gradient)
    let gx = (tr + 2.0 * r + br) - (tl + 2.0 * l + bl);
    // Gy = bottom - top (vertical gradient)
    let gy = (bl + 2.0 * b + br) - (tl + 2.0 * t + tr);

    // Apply strength and flip Y if needed
    let scaledGx = -gx * uniforms.strength;
    var scaledGy = -gy * uniforms.strength;

    // Flip Y for DirectX convention (green channel inverted)
    scaledGy = mix(scaledGy, -scaledGy, uniforms.flipY);

    // Construct normal vector (tangent space, Z points toward viewer)
    let normal = normalize(vec3f(scaledGx, scaledGy, 1.0));

    // Encode normal to 0-1 range (0.5 = zero, 0 = -1, 1 = +1)
    let encoded = normal * 0.5 + 0.5;

    return vec4f(encoded, 1.0);
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder for cleaner pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(NormalMapUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(NormalMapUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void NormalMap::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    NormalMapUniforms uniforms = {};
    uniforms.strength = strength;
    uniforms.flipY = flipY;
    uniforms.texelWidth = 1.0f / static_cast<float>(m_width);
    uniforms.texelHeight = 1.0f / static_cast<float>(m_height);

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(NormalMapUniforms);
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

void NormalMap::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
