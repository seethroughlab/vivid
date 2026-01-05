// Vivid Effects 2D - Ramp Operator Implementation

#include <vivid/effects/ramp.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/asset_loader.h>
#include <vivid/context.h>
#include <cstring>
#include <string>

namespace vivid::effects {

// Uniform buffer structure (must match shader)
// Note: Total size must be multiple of 16 for WebGPU alignment
struct RampUniforms {
    float resolution[2];  // 8 bytes
    float time;           // 4 bytes
    int rampType;         // 4 bytes (16 total)
    float angle;          // 4 bytes
    float offsetX;        // 4 bytes
    float offsetY;        // 4 bytes
    float scale;          // 4 bytes (32 total)
    float repeat;         // 4 bytes
    float hueOffset;      // 4 bytes
    float hueSpeed;       // 4 bytes
    float hueRange;       // 4 bytes (48 total)
    float saturation;     // 4 bytes
    float brightness;     // 4 bytes
    float _pad[2];        // 8 bytes (64 total)
};

Ramp::~Ramp() {
    cleanup();
}

void Ramp::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);
    createPipeline(ctx);
}

void Ramp::createPipeline(Context& ctx) {
    // Load shader
    std::string externalShaderSource = AssetLoader::instance().loadShader("ramp.wgsl");
    std::string shaderSource;
    if (externalShaderSource.empty()) {
        // Fallback: embedded minimal shader
        const char* fragmentShader = R"(
struct Uniforms {
    resolution: vec2f,
    time: f32,
    rampType: i32,
    angle: f32,
    offsetX: f32,
    offsetY: f32,
    scale: f32,
    repeat: f32,
    hueOffset: f32,
    hueSpeed: f32,
    hueRange: f32,
    saturation: f32,
    brightness: f32,
    _pad: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

fn hsv2rgb(hsv: vec3f) -> vec3f {
    let h = hsv.x;
    let s = hsv.y;
    let v = hsv.z;
    let c = v * s;
    let hp = h * 6.0;
    let x = c * (1.0 - abs(hp % 2.0 - 1.0));
    let m = v - c;
    var rgb: vec3f;
    if (hp < 1.0) { rgb = vec3f(c, x, 0.0); }
    else if (hp < 2.0) { rgb = vec3f(x, c, 0.0); }
    else if (hp < 3.0) { rgb = vec3f(0.0, c, x); }
    else if (hp < 4.0) { rgb = vec3f(0.0, x, c); }
    else if (hp < 5.0) { rgb = vec3f(x, 0.0, c); }
    else { rgb = vec3f(c, 0.0, x); }
    return rgb + vec3f(m, m, m);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var t = in.uv.x * u.repeat;
    t = fract(t);
    let hue = fract(u.hueOffset + u.time * u.hueSpeed + t * u.hueRange);
    let rgb = hsv2rgb(vec3f(hue, u.saturation, u.brightness));
    return vec4f(rgb, 1.0);
}
)";
        // Combine shared vertex shader with effect-specific fragment shader
        shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;
    } else {
        shaderSource = externalShaderSource;
    }

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Ramp Uniforms");
    bufferDesc.size = sizeof(RampUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Use PipelineBuilder to create pipeline and bind group layout
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(RampUniforms));

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

    // Create bind group
    WGPUBindGroupEntry bindEntry = {};
    bindEntry.binding = 0;
    bindEntry.buffer = m_uniformBuffer;
    bindEntry.offset = 0;
    bindEntry.size = sizeof(RampUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("Ramp Bind Group");
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);
}

void Ramp::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }
    // Generators use their declared resolution (default 1280x720)

    // Ramp is animated if hueSpeed > 0
    bool animated = (hueSpeed > 0.0f);
    if (!animated && !needsCook()) return;

    // Update uniforms
    RampUniforms uniforms = {};
    uniforms.resolution[0] = static_cast<float>(ctx.width());
    uniforms.resolution[1] = static_cast<float>(ctx.height());
    uniforms.time = static_cast<float>(ctx.time());
    uniforms.rampType = static_cast<int>(m_type);
    uniforms.angle = angle;
    uniforms.offsetX = offset.x();
    uniforms.offsetY = offset.y();
    uniforms.scale = scale;
    uniforms.repeat = repeat;
    uniforms.hueOffset = hueOffset;
    uniforms.hueSpeed = hueSpeed;
    uniforms.hueRange = hueRange;
    uniforms.saturation = saturation;
    uniforms.brightness = brightness;

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

void Ramp::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
