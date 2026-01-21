// Fluid Simulation - Velocity Advection Shader
// Semi-Lagrangian advection for velocity field

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
@group(0) @binding(2) var velocityOut: texture_storage_2d<rgba16float, write>;
@group(0) @binding(3) var linearSampler: sampler;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let texel = vec2f(u.texelW, u.texelH);
    let uv = (vec2f(id.xy) + 0.5) * texel;

    // Sample current velocity
    let vel = textureSampleLevel(velocityIn, linearSampler, uv, 0.0).xy;

    // Semi-Lagrangian: trace back in time
    let backPos = uv - vel * u.dt * texel * vec2f(f32(u.width), f32(u.height));

    // Sample velocity at traced position
    let advectedVel = textureSampleLevel(velocityIn, linearSampler, backPos, 0.0).xy;

    // Apply dissipation
    let result = advectedVel * u.dissipation;

    textureStore(velocityOut, id.xy, vec4f(result, 0.0, 1.0));
}
