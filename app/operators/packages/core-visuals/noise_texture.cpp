// Core visual package operator: NoiseTexture — a 3D FBm noise generator (WGSL).
// Ported from vivid-classic:operators/gpu/noise/noise.cpp (shader math verbatim),
// re-expressed against the current app's loadable-operator ABI: a self-contained
// .cpp using VIVID_REGISTER, compiled at build time into a MODULE dylib that the
// host dlopen's from PlugIns/ at launch. Registered as "NoiseTexture" — audio and
// visual ops share one OpRegistry and classic ships an audio noise op, so the
// texture generator is disambiguated by name.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <array>
#include <cstdint>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// Fragment shader. The fullscreen-vertex preamble (FullscreenOutput +
// fullscreenTriangle) is prepended by create_shader_checked(); we define vs_main
// (the pipeline's vertex entry) + fs_main here. Noise functions are the standard
// Gustavson/Ashima/IQ ports carried over from vivid-classic.
const char* kNoiseWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}

// All params arrive NORMALIZED [0,1] (the app's convention: base is normalized,
// the op remaps to semantic ranges here; the inspector's min/max are display-only).
struct U {
    res: vec2f,
    time: f32,
    scale: f32,
    speed: f32,
    octaves: f32,
    lacunarity: f32,
    persistence: f32,
    noise_type: f32,
    channels: f32,
    scale_from_x: f32,
    scale_from_y: f32,
};
@group(0) @binding(0) var<uniform> u: U;

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
fn permute(x: vec4f) -> vec4f { return (((x * 34.0) + 1.0) * x) % 289.0; }
fn taylorInvSqrt(r: vec4f) -> vec4f { return 1.79284291400159 - 0.85373472095314 * r; }
fn fade3(t: vec3f) -> vec3f { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

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
    g000 = g000 * norm0.x; g010 = g010 * norm0.y; g100 = g100 * norm0.z; g110 = g110 * norm0.w;
    let norm1 = taylorInvSqrt(vec4f(dot(g001, g001), dot(g011, g011), dot(g101, g101), dot(g111, g111)));
    g001 = g001 * norm1.x; g011 = g011 * norm1.y; g101 = g101 * norm1.z; g111 = g111 * norm1.w;
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
    return mix(n_yz.x, n_yz.y, fade_xyz.x);
}

fn simplex3D(v: vec3f) -> f32 {
    let C = vec2f(1.0/6.0, 1.0/3.0);
    let D = vec4f(0.0, 0.5, 1.0, 2.0);
    var i = floor(v + dot(v, C.yyy));
    let x0 = v - i + dot(i, C.xxx);
    let g = step(x0.yzx, x0.xyz);
    let l = 1.0 - g;
    let i1 = min(g.xyz, l.zxy);
    let i2 = max(g.xyz, l.zxy);
    let x1 = x0 - i1 + C.xxx;
    let x2 = x0 - i2 + C.yyy;
    let x3 = x0 - D.yyy;
    i = i % 289.0;
    let p = permute(permute(permute(
             i.z + vec4f(0.0, i1.z, i2.z, 1.0))
           + i.y + vec4f(0.0, i1.y, i2.y, 1.0))
           + i.x + vec4f(0.0, i1.x, i2.x, 1.0));
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
    let norm = taylorInvSqrt(vec4f(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 = p0 * norm.x; p1 = p1 * norm.y; p2 = p2 * norm.z; p3 = p3 * norm.w;
    var m = max(vec4f(0.5) - vec4f(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), vec4f(0.0));
    m = m * m;
    return dot(m * m, vec4f(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3))) * 105.0;
}

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
                minDist = min(minDist, length(diff));
            }
        }
    }
    return minDist * 2.0 - 1.0;
}

fn valueNoise3D(P: vec3f) -> f32 {
    let i = floor(P);
    let f = fract(P);
    let a = hash31(i);
    let b = hash31(i + vec3f(1.0, 0.0, 0.0));
    let c = hash31(i + vec3f(0.0, 1.0, 0.0));
    let d = hash31(i + vec3f(1.0, 1.0, 0.0));
    let e = hash31(i + vec3f(0.0, 0.0, 1.0));
    let ff = hash31(i + vec3f(1.0, 0.0, 1.0));
    let g = hash31(i + vec3f(0.0, 1.0, 1.0));
    let h = hash31(i + vec3f(1.0, 1.0, 1.0));
    let uu = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(mix(a, b, uu.x), mix(c, d, uu.x), uu.y),
        mix(mix(e, ff, uu.x), mix(g, h, uu.x), uu.y),
        uu.z
    ) * 2.0 - 1.0;
}

fn sampleNoise3D(p: vec3f, noiseType: i32) -> f32 {
    if (noiseType == 1) { return simplex3D(p); }
    else if (noiseType == 2) { return worley3D(p); }
    else if (noiseType == 3) { return valueNoise3D(p); }
    return perlin3D(p);
}

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
    let normalized = value / max(maxValue, 1e-5);
    let corrected = clamp(normalized * 2.0, -1.0, 1.0);
    return corrected * 0.5 + 0.5;
}

@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    // Remap the normalized [0,1] params to their semantic ranges.
    let scale       = 0.5 + u.scale * 15.0;                  // 0.5 .. 15.5 zoom
    let speed       = u.speed * 5.0;                         // 0 .. 5
    let octaves     = i32(1.0 + round(u.octaves * 6.0));     // 1 .. 7 FBm layers
    let lacunarity  = 1.5 + u.lacunarity * 2.0;              // 1.5 .. 3.5
    let persistence = u.persistence;                         // 0 .. 1
    let noise_type  = i32(round(u.noise_type * 3.0));        // 0..3 Perlin/Simplex/Worley/Value
    let channels    = i32(round(u.channels * 2.0));          // 0..2 Mono/2ch/RGB

    let aspect = u.res.x / max(u.res.y, 1.0);
    let correctedUV = vec2f(inp.uv.x * aspect, inp.uv.y);
    let origin = vec2f(u.scale_from_x * aspect, u.scale_from_y);
    let xy = (correctedUV - origin) * scale + origin;
    let z = u.time * speed;
    let p = vec3f(xy, z);
    if (channels == 1) {
        let r = fbm3D(p, octaves, lacunarity, persistence, noise_type);
        let g = fbm3D(p + vec3f(100.0, 0.0, 0.0), octaves, lacunarity, persistence, noise_type);
        return vec4f(r, g, 0.0, 1.0);
    } else if (channels == 2) {
        let r = fbm3D(p, octaves, lacunarity, persistence, noise_type);
        let g = fbm3D(p + vec3f(100.0, 0.0, 0.0), octaves, lacunarity, persistence, noise_type);
        let b = fbm3D(p + vec3f(0.0, 100.0, 0.0), octaves, lacunarity, persistence, noise_type);
        return vec4f(r, g, b, 1.0);
    }
    let n = fbm3D(p, octaves, lacunarity, persistence, noise_type);
    return vec4f(n, n, n, 1.0);
}
)";

// std140/WGSL-aligned mirror of `struct U` — all normalized-f32 scalars after res.
// vec2f forces 8-byte struct alignment; 11 f32 fields = 48 bytes (a multiple of 8).
struct alignas(8) NoiseUniforms {
    float res[2];        // 0
    float time;          // 8
    float scale;         // 12
    float speed;         // 16
    float octaves;       // 20
    float lacunarity;    // 24
    float persistence;   // 28
    float noise_type;    // 32
    float channels;      // 36
    float scale_from_x;  // 40
    float scale_from_y;  // 44
};                       // 48, alignof 8 -> size 48
static_assert(sizeof(NoiseUniforms) == 48, "NoiseTexture uniforms must stay WGSL-aligned");
}  // namespace

struct NoiseTextureOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "NoiseTexture";
    static constexpr const char* kDisplayName = "Noise Texture";
    static constexpr const char* kSummary = "3D FBm noise generator (Perlin/Simplex/Worley/Value) animated over time.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "noise", "fbm"};

    // Params are normalized [0,1] (the app convention); the shader remaps to semantic
    // ranges. Enums carry a normalized index (base = idx / (choices-1)).
    vivid::Param<float> scale       {"scale",        0.35f, 0.f, 1.f};   // -> 0.5 .. 15.5 zoom
    vivid::Param<float> speed       {"speed",        0.2f,  0.f, 1.f};   // -> 0 .. 5
    vivid::Param<float> octaves     {"octaves",      0.5f,  0.f, 1.f};   // -> 1 .. 7 FBm layers
    vivid::Param<float> lacunarity  {"lacunarity",   0.33f, 0.f, 1.f};  // -> 1.5 .. 3.5
    vivid::Param<float> persistence {"persistence",  0.5f,  0.f, 1.f};   // -> 0 .. 1
    vivid::Param<int>   noise_type  {"noise_type",   0, {"Perlin", "Simplex", "Worley", "Value"}};
    vivid::Param<int>   channels    {"channels",     0, {"Mono", "2 Channel", "RGB"}};
    vivid::Param<float> scale_from_x{"scale_from_x", 0.5f,  0.f, 1.f};
    vivid::Param<float> scale_from_y{"scale_from_y", 0.5f,  0.f, 1.f};

    NoiseTextureOp() {
        vivid::description(scale, "Zoom level of the noise pattern");
        vivid::description(speed, "Rate of animation through the noise Z axis");
        vivid::description(octaves, "Number of FBm layers; more means finer detail");
        vivid::description(lacunarity, "Frequency multiplier between successive octaves");
        vivid::description(persistence, "Amplitude falloff between successive octaves");
        vivid::description(noise_type, "Algorithm: Perlin, Simplex, Worley, or Value");
        vivid::description(channels, "Output channels: Mono grayscale, 2 Channel RG, or full RGB");
        vivid::description(scale_from_x, "Horizontal UV origin the zoom scales about");
        vivid::description(scale_from_y, "Vertical UV origin the zoom scales about");
        vivid::semantic_intent(speed, "animation_rate");
    }

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&scale); o.push_back(&speed); o.push_back(&octaves);
        o.push_back(&lacunarity); o.push_back(&persistence); o.push_back(&noise_type);
        o.push_back(&channels); o.push_back(&scale_from_x); o.push_back(&scale_from_y);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    ~NoiseTextureOp() override {
        if (bg_) wgpuBindGroupRelease(bg_);
        if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_);
        if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_);
        if (sh_) wgpuShaderModuleRelease(sh_);
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kNoiseWGSL, "NoiseTexture", err);
        if (!sh_ || !err.empty()) { err_ = err.empty() ? "shader module null" : err; return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, sizeof(NoiseUniforms), "NoiseTexture U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = sizeof(NoiseUniforms);
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "NoiseTexture Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = sizeof(NoiseUniforms);
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!pipe_) { if (!lazy_init(c)) { init_failed_ = true; return; } }

        const float* p = c->param_values;
        auto pf = [&](int i, float d) { return p ? p[i] : d; };
        NoiseUniforms u{};
        u.res[0] = static_cast<float>(c->output_width);
        u.res[1] = static_cast<float>(c->output_height);
        u.time         = static_cast<float>(c->time);
        u.scale        = pf(0, scale.value);
        u.speed        = pf(1, speed.value);
        u.octaves      = pf(2, octaves.value);
        u.lacunarity   = pf(3, lacunarity.value);
        u.persistence  = pf(4, persistence.value);
        u.noise_type   = pf(5, static_cast<float>(noise_type.int_value()));
        u.channels     = pf(6, static_cast<float>(channels.int_value()));
        u.scale_from_x = pf(7, scale_from_x.value);
        u.scale_from_y = pf(8, scale_from_y.value);
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, &u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "NoiseTexture Pass");
    }

private:
    WGPUShaderModule    sh_  = nullptr;
    WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout  pl_  = nullptr;
    WGPURenderPipeline  pipe_ = nullptr;
    WGPUBuffer          ubo_ = nullptr;
    WGPUBindGroup       bg_  = nullptr;
    bool                init_failed_ = false;
    std::string         err_;
};

VIVID_REGISTER(NoiseTextureOp)
