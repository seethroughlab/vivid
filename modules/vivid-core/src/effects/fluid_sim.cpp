// Vivid Effects 2D - Fluid Simulation Implementation
// GPU-accelerated 2D Navier-Stokes solver

#include <vivid/effects/fluid_sim.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/asset_loader.h>
#include <vivid/context.h>
#include <cmath>
#include <algorithm>

namespace vivid::effects {

// Shader sources loaded from external files
static std::string s_advectVelocityShader;
static std::string s_advectDyeShader;
static std::string s_divergenceShader;
static std::string s_pressureShader;
static std::string s_gradientSubtractShader;
static std::string s_vorticityShader;
static std::string s_vorticityForceShader;
static std::string s_addForceShader;
static std::string s_addDyeShader;
static std::string s_clearShader;
static std::string s_renderShader;

static void ensureFluidShadersLoaded() {
    auto& loader = vivid::AssetLoader::instance();
    if (s_advectVelocityShader.empty()) s_advectVelocityShader = loader.loadShader("fluid_advect_velocity.wgsl");
    if (s_advectDyeShader.empty()) s_advectDyeShader = loader.loadShader("fluid_advect_dye.wgsl");
    if (s_divergenceShader.empty()) s_divergenceShader = loader.loadShader("fluid_divergence.wgsl");
    if (s_pressureShader.empty()) s_pressureShader = loader.loadShader("fluid_pressure.wgsl");
    if (s_gradientSubtractShader.empty()) s_gradientSubtractShader = loader.loadShader("fluid_gradient_subtract.wgsl");
    if (s_vorticityShader.empty()) s_vorticityShader = loader.loadShader("fluid_vorticity.wgsl");
    if (s_vorticityForceShader.empty()) s_vorticityForceShader = loader.loadShader("fluid_vorticity_force.wgsl");
    if (s_addForceShader.empty()) s_addForceShader = loader.loadShader("fluid_add_force.wgsl");
    if (s_addDyeShader.empty()) s_addDyeShader = loader.loadShader("fluid_add_dye.wgsl");
    if (s_clearShader.empty()) s_clearShader = loader.loadShader("fluid_clear.wgsl");
    if (s_renderShader.empty()) s_renderShader = loader.loadShader("fluid_render.wgsl");
}

// =============================================================================
// Uniform Structures (must match WGSL shaders)
// =============================================================================

struct FluidUniforms {
    float dt;
    float dissipation;
    float texelW;
    float texelH;
    float dyeDissipation;
    float viscosity;
    float vorticityScale;
    float forceScale;
    uint32_t width;
    uint32_t height;
    float _pad[2];
};

struct ImpulseUniforms {
    float posX, posY;
    float valueX, valueY, valueZ, valueW;
    float radius;
    float _pad;
};

// =============================================================================
// WGSL Shaders - Advection
// =============================================================================

static constexpr const char* ADVECT_VELOCITY_SHADER_FALLBACK = R"(
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
@group(0) @binding(2) var velocityOut: texture_storage_2d<rg16float, write>;
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
)";

static constexpr const char* ADVECT_DYE_SHADER_FALLBACK = R"(
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
)";

// =============================================================================
// WGSL Shaders - Divergence
// =============================================================================

static constexpr const char* DIVERGENCE_SHADER_FALLBACK = R"(
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
)";

// =============================================================================
// WGSL Shaders - Pressure Solve (Jacobi Iteration)
// =============================================================================

static constexpr const char* PRESSURE_SHADER_FALLBACK = R"(
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
@group(0) @binding(3) var pressureOut: texture_storage_2d<r16float, write>;

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
)";

// =============================================================================
// WGSL Shaders - Gradient Subtraction
// =============================================================================

static constexpr const char* GRADIENT_SUBTRACT_SHADER_FALLBACK = R"(
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
@group(0) @binding(2) var velocityIn: texture_2d<f32>;
@group(0) @binding(3) var velocityOut: texture_storage_2d<rg16float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let x = i32(id.x);
    let y = i32(id.y);
    let w = i32(u.width);
    let h = i32(u.height);

    // Sample neighboring pressure
    let pL = textureLoad(pressureIn, vec2i(max(x - 1, 0), y), 0).r;
    let pR = textureLoad(pressureIn, vec2i(min(x + 1, w - 1), y), 0).r;
    let pB = textureLoad(pressureIn, vec2i(x, max(y - 1, 0)), 0).r;
    let pT = textureLoad(pressureIn, vec2i(x, min(y + 1, h - 1)), 0).r;

    // Compute pressure gradient
    let grad = 0.5 * vec2f(pR - pL, pT - pB);

    // Sample current velocity
    let vel = textureLoad(velocityIn, vec2i(x, y), 0).xy;

    // Subtract gradient to make velocity divergence-free
    let result = vel - grad;

    textureStore(velocityOut, id.xy, vec4f(result, 0.0, 1.0));
}
)";

// =============================================================================
// WGSL Shaders - Vorticity
// =============================================================================

static constexpr const char* VORTICITY_SHADER_FALLBACK = R"(
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
@group(0) @binding(2) var vorticityOut: texture_storage_2d<r16float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let x = i32(id.x);
    let y = i32(id.y);
    let w = i32(u.width);
    let h = i32(u.height);

    // Sample neighboring velocities
    let vL = textureLoad(velocityIn, vec2i(max(x - 1, 0), y), 0).xy;
    let vR = textureLoad(velocityIn, vec2i(min(x + 1, w - 1), y), 0).xy;
    let vB = textureLoad(velocityIn, vec2i(x, max(y - 1, 0)), 0).xy;
    let vT = textureLoad(velocityIn, vec2i(x, min(y + 1, h - 1)), 0).xy;

    // Compute curl (vorticity) = dVy/dx - dVx/dy
    let curl = 0.5 * ((vR.y - vL.y) - (vT.x - vB.x));

    textureStore(vorticityOut, id.xy, vec4f(curl, 0.0, 0.0, 1.0));
}
)";

static constexpr const char* VORTICITY_FORCE_SHADER_FALLBACK = R"(
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
@group(0) @binding(1) var vorticityIn: texture_2d<f32>;
@group(0) @binding(2) var velocityIn: texture_2d<f32>;
@group(0) @binding(3) var velocityOut: texture_storage_2d<rg16float, write>;

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= u.width || id.y >= u.height) { return; }

    let x = i32(id.x);
    let y = i32(id.y);
    let w = i32(u.width);
    let h = i32(u.height);

    // Sample neighboring vorticity (absolute values for gradient)
    let vL = abs(textureLoad(vorticityIn, vec2i(max(x - 1, 0), y), 0).r);
    let vR = abs(textureLoad(vorticityIn, vec2i(min(x + 1, w - 1), y), 0).r);
    let vB = abs(textureLoad(vorticityIn, vec2i(x, max(y - 1, 0)), 0).r);
    let vT = abs(textureLoad(vorticityIn, vec2i(x, min(y + 1, h - 1)), 0).r);
    let vC = textureLoad(vorticityIn, vec2i(x, y), 0).r;

    // Compute gradient of vorticity magnitude
    var grad = vec2f(vR - vL, vT - vB) * 0.5;
    let len = length(grad);
    if (len > 0.0001) {
        grad = grad / len;
    }

    // Vorticity confinement force (perpendicular to gradient)
    let force = vec2f(grad.y, -grad.x) * vC * u.vorticityScale;

    // Add force to velocity
    let vel = textureLoad(velocityIn, vec2i(x, y), 0).xy;
    let result = vel + force * u.dt;

    textureStore(velocityOut, id.xy, vec4f(result, 0.0, 1.0));
}
)";

// =============================================================================
// WGSL Shaders - Force/Dye Injection
// =============================================================================

static constexpr const char* ADD_FORCE_SHADER_FALLBACK = R"(
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
@group(0) @binding(3) var velocityOut: texture_storage_2d<rg16float, write>;

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
)";

static constexpr const char* ADD_DYE_SHADER_FALLBACK = R"(
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
@group(0) @binding(2) var dyeIn: texture_2d<f32>;
@group(0) @binding(3) var dyeOut: texture_storage_2d<rgba16float, write>;

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

    // Sample current dye
    let dye = textureLoad(dyeIn, vec2i(id.xy), 0);

    // Add dye color
    let color = vec4f(impulse.valueX, impulse.valueY, impulse.valueZ, impulse.valueW);
    let result = dye + color * weight;

    textureStore(dyeOut, id.xy, result);
}
)";

// =============================================================================
// WGSL Shaders - Clear
// =============================================================================

static constexpr const char* CLEAR_SHADER_FALLBACK = R"(
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
)";

// =============================================================================
// WGSL Shaders - Render (output dye to texture)
// =============================================================================

static constexpr const char* RENDER_SHADER_FALLBACK = R"(
struct Uniforms {
    clearR: f32,
    clearG: f32,
    clearB: f32,
    clearA: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var dyeTex: texture_2d<f32>;
@group(0) @binding(2) var linearSampler: sampler;

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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dye = textureSample(dyeTex, linearSampler, input.uv);
    let clear = vec4f(u.clearR, u.clearG, u.clearB, u.clearA);

    // Blend dye over clear color
    let result = vec4f(
        mix(clear.rgb, dye.rgb, dye.a),
        max(clear.a, dye.a)
    );
    return result;
}
)";

// =============================================================================
// FluidSim Implementation
// =============================================================================

FluidSim::FluidSim() {
    registerParam(viscosity);
    registerParam(dissipation);
    registerParam(vorticity);
    registerParam(dyeDissipation);
    registerParam(pressureIterations);
    registerParam(forceScale);
    registerParam(clearColor);
}

FluidSim::~FluidSim() {
    cleanup();
}

void FluidSim::addForce(float x, float y, float dx, float dy, float radius) {
    FluidImpulse imp;
    imp.x = x;
    imp.y = y;
    imp.dx = dx;
    imp.dy = dy;
    imp.radius = radius;
    imp.b = 0;
    imp.a = 0;
    imp.isDye = false;
    m_pendingImpulses.push_back(imp);
}

void FluidSim::addDye(float x, float y, float r, float g, float b, float radius, float a) {
    FluidImpulse imp;
    imp.x = x;
    imp.y = y;
    imp.dx = r;
    imp.dy = g;
    imp.radius = radius;
    imp.b = b;
    imp.a = a;
    imp.isDye = true;
    m_pendingImpulses.push_back(imp);
}

void FluidSim::clear() {
    m_clearPending = true;
}

void FluidSim::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);

    m_simWidth = m_width;
    m_simHeight = m_height;

    auto device = ctx.device();
    createTextures(device);
    createPipelines(device);
}

void FluidSim::createTextures(WGPUDevice device) {
    // Velocity textures (RG16Float for 2D velocity)
    WGPUTextureDescriptor velDesc = {};
    velDesc.size.width = m_simWidth;
    velDesc.size.height = m_simHeight;
    velDesc.size.depthOrArrayLayers = 1;
    velDesc.mipLevelCount = 1;
    velDesc.sampleCount = 1;
    velDesc.dimension = WGPUTextureDimension_2D;
    velDesc.format = WGPUTextureFormat_RG16Float;
    velDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;

    m_velocityA.reset(wgpuDeviceCreateTexture(device, &velDesc));
    m_velocityB.reset(wgpuDeviceCreateTexture(device, &velDesc));

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RG16Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    m_velocityViewA.reset(wgpuTextureCreateView(m_velocityA, &viewDesc));
    m_velocityViewB.reset(wgpuTextureCreateView(m_velocityB, &viewDesc));

    // Pressure texture (R16Float)
    WGPUTextureDescriptor pressDesc = {};
    pressDesc.size.width = m_simWidth;
    pressDesc.size.height = m_simHeight;
    pressDesc.size.depthOrArrayLayers = 1;
    pressDesc.mipLevelCount = 1;
    pressDesc.sampleCount = 1;
    pressDesc.dimension = WGPUTextureDimension_2D;
    pressDesc.format = WGPUTextureFormat_R16Float;
    pressDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;

    m_pressure.reset(wgpuDeviceCreateTexture(device, &pressDesc));
    viewDesc.format = WGPUTextureFormat_R16Float;
    m_pressureView.reset(wgpuTextureCreateView(m_pressure, &viewDesc));

    // Divergence texture (R16Float)
    m_divergence.reset(wgpuDeviceCreateTexture(device, &pressDesc));
    m_divergenceView.reset(wgpuTextureCreateView(m_divergence, &viewDesc));

    // Vorticity texture (R16Float)
    m_vorticity.reset(wgpuDeviceCreateTexture(device, &pressDesc));
    m_vorticityView.reset(wgpuTextureCreateView(m_vorticity, &viewDesc));

    // Dye textures (RGBA16Float)
    WGPUTextureDescriptor dyeDesc = {};
    dyeDesc.size.width = m_simWidth;
    dyeDesc.size.height = m_simHeight;
    dyeDesc.size.depthOrArrayLayers = 1;
    dyeDesc.mipLevelCount = 1;
    dyeDesc.sampleCount = 1;
    dyeDesc.dimension = WGPUTextureDimension_2D;
    dyeDesc.format = WGPUTextureFormat_RGBA16Float;
    dyeDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;

    m_dyeA.reset(wgpuDeviceCreateTexture(device, &dyeDesc));
    m_dyeB.reset(wgpuDeviceCreateTexture(device, &dyeDesc));

    viewDesc.format = WGPUTextureFormat_RGBA16Float;
    m_dyeViewA.reset(wgpuTextureCreateView(m_dyeA, &viewDesc));
    m_dyeViewB.reset(wgpuTextureCreateView(m_dyeB, &viewDesc));

    // Uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = sizeof(FluidUniforms);
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer.reset(wgpuDeviceCreateBuffer(device, &bufDesc));

    // Impulse buffer
    bufDesc.size = sizeof(ImpulseUniforms);
    m_impulseBuffer.reset(wgpuDeviceCreateBuffer(device, &bufDesc));

    // Sampler
    m_sampler = gpu::getLinearClampSampler(device);
}

void FluidSim::createPipelines(WGPUDevice device) {
    createAdvectPipeline(device);
    createDivergencePipeline(device);
    createPressurePipeline(device);
    createGradientSubtractPipeline(device);
    createVorticityPipelines(device);
    createAddForcePipeline(device);
    createClearPipeline(device);
    createRenderPipeline(device);
}

void FluidSim::createAdvectPipeline(WGPUDevice device) {
    ensureFluidShadersLoaded();

    // Advect velocity pipeline
    {
        const std::string& shaderSource = s_advectVelocityShader.empty() ? ADVECT_VELOCITY_SHADER_FALLBACK : s_advectVelocityShader;
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = gpu::toStringView(shaderSource.c_str());

        WGPUShaderModuleDescriptor moduleDesc = {};
        moduleDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

        // Bind group layout: uniform, velocity in (texture), velocity out (storage), sampler
        WGPUBindGroupLayoutEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[2].storageTexture.format = WGPUTextureFormat_RG16Float;
        entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 4;
        layoutDesc.entries = entries;
        m_advectLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        WGPUBindGroupLayout layouts[] = { m_advectLayout };
        pipelineLayoutDesc.bindGroupLayouts = layouts;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = gpu::toStringView("main");
        m_advectVelocityPipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // Advect dye pipeline
    {
        const std::string& shaderSource = s_advectDyeShader.empty() ? ADVECT_DYE_SHADER_FALLBACK : s_advectDyeShader;
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = gpu::toStringView(shaderSource.c_str());

        WGPUShaderModuleDescriptor moduleDesc = {};
        moduleDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

        // Use layout from pipeline (similar but with RGBA output)
        WGPUBindGroupLayoutEntry entries[5] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[3].storageTexture.format = WGPUTextureFormat_RGBA16Float;
        entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        entries[4].binding = 4;
        entries[4].visibility = WGPUShaderStage_Compute;
        entries[4].sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 5;
        layoutDesc.entries = entries;
        WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &layout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = gpu::toStringView("main");
        m_advectDyePipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

        wgpuBindGroupLayoutRelease(layout);
        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }
}

void FluidSim::createDivergencePipeline(WGPUDevice device) {
    ensureFluidShadersLoaded();
    const std::string& shaderSource = s_divergenceShader.empty() ? DIVERGENCE_SHADER_FALLBACK : s_divergenceShader;
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = gpu::toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, velocity in (texture), divergence out (storage)
    WGPUBindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[2].storageTexture.format = WGPUTextureFormat_R16Float;
    entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    m_divergenceLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_divergenceLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = gpu::toStringView("main");
    m_divergencePipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void FluidSim::createPressurePipeline(WGPUDevice device) {
    ensureFluidShadersLoaded();
    const std::string& shaderSource = s_pressureShader.empty() ? PRESSURE_SHADER_FALLBACK : s_pressureShader;
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = gpu::toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, pressure in, divergence in, pressure out
    WGPUBindGroupLayoutEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Compute;
    entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[3].storageTexture.format = WGPUTextureFormat_R16Float;
    entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = entries;
    m_pressureLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_pressureLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = gpu::toStringView("main");
    m_pressurePipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void FluidSim::createGradientSubtractPipeline(WGPUDevice device) {
    ensureFluidShadersLoaded();
    const std::string& shaderSource = s_gradientSubtractShader.empty() ? GRADIENT_SUBTRACT_SHADER_FALLBACK : s_gradientSubtractShader;
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = gpu::toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, pressure in, velocity in, velocity out
    WGPUBindGroupLayoutEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Compute;
    entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[3].storageTexture.format = WGPUTextureFormat_RG16Float;
    entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = entries;
    m_gradientLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_gradientLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = gpu::toStringView("main");
    m_gradientSubtractPipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void FluidSim::createVorticityPipelines(WGPUDevice device) {
    ensureFluidShadersLoaded();

    // Vorticity computation pipeline
    {
        const std::string& shaderSource = s_vorticityShader.empty() ? VORTICITY_SHADER_FALLBACK : s_vorticityShader;
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = gpu::toStringView(shaderSource.c_str());

        WGPUShaderModuleDescriptor moduleDesc = {};
        moduleDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

        // Bind group layout: uniform, velocity in, vorticity out
        WGPUBindGroupLayoutEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[2].storageTexture.format = WGPUTextureFormat_R16Float;
        entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 3;
        layoutDesc.entries = entries;
        m_vorticityLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        WGPUBindGroupLayout layouts[] = { m_vorticityLayout };
        pipelineLayoutDesc.bindGroupLayouts = layouts;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = gpu::toStringView("main");
        m_vorticityPipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // Vorticity force application pipeline
    {
        const std::string& shaderSource = s_vorticityForceShader.empty() ? VORTICITY_FORCE_SHADER_FALLBACK : s_vorticityForceShader;
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = gpu::toStringView(shaderSource.c_str());

        WGPUShaderModuleDescriptor moduleDesc = {};
        moduleDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

        // Bind group layout: uniform, vorticity in, velocity in, velocity out
        WGPUBindGroupLayoutEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[3].storageTexture.format = WGPUTextureFormat_RG16Float;
        entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 4;
        layoutDesc.entries = entries;
        m_vorticityForceLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        WGPUBindGroupLayout layouts[] = { m_vorticityForceLayout };
        pipelineLayoutDesc.bindGroupLayouts = layouts;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = gpu::toStringView("main");
        m_vorticityForcePipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }
}

void FluidSim::createAddForcePipeline(WGPUDevice device) {
    ensureFluidShadersLoaded();

    // Force pipeline
    {
        const std::string& shaderSource = s_addForceShader.empty() ? ADD_FORCE_SHADER_FALLBACK : s_addForceShader;
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = gpu::toStringView(shaderSource.c_str());

        WGPUShaderModuleDescriptor moduleDesc = {};
        moduleDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

        // Bind group layout: uniform, impulse, velocity in, velocity out
        WGPUBindGroupLayoutEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].buffer.type = WGPUBufferBindingType_Uniform;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[3].storageTexture.format = WGPUTextureFormat_RG16Float;
        entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 4;
        layoutDesc.entries = entries;
        m_addForceLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        WGPUBindGroupLayout layouts[] = { m_addForceLayout };
        pipelineLayoutDesc.bindGroupLayouts = layouts;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = gpu::toStringView("main");
        m_addForcePipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // Dye injection pipeline
    {
        const std::string& shaderSource = s_addDyeShader.empty() ? ADD_DYE_SHADER_FALLBACK : s_addDyeShader;
        WGPUShaderSourceWGSL wgslDesc = {};
        wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgslDesc.code = gpu::toStringView(shaderSource.c_str());

        WGPUShaderModuleDescriptor moduleDesc = {};
        moduleDesc.nextInChain = &wgslDesc.chain;
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

        // Bind group layout: uniform, impulse, dye in, dye out
        WGPUBindGroupLayoutEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].buffer.type = WGPUBufferBindingType_Uniform;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[3].storageTexture.format = WGPUTextureFormat_RGBA16Float;
        entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 4;
        layoutDesc.entries = entries;
        m_addDyeLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        WGPUBindGroupLayout layouts[] = { m_addDyeLayout };
        pipelineLayoutDesc.bindGroupLayouts = layouts;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = gpu::toStringView("main");
        m_addDyePipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }
}

void FluidSim::createClearPipeline(WGPUDevice device) {
    ensureFluidShadersLoaded();
    const std::string& shaderSource = s_clearShader.empty() ? CLEAR_SHADER_FALLBACK : s_clearShader;
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = gpu::toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, velocity out, pressure out, dye out
    WGPUBindGroupLayoutEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[1].storageTexture.format = WGPUTextureFormat_RG16Float;
    entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[2].storageTexture.format = WGPUTextureFormat_R16Float;
    entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Compute;
    entries[3].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    entries[3].storageTexture.format = WGPUTextureFormat_RGBA16Float;
    entries[3].storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = entries;
    m_clearLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_clearLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = gpu::toStringView("main");
    m_clearPipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void FluidSim::createRenderPipeline(WGPUDevice device) {
    ensureFluidShadersLoaded();
    const std::string& shaderSource = s_renderShader.empty() ? RENDER_SHADER_FALLBACK : s_renderShader;
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = gpu::toStringView(shaderSource.c_str());

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, dye texture, sampler
    WGPUBindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    m_renderBindGroupLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_renderBindGroupLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = module;
    fragmentState.entryPoint = gpu::toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = gpu::toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_renderPipeline.reset(wgpuDeviceCreateRenderPipeline(device, &pipelineDesc));

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void FluidSim::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    float dt = static_cast<float>(ctx.dt());
    auto device = ctx.device();
    auto queue = ctx.queue();

    // Update uniforms
    FluidUniforms uniforms = {};
    uniforms.dt = dt;
    uniforms.dissipation = static_cast<float>(dissipation);
    uniforms.texelW = 1.0f / m_simWidth;
    uniforms.texelH = 1.0f / m_simHeight;
    uniforms.dyeDissipation = static_cast<float>(dyeDissipation);
    uniforms.viscosity = static_cast<float>(viscosity);
    uniforms.vorticityScale = static_cast<float>(vorticity);
    uniforms.forceScale = static_cast<float>(forceScale);
    uniforms.width = m_simWidth;
    uniforms.height = m_simHeight;
    wgpuQueueWriteBuffer(queue, m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Handle clear request
    if (m_clearPending) {
        dispatchClear(ctx);
        m_clearPending = false;
    }

    // Process impulses (force and dye injection)
    dispatchImpulses(ctx);

    // Simulation steps
    dispatchAdvection(ctx, dt);
    dispatchVorticity(ctx, dt);
    dispatchDivergence(ctx);
    dispatchPressureSolve(ctx);
    dispatchGradientSubtract(ctx);

    // Render dye to output
    renderDye(ctx);

    didCook();
}

void FluidSim::dispatchAdvection(Context& ctx, float dt) {
    auto device = ctx.device();
    auto queue = ctx.queue();

    uint32_t workgroupsX = (m_simWidth + 7) / 8;
    uint32_t workgroupsY = (m_simHeight + 7) / 8;

    // Advect velocity
    {
        WGPUBindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(FluidUniforms);

        entries[1].binding = 1;
        entries[1].textureView = velocityReadView();

        entries[2].binding = 2;
        entries[2].textureView = velocityWriteView();

        entries[3].binding = 3;
        entries[3].sampler = m_sampler;

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_advectLayout;
        bindDesc.entryCount = 4;
        bindDesc.entries = entries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
        WGPUComputePassDescriptor passDesc = {};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        wgpuComputePassEncoderSetPipeline(pass, m_advectVelocityPipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);

        swapVelocity();
    }

    // Advect dye
    {
        // Get layout from pipeline
        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(m_advectDyePipeline, 0);

        WGPUBindGroupEntry entries[5] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(FluidUniforms);

        entries[1].binding = 1;
        entries[1].textureView = velocityReadView();

        entries[2].binding = 2;
        entries[2].textureView = dyeReadView();

        entries[3].binding = 3;
        entries[3].textureView = dyeWriteView();

        entries[4].binding = 4;
        entries[4].sampler = m_sampler;

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = layout;
        bindDesc.entryCount = 5;
        bindDesc.entries = entries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
        WGPUComputePassDescriptor passDesc = {};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        wgpuComputePassEncoderSetPipeline(pass, m_advectDyePipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);
        wgpuBindGroupLayoutRelease(layout);

        swapDye();
    }
}

void FluidSim::dispatchDivergence(Context& ctx) {
    auto device = ctx.device();
    auto queue = ctx.queue();

    uint32_t workgroupsX = (m_simWidth + 7) / 8;
    uint32_t workgroupsY = (m_simHeight + 7) / 8;

    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = sizeof(FluidUniforms);

    entries[1].binding = 1;
    entries[1].textureView = velocityReadView();

    entries[2].binding = 2;
    entries[2].textureView = m_divergenceView;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_divergenceLayout;
    bindDesc.entryCount = 3;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_divergencePipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
}

void FluidSim::dispatchPressureSolve(Context& ctx) {
    auto device = ctx.device();
    auto queue = ctx.queue();

    uint32_t workgroupsX = (m_simWidth + 7) / 8;
    uint32_t workgroupsY = (m_simHeight + 7) / 8;
    int iterations = static_cast<int>(pressureIterations);

    // We need a second pressure texture for ping-pong
    // For simplicity, we'll reuse the vorticity texture as temp storage
    // (not ideal but works for this implementation)
    WGPUTextureView pressureRead = m_pressureView;
    WGPUTextureView pressureWrite = m_vorticityView;  // Reuse vorticity as temp

    for (int i = 0; i < iterations; i++) {
        WGPUBindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(FluidUniforms);

        entries[1].binding = 1;
        entries[1].textureView = pressureRead;

        entries[2].binding = 2;
        entries[2].textureView = m_divergenceView;

        entries[3].binding = 3;
        entries[3].textureView = pressureWrite;

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_pressureLayout;
        bindDesc.entryCount = 4;
        bindDesc.entries = entries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
        WGPUComputePassDescriptor passDesc = {};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        wgpuComputePassEncoderSetPipeline(pass, m_pressurePipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);

        // Swap read/write
        std::swap(pressureRead, pressureWrite);
    }

    // After even iterations, result is in m_pressureView
    // After odd iterations, result is in m_vorticityView
    // Copy back if needed
    if (iterations % 2 == 1) {
        // Result is in vorticity, we need it in pressure for gradient subtract
        // Simple solution: do one more iteration to put it back
        // Or we just use wherever the result is - adjust gradient subtract
    }
}

void FluidSim::dispatchGradientSubtract(Context& ctx) {
    auto device = ctx.device();
    auto queue = ctx.queue();

    uint32_t workgroupsX = (m_simWidth + 7) / 8;
    uint32_t workgroupsY = (m_simHeight + 7) / 8;

    // Determine which texture has the pressure result
    int iterations = static_cast<int>(pressureIterations);
    WGPUTextureView pressureResult = (iterations % 2 == 0) ? m_pressureView.get() : m_vorticityView.get();

    WGPUBindGroupEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = sizeof(FluidUniforms);

    entries[1].binding = 1;
    entries[1].textureView = pressureResult;

    entries[2].binding = 2;
    entries[2].textureView = velocityReadView();

    entries[3].binding = 3;
    entries[3].textureView = velocityWriteView();

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_gradientLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_gradientSubtractPipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);

    swapVelocity();
}

void FluidSim::dispatchVorticity(Context& ctx, float dt) {
    if (static_cast<float>(vorticity) < 0.001f) return;

    auto device = ctx.device();
    auto queue = ctx.queue();

    uint32_t workgroupsX = (m_simWidth + 7) / 8;
    uint32_t workgroupsY = (m_simHeight + 7) / 8;

    // Compute vorticity (curl)
    {
        WGPUBindGroupEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(FluidUniforms);

        entries[1].binding = 1;
        entries[1].textureView = velocityReadView();

        entries[2].binding = 2;
        entries[2].textureView = m_vorticityView;

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_vorticityLayout;
        bindDesc.entryCount = 3;
        bindDesc.entries = entries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
        WGPUComputePassDescriptor passDesc = {};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        wgpuComputePassEncoderSetPipeline(pass, m_vorticityPipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);
    }

    // Apply vorticity confinement force
    {
        WGPUBindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].buffer = m_uniformBuffer;
        entries[0].size = sizeof(FluidUniforms);

        entries[1].binding = 1;
        entries[1].textureView = m_vorticityView;

        entries[2].binding = 2;
        entries[2].textureView = velocityReadView();

        entries[3].binding = 3;
        entries[3].textureView = velocityWriteView();

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_vorticityForceLayout;
        bindDesc.entryCount = 4;
        bindDesc.entries = entries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
        WGPUComputePassDescriptor passDesc = {};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        wgpuComputePassEncoderSetPipeline(pass, m_vorticityForcePipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);

        swapVelocity();
    }
}

void FluidSim::dispatchImpulses(Context& ctx) {
    if (m_pendingImpulses.empty()) return;

    auto device = ctx.device();
    auto queue = ctx.queue();

    uint32_t workgroupsX = (m_simWidth + 7) / 8;
    uint32_t workgroupsY = (m_simHeight + 7) / 8;

    for (const auto& imp : m_pendingImpulses) {
        ImpulseUniforms impulseData = {};
        impulseData.posX = imp.x;
        impulseData.posY = imp.y;
        impulseData.radius = imp.radius;

        if (imp.isDye) {
            // Add dye
            impulseData.valueX = imp.dx;  // r
            impulseData.valueY = imp.dy;  // g
            impulseData.valueZ = imp.b;
            impulseData.valueW = imp.a;
            wgpuQueueWriteBuffer(queue, m_impulseBuffer, 0, &impulseData, sizeof(impulseData));

            WGPUBindGroupEntry entries[4] = {};
            entries[0].binding = 0;
            entries[0].buffer = m_uniformBuffer;
            entries[0].size = sizeof(FluidUniforms);

            entries[1].binding = 1;
            entries[1].buffer = m_impulseBuffer;
            entries[1].size = sizeof(ImpulseUniforms);

            entries[2].binding = 2;
            entries[2].textureView = dyeReadView();

            entries[3].binding = 3;
            entries[3].textureView = dyeWriteView();

            WGPUBindGroupDescriptor bindDesc = {};
            bindDesc.layout = m_addDyeLayout;
            bindDesc.entryCount = 4;
            bindDesc.entries = entries;
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

            WGPUCommandEncoderDescriptor encDesc = {};
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
            WGPUComputePassDescriptor passDesc = {};
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

            wgpuComputePassEncoderSetPipeline(pass, m_addDyePipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);

            WGPUCommandBufferDescriptor cmdDesc = {};
            WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
            wgpuQueueSubmit(queue, 1, &cmdBuffer);
            wgpuCommandBufferRelease(cmdBuffer);
            wgpuCommandEncoderRelease(encoder);
            wgpuBindGroupRelease(bindGroup);

            swapDye();

        } else {
            // Add force
            impulseData.valueX = imp.dx;
            impulseData.valueY = imp.dy;
            impulseData.valueZ = 0;
            impulseData.valueW = 0;
            wgpuQueueWriteBuffer(queue, m_impulseBuffer, 0, &impulseData, sizeof(impulseData));

            WGPUBindGroupEntry entries[4] = {};
            entries[0].binding = 0;
            entries[0].buffer = m_uniformBuffer;
            entries[0].size = sizeof(FluidUniforms);

            entries[1].binding = 1;
            entries[1].buffer = m_impulseBuffer;
            entries[1].size = sizeof(ImpulseUniforms);

            entries[2].binding = 2;
            entries[2].textureView = velocityReadView();

            entries[3].binding = 3;
            entries[3].textureView = velocityWriteView();

            WGPUBindGroupDescriptor bindDesc = {};
            bindDesc.layout = m_addForceLayout;
            bindDesc.entryCount = 4;
            bindDesc.entries = entries;
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

            WGPUCommandEncoderDescriptor encDesc = {};
            WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
            WGPUComputePassDescriptor passDesc = {};
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

            wgpuComputePassEncoderSetPipeline(pass, m_addForcePipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);

            WGPUCommandBufferDescriptor cmdDesc = {};
            WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
            wgpuQueueSubmit(queue, 1, &cmdBuffer);
            wgpuCommandBufferRelease(cmdBuffer);
            wgpuCommandEncoderRelease(encoder);
            wgpuBindGroupRelease(bindGroup);

            swapVelocity();
        }
    }

    m_pendingImpulses.clear();
}

void FluidSim::dispatchClear(Context& ctx) {
    auto device = ctx.device();
    auto queue = ctx.queue();

    uint32_t workgroupsX = (m_simWidth + 7) / 8;
    uint32_t workgroupsY = (m_simHeight + 7) / 8;

    WGPUBindGroupEntry entries[4] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = sizeof(FluidUniforms);

    entries[1].binding = 1;
    entries[1].textureView = velocityWriteView();

    entries[2].binding = 2;
    entries[2].textureView = m_pressureView;

    entries[3].binding = 3;
    entries[3].textureView = dyeWriteView();

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_clearLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);
    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_clearPipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);

    swapVelocity();
    swapDye();
}

void FluidSim::renderDye(Context& ctx) {
    auto device = ctx.device();
    auto queue = ctx.queue();

    // Create a small uniform buffer for render uniforms
    struct RenderUniforms {
        float clearR, clearG, clearB, clearA;
    };
    RenderUniforms renderUniforms = {
        clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()
    };

    // Create temp buffer for render uniforms
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = sizeof(RenderUniforms);
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer renderUniformBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    wgpuQueueWriteBuffer(queue, renderUniformBuffer, 0, &renderUniforms, sizeof(renderUniforms));

    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = renderUniformBuffer;
    entries[0].size = sizeof(RenderUniforms);

    entries[1].binding = 1;
    entries[1].textureView = dyeReadView();

    entries[2].binding = 2;
    entries[2].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_renderBindGroupLayout;
    bindDesc.entryCount = 3;
    bindDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    auto encoder = ctx.gpuEncoder();
    auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, m_renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
    wgpuBufferRelease(renderUniformBuffer);
}

void FluidSim::cleanup() {
    m_velocityA.reset();
    m_velocityViewA.reset();
    m_velocityB.reset();
    m_velocityViewB.reset();
    m_pressure.reset();
    m_pressureView.reset();
    m_divergence.reset();
    m_divergenceView.reset();
    m_vorticity.reset();
    m_vorticityView.reset();
    m_dyeA.reset();
    m_dyeViewA.reset();
    m_dyeB.reset();
    m_dyeViewB.reset();

    m_advectVelocityPipeline.reset();
    m_advectDyePipeline.reset();
    m_divergencePipeline.reset();
    m_pressurePipeline.reset();
    m_gradientSubtractPipeline.reset();
    m_vorticityPipeline.reset();
    m_vorticityForcePipeline.reset();
    m_addForcePipeline.reset();
    m_addDyePipeline.reset();
    m_clearPipeline.reset();
    m_renderPipeline.reset();

    m_advectLayout.reset();
    m_divergenceLayout.reset();
    m_pressureLayout.reset();
    m_gradientLayout.reset();
    m_vorticityLayout.reset();
    m_vorticityForceLayout.reset();
    m_addForceLayout.reset();
    m_addDyeLayout.reset();
    m_clearLayout.reset();
    m_renderBindGroupLayout.reset();

    m_uniformBuffer.reset();
    m_impulseBuffer.reset();
    m_sampler = nullptr;

    releaseOutput();
    m_initialized = false;
}

// =============================================================================
// State Preservation for Hot-Reload
// =============================================================================

struct FluidSimState : public OperatorState {
    int velocityRead = 0;
    int dyeRead = 0;
    // Note: We could save texture data here for full state preservation,
    // but that would be expensive. For now, just preserve buffer indices.
};

std::unique_ptr<OperatorState> FluidSim::saveState() {
    auto state = std::make_unique<FluidSimState>();
    state->velocityRead = m_velocityRead;
    state->dyeRead = m_dyeRead;
    return state;
}

void FluidSim::loadState(std::unique_ptr<OperatorState> state) {
    if (auto* s = dynamic_cast<FluidSimState*>(state.get())) {
        m_velocityRead = s->velocityRead;
        m_dyeRead = s->dyeRead;
    }
}

} // namespace vivid::effects
