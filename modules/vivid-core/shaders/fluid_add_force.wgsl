// Fluid Simulation - Force Injection Shader
// Add external force impulse to velocity field

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

struct Impulse {
    posX: f32,
    posY: f32,
    valueX: f32,
    valueY: f32,
    valueZ: f32,
    valueW: f32,
    radius: f32,
    _pad: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var<uniform> impulse: Impulse;
@group(0) @binding(2) var velocityIn: texture_2d<f32>;
@group(0) @binding(3) var velocityOut: texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let uv = (vec2f(id.xy) + 0.5) / vec2f(f32(u.width), f32(u.height));
    let pos = vec2f(impulse.posX, impulse.posY);

    // Distance to impulse center
    let dist = length(uv - pos);

    // Gaussian falloff
    let sigma = impulse.radius;
    let weight = exp(-dist * dist / (2.0 * sigma * sigma));

    // Sample current velocity
    let vel = textureLoad(velocityIn, vec2i(id.xy), 0).xy;

    // Add force impulse
    let force = vec2f(impulse.valueX, impulse.valueY) * u.forceScale;
    let result = vel + force * weight * u.dt;

    textureStore(velocityOut, id.xy, vec4f(result, 0.0, 1.0));
}
