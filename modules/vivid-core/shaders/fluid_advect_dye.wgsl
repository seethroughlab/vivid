// Fluid Simulation - Dye Advection Shader
// Semi-Lagrangian advection for dye/density field

struct Uniforms {
    dt: f32,
    dissipation: f32,
    texelW: f32,
    texelH: f32,
    dyeDissipation: f32,
    viscosity: f32,
    vorticityScale: f32,
    forceScale: f32,
    width: u32,
    height: u32,
    _pad0: f32,
    _pad1: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var velocityIn: texture_2d<f32>;
@group(0) @binding(2) var dyeIn: texture_2d<f32>;
@group(0) @binding(3) var dyeOut: texture_storage_2d<rgba16float, write>;
@group(0) @binding(4) var linearSampler: sampler;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let texel = vec2f(u.texelW, u.texelH);
    let uv = (vec2f(id.xy) + 0.5) * texel;

    // Sample velocity
    let vel = textureSampleLevel(velocityIn, linearSampler, uv, 0.0).xy;

    // Semi-Lagrangian: trace back in time
    let backPos = uv - vel * u.dt * texel * vec2f(f32(u.width), f32(u.height));

    // Sample dye at traced position
    let advectedDye = textureSampleLevel(dyeIn, linearSampler, backPos, 0.0);

    // Apply dye dissipation
    let result = advectedDye * u.dyeDissipation;

    textureStore(dyeOut, id.xy, result);
}
