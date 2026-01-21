// Fluid Simulation - Clear Shader
// Clear velocity, pressure, and dye fields

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
@group(0) @binding(1) var velocityOut: texture_storage_2d<rg16float, write>;
@group(0) @binding(2) var pressureOut: texture_storage_2d<r16float, write>;
@group(0) @binding(3) var dyeOut: texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    textureStore(velocityOut, id.xy, vec4f(0.0, 0.0, 0.0, 1.0));
    textureStore(pressureOut, id.xy, vec4f(0.0, 0.0, 0.0, 1.0));
    textureStore(dyeOut, id.xy, vec4f(0.0, 0.0, 0.0, 0.0));
}
