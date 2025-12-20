// Vivid Effects 2D - Feedback Operator Implementation

#include <vivid/effects/feedback.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <string>
#include <vivid/context.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace fs = std::filesystem;

namespace vivid::effects {

// Uniform buffer structure (must match shader)
struct FeedbackUniforms {
    float resolution[2];
    float decay;
    float mix_amount;
    float offsetX;
    float offsetY;
    float zoom;
    float rotate;
};

// Find shader file relative to executable or source
static std::string findShaderPath(const std::string& name) {
    fs::path devPath = fs::path("addons/vivid-effects-2d/shaders") / name;
    if (fs::exists(devPath)) {
        return devPath.string();
    }

#ifdef __APPLE__
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        fs::path exePath = fs::path(pathBuf).parent_path();
        fs::path shaderPath = exePath / "shaders" / name;
        if (fs::exists(shaderPath)) {
            return shaderPath.string();
        }
        shaderPath = exePath / ".." / ".." / "addons" / "vivid-effects-2d" / "shaders" / name;
        if (fs::exists(shaderPath)) {
            return shaderPath.string();
        }
    }
#endif

    return "";
}

static std::string loadShaderSource(const std::string& name) {
    std::string path = findShaderPath(name);
    if (path.empty()) {
        return "";
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Feedback::~Feedback() {
    cleanup();
}

void Feedback::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);
    createBufferTexture(ctx);
    createPipeline(ctx);

    m_firstFrame = true;
}

void Feedback::createBufferTexture(Context& ctx) {
    // Create feedback buffer texture (same size as output)
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Feedback Buffer");
    texDesc.size = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), 1};
    texDesc.format = EFFECTS_FORMAT;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;

    m_buffer = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;

    m_bufferView = wgpuTextureCreateView(m_buffer, &viewDesc);
}

void Feedback::createPipeline(Context& ctx) {
    std::string externalShader = loadShaderSource("feedback.wgsl");
    const char* fragmentShader;
    if (externalShader.empty()) {
        // Fallback embedded shader
        fragmentShader = R"(
struct Uniforms {
    resolution: vec2f,
    decay: f32,
    mix_amount: f32,
    offsetX: f32,
    offsetY: f32,
    zoom: f32,
    rotate: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTexture: texture_2d<f32>;
@group(0) @binding(3) var bufferTexture: texture_2d<f32>;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let input_color = textureSample(inputTexture, texSampler, in.uv);
    var feedback_uv = in.uv;
    let pixel_offset = vec2f(u.offsetX, u.offsetY) / u.resolution;
    feedback_uv = feedback_uv - pixel_offset;
    let center = vec2f(0.5, 0.5);
    feedback_uv = (feedback_uv - center) * u.zoom + center;
    let rotated = feedback_uv - center;
    let cos_r = cos(u.rotate);
    let sin_r = sin(u.rotate);
    feedback_uv = vec2f(
        rotated.x * cos_r - rotated.y * sin_r,
        rotated.x * sin_r + rotated.y * cos_r
    ) + center;
    let feedback_color = textureSample(bufferTexture, texSampler, feedback_uv) * u.decay;
    let result = mix(feedback_color, input_color, u.mix_amount);
    return result;
}
)";
    }

    // Build shader source - external shaders are complete, fallback needs vertex prepended
    std::string shaderSource;
    if (externalShader.empty()) {
        shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;
    } else {
        shaderSource = externalShader;  // External shader includes vertex shader
    }

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Feedback Uniforms");
    bufferDesc.size = sizeof(FeedbackUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use shared cached sampler (do NOT release - managed by gpu_common)
    m_sampler = gpu::getLinearClampSampler(ctx.device());

    // Build pipeline using PipelineBuilder
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(FeedbackUniforms))
           .sampler(1)
           .texture(2)
           .texture(3);

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();
}

void Feedback::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Match input resolution
    matchInputResolution(0);

    // Feedback is stateful - always cooks

    // Get input texture view
    WGPUTextureView inView = TextureOperator::inputView(0);
    if (!inView) {
        // No input - just pass through black
        return;
    }

    // Update uniforms
    FeedbackUniforms uniforms = {};
    uniforms.resolution[0] = static_cast<float>(m_width);
    uniforms.resolution[1] = static_cast<float>(m_height);
    uniforms.decay = decay;
    uniforms.mix_amount = m_firstFrame ? 1.0f : static_cast<float>(mix);  // First frame: 100% input
    uniforms.offsetX = offset.x();
    uniforms.offsetY = offset.y();
    uniforms.zoom = zoom;
    uniforms.rotate = rotate;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group with current textures
    WGPUBindGroupEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = sizeof(FeedbackUniforms);

    entries[1].binding = 1;
    entries[1].sampler = m_sampler;

    entries[2].binding = 2;
    entries[2].textureView = inView;

    entries[3].binding = 3;
    entries[3].textureView = m_bufferView;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    // Render to output
    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);

    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    endRenderPass(pass, encoder, ctx);

    // Copy output to buffer for next frame
    WGPUTexelCopyTextureInfo srcInfo = {};
    srcInfo.texture = m_output;

    WGPUTexelCopyTextureInfo dstInfo = {};
    dstInfo.texture = m_buffer;

    WGPUExtent3D copySize = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), 1};

    WGPUCommandEncoder copyEncoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);
    wgpuCommandEncoderCopyTextureToTexture(copyEncoder, &srcInfo, &dstInfo, &copySize);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(copyEncoder, &cmdBufferDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(copyEncoder);
    wgpuBindGroupRelease(bindGroup);

    m_firstFrame = false;
    didCook();
}

std::unique_ptr<OperatorState> Feedback::saveState() {
    // Save the buffer texture contents to CPU memory
    auto state = std::make_unique<TextureState>();
    state->width = m_width;
    state->height = m_height;

    if (!m_buffer || m_firstFrame) {
        return state;  // No buffer to save yet
    }

    // We need to read back the buffer texture
    // For now, just mark that we have state (actual readback is complex)
    // In a full implementation, we'd use wgpuBufferMapAsync to read pixels

    // Simplified: just preserve the flag that we're not on first frame
    state->pixels.resize(1);  // Non-empty means we had state
    state->pixels[0] = m_firstFrame ? 0 : 1;

    return state;
}

void Feedback::loadState(std::unique_ptr<OperatorState> state) {
    auto* texState = dynamic_cast<TextureState*>(state.get());
    if (texState && texState->hasData()) {
        // We had previous state - don't reset to first frame
        m_firstFrame = (texState->pixels[0] == 0);
    }
}

void Feedback::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    m_sampler = nullptr;
    gpu::release(m_bufferView);
    gpu::release(m_buffer);
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
