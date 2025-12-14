// Vivid Effects 2D - Gradient Operator Implementation

#include <vivid/effects/gradient.h>
#include <vivid/context.h>

namespace vivid::effects {

struct GradientUniforms {
    int mode;
    float angle;
    float centerX;
    float centerY;
    float scale;
    float offset;
    float aspect;
    float _pad;
    float colorA[4];
    float colorB[4];
};

Gradient::~Gradient() {
    cleanup();
}

void Gradient::init(Context& ctx) {
    if (m_initialized) return;
    createOutput(ctx);
    createPipeline(ctx);
    m_initialized = true;
}

void Gradient::createPipeline(Context& ctx) {
    const char* shaderSource = R"(
struct Uniforms {
    mode: i32,
    angle: f32,
    centerX: f32,
    centerY: f32,
    scale: f32,
    offset: f32,
    aspect: f32,
    _pad: f32,
    colorA: vec4f,
    colorB: vec4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );
    var output: VertexOutput;
    output.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    output.uv = (positions[vertexIndex] + 1.0) * 0.5;
    output.uv.y = 1.0 - output.uv.y;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let center = vec2f(uniforms.centerX, uniforms.centerY);
    var p = input.uv - center;
    p.x *= uniforms.aspect;

    var t: f32;

    if (uniforms.mode == 0) {
        // Linear gradient
        let c = cos(uniforms.angle);
        let s = sin(uniforms.angle);
        let rotated = vec2f(p.x * c + p.y * s, -p.x * s + p.y * c);
        t = rotated.x * uniforms.scale + 0.5 + uniforms.offset;
    } else if (uniforms.mode == 1) {
        // Radial gradient
        t = length(p) * uniforms.scale * 2.0 + uniforms.offset;
    } else if (uniforms.mode == 2) {
        // Angular gradient
        t = (atan2(p.y, p.x) + PI) / TAU + uniforms.offset;
        t = fract(t * uniforms.scale);
    } else {
        // Diamond gradient
        t = (abs(p.x) + abs(p.y)) * uniforms.scale * 2.0 + uniforms.offset;
    }

    t = clamp(t, 0.0, 1.0);
    return mix(uniforms.colorA, uniforms.colorB, t);
}
)";

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(GradientUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    WGPUBindGroupLayoutEntry entries[1] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(GradientUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = entries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    WGPUBindGroupEntry bindEntries[1] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(GradientUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = bindEntries;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void Gradient::process(Context& ctx) {
    if (!m_initialized) init(ctx);
    // Generators use their declared resolution (default 1280x720)

    if (!needsCook()) return;

    GradientUniforms uniforms = {};
    uniforms.mode = static_cast<int>(m_mode);
    uniforms.angle = angle;
    uniforms.centerX = center.x();
    uniforms.centerY = center.y();
    uniforms.scale = scale;
    uniforms.offset = offset;
    uniforms.aspect = static_cast<float>(m_width) / m_height;
    uniforms.colorA[0] = colorA.r();
    uniforms.colorA[1] = colorA.g();
    uniforms.colorA[2] = colorA.b();
    uniforms.colorA[3] = colorA.a();
    uniforms.colorB[0] = colorB.r();
    uniforms.colorB[1] = colorB.g();
    uniforms.colorB[2] = colorB.b();
    uniforms.colorB[3] = colorB.a();

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);

    didCook();
}

void Gradient::cleanup() {
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_bindGroupLayout) { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
