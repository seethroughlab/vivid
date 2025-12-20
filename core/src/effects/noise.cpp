// Vivid Effects 2D - Noise Operator Implementation

#include <vivid/effects/noise.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/pipeline_builder.h>
#include <vivid/asset_loader.h>
#include <vivid/context.h>
#include <cstring>
#include <string>

namespace vivid::effects {

// Uniform buffer structure (must match shader, 16-byte aligned)
struct NoiseUniforms {
    float time;
    float scale;
    float speed;
    float z;            // 3rd dimension
    float lacunarity;
    float persistence;
    float offsetX;
    float offsetY;
    int octaves;
    int noiseType;      // 0=Perlin, 1=Simplex, 2=Worley, 3=Value
    float _pad[2];      // Padding to 48 bytes (multiple of 16)
};

Noise::~Noise() {
    cleanup();
}

void Noise::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);
    createPipeline(ctx);
}

void Noise::createPipeline(Context& ctx) {
    // Load shader
    std::string shaderSource = AssetLoader::instance().loadShader("noise.wgsl");
    if (shaderSource.empty()) {
        // Fallback: embedded shader with all noise types (3D)
        // Uses shared vertex shader from gpu_common.h
        const char* fragmentShader = R"(
struct Uniforms {
    time: f32,
    scale: f32,
    speed: f32,
    z: f32,            // 3rd dimension
    lacunarity: f32,
    persistence: f32,
    offsetX: f32,
    offsetY: f32,
    octaves: i32,
    noiseType: i32,    // 0=Perlin, 1=Simplex, 2=Worley, 3=Value
    _pad1: f32,
    _pad2: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// ============================================================================
// Hash functions for 3D noise
// ============================================================================

fn hash31(p: vec3f) -> f32 {
    var p3 = fract(p * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

fn hash33(p: vec3f) -> vec3f {
    var p3 = fract(p * vec3f(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}

fn permute(x: vec4f) -> vec4f {
    return (((x * 34.0) + 1.0) * x) % 289.0;
}

fn taylorInvSqrt(r: vec4f) -> vec4f {
    return 1.79284291400159 - 0.85373472095314 * r;
}

fn fade3(t: vec3f) -> vec3f {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// ============================================================================
// 3D Perlin Noise - Classic gradient noise
// ============================================================================

fn perlin3D(P: vec3f) -> f32 {
    var Pi0 = floor(P);
    var Pi1 = Pi0 + vec3f(1.0);
    Pi0 = Pi0 % 289.0;
    Pi1 = Pi1 % 289.0;
    let Pf0 = fract(P);
    let Pf1 = Pf0 - vec3f(1.0);

    let ix = vec4f(Pi0.x, Pi1.x, Pi0.x, Pi1.x);
    let iy = vec4f(Pi0.yy, Pi1.yy);
    let iz0 = Pi0.zzzz;
    let iz1 = Pi1.zzzz;

    let ixy = permute(permute(ix) + iy);
    let ixy0 = permute(ixy + iz0);
    let ixy1 = permute(ixy + iz1);

    var gx0 = ixy0 / 7.0;
    var gy0 = fract(floor(gx0) / 7.0) - 0.5;
    gx0 = fract(gx0);
    var gz0 = vec4f(0.5) - abs(gx0) - abs(gy0);
    var sz0 = step(gz0, vec4f(0.0));
    gx0 = gx0 - sz0 * (step(vec4f(0.0), gx0) - 0.5);
    gy0 = gy0 - sz0 * (step(vec4f(0.0), gy0) - 0.5);

    var gx1 = ixy1 / 7.0;
    var gy1 = fract(floor(gx1) / 7.0) - 0.5;
    gx1 = fract(gx1);
    var gz1 = vec4f(0.5) - abs(gx1) - abs(gy1);
    var sz1 = step(gz1, vec4f(0.0));
    gx1 = gx1 - sz1 * (step(vec4f(0.0), gx1) - 0.5);
    gy1 = gy1 - sz1 * (step(vec4f(0.0), gy1) - 0.5);

    var g000 = vec3f(gx0.x, gy0.x, gz0.x);
    var g100 = vec3f(gx0.y, gy0.y, gz0.y);
    var g010 = vec3f(gx0.z, gy0.z, gz0.z);
    var g110 = vec3f(gx0.w, gy0.w, gz0.w);
    var g001 = vec3f(gx1.x, gy1.x, gz1.x);
    var g101 = vec3f(gx1.y, gy1.y, gz1.y);
    var g011 = vec3f(gx1.z, gy1.z, gz1.z);
    var g111 = vec3f(gx1.w, gy1.w, gz1.w);

    let norm0 = taylorInvSqrt(vec4f(dot(g000, g000), dot(g010, g010), dot(g100, g100), dot(g110, g110)));
    g000 = g000 * norm0.x;
    g010 = g010 * norm0.y;
    g100 = g100 * norm0.z;
    g110 = g110 * norm0.w;
    let norm1 = taylorInvSqrt(vec4f(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
    g001 = g001 * norm1.x;
    g011 = g011 * norm1.y;
    g101 = g101 * norm1.z;
    g111 = g111 * norm1.w;

    let n000 = dot(g000, Pf0);
    let n100 = dot(g100, vec3f(Pf1.x, Pf0.yz));
    let n010 = dot(g010, vec3f(Pf0.x, Pf1.y, Pf0.z));
    let n110 = dot(g110, vec3f(Pf1.xy, Pf0.z));
    let n001 = dot(g001, vec3f(Pf0.xy, Pf1.z));
    let n101 = dot(g101, vec3f(Pf1.x, Pf0.y, Pf1.z));
    let n011 = dot(g011, vec3f(Pf0.x, Pf1.yz));
    let n111 = dot(g111, Pf1);

    let fade_xyz = fade3(Pf0);
    let n_z = mix(vec4f(n000, n100, n010, n110), vec4f(n001, n101, n011, n111), fade_xyz.z);
    let n_yz = mix(n_z.xy, n_z.zw, fade_xyz.y);
    let n_xyz = mix(n_yz.x, n_yz.y, fade_xyz.x);
    return n_xyz * 0.5 + 0.5;
}

// ============================================================================
// 3D Simplex Noise - Improved gradient noise with fewer artifacts
// ============================================================================

fn simplex3D(v: vec3f) -> f32 {
    let C = vec2f(1.0/6.0, 1.0/3.0);
    let D = vec4f(0.0, 0.5, 1.0, 2.0);

    // First corner
    var i = floor(v + dot(v, C.yyy));
    let x0 = v - i + dot(i, C.xxx);

    // Other corners
    let g = step(x0.yzx, x0.xyz);
    let l = 1.0 - g;
    let i1 = min(g.xyz, l.zxy);
    let i2 = max(g.xyz, l.zxy);

    let x1 = x0 - i1 + C.xxx;
    let x2 = x0 - i2 + C.yyy;
    let x3 = x0 - D.yyy;

    // Permutations
    i = i % 289.0;
    let p = permute(permute(permute(
             i.z + vec4f(0.0, i1.z, i2.z, 1.0))
           + i.y + vec4f(0.0, i1.y, i2.y, 1.0))
           + i.x + vec4f(0.0, i1.x, i2.x, 1.0));

    // Gradients: 7x7 points over a square, mapped onto an octahedron
    let n_ = 0.142857142857;
    let ns = n_ * D.wyz - D.xzx;

    let j = p - 49.0 * floor(p * ns.z * ns.z);

    let x_ = floor(j * ns.z);
    let y_ = floor(j - 7.0 * x_);

    let x = x_ * ns.x + ns.yyyy;
    let y = y_ * ns.x + ns.yyyy;
    let h = 1.0 - abs(x) - abs(y);

    let b0 = vec4f(x.xy, y.xy);
    let b1 = vec4f(x.zw, y.zw);

    let s0 = floor(b0) * 2.0 + 1.0;
    let s1 = floor(b1) * 2.0 + 1.0;
    let sh = -step(h, vec4f(0.0));

    let a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    let a1 = b1.xzyw + s1.xzyw * sh.zzww;

    var p0 = vec3f(a0.xy, h.x);
    var p1 = vec3f(a0.zw, h.y);
    var p2 = vec3f(a1.xy, h.z);
    var p3 = vec3f(a1.zw, h.w);

    // Normalise gradients
    let norm = taylorInvSqrt(vec4f(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 = p0 * norm.x;
    p1 = p1 * norm.y;
    p2 = p2 * norm.z;
    p3 = p3 * norm.w;

    // Mix final noise value
    var m = max(vec4f(0.5) - vec4f(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), vec4f(0.0));
    m = m * m;
    return (dot(m * m, vec4f(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3))) * 105.0) * 0.5 + 0.5;
}

// ============================================================================
// 3D Worley/Voronoi Noise - Cellular patterns
// ============================================================================

fn worley3D(P: vec3f) -> f32 {
    let n = floor(P);
    let f = fract(P);

    var minDist = 1.0;

    for (var k = -1; k <= 1; k++) {
        for (var j = -1; j <= 1; j++) {
            for (var i = -1; i <= 1; i++) {
                let neighbor = vec3f(f32(i), f32(j), f32(k));
                let point = hash33(n + neighbor);
                let diff = neighbor + point - f;
                let dist = length(diff);
                minDist = min(minDist, dist);
            }
        }
    }

    return minDist;
}

// ============================================================================
// 3D Value Noise - Simple interpolated random values
// ============================================================================

fn valueNoise3D(P: vec3f) -> f32 {
    let i = floor(P);
    let f = fract(P);

    // Eight corners
    let a = hash31(i);
    let b = hash31(i + vec3f(1.0, 0.0, 0.0));
    let c = hash31(i + vec3f(0.0, 1.0, 0.0));
    let d = hash31(i + vec3f(1.0, 1.0, 0.0));
    let e = hash31(i + vec3f(0.0, 0.0, 1.0));
    let ff = hash31(i + vec3f(1.0, 0.0, 1.0));
    let g = hash31(i + vec3f(0.0, 1.0, 1.0));
    let h = hash31(i + vec3f(1.0, 1.0, 1.0));

    // Smooth interpolation
    let u = f * f * (3.0 - 2.0 * f);

    // Trilinear interpolation
    return mix(
        mix(mix(a, b, u.x), mix(c, d, u.x), u.y),
        mix(mix(e, ff, u.x), mix(g, h, u.x), u.y),
        u.z
    );
}

// ============================================================================
// Sample noise by type (3D)
// ============================================================================

fn sampleNoise3D(p: vec3f, noiseType: i32) -> f32 {
    if (noiseType == 1) {
        return simplex3D(p);
    } else if (noiseType == 2) {
        return worley3D(p);
    } else if (noiseType == 3) {
        return valueNoise3D(p);
    }
    return perlin3D(p);
}

// ============================================================================
// FBM - Fractal Brownian Motion for 3D noise
// ============================================================================

fn fbm3D(p: vec3f, octaves: i32, lacunarity: f32, persistence: f32, noiseType: i32) -> f32 {
    var value = 0.0;
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxValue = 0.0;

    for (var i = 0; i < octaves; i++) {
        value += amplitude * sampleNoise3D(p * frequency, noiseType);
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

// ============================================================================
// Fragment shader
// ============================================================================

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // XY from UV, Z from parameter + time animation
    let xy = input.uv * uniforms.scale + vec2f(uniforms.offsetX, uniforms.offsetY);
    let z = uniforms.z + uniforms.time * uniforms.speed;

    let p = vec3f(xy, z);
    let n = fbm3D(p, uniforms.octaves, uniforms.lacunarity, uniforms.persistence, uniforms.noiseType);

    return vec4f(n, n, n, 1.0);
}
)";
        shaderSource = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + fragmentShader;
    }

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Noise Uniforms");
    bufferDesc.size = sizeof(NoiseUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Build pipeline using PipelineBuilder
    gpu::PipelineBuilder builder(ctx.device());
    builder.shader(shaderSource)
           .colorTarget(EFFECTS_FORMAT)
           .uniform(0, sizeof(NoiseUniforms));

    m_pipeline = builder.build();
    m_bindGroupLayout = builder.bindGroupLayout();

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
}

void Noise::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Generators use their declared resolution (default 1280x720)

    // Noise is animated if speed > 0
    bool animated = (speed > 0.0f);
    if (!animated && !needsCook()) return;

    // Update uniforms
    NoiseUniforms uniforms = {};
    uniforms.time = static_cast<float>(ctx.time());
    uniforms.scale = scale;
    uniforms.speed = speed;
    uniforms.z = offset.z();
    uniforms.lacunarity = lacunarity;
    uniforms.persistence = persistence;
    uniforms.offsetX = offset.x();
    uniforms.offsetY = offset.y();
    uniforms.octaves = octaves;
    uniforms.noiseType = static_cast<int>(m_type);

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

void Noise::cleanup() {
    gpu::release(m_pipeline);
    gpu::release(m_bindGroup);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
