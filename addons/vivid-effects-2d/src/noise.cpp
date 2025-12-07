// Vivid Effects 2D - Noise Operator Implementation

#include <vivid/effects/noise.h>
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
struct NoiseUniforms {
    float time;
    float scale;
    float speed;
    float lacunarity;
    float persistence;
    float offsetX;
    float offsetY;
    int octaves;
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

Noise::~Noise() {
    cleanup();
}

void Noise::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Noise::createPipeline(Context& ctx) {
    // Load shader
    std::string shaderSource = loadShaderSource("noise.wgsl");
    if (shaderSource.empty()) {
        // Fallback: embedded minimal shader
        shaderSource = R"(
struct Uniforms {
    time: f32,
    scale: f32,
    speed: f32,
    lacunarity: f32,
    persistence: f32,
    offsetX: f32,
    offsetY: f32,
    octaves: i32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

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

// Permutation and gradient functions for Perlin noise
fn permute(x: vec4f) -> vec4f {
    return (((x * 34.0) + 1.0) * x) % 289.0;
}

fn taylorInvSqrt(r: vec4f) -> vec4f {
    return 1.79284291400159 - 0.85373472095314 * r;
}

fn fade(t: vec2f) -> vec2f {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

fn perlin(P: vec2f) -> f32 {
    var Pi = floor(P) % 289.0;
    if (Pi.x < 0.0) { Pi.x += 289.0; }
    if (Pi.y < 0.0) { Pi.y += 289.0; }
    let Pf = fract(P);
    let ix = vec4f(Pi.x, Pi.x + 1.0, Pi.x, Pi.x + 1.0);
    let iy = vec4f(Pi.y, Pi.y, Pi.y + 1.0, Pi.y + 1.0);
    let fx = vec4f(Pf.x, Pf.x - 1.0, Pf.x, Pf.x - 1.0);
    let fy = vec4f(Pf.y, Pf.y, Pf.y - 1.0, Pf.y - 1.0);

    let i = permute(permute(ix) + iy);
    let phi = i * 0.0243902439;
    let gx = cos(phi * 6.283185307);
    let gy = sin(phi * 6.283185307);
    let g = vec4f(gx.x * fx.x + gy.x * fy.x,
                  gx.y * fx.y + gy.y * fy.y,
                  gx.z * fx.z + gy.z * fy.z,
                  gx.w * fx.w + gy.w * fy.w);

    let fade_xy = fade(Pf);
    let n_x = mix(vec2f(g.x, g.z), vec2f(g.y, g.w), fade_xy.x);
    return mix(n_x.x, n_x.y, fade_xy.y) * 0.5 + 0.5;
}

fn fbm(p: vec2f, octaves: i32, lacunarity: f32, persistence: f32) -> f32 {
    var value = 0.0;
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxValue = 0.0;

    for (var i = 0; i < octaves; i++) {
        value += amplitude * perlin(p * frequency);
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let p = input.uv * uniforms.scale + vec2f(uniforms.offsetX, uniforms.offsetY);
    let t = uniforms.time * uniforms.speed;

    // Animate by offsetting in z (simulated as additional x offset)
    let animatedP = p + vec2f(t * 0.1, t * 0.07);

    let n = fbm(animatedP, uniforms.octaves, uniforms.lacunarity, uniforms.persistence);

    return vec4f(n, n, n, 1.0);
}
)";
    }

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Noise Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Noise Uniforms");
    bufferDesc.size = sizeof(NoiseUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(NoiseUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("Noise Bind Group Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

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
    pipelineDesc.label = toStringView("Noise Pipeline");
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

void Noise::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Update uniforms
    NoiseUniforms uniforms = {};
    uniforms.time = static_cast<float>(ctx.time());
    uniforms.scale = m_scale;
    uniforms.speed = m_speed;
    uniforms.lacunarity = m_lacunarity;
    uniforms.persistence = m_persistence;
    uniforms.offsetX = m_offsetX;
    uniforms.offsetY = m_offsetY;
    uniforms.octaves = m_octaves;

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
}

void Noise::cleanup() {
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
