#pragma once

/**
 * @file gpu_common.h
 * @brief Common GPU utilities for effects
 *
 * Provides shared resources to reduce code duplication:
 * - Full-screen triangle vertex shader (used by all 2D effects)
 * - Cached samplers for common configurations
 * - Safe release helpers for GPU resources
 */

#include <webgpu/webgpu.h>
#include <cstring>

namespace vivid::effects::gpu {

// =============================================================================
// String Helper
// =============================================================================

/**
 * @brief Convert C string to WebGPU string view
 */
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView view;
    view.data = str;
    view.length = std::strlen(str);
    return view;
}

// =============================================================================
// Shared Vertex Shader
// =============================================================================

/**
 * @brief Standard full-screen triangle vertex shader for 2D effects
 *
 * Generates a triangle that covers the entire viewport using vertex index.
 * No vertex buffer required. Outputs UV coordinates for texture sampling.
 *
 * Usage in WGSL shader:
 * @code
 * // Concatenate with your fragment shader:
 * std::string shader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) + R"(
 * @fragment
 * fn fs_main(input: VertexOutput) -> @location(0) vec4f {
 *     return textureSample(tex, samp, input.uv);
 * }
 * )";
 * @endcode
 */
inline constexpr const char* FULLSCREEN_VERTEX_SHADER = R"(
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
)";

// =============================================================================
// Sampler Factory
// =============================================================================

/**
 * @brief Get a cached linear filtering, clamp-to-edge sampler
 *
 * Most common sampler for 2D effects. Samplers are cached per-device
 * and reused across all effects.
 *
 * @param device The WebGPU device
 * @return Cached sampler (do NOT release - managed internally)
 */
WGPUSampler getLinearClampSampler(WGPUDevice device);

/**
 * @brief Get a cached nearest filtering, clamp-to-edge sampler
 *
 * Useful for pixel-art or when exact texel values are needed.
 *
 * @param device The WebGPU device
 * @return Cached sampler (do NOT release - managed internally)
 */
WGPUSampler getNearestClampSampler(WGPUDevice device);

/**
 * @brief Get a cached linear filtering, repeat sampler
 *
 * Useful for tiling textures.
 *
 * @param device The WebGPU device
 * @return Cached sampler (do NOT release - managed internally)
 */
WGPUSampler getLinearRepeatSampler(WGPUDevice device);

// =============================================================================
// Resource Cleanup Helpers
// =============================================================================

/**
 * @brief Safe release helpers that check for null, release, and set to nullptr
 *
 * Usage:
 * @code
 * void cleanup() {
 *     gpu::release(m_pipeline);
 *     gpu::release(m_bindGroupLayout);
 *     gpu::release(m_buffer);
 * }
 * @endcode
 */

inline void release(WGPURenderPipeline& p) {
    if (p) { wgpuRenderPipelineRelease(p); p = nullptr; }
}

inline void release(WGPUComputePipeline& p) {
    if (p) { wgpuComputePipelineRelease(p); p = nullptr; }
}

inline void release(WGPUBindGroupLayout& l) {
    if (l) { wgpuBindGroupLayoutRelease(l); l = nullptr; }
}

inline void release(WGPUBindGroup& g) {
    if (g) { wgpuBindGroupRelease(g); g = nullptr; }
}

inline void release(WGPUBuffer& b) {
    if (b) { wgpuBufferRelease(b); b = nullptr; }
}

inline void release(WGPUSampler& s) {
    if (s) { wgpuSamplerRelease(s); s = nullptr; }
}

inline void release(WGPUTexture& t) {
    if (t) { wgpuTextureRelease(t); t = nullptr; }
}

inline void release(WGPUTextureView& v) {
    if (v) { wgpuTextureViewRelease(v); v = nullptr; }
}

inline void release(WGPUShaderModule& m) {
    if (m) { wgpuShaderModuleRelease(m); m = nullptr; }
}

inline void release(WGPUPipelineLayout& l) {
    if (l) { wgpuPipelineLayoutRelease(l); l = nullptr; }
}

// =============================================================================
// WGSL Shader Composition - Common Functions
// =============================================================================

/**
 * @brief Common mathematical constants for WGSL shaders
 *
 * Usage:
 * @code
 * std::string shader = std::string(gpu::FULLSCREEN_VERTEX_SHADER) +
 *                      gpu::wgsl::CONSTANTS +
 *                      myFragmentShader;
 * @endcode
 */
namespace wgsl {

inline constexpr const char* CONSTANTS = R"(
const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;
const E: f32 = 2.71828182846;
const PHI: f32 = 1.61803398875;
const SQRT2: f32 = 1.41421356237;
)";

/**
 * @brief RGB to HSV and HSV to RGB conversion functions
 */
inline constexpr const char* COLOR_CONVERT = R"(
fn rgb2hsv(c: vec3f) -> vec3f {
    let K = vec4f(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    let p = mix(vec4f(c.bg, K.wz), vec4f(c.gb, K.xy), vec4f(step(c.b, c.g)));
    let q = mix(vec4f(p.xyw, c.r), vec4f(c.r, p.yzx), vec4f(step(p.x, c.r)));
    let d = q.x - min(q.w, q.y);
    let e = 1.0e-10;
    return vec3f(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

fn hsv2rgb(c: vec3f) -> vec3f {
    let K = vec4f(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    let p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, vec3f(0.0), vec3f(1.0)), c.y);
}
)";

/**
 * @brief Common noise functions (hash, value noise, fbm)
 */
inline constexpr const char* NOISE_FUNCTIONS = R"(
fn hash21(p: vec2f) -> f32 {
    var p3 = fract(vec3f(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

fn hash22(p: vec2f) -> vec2f {
    var p3 = fract(vec3f(p.xyx) * vec3f(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

fn hash31(p: vec3f) -> f32 {
    var p3 = fract(p * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

fn valueNoise(p: vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);
    return mix(
        mix(hash21(i + vec2f(0.0, 0.0)), hash21(i + vec2f(1.0, 0.0)), u.x),
        mix(hash21(i + vec2f(0.0, 1.0)), hash21(i + vec2f(1.0, 1.0)), u.x),
        u.y
    );
}

fn fbm(p: vec2f, octaves: i32) -> f32 {
    var value = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    var pos = p;
    for (var i = 0; i < octaves; i++) {
        value += amplitude * valueNoise(pos * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}
)";

/**
 * @brief Simplex noise implementation
 */
inline constexpr const char* SIMPLEX_NOISE = R"(
fn mod289_3(x: vec3f) -> vec3f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn mod289_4(x: vec4f) -> vec4f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn permute(x: vec4f) -> vec4f { return mod289_4(((x * 34.0) + 1.0) * x); }
fn taylorInvSqrt(r: vec4f) -> vec4f { return 1.79284291400159 - 0.85373472095314 * r; }

fn simplexNoise(v: vec3f) -> f32 {
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

    i = mod289_3(i);
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
    p0 *= norm.x; p1 *= norm.y; p2 *= norm.z; p3 *= norm.w;

    var m = max(0.5 - vec4f(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), vec4f(0.0));
    m = m * m;
    return 105.0 * dot(m * m, vec4f(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}
)";

/**
 * @brief Blend mode functions for compositing
 */
inline constexpr const char* BLEND_MODES = R"(
fn blendMultiply(base: vec3f, blend: vec3f) -> vec3f { return base * blend; }
fn blendScreen(base: vec3f, blend: vec3f) -> vec3f { return 1.0 - (1.0 - base) * (1.0 - blend); }
fn blendOverlay(base: vec3f, blend: vec3f) -> vec3f {
    return mix(
        2.0 * base * blend,
        1.0 - 2.0 * (1.0 - base) * (1.0 - blend),
        step(vec3f(0.5), base)
    );
}
fn blendSoftLight(base: vec3f, blend: vec3f) -> vec3f {
    return mix(
        2.0 * base * blend + base * base * (1.0 - 2.0 * blend),
        sqrt(base) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend),
        step(vec3f(0.5), blend)
    );
}
fn blendHardLight(base: vec3f, blend: vec3f) -> vec3f { return blendOverlay(blend, base); }
fn blendColorDodge(base: vec3f, blend: vec3f) -> vec3f { return base / max(1.0 - blend, vec3f(0.0001)); }
fn blendColorBurn(base: vec3f, blend: vec3f) -> vec3f { return 1.0 - (1.0 - base) / max(blend, vec3f(0.0001)); }
fn blendDifference(base: vec3f, blend: vec3f) -> vec3f { return abs(base - blend); }
fn blendExclusion(base: vec3f, blend: vec3f) -> vec3f { return base + blend - 2.0 * base * blend; }
fn blendLinearDodge(base: vec3f, blend: vec3f) -> vec3f { return min(base + blend, vec3f(1.0)); }
fn blendLinearBurn(base: vec3f, blend: vec3f) -> vec3f { return max(base + blend - 1.0, vec3f(0.0)); }
)";

/**
 * @brief Common UV manipulation functions
 */
inline constexpr const char* UV_UTILS = R"(
fn rotateUV(uv: vec2f, angle: f32, center: vec2f) -> vec2f {
    let c = cos(angle);
    let s = sin(angle);
    let p = uv - center;
    return vec2f(p.x * c - p.y * s, p.x * s + p.y * c) + center;
}

fn scaleUV(uv: vec2f, scale: vec2f, center: vec2f) -> vec2f {
    return (uv - center) / scale + center;
}

fn tileUV(uv: vec2f, tiles: vec2f) -> vec2f {
    return fract(uv * tiles);
}

fn mirrorUV(uv: vec2f) -> vec2f {
    let m = abs(fract(uv * 0.5) * 2.0 - 1.0);
    return vec2f(1.0) - m;
}

fn polarUV(uv: vec2f, center: vec2f) -> vec2f {
    let p = uv - center;
    let r = length(p);
    let a = atan2(p.y, p.x) / 6.28318530718 + 0.5;
    return vec2f(a, r);
}
)";

/**
 * @brief SDF (Signed Distance Field) primitives
 */
inline constexpr const char* SDF_PRIMITIVES = R"(
fn sdCircle(p: vec2f, r: f32) -> f32 { return length(p) - r; }
fn sdBox(p: vec2f, b: vec2f) -> f32 { let d = abs(p) - b; return length(max(d, vec2f(0.0))) + min(max(d.x, d.y), 0.0); }
fn sdRoundedBox(p: vec2f, b: vec2f, r: f32) -> f32 { let d = abs(p) - b + r; return length(max(d, vec2f(0.0))) + min(max(d.x, d.y), 0.0) - r; }
fn sdSegment(p: vec2f, a: vec2f, b: vec2f) -> f32 { let pa = p - a; let ba = b - a; let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return length(pa - ba * h); }
fn sdTriangle(p: vec2f, p0: vec2f, p1: vec2f, p2: vec2f) -> f32 {
    let e0 = p1 - p0; let e1 = p2 - p1; let e2 = p0 - p2;
    let v0 = p - p0; let v1 = p - p1; let v2 = p - p2;
    let pq0 = v0 - e0 * clamp(dot(v0, e0) / dot(e0, e0), 0.0, 1.0);
    let pq1 = v1 - e1 * clamp(dot(v1, e1) / dot(e1, e1), 0.0, 1.0);
    let pq2 = v2 - e2 * clamp(dot(v2, e2) / dot(e2, e2), 0.0, 1.0);
    let s = sign(e0.x * e2.y - e0.y * e2.x);
    let d = min(min(vec2f(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                    vec2f(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                    vec2f(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
    return -sqrt(d.x) * sign(d.y);
}
fn sdStar(p: vec2f, r: f32, n: i32, m: f32) -> f32 {
    let an = 3.141593 / f32(n);
    let en = 3.141593 / m;
    let acs = vec2f(cos(an), sin(an));
    let ecs = vec2f(cos(en), sin(en));
    var bn = atan2(abs(p.x), p.y) % (2.0 * an) - an;
    var q = length(p) * vec2f(cos(bn), abs(sin(bn)));
    q = q - r * acs;
    q = q + ecs * clamp(-dot(q, ecs), 0.0, r * acs.y / ecs.y);
    return length(q) * sign(q.x);
}
)";

} // namespace wgsl

} // namespace vivid::effects::gpu
