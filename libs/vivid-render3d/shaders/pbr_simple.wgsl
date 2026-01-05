// Vivid Render3D - Simple PBR Shader (Metallic-Roughness, No Textures)
// Uses scalar material values only - suitable for procedural geometry

const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;

// ============================================================================
// Uniforms
// ============================================================================

struct Uniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    normalMatrix: mat4x4f,
    cameraPos: vec3f,
    _pad0: f32,
    lightDir: vec3f,
    _pad1: f32,
    lightColor: vec3f,
    ambientIntensity: f32,
    baseColor: vec4f,
    metallic: f32,
    roughness: f32,
    _pad2: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

// ============================================================================
// Vertex Shader
// ============================================================================

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
}

struct VertexOutput {
    @builtin(position) clipPos: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) color: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = uniforms.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = uniforms.mvp * vec4f(in.position, 1.0);
    out.worldNormal = normalize((uniforms.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.color = in.color;

    return out;
}

// ============================================================================
// PBR Functions
// ============================================================================

// Normal Distribution Function (GGX/Trowbridge-Reitz)
fn D_GGX(NdotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + EPSILON);
}

// Geometry function (Schlick-GGX)
fn G_SchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + EPSILON);
}

// Smith's method for geometry obstruction
fn G_Smith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
fn F_Schlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// Fragment Shader
// ============================================================================

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let N = normalize(in.worldNormal);
    let V = normalize(uniforms.cameraPos - in.worldPos);
    let L = normalize(uniforms.lightDir);
    let H = normalize(V + L);

    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), EPSILON);
    let NdotH = max(dot(N, H), 0.0);
    let HdotV = max(dot(H, V), 0.0);

    // Material properties
    let albedo = uniforms.baseColor.rgb * in.color.rgb;
    let metallic = uniforms.metallic;
    let roughness = max(uniforms.roughness, 0.04);  // Avoid div by zero

    // Calculate F0 (reflectance at normal incidence)
    let F0 = mix(vec3f(0.04), albedo, metallic);

    // Cook-Torrance BRDF
    let D = D_GGX(NdotH, roughness);
    let G = G_Smith(NdotV, NdotL, roughness);
    let F = F_Schlick(HdotV, F0);

    // Specular reflection
    let numerator = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + EPSILON;
    let specular = numerator / denominator;

    // Energy conservation
    let kS = F;
    var kD = vec3f(1.0) - kS;
    kD *= 1.0 - metallic;  // Metals have no diffuse

    // Lambertian diffuse
    let diffuse = kD * albedo / PI;

    // Direct lighting
    let Lo = (diffuse + specular) * uniforms.lightColor * NdotL;

    // Simple ambient
    let ambient = vec3f(0.03) * albedo * uniforms.ambientIntensity;

    // Final color
    var color = ambient + Lo;

    // Simple Reinhard tone mapping
    color = color / (color + vec3f(1.0));

    // Gamma correction
    color = pow(color, vec3f(1.0 / 2.2));

    return vec4f(color, uniforms.baseColor.a * in.color.a);
}
