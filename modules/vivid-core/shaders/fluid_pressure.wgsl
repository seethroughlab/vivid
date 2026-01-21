// Fluid Simulation - Pressure Solve Shader
// Jacobi iteration for solving Poisson equation

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
@group(0) @binding(1) var pressureIn: texture_2d<f32>;
@group(0) @binding(2) var divergenceIn: texture_2d<f32>;
@group(0) @binding(3) var pressureOut: texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let x = i32(id.x);
    let y = i32(id.y);
    let w = i32(u.width);
    let h = i32(u.height);

    // Sample neighboring pressure (with boundary: pressure = 0 at edges)
    let pL = textureLoad(pressureIn, vec2i(max(x - 1, 0), y), 0).r;
    let pR = textureLoad(pressureIn, vec2i(min(x + 1, w - 1), y), 0).r;
    let pB = textureLoad(pressureIn, vec2i(x, max(y - 1, 0)), 0).r;
    let pT = textureLoad(pressureIn, vec2i(x, min(y + 1, h - 1)), 0).r;

    // Sample divergence
    let div = textureLoad(divergenceIn, vec2i(x, y), 0).r;

    // Jacobi iteration: solve Poisson equation (Laplacian(p) = divergence)
    let pressure = (pL + pR + pB + pT + div) * 0.25;

    textureStore(pressureOut, id.xy, vec4f(pressure, 0.0, 0.0, 1.0));
}
