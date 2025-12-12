// Vivid Effects 2D - Shape Operator Implementation
// SDF-based shape generator

#include <vivid/effects/shape.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::effects {

struct ShapeUniforms {
    int shapeType;
    float sizeX;
    float sizeY;
    float posX;
    float posY;
    float rotation;
    int sides;
    float cornerRadius;
    float thickness;
    float softness;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float aspect;
    float _pad;
};

Shape::~Shape() {
    cleanup();
}

void Shape::init(Context& ctx) {
    if (m_initialized) return;
    createOutput(ctx);
    createPipeline(ctx);
    m_initialized = true;
}

void Shape::createPipeline(Context& ctx) {
    const char* shaderSource = R"(
struct Uniforms {
    shapeType: i32,
    sizeX: f32,
    sizeY: f32,
    posX: f32,
    posY: f32,
    rotation: f32,
    sides: i32,
    cornerRadius: f32,
    thickness: f32,
    softness: f32,
    colorR: f32,
    colorG: f32,
    colorB: f32,
    colorA: f32,
    aspect: f32,
    _pad: f32,
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

fn rotate2d(p: vec2f, a: f32) -> vec2f {
    let c = cos(a);
    let s = sin(a);
    return vec2f(p.x * c - p.y * s, p.x * s + p.y * c);
}

fn sdCircle(p: vec2f, r: f32) -> f32 {
    return length(p) - r;
}

fn sdBox(p: vec2f, b: vec2f) -> f32 {
    let d = abs(p) - b;
    return length(max(d, vec2f(0.0))) + min(max(d.x, d.y), 0.0);
}

fn sdRoundedBox(p: vec2f, b: vec2f, r: f32) -> f32 {
    let q = abs(p) - b + r;
    return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

fn sdEquilateralTriangle(p: vec2f, r: f32) -> f32 {
    let k = sqrt(3.0);
    var q = vec2f(abs(p.x) - r, p.y + r / k);
    if (q.x + k * q.y > 0.0) {
        q = vec2f(q.x - k * q.y, -k * q.x - q.y) / 2.0;
    }
    q.x -= clamp(q.x, -2.0 * r, 0.0);
    return -length(q) * sign(q.y);
}

fn sdStar(p: vec2f, r: f32, n: i32, m: f32) -> f32 {
    let an = PI / f32(n);
    let en = PI / m;
    let acs = vec2f(cos(an), sin(an));
    let ecs = vec2f(cos(en), sin(en));

    var q = vec2f(abs(p.x), p.y);
    let bn = (atan2(q.x, q.y) % (2.0 * an)) - an;
    q = length(q) * vec2f(cos(bn), abs(sin(bn)));
    q = q - r * acs;
    q = q + ecs * clamp(-dot(q, ecs), 0.0, r * acs.y / ecs.y);
    return length(q) * sign(q.x);
}

fn sdPolygon(p: vec2f, r: f32, n: i32) -> f32 {
    let an = TAU / f32(n);
    let he = r * tan(an * 0.5);
    var q = vec2f(abs(p.x), p.y);
    let bn = (atan2(q.x, q.y) % an) - an * 0.5;
    q = length(q) * vec2f(cos(bn), abs(sin(bn)));
    return q.x - r;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Transform UV to centered coordinates
    var p = (input.uv - vec2f(uniforms.posX, uniforms.posY)) * 2.0;
    p.x *= uniforms.aspect;

    // Apply rotation
    p = rotate2d(p, uniforms.rotation);

    // Calculate SDF based on shape type
    var d: f32;

    if (uniforms.shapeType == 0) {
        // Circle
        d = sdCircle(p, uniforms.sizeX);
    } else if (uniforms.shapeType == 1) {
        // Rectangle
        d = sdBox(p, vec2f(uniforms.sizeX, uniforms.sizeY));
    } else if (uniforms.shapeType == 2) {
        // Rounded Rectangle
        d = sdRoundedBox(p, vec2f(uniforms.sizeX, uniforms.sizeY), uniforms.cornerRadius);
    } else if (uniforms.shapeType == 3) {
        // Triangle
        d = sdEquilateralTriangle(p, uniforms.sizeX);
    } else if (uniforms.shapeType == 4) {
        // Star
        d = sdStar(p, uniforms.sizeX, uniforms.sides, 2.0);
    } else if (uniforms.shapeType == 5) {
        // Ring
        d = abs(sdCircle(p, uniforms.sizeX)) - uniforms.thickness;
    } else {
        // Polygon
        d = sdPolygon(p, uniforms.sizeX, uniforms.sides);
    }

    // Apply softness
    let alpha = 1.0 - smoothstep(-uniforms.softness, uniforms.softness, d);

    let color = vec4f(uniforms.colorR, uniforms.colorG, uniforms.colorB, uniforms.colorA * alpha);
    return color;
}
)";

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(ShapeUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    WGPUBindGroupLayoutEntry entries[1] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(ShapeUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = entries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    WGPUBindGroupEntry bindEntries[1] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(ShapeUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = bindEntries;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    // Enable alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
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

void Shape::process(Context& ctx) {
    if (!m_initialized) init(ctx);
    checkResize(ctx);

    if (!needsCook()) return;

    ShapeUniforms uniforms = {};
    uniforms.shapeType = static_cast<int>(m_type);
    uniforms.sizeX = m_size.x();
    uniforms.sizeY = m_size.y();
    uniforms.posX = m_position.x();
    uniforms.posY = m_position.y();
    uniforms.rotation = m_rotation;
    uniforms.sides = m_sides;
    uniforms.cornerRadius = m_cornerRadius;
    uniforms.thickness = m_thickness;
    uniforms.softness = m_softness;
    uniforms.colorR = m_color.r();
    uniforms.colorG = m_color.g();
    uniforms.colorB = m_color.b();
    uniforms.colorA = m_color.a();
    uniforms.aspect = static_cast<float>(m_width) / m_height;

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

void Shape::cleanup() {
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_bindGroupLayout) { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
