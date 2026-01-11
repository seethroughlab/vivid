// Vivid Effects 2D - Crop Operator Implementation

#include <vivid/effects/crop.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>
#include <algorithm>

namespace vivid::effects {

struct CropUniforms {
    float left;
    float right;
    float top;
    float bottom;
};

Crop::~Crop() {
    cleanup();
}

void Crop::init(Context& ctx) {
    if (!beginInit()) return;
    createOutput(ctx);
    createPipeline(ctx);
}

void Crop::createPipeline(Context& ctx) {
    // Fragment shader - sample from crop region
    const char* fragmentShader = R"(
struct Uniforms {
    left: f32,
    right: f32,
    top: f32,
    bottom: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Remap UV from output space (0-1) to crop region in input space
    let cropWidth = uniforms.right - uniforms.left;
    let cropHeight = uniforms.bottom - uniforms.top;

    let sourceU = uniforms.left + input.uv.x * cropWidth;
    let sourceV = uniforms.top + input.uv.y * cropHeight;

    return textureSample(inputTex, texSampler, vec2f(sourceU, sourceV));
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder for cleaner pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(CropUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(CropUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void Crop::updateOutputSize(Context& ctx) {
    // Get input operator
    Operator* inputOp = getInput(0);
    if (!inputOp) return;

    // Get input dimensions (must be a texture operator)
    auto* texInput = dynamic_cast<TextureOperator*>(inputOp);
    if (!texInput) return;

    int inWidth = texInput->outputWidth();
    int inHeight = texInput->outputHeight();
    if (inWidth <= 0 || inHeight <= 0) return;

    // Calculate crop dimensions
    float l = std::clamp(static_cast<float>(left), 0.0f, 1.0f);
    float r = std::clamp(static_cast<float>(right), 0.0f, 1.0f);
    float t = std::clamp(static_cast<float>(top), 0.0f, 1.0f);
    float b = std::clamp(static_cast<float>(bottom), 0.0f, 1.0f);

    // Ensure valid ranges
    if (r <= l) r = l + 0.01f;
    if (b <= t) b = t + 0.01f;

    float cropWidth = r - l;
    float cropHeight = b - t;

    int outWidth = std::max(1, static_cast<int>(inWidth * cropWidth));
    int outHeight = std::max(1, static_cast<int>(inHeight * cropHeight));

    // Resize if dimensions changed
    if (m_width != outWidth || m_height != outHeight) {
        m_width = outWidth;
        m_height = outHeight;
        createOutput(ctx, outWidth, outHeight);
    }
}

void Crop::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Calculate and set output size based on crop region
    updateOutputSize(ctx);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    CropUniforms uniforms = {};
    uniforms.left = std::clamp(static_cast<float>(left), 0.0f, 1.0f);
    uniforms.right = std::clamp(static_cast<float>(right), 0.0f, 1.0f);
    uniforms.top = std::clamp(static_cast<float>(top), 0.0f, 1.0f);
    uniforms.bottom = std::clamp(static_cast<float>(bottom), 0.0f, 1.0f);

    // Ensure valid ranges
    if (uniforms.right <= uniforms.left) uniforms.right = uniforms.left + 0.01f;
    if (uniforms.bottom <= uniforms.top) uniforms.bottom = uniforms.top + 0.01f;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(CropUniforms);
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

void Crop::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
