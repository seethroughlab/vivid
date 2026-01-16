// Vivid Effects 2D - Fit Operator Implementation

#include <vivid/effects/fit.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <string>
#include <algorithm>
#include <cmath>

namespace vivid::effects {

struct FitUniforms {
    // UV transform: outputUV * scale + offset = inputUV
    float scaleX;
    float scaleY;
    float offsetX;
    float offsetY;
    // Background color
    float bgR;
    float bgG;
    float bgB;
    float bgA;
};

Fit::~Fit() {
    cleanup();
}

void Fit::init(Context& ctx) {
    if (!beginInit()) return;
    // Don't create output yet - we need to determine size first
    createPipeline(ctx);
}

void Fit::createPipeline(Context& ctx) {
    // Fragment shader - sample from fitted region with background
    const char* fragmentShader = R"(
struct Uniforms {
    scaleX: f32,
    scaleY: f32,
    offsetX: f32,
    offsetY: f32,
    bgR: f32,
    bgG: f32,
    bgB: f32,
    bgA: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Transform output UV to input UV
    let inputUV = input.uv * vec2f(uniforms.scaleX, uniforms.scaleY) + vec2f(uniforms.offsetX, uniforms.offsetY);

    // Check if we're outside the input texture bounds
    if (inputUV.x < 0.0 || inputUV.x > 1.0 || inputUV.y < 0.0 || inputUV.y > 1.0) {
        return vec4f(uniforms.bgR, uniforms.bgG, uniforms.bgB, uniforms.bgA);
    }

    return textureSample(inputTex, texSampler, inputUV);
}
)";

    // Combine shared vertex shader with fragment shader
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Use PipelineBuilder for cleaner pipeline creation
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(FitUniforms))
           .texture(1)
           .sampler(2);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(FitUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());
}

void Fit::updateOutputSize(Context& ctx) {
    // Get input operator
    Operator* inputOp = getInput(0);
    if (!inputOp) return;

    // Get input dimensions (must be a texture operator)
    auto* texInput = dynamic_cast<TextureOperator*>(inputOp);
    if (!texInput) return;

    int inWidth = texInput->outputWidth();
    int inHeight = texInput->outputHeight();
    if (inWidth <= 0 || inHeight <= 0) return;

    // Determine output size based on fit mode
    int outWidth, outHeight;
    FitScaleMode mode = static_cast<FitScaleMode>(static_cast<int>(fitMode));

    if (mode == FitScaleMode::Native) {
        // Use input's native resolution
        outWidth = inWidth;
        outHeight = inHeight;
    } else {
        // Use target resolution
        outWidth = std::max(1, static_cast<int>(width));
        outHeight = std::max(1, static_cast<int>(height));
    }

    // Resize output if dimensions changed
    if (m_width != outWidth || m_height != outHeight) {
        m_width = outWidth;
        m_height = outHeight;
        createOutput(ctx, outWidth, outHeight);
    }
}

void Fit::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    // Update output size based on fit mode and parameters
    updateOutputSize(ctx);

    WGPUTextureView inView = inputView(0);
    if (!inView) return;

    // Skip if nothing changed
    if (!needsCook()) return;

    // Get input dimensions
    Operator* inputOp = getInput(0);
    auto* texInput = dynamic_cast<TextureOperator*>(inputOp);
    if (!texInput) return;

    int inWidth = texInput->outputWidth();
    int inHeight = texInput->outputHeight();
    if (inWidth <= 0 || inHeight <= 0) return;

    // Calculate UV transform based on fit mode
    FitUniforms uniforms = {};
    uniforms.bgR = backgroundColor.r();
    uniforms.bgG = backgroundColor.g();
    uniforms.bgB = backgroundColor.b();
    uniforms.bgA = backgroundColor.a();

    FitScaleMode mode = static_cast<FitScaleMode>(static_cast<int>(fitMode));
    FitHJustify hj = static_cast<FitHJustify>(static_cast<int>(hJustify));
    FitVJustify vj = static_cast<FitVJustify>(static_cast<int>(vJustify));

    float inputAspect = static_cast<float>(inWidth) / static_cast<float>(inHeight);
    float outputAspect = static_cast<float>(m_width) / static_cast<float>(m_height);

    switch (mode) {
        case FitScaleMode::Fit: {
            // Scale to fit entirely within bounds (letterbox/pillarbox)
            // The fitted image should appear smaller than or equal to output
            if (inputAspect > outputAspect) {
                // Input is wider - pillarbox (bars on top/bottom)
                // Scale by width, center vertically
                float scale = outputAspect / inputAspect;
                uniforms.scaleX = 1.0f;
                uniforms.scaleY = 1.0f / scale;
                uniforms.offsetX = 0.0f;

                // Vertical justify
                float margin = (1.0f - scale) / scale;
                switch (vj) {
                    case FitVJustify::Top:    uniforms.offsetY = 0.0f; break;
                    case FitVJustify::Center: uniforms.offsetY = -margin * 0.5f; break;
                    case FitVJustify::Bottom: uniforms.offsetY = -margin; break;
                }
            } else {
                // Input is taller - letterbox (bars on left/right)
                // Scale by height, center horizontally
                float scale = inputAspect / outputAspect;
                uniforms.scaleX = 1.0f / scale;
                uniforms.scaleY = 1.0f;
                uniforms.offsetY = 0.0f;

                // Horizontal justify
                float margin = (1.0f - scale) / scale;
                switch (hj) {
                    case FitHJustify::Left:   uniforms.offsetX = 0.0f; break;
                    case FitHJustify::Center: uniforms.offsetX = -margin * 0.5f; break;
                    case FitHJustify::Right:  uniforms.offsetX = -margin; break;
                }
            }
            break;
        }

        case FitScaleMode::Fill: {
            // Scale to fill bounds completely (may crop edges)
            // The fitted image should fill or exceed output
            if (inputAspect > outputAspect) {
                // Input is wider - crop sides
                float scale = inputAspect / outputAspect;
                uniforms.scaleX = scale;
                uniforms.scaleY = 1.0f;
                uniforms.offsetY = 0.0f;

                // Horizontal justify (where to crop)
                float margin = (scale - 1.0f);
                switch (hj) {
                    case FitHJustify::Left:   uniforms.offsetX = 0.0f; break;
                    case FitHJustify::Center: uniforms.offsetX = -margin * 0.5f; break;
                    case FitHJustify::Right:  uniforms.offsetX = -margin; break;
                }
            } else {
                // Input is taller - crop top/bottom
                float scale = outputAspect / inputAspect;
                uniforms.scaleX = 1.0f;
                uniforms.scaleY = scale;
                uniforms.offsetX = 0.0f;

                // Vertical justify (where to crop)
                float margin = (scale - 1.0f);
                switch (vj) {
                    case FitVJustify::Top:    uniforms.offsetY = 0.0f; break;
                    case FitVJustify::Center: uniforms.offsetY = -margin * 0.5f; break;
                    case FitVJustify::Bottom: uniforms.offsetY = -margin; break;
                }
            }
            break;
        }

        case FitScaleMode::Stretch:
            // Direct mapping - no aspect ratio preservation
            uniforms.scaleX = 1.0f;
            uniforms.scaleY = 1.0f;
            uniforms.offsetX = 0.0f;
            uniforms.offsetY = 0.0f;
            break;

        case FitScaleMode::Native:
            // Direct mapping at native resolution
            uniforms.scaleX = 1.0f;
            uniforms.scaleY = 1.0f;
            uniforms.offsetX = 0.0f;
            uniforms.offsetY = 0.0f;
            break;
    }

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(FitUniforms);
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

void Fit::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    // Note: m_sampler is managed by gpu_common cache, do not release
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
