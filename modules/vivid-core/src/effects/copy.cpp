// Vivid Effects 2D - Copy Operator Implementation

#include <vivid/effects/copy.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/context.h>
#include <cstring>
#include <cmath>
#include <sstream>

namespace vivid::effects {

Copy::~Copy() {
    cleanup();
}

void Copy::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);

    // Create uniform buffer (sized for max copies)
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Copy Uniforms");
    bufferDesc.size = sizeof(TransformData);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler
    m_sampler = gpu::getLinearClampSampler(ctx.device());

    // Create initial pipeline for default count
    getOrCreatePipeline(ctx, count);
    m_lastCount = count;
}

std::string Copy::generateShader(int copyCount) {
    std::ostringstream ss;

    // Uniform structure with transform matrices and opacities
    ss << R"(
struct Uniforms {
    transforms: array<mat4x4f, )" << MAX_COPIES << R"(>,
    opacities: array<vec4f, )" << (MAX_COPIES / 4) << R"(>,
    count: i32,
    _pad0: i32,
    _pad1: i32,
    _pad2: i32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

fn getOpacity(idx: i32) -> f32 {
    let vec_idx = idx / 4;
    let comp_idx = idx % 4;
    let v = u.opacities[vec_idx];
    if (comp_idx == 0) { return v.x; }
    if (comp_idx == 1) { return v.y; }
    if (comp_idx == 2) { return v.z; }
    return v.w;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var result = vec4f(0.0);
)";

    // Unrolled loop for each copy (back to front for correct compositing)
    for (int i = copyCount - 1; i >= 0; i--) {
        ss << "    {\n";
        ss << "        let transformed = u.transforms[" << i << "] * vec4f(in.uv, 0.0, 1.0);\n";
        ss << "        let uv = transformed.xy;\n";
        ss << "        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {\n";
        ss << "            let sample = textureSample(inputTex, texSampler, uv);\n";
        ss << "            let alpha = sample.a * getOpacity(" << i << ");\n";
        ss << "            result = mix(result, vec4f(sample.rgb, 1.0), alpha);\n";
        ss << "        }\n";
        ss << "    }\n";
    }

    ss << "    return result;\n}\n";

    return ss.str();
}

WGPURenderPipeline Copy::getOrCreatePipeline(Context& ctx, int copyCount) {
    auto it = m_pipelineCache.find(copyCount);
    if (it != m_pipelineCache.end()) {
        return it->second;
    }

    // Generate shader for this count
    std::string fragmentShader = generateShader(copyCount);
    std::string shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;

    // Build pipeline
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(TransformData))
           .sampler(1)
           .texture(2);

    WGPURenderPipeline pipeline = builder.build();

    // Store bind group layout (same for all counts)
    if (!m_bindGroupLayout) {
        m_bindGroupLayout = builder.bindGroupLayout();
    }

    m_pipelineCache[copyCount] = pipeline;
    return pipeline;
}

void Copy::computeTransforms() {
    int n = static_cast<int>(count);
    float pivotX = pivot.x();
    float pivotY = pivot.y();

    // Clear transform data
    std::memset(&m_transformData, 0, sizeof(m_transformData));
    m_transformData.count = n;

    for (int i = 0; i < n; i++) {
        float tx = 0.0f, ty = 0.0f;
        float rot = 0.0f;
        float scl = 1.0f;

        switch (static_cast<CopyMode>(mode)) {
            case CopyMode::Linear: {
                tx = offset.x() * static_cast<float>(i);
                ty = offset.y() * static_cast<float>(i);
                rot = static_cast<float>(rotationStep) * static_cast<float>(i);
                scl = std::pow(static_cast<float>(scaleStep), static_cast<float>(i));
                break;
            }
            case CopyMode::Radial: {
                float start = static_cast<float>(startAngle);
                float end = static_cast<float>(endAngle);
                float angle = start + (end - start) * static_cast<float>(i) / static_cast<float>(n);
                float r = static_cast<float>(radius);
                tx = std::cos(angle) * r;
                ty = std::sin(angle) * r;
                rot = angle;  // Face outward
                break;
            }
            case CopyMode::Grid: {
                int cols = static_cast<int>(columns);
                int col = i % cols;
                int row = i / cols;
                tx = static_cast<float>(col) * spacing.x() - (static_cast<float>(cols - 1) * spacing.x() * 0.5f);
                ty = static_cast<float>(row) * spacing.y() - (static_cast<float>((n - 1) / cols) * spacing.y() * 0.5f);
                break;
            }
        }

        // Build inverse transform matrix (for UV lookup)
        // Order: translate to pivot, scale, rotate, translate back, apply offset
        float c = std::cos(-rot);
        float s = std::sin(-rot);
        float invScl = 1.0f / scl;

        // Combined inverse transform matrix
        // UV' = (UV - pivot) * invScale * invRotate + pivot - offset
        float* m = &m_transformData.transforms[i * 16];

        // Row-major mat4x4 for WGSL (but stored column-major in memory)
        // Column 0
        m[0] = c * invScl;
        m[1] = s * invScl;
        m[2] = 0.0f;
        m[3] = 0.0f;

        // Column 1
        m[4] = -s * invScl;
        m[5] = c * invScl;
        m[6] = 0.0f;
        m[7] = 0.0f;

        // Column 2
        m[8] = 0.0f;
        m[9] = 0.0f;
        m[10] = 1.0f;
        m[11] = 0.0f;

        // Column 3 (translation)
        // Combined: -pivot * invScaleRot + pivot - offset
        float transX = -pivotX * c * invScl + pivotY * s * invScl + pivotX - tx;
        float transY = -pivotX * s * invScl - pivotY * c * invScl + pivotY - ty;
        m[12] = transX;
        m[13] = transY;
        m[14] = 0.0f;
        m[15] = 1.0f;

        // Compute opacity with falloff
        float falloff = static_cast<float>(opacityFalloff);
        float op = std::pow(1.0f - falloff, static_cast<float>(i));

        // Store in packed vec4 array
        int vecIdx = i / 4;
        int compIdx = i % 4;
        reinterpret_cast<float*>(&m_transformData.opacities)[vecIdx * 4 + compIdx] = op;
    }
}

void Copy::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    matchInputResolution(0);

    // Get input texture view
    WGPUTextureView inView = TextureOperator::inputView(0);
    if (!inView) {
        return;
    }

    int currentCount = static_cast<int>(count);

    // Get or create pipeline for current count
    WGPURenderPipeline pipeline = getOrCreatePipeline(ctx, currentCount);
    m_lastCount = currentCount;

    // Compute transforms
    computeTransforms();

    // Upload uniforms
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &m_transformData, sizeof(m_transformData));

    // Create bind group
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = sizeof(TransformData);

    entries[1].binding = 1;
    entries[1].sampler = m_sampler;

    entries[2].binding = 2;
    entries[2].textureView = inView;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 3;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Render
    WGPUCommandEncoder encoder = ctx.gpuEncoder();
    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    endRenderPass(pass, encoder, ctx);

    wgpuBindGroupRelease(bindGroup);
    didCook();
}

void Copy::cleanup() {
    for (auto& [count, pipeline] : m_pipelineCache) {
        gpu::release(pipeline);
    }
    m_pipelineCache.clear();

    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    m_sampler = nullptr;
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
