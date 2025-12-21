// Vivid Effects 2D - FilmGrain Operator Implementation

#include <vivid/effects/film_grain.h>
#include <vivid/context.h>

namespace vivid::effects {

void FilmGrain::process(Context& ctx) {
    m_time = static_cast<float>(ctx.time());
    SimpleTextureEffect::process(ctx);
}

const char* FilmGrain::fragmentShader() const {
    return R"(
struct Uniforms {
    intensity: f32,
    size: f32,
    speed: f32,
    time: f32,
    colored: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

// Simple hash for grain - inlined for WGSL compatibility
fn hash(p: vec2f, t: f32) -> f32 {
    var p3 = fract(vec3f(p.x, p.y, t) * 0.1031);
    p3 = p3 + dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);

    // Quantize time for film frame effect
    let frameTime = floor(uniforms.time * uniforms.speed);

    // Scale UV for grain density
    let grainUV = input.uv * vec2f(1920.0, 1080.0) / uniforms.size;

    // Generate grain
    let n = hash(grainUV, frameTime) - 0.5;

    // Color variation (if enabled)
    let nr = hash(grainUV + vec2f(100.0, 0.0), frameTime) - 0.5;
    let ng = hash(grainUV + vec2f(0.0, 100.0), frameTime) - 0.5;
    let nb = hash(grainUV + vec2f(100.0, 100.0), frameTime) - 0.5;
    let colorGrain = vec3f(nr, ng, nb) * uniforms.colored;
    let grain = vec3f(n) + colorGrain;

    // Apply with intensity
    let result = color.rgb + grain * uniforms.intensity;

    return vec4f(result, color.a);
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleTextureEffect<FilmGrain, FilmGrainUniforms>;

} // namespace vivid::effects
