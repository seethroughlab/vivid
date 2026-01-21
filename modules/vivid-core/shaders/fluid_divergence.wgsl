// Fluid Simulation - Divergence Shader
// Compute divergence of velocity field for pressure solve

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
@group(0) @binding(2) var divergenceOut: texture_storage_2d<r16float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let x = i32(id.x);
    let y = i32(id.y);
    let w = i32(u.width);
    let h = i32(u.height);

    // Sample neighboring velocities (with boundary clamping)
    let vL = textureLoad(velocityIn, vec2i(max(x - 1, 0), y), 0).xy;
    let vR = textureLoad(velocityIn, vec2i(min(x + 1, w - 1), y), 0).xy;
    let vB = textureLoad(velocityIn, vec2i(x, max(y - 1, 0)), 0).xy;
    let vT = textureLoad(velocityIn, vec2i(x, min(y + 1, h - 1)), 0).xy;

    // Compute divergence using central differences
    let div = 0.5 * ((vR.x - vL.x) + (vT.y - vB.y));

    textureStore(divergenceOut, id.xy, vec4f(-div, 0.0, 0.0, 1.0));
}
