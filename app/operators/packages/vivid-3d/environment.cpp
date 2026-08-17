// vivid-3d operator: Environment — an image-based-lighting (IBL) source (ADR-0060 Phase 2).
//
// Emits an ENVIRONMENT scene fragment carrying the baked IBL cubemaps + BRDF LUT that Render3D (and,
// via Phase 2, SDF3D) sample for diffuse ambient + specular reflections. The environment is a
// PROCEDURAL dark-stage sky (ADR-0058: near-black with a subtle horizon glow and cyan/magenta signal
// accents) — no assets, always available. HDR-equirect loading can be added later behind the same
// fragment contract.
//
// The bake is a standard split-sum IBL pipeline, run once and cached (re-baked only when a param
// changes), all render-pass based (no compute), using the shared cube helpers in gpu_3d.h:
//   1. sky        — render 6 faces of the procedural sky into a base cube (RGBA16Float)
//   2. irradiance — convolve the base cube over the hemisphere → diffuse irradiance cube (low-res)
//   3. prefilter  — GGX importance-sample the base cube per roughness mip → specular cube (mip chain)
//   4. brdf lut   — the split-sum environment BRDF integration → RG16Float 2D LUT
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_3d.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {

// Minimal IEEE float32 → float16 (half) pack — enough for HDR upload (denormals flushed to zero,
// truncating mantissa). Used to load an equirect .hdr into an RGBA16Float (filterable) texture.
inline uint16_t f32_to_f16(float f) {
    uint32_t x; std::memcpy(&x, &f, 4);
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xffu) - 127 + 15;
    const uint32_t mant = x & 0x7fffffu;
    if (exp <= 0)  return static_cast<uint16_t>(sign);                       // underflow → 0
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7c00u);            // overflow → inf
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
}

constexpr WGPUTextureFormat kCubeFmt = WGPUTextureFormat_RGBA16Float;
constexpr WGPUTextureFormat kLutFmt  = WGPUTextureFormat_RG16Float;
constexpr uint32_t kBaseSize   = 128;   // base sky + prefiltered face size
constexpr uint32_t kIrrSize    = 32;    // irradiance face size (low-freq, cheap)
constexpr uint32_t kLutSize    = 256;   // BRDF LUT
constexpr uint32_t kPrefMips   = 5;     // prefiltered roughness mips (0 = mirror … 4 = rough)

// Per-face basis (right, up, forward) mapping a fullscreen uv in [-1,1] to a world direction, in the
// standard cube-face order the WGSL texture_cube sampler expects: +X,-X,+Y,-Y,+Z,-Z.
struct FaceBasis { float r[3], u[3], f[3]; };
constexpr std::array<FaceBasis, 6> kFaces = {{
    {{ 0, 0,-1},{ 0,-1, 0},{ 1, 0, 0}},   // +X
    {{ 0, 0, 1},{ 0,-1, 0},{-1, 0, 0}},   // -X
    {{ 1, 0, 0},{ 0, 0, 1},{ 0, 1, 0}},   // +Y
    {{ 1, 0, 0},{ 0, 0,-1},{ 0,-1, 0}},   // -Y
    {{ 1, 0, 0},{ 0,-1, 0},{ 0, 0, 1}},   // +Z
    {{-1, 0, 0},{ 0,-1, 0},{ 0, 0,-1}},   // -Z
}};

// A 64-byte uniform: face basis (3 x vec4) + params (roughness, intensity, _, _).
struct FaceUniform { float right[4], up[4], fwd[4], params[4]; };
static_assert(sizeof(FaceUniform) == 64, "FaceUniform must be 64 bytes");

// ---- WGSL: the procedural dark-stage sky, evaluated per direction ----
const char* kSkyWGSL = R"(
struct FaceU { right: vec4f, up: vec4f, fwd: vec4f, params: vec4f };
@group(0) @binding(0) var<uniform> u: FaceU;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }

fn sky(dir: vec3f) -> vec3f {
    let d = normalize(dir);
    let t = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);          // 0 = straight down, 1 = zenith
    // A near-black DARK STAGE (ADR-0058) so the accents read as luminous reflections, not a bright room.
    let ground  = vec3f(0.003, 0.003, 0.006);
    let horizon = vec3f(0.025, 0.030, 0.055);
    let zenith  = vec3f(0.006, 0.008, 0.020);
    var col = mix(ground, horizon, smoothstep(0.0, 0.5, t));
    col = mix(col, zenith, smoothstep(0.5, 1.0, t));
    // A soft warm horizon glow band.
    let band = exp(-pow((t - 0.5) * 7.0, 2.0));
    col += vec3f(0.05, 0.045, 0.08) * band;
    // Two mid-width "studio" signal sources the chrome reflects as bright moving highlights: a cyan key
    // and a magenta rim on opposite azimuths near the horizon (ADR-0058 signal palette).
    let cyanDir    = normalize(vec3f( 0.85, 0.22,  0.5));
    let magentaDir = normalize(vec3f(-0.75, 0.14, -0.6));
    col += vec3f(0.10, 0.90, 1.15) * pow(max(dot(d, cyanDir),    0.0), 26.0) * 1.5;
    col += vec3f(1.15, 0.16, 0.66) * pow(max(dot(d, magentaDir), 0.0), 26.0) * 1.3;
    return col;
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv * 2.0 - 1.0;
    let dir = u.fwd.xyz + uv.x * u.right.xyz + uv.y * u.up.xyz;
    return vec4f(sky(dir), 1.0);
}
)";

// ---- WGSL: equirectangular HDR → base cube (samples the loaded 2D equirect by direction) ----
const char* kEquirectWGSL = R"(
struct FaceU { right: vec4f, up: vec4f, fwd: vec4f, params: vec4f };
@group(0) @binding(0) var<uniform> u: FaceU;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var equirect: texture_2d<f32>;
const PI: f32 = 3.14159265359;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv * 2.0 - 1.0;
    let d = normalize(u.fwd.xyz + uv.x * u.right.xyz + uv.y * u.up.xyz);
    let lon = atan2(d.z, d.x);
    let lat = asin(clamp(d.y, -1.0, 1.0));
    let euv = vec2f(lon / (2.0 * PI) + 0.5, 0.5 - lat / PI);
    return vec4f(textureSampleLevel(equirect, samp, euv, 0.0).rgb, 1.0);
}
)";

// ---- WGSL: irradiance convolution (diffuse) — samples the base cube over the hemisphere ----
const char* kIrradianceWGSL = R"(
struct FaceU { right: vec4f, up: vec4f, fwd: vec4f, params: vec4f };
@group(0) @binding(0) var<uniform> u: FaceU;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var env: texture_cube<f32>;
const PI: f32 = 3.14159265359;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv * 2.0 - 1.0;
    let N = normalize(u.fwd.xyz + uv.x * u.right.xyz + uv.y * u.up.xyz);
    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) { up = vec3f(1.0, 0.0, 0.0); }
    let right = normalize(cross(up, N));
    up = cross(N, right);
    var irr = vec3f(0.0);
    var nsamp = 0.0;
    let dphi = 0.20;
    let dtheta = 0.10;
    for (var phi = 0.0; phi < 2.0 * PI; phi += dphi) {
        for (var theta = 0.0; theta < 0.5 * PI; theta += dtheta) {
            let tv = vec3f(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            let dir = tv.x * right + tv.y * up + tv.z * N;
            irr += textureSampleLevel(env, samp, dir, 0.0).rgb * cos(theta) * sin(theta);
            nsamp += 1.0;
        }
    }
    irr = PI * irr / nsamp;
    return vec4f(irr, 1.0);
}
)";

// ---- WGSL: GGX prefilter (specular) — importance-sample the base cube at a given roughness ----
const char* kPrefilterWGSL = R"(
struct FaceU { right: vec4f, up: vec4f, fwd: vec4f, params: vec4f };  // params.x = roughness
@group(0) @binding(0) var<uniform> u: FaceU;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var env: texture_cube<f32>;
const PI: f32 = 3.14159265359;
fn radicalInverse(bitsIn: u32) -> f32 {
    var bits = bitsIn;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}
fn hammersley(i: u32, n: u32) -> vec2f { return vec2f(f32(i) / f32(n), radicalInverse(i)); }
fn importanceGGX(xi: vec2f, N: vec3f, rough: f32) -> vec3f {
    let a = rough * rough;
    let phi = 2.0 * PI * xi.x;
    let ct = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    let st = sqrt(1.0 - ct * ct);
    let H = vec3f(cos(phi) * st, sin(phi) * st, ct);
    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) { up = vec3f(1.0, 0.0, 0.0); }
    let tx = normalize(cross(up, N));
    let ty = cross(N, tx);
    return normalize(tx * H.x + ty * H.y + N * H.z);
}
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv * 2.0 - 1.0;
    let N = normalize(u.fwd.xyz + uv.x * u.right.xyz + uv.y * u.up.xyz);
    let R = N; let V = N;
    let rough = u.params.x;
    let NUM = 128u;
    var prefiltered = vec3f(0.0);
    var wsum = 0.0;
    for (var i = 0u; i < NUM; i++) {
        let xi = hammersley(i, NUM);
        let H = importanceGGX(xi, N, rough);
        let L = normalize(2.0 * dot(V, H) * H - V);
        let ndl = max(dot(N, L), 0.0);
        if (ndl > 0.0) {
            prefiltered += textureSampleLevel(env, samp, L, 0.0).rgb * ndl;
            wsum += ndl;
        }
    }
    if (wsum > 0.0) { prefiltered = prefiltered / wsum; }
    return vec4f(prefiltered, 1.0);
}
)";

// ---- WGSL: split-sum BRDF LUT (Karis) → RG ----
const char* kBrdfWGSL = R"(
const PI: f32 = 3.14159265359;
fn radicalInverse(bitsIn: u32) -> f32 {
    var bits = bitsIn;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}
fn hammersley(i: u32, n: u32) -> vec2f { return vec2f(f32(i) / f32(n), radicalInverse(i)); }
fn importanceGGX(xi: vec2f, N: vec3f, rough: f32) -> vec3f {
    let a = rough * rough;
    let phi = 2.0 * PI * xi.x;
    let ct = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    let st = sqrt(1.0 - ct * ct);
    let H = vec3f(cos(phi) * st, sin(phi) * st, ct);
    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.z) > 0.999) { up = vec3f(1.0, 0.0, 0.0); }
    let tx = normalize(cross(up, N));
    let ty = cross(N, tx);
    return normalize(tx * H.x + ty * H.y + N * H.z);
}
fn gSchlick(ndv: f32, rough: f32) -> f32 {
    let k = (rough * rough) / 2.0;
    return ndv / (ndv * (1.0 - k) + k);
}
fn gSmith(N: vec3f, V: vec3f, L: vec3f, rough: f32) -> f32 {
    return gSchlick(max(dot(N, V), 0.0), rough) * gSchlick(max(dot(N, L), 0.0), rough);
}
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec2f {
    let ndv = max(inp.uv.x, 0.001);
    let rough = 1.0 - inp.uv.y;   // uv.y 0=top; map so bottom = rough 0
    let V = vec3f(sqrt(1.0 - ndv * ndv), 0.0, ndv);
    let N = vec3f(0.0, 0.0, 1.0);
    var A = 0.0; var B = 0.0;
    let NUM = 512u;
    for (var i = 0u; i < NUM; i++) {
        let xi = hammersley(i, NUM);
        let H = importanceGGX(xi, N, rough);
        let L = normalize(2.0 * dot(V, H) * H - V);
        let ndl = max(L.z, 0.0);
        let ndh = max(H.z, 0.0);
        let vdh = max(dot(V, H), 0.0);
        if (ndl > 0.0) {
            let G = gSmith(N, V, L, rough);
            let gvis = (G * vdh) / (ndh * ndv + 1e-5);
            let fc = pow(1.0 - vdh, 5.0);
            A += (1.0 - fc) * gvis;
            B += fc * gvis;
        }
    }
    return vec2f(A / f32(NUM), B / f32(NUM));
}
)";

}  // namespace

struct Environment : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Environment";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;
    static constexpr const char* kDisplayName = "Environment";
    static constexpr const char* kSummary =
        "An image-based-lighting source: a procedural dark-stage sky baked into IBL cubemaps + a BRDF "
        "LUT, so metal/glossy surfaces (meshes AND SDF metaballs) get diffuse ambient and specular "
        "reflections. Merge it into the scene alongside Light3D.";
    static constexpr std::array<const char*, 3> kKeywords = {"environment", "ibl", "reflection"};

    vivid::Param<float> intensity{"intensity", 1.0f, 0.0f, 8.0f};
    vivid::Param<vivid::FilePath> file{"file", ""};

    Environment() {
        vivid::description(intensity, "Environment reflection/ambient intensity");
        vivid::description(file, "Equirectangular HDR (.hdr) to load — empty = procedural dark-stage sky");
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&intensity); o.push_back(&file); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_OUTPUT));
    }

    ~Environment() override {
        vivid::gpu::release(sky_pipe_); vivid::gpu::release(irr_pipe_);
        vivid::gpu::release(pref_pipe_); vivid::gpu::release(brdf_pipe_);
        vivid::gpu::release(sky_sh_); vivid::gpu::release(irr_sh_);
        vivid::gpu::release(pref_sh_); vivid::gpu::release(brdf_sh_);
        vivid::gpu::release(face_ubo_);
        vivid::gpu::release(equirect_pipe_); vivid::gpu::release(equirect_sh_);
        vivid::gpu::release(equirect_pl_); vivid::gpu::release(equirect_layout_);
        if (equirect_view_) wgpuTextureViewRelease(equirect_view_);
        if (equirect_tex_) wgpuTextureRelease(equirect_tex_);
        if (sampler_) wgpuSamplerRelease(sampler_);
        if (sky_view_) wgpuTextureViewRelease(sky_view_);
        if (irr_view_) wgpuTextureViewRelease(irr_view_);
        if (pref_view_) wgpuTextureViewRelease(pref_view_);
        if (lut_view_) wgpuTextureViewRelease(lut_view_);
        if (sky_tex_) wgpuTextureRelease(sky_tex_);
        if (irr_tex_) wgpuTextureRelease(irr_tex_);
        if (pref_tex_) wgpuTextureRelease(pref_tex_);
        if (lut_tex_) wgpuTextureRelease(lut_tex_);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (init_failed_) return;
        if (!ready_ && !lazy_init(ctx)) { init_failed_ = true; return; }
        if (file.str_value != loaded_file_) { loaded_file_ = file.str_value; load_equirect(ctx); baked_ = false; }
        if (!baked_) { bake(ctx); baked_ = true; }

        fragment_ = {};
        fragment_.fragment_type = vivid::gpu::VividSceneFragment::ENVIRONMENT;
        fragment_.ibl_irradiance  = irr_view_;
        fragment_.ibl_prefiltered = pref_view_;
        fragment_.ibl_brdf_lut    = lut_view_;
        fragment_.ibl_sampler     = sampler_;
        fragment_.ibl_intensity   = intensity.value;
        if (ctx->custom_outputs && ctx->custom_output_count > 0)
            ctx->custom_outputs[0] = &fragment_;
    }

private:
    bool lazy_init(const VividGpuContext* ctx) {
        std::string err;
        // Bake bind group layout: face UBO (0) + sampler (1) + source cube (2). The sky pass uses only
        // binding 0, but a single layout with all three keeps one pipeline layout for every bake pass
        // (the sky pass just leaves the sampler/texture bindings on their fallbacks... actually the sky
        // pass needs its own single-binding layout — build two layouts).
        {
            WGPUBindGroupLayoutEntry e{};
            e.binding = 0; e.visibility = WGPUShaderStage_Fragment;
            e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = sizeof(FaceUniform);
            WGPUBindGroupLayoutDescriptor d{}; d.entryCount = 1; d.entries = &e;
            sky_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &d);
            WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &sky_layout_;
            sky_pl_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pld);
        }
        {
            WGPUBindGroupLayoutEntry e[3]{};
            e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
            e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = sizeof(FaceUniform);
            e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
            e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
            e[2].texture.sampleType = WGPUTextureSampleType_Float; e[2].texture.viewDimension = WGPUTextureViewDimension_Cube;
            WGPUBindGroupLayoutDescriptor d{}; d.entryCount = 3; d.entries = e;
            bake_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &d);
            WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bake_layout_;
            bake_pl_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pld);
        }
        // BRDF LUT uses no bindings.
        {
            WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 0;
            brdf_pl_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pld);
        }

        sky_sh_ = vivid::gpu::create_shader_checked(ctx->device, kSkyWGSL, "Env Sky", err);
        if (!sky_sh_ || !err.empty()) return false;
        sky_pipe_ = vivid::gpu::create_pipeline(ctx->device, sky_sh_, sky_pl_, kCubeFmt, "Env Sky");

        irr_sh_ = vivid::gpu::create_shader_checked(ctx->device, kIrradianceWGSL, "Env Irr", err);
        if (!irr_sh_ || !err.empty()) return false;
        irr_pipe_ = vivid::gpu::create_pipeline(ctx->device, irr_sh_, bake_pl_, kCubeFmt, "Env Irr");

        pref_sh_ = vivid::gpu::create_shader_checked(ctx->device, kPrefilterWGSL, "Env Pref", err);
        if (!pref_sh_ || !err.empty()) return false;
        pref_pipe_ = vivid::gpu::create_pipeline(ctx->device, pref_sh_, bake_pl_, kCubeFmt, "Env Pref");

        brdf_sh_ = vivid::gpu::create_shader_checked(ctx->device, kBrdfWGSL, "Env BRDF", err);
        if (!brdf_sh_ || !err.empty()) return false;
        brdf_pipe_ = vivid::gpu::create_pipeline(ctx->device, brdf_sh_, brdf_pl_, kLutFmt, "Env BRDF");

        if (!sky_pipe_ || !irr_pipe_ || !pref_pipe_ || !brdf_pipe_) return false;

        face_ubo_ = vivid::gpu::create_uniform_buffer(ctx->device, sizeof(FaceUniform), "Env Face UBO");
        sampler_  = vivid::gpu::create_clamp_linear_sampler(ctx->device, "Env IBL Sampler");

        sky_tex_  = vivid::gpu::create_cubemap_texture(ctx->device, kBaseSize, 1, kCubeFmt, "Env Sky Cube");
        sky_view_ = vivid::gpu::create_cubemap_view(sky_tex_, kCubeFmt, 1, "Env Sky View");
        irr_tex_  = vivid::gpu::create_cubemap_texture(ctx->device, kIrrSize, 1, kCubeFmt, "Env Irr Cube");
        irr_view_ = vivid::gpu::create_cubemap_view(irr_tex_, kCubeFmt, 1, "Env Irr View");
        pref_tex_ = vivid::gpu::create_cubemap_texture(ctx->device, kBaseSize, kPrefMips, kCubeFmt, "Env Pref Cube");
        pref_view_= vivid::gpu::create_cubemap_view(pref_tex_, kCubeFmt, kPrefMips, "Env Pref View");

        // BRDF LUT: a plain 2D render target.
        {
            WGPUTextureDescriptor td{}; td.label = vivid_sv("Env BRDF LUT");
            td.size = { kLutSize, kLutSize, 1 }; td.mipLevelCount = 1; td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D; td.format = kLutFmt;
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
            lut_tex_ = wgpuDeviceCreateTexture(ctx->device, &td);
            WGPUTextureViewDescriptor vd{}; vd.format = kLutFmt; vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1; vd.arrayLayerCount = 1;
            lut_view_ = wgpuTextureCreateView(lut_tex_, &vd);
        }

        // Equirect→cube pass (its own layout: binding 2 is a 2D texture, not a cube).
        {
            WGPUBindGroupLayoutEntry e[3]{};
            e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
            e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = sizeof(FaceUniform);
            e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
            e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
            e[2].texture.sampleType = WGPUTextureSampleType_Float; e[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            WGPUBindGroupLayoutDescriptor d{}; d.entryCount = 3; d.entries = e;
            equirect_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &d);
            WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &equirect_layout_;
            equirect_pl_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pld);
        }
        equirect_sh_ = vivid::gpu::create_shader_checked(ctx->device, kEquirectWGSL, "Env Equirect", err);
        if (!equirect_sh_ || !err.empty()) return false;
        equirect_pipe_ = vivid::gpu::create_pipeline(ctx->device, equirect_sh_, equirect_pl_, kCubeFmt, "Env Equirect");
        if (!equirect_pipe_) return false;

        ready_ = true;
        return true;
    }

    void load_equirect(const VividGpuContext* ctx) {
        has_equirect_ = false;
        if (equirect_view_) { wgpuTextureViewRelease(equirect_view_); equirect_view_ = nullptr; }
        if (equirect_tex_)  { wgpuTextureRelease(equirect_tex_); equirect_tex_ = nullptr; }
        if (loaded_file_.empty()) return;
        int w = 0, h = 0, n = 0;
        float* data = stbi_loadf(loaded_file_.c_str(), &w, &h, &n, 4);
        if (!data) { std::fprintf(stderr, "[Environment] failed to load HDR: %s\n", loaded_file_.c_str()); return; }
        std::vector<uint16_t> half(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        for (size_t i = 0; i < half.size(); ++i) half[i] = f32_to_f16(data[i]);
        stbi_image_free(data);
        WGPUTextureDescriptor td{}; td.label = vivid_sv("Env Equirect");
        td.size = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA16Float;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        equirect_tex_ = wgpuDeviceCreateTexture(ctx->device, &td);
        WGPUTexelCopyTextureInfo dst{}; dst.texture = equirect_tex_; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout bl{}; bl.bytesPerRow = static_cast<uint32_t>(w) * 8; bl.rowsPerImage = static_cast<uint32_t>(h);
        WGPUExtent3D ext = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        wgpuQueueWriteTexture(ctx->queue, &dst, half.data(), half.size() * 2, &bl, &ext);
        WGPUTextureViewDescriptor vd{}; vd.format = WGPUTextureFormat_RGBA16Float;
        vd.dimension = WGPUTextureViewDimension_2D; vd.mipLevelCount = 1; vd.arrayLayerCount = 1;
        equirect_view_ = wgpuTextureCreateView(equirect_tex_, &vd);
        has_equirect_ = true;
        std::fprintf(stderr, "[Environment] loaded HDR %dx%d: %s\n", w, h, loaded_file_.c_str());
    }

    void write_face(const VividGpuContext* ctx, uint32_t face, float roughness) {
        FaceUniform fu{};
        for (int i = 0; i < 3; ++i) { fu.right[i] = kFaces[face].r[i]; fu.up[i] = kFaces[face].u[i]; fu.fwd[i] = kFaces[face].f[i]; }
        fu.params[0] = roughness;
        wgpuQueueWriteBuffer(ctx->queue, face_ubo_, 0, &fu, sizeof(fu));
    }

    WGPUBindGroup make_bg(const VividGpuContext* ctx, WGPUBindGroupLayout layout, bool with_env, WGPUTextureView envv) {
        WGPUBindGroupEntry e[3]{};
        e[0].binding = 0; e[0].buffer = face_ubo_; e[0].size = sizeof(FaceUniform);
        uint32_t n = 1;
        if (with_env) {
            e[1].binding = 1; e[1].sampler = sampler_;
            e[2].binding = 2; e[2].textureView = envv;
            n = 3;
        }
        WGPUBindGroupDescriptor d{}; d.layout = layout; d.entryCount = n; d.entries = e;
        return wgpuDeviceCreateBindGroup(ctx->device, &d);
    }

    void bake(const VividGpuContext* ctx) {
        // 1) Base cube (6 faces): the loaded equirect HDR if present, else the procedural sky.
        for (uint32_t f = 0; f < 6; ++f) {
            write_face(ctx, f, 0.0f);
            WGPUTextureView tv = vivid::gpu::create_cubemap_face_view(sky_tex_, kCubeFmt, f, 0, "base face");
            if (has_equirect_) {
                WGPUBindGroupEntry e[3]{};
                e[0].binding = 0; e[0].buffer = face_ubo_; e[0].size = sizeof(FaceUniform);
                e[1].binding = 1; e[1].sampler = sampler_;
                e[2].binding = 2; e[2].textureView = equirect_view_;
                WGPUBindGroupDescriptor d{}; d.layout = equirect_layout_; d.entryCount = 3; d.entries = e;
                WGPUBindGroup bg = wgpuDeviceCreateBindGroup(ctx->device, &d);
                vivid::gpu::run_pass(ctx->command_encoder, equirect_pipe_, bg, tv, "Env Equirect Bake");
                wgpuBindGroupRelease(bg);
            } else {
                WGPUBindGroup bg = make_bg(ctx, sky_layout_, false, nullptr);
                vivid::gpu::run_pass(ctx->command_encoder, sky_pipe_, bg, tv, "Env Sky Bake");
                wgpuBindGroupRelease(bg);
            }
            wgpuTextureViewRelease(tv);
        }
        // 2) Irradiance ← convolve sky (6 faces).
        for (uint32_t f = 0; f < 6; ++f) {
            write_face(ctx, f, 0.0f);
            WGPUBindGroup bg = make_bg(ctx, bake_layout_, true, sky_view_);
            WGPUTextureView tv = vivid::gpu::create_cubemap_face_view(irr_tex_, kCubeFmt, f, 0, "irr face");
            vivid::gpu::run_pass(ctx->command_encoder, irr_pipe_, bg, tv, "Env Irr Bake");
            wgpuTextureViewRelease(tv); wgpuBindGroupRelease(bg);
        }
        // 3) Prefilter ← GGX importance-sample sky, per roughness mip (6 faces × mips).
        for (uint32_t mip = 0; mip < kPrefMips; ++mip) {
            float rough = (kPrefMips > 1) ? (float)mip / (float)(kPrefMips - 1) : 0.0f;
            for (uint32_t f = 0; f < 6; ++f) {
                write_face(ctx, f, rough);
                WGPUBindGroup bg = make_bg(ctx, bake_layout_, true, sky_view_);
                WGPUTextureView tv = vivid::gpu::create_cubemap_face_view(pref_tex_, kCubeFmt, f, mip, "pref face");
                vivid::gpu::run_pass(ctx->command_encoder, pref_pipe_, bg, tv, "Env Pref Bake");
                wgpuTextureViewRelease(tv); wgpuBindGroupRelease(bg);
            }
        }
        // 4) BRDF LUT (no bindings).
        vivid::gpu::run_pass(ctx->command_encoder, brdf_pipe_, nullptr, lut_view_, "Env BRDF Bake");
    }

    // pipelines / shaders / layouts
    WGPUBindGroupLayout sky_layout_ = nullptr, bake_layout_ = nullptr;
    WGPUPipelineLayout  sky_pl_ = nullptr, bake_pl_ = nullptr, brdf_pl_ = nullptr;
    WGPUShaderModule sky_sh_ = nullptr, irr_sh_ = nullptr, pref_sh_ = nullptr, brdf_sh_ = nullptr;
    WGPURenderPipeline sky_pipe_ = nullptr, irr_pipe_ = nullptr, pref_pipe_ = nullptr, brdf_pipe_ = nullptr;
    WGPUBuffer face_ubo_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    // textures + views
    WGPUTexture sky_tex_ = nullptr, irr_tex_ = nullptr, pref_tex_ = nullptr, lut_tex_ = nullptr;
    WGPUTextureView sky_view_ = nullptr, irr_view_ = nullptr, pref_view_ = nullptr, lut_view_ = nullptr;
    // optional equirect HDR source
    WGPUBindGroupLayout equirect_layout_ = nullptr;
    WGPUPipelineLayout  equirect_pl_ = nullptr;
    WGPUShaderModule    equirect_sh_ = nullptr;
    WGPURenderPipeline  equirect_pipe_ = nullptr;
    WGPUTexture         equirect_tex_ = nullptr;
    WGPUTextureView     equirect_view_ = nullptr;
    std::string         loaded_file_;
    bool                has_equirect_ = false;

    vivid::gpu::VividSceneFragment fragment_{};
    bool ready_ = false, baked_ = false, init_failed_ = false;
};

VIVID_REGISTER(Environment)
