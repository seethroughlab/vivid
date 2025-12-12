// Vivid Effects 2D - Ramp Operator Implementation

#include <vivid/effects/ramp.h>
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

// Find shader file relative to executable or source
static std::string findShaderPath(const std::string& name) {
    // Try relative to current working directory first (for development)
    fs::path devPath = fs::path("addons/vivid-effects-2d/shaders") / name;
    if (fs::exists(devPath)) {
        return devPath.string();
    }

    // Try relative to executable
#ifdef __APPLE__
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        fs::path exePath = fs::path(pathBuf).parent_path();
        fs::path shaderPath = exePath / "shaders" / name;
        if (fs::exists(shaderPath)) {
            return shaderPath.string();
        }
        // Also try ../../addons path for app bundle
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

Ramp::~Ramp() {
    cleanup();
}

void Ramp::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Ramp::createPipeline(Context& ctx) {
    // Load shader
    std::string shaderSource = loadShaderSource("ramp.wgsl");
    if (shaderSource.empty()) {
        // Fallback: embedded minimal shader
        shaderSource = R"(
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

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

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
    }

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Ramp Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Ramp Uniforms");
    bufferDesc.size = sizeof(RampUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(RampUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("Ramp Bind Group Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

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

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    // Create render pipeline
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Ramp Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    // Cleanup
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void Ramp::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }
    checkResize(ctx);

    // Ramp is animated if hueSpeed > 0
    bool animated = (m_hueSpeed > 0.0f);
    if (!animated && !needsCook()) return;

    // Update uniforms
    RampUniforms uniforms = {};
    uniforms.resolution[0] = static_cast<float>(ctx.width());
    uniforms.resolution[1] = static_cast<float>(ctx.height());
    uniforms.time = static_cast<float>(ctx.time());
    uniforms.rampType = static_cast<int>(m_type);
    uniforms.angle = m_angle;
    uniforms.offsetX = m_offset.x();
    uniforms.offsetY = m_offset.y();
    uniforms.scale = m_scale;
    uniforms.repeat = m_repeat;
    uniforms.hueOffset = m_hueOffset;
    uniforms.hueSpeed = m_hueSpeed;
    uniforms.hueRange = m_hueRange;
    uniforms.saturation = m_saturation;
    uniforms.brightness = m_brightness;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

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

void Ramp::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
