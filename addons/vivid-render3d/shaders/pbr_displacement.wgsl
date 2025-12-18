// Vivid Render3D - PBR Shader with Vertex Displacement
// Extends PBR shader with displacement mapping in vertex shader

// ============================================================================
// Constants
// ============================================================================

const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;

// ============================================================================
// Uniforms
// ============================================================================

struct SceneUniforms {
    viewProj: mat4x4f,
    cameraPos: vec3f,
    _pad0: f32,
    lightDir: vec3f,
    _pad1: f32,
    lightColor: vec3f,
    ambientIntensity: f32,
}

struct ObjectUniforms {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

struct MaterialUniforms {
    baseColor: vec4f,
    emissive: vec3f,
    metallic: f32,
    roughness: f32,
    normalScale: f32,
    occlusionStrength: f32,
    emissiveStrength: f32,
    alphaCutoff: f32,
    alphaMode: u32,
    hasBaseColorTex: u32,
    hasMetallicRoughnessTex: u32,
    hasNormalTex: u32,
    hasOcclusionTex: u32,
    hasEmissiveTex: u32,
    _pad: u32,
}

struct DisplacementUniforms {
    amplitude: f32,
    midpoint: f32,
    _pad0: f32,
    _pad1: f32,
}

@group(0) @binding(0) var<uniform> scene: SceneUniforms;
@group(1) @binding(0) var<uniform> object: ObjectUniforms;
@group(2) @binding(0) var<uniform> material: MaterialUniforms;

// Material textures
@group(2) @binding(1) var texSampler: sampler;
@group(2) @binding(2) var baseColorTex: texture_2d<f32>;
@group(2) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
@group(2) @binding(4) var normalTex: texture_2d<f32>;
@group(2) @binding(5) var occlusionTex: texture_2d<f32>;
@group(2) @binding(6) var emissiveTex: texture_2d<f32>;

// Displacement map (group 4)
@group(4) @binding(0) var<uniform> displacement: DisplacementUniforms;
@group(4) @binding(1) var dispSampler: sampler;
@group(4) @binding(2) var displacementTex: texture_2d<f32>;

// ============================================================================
// Vertex Shader with Displacement
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
    @location(1) normal: vec3f,
    @location(2) tangent: vec3f,
    @location(3) bitangent: vec3f,
    @location(4) uv: vec2f,
    @location(5) color: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    // Sample displacement map at vertex UV
    // Use textureSampleLevel with LOD 0 for vertex shader compatibility
    let dispSample = textureSampleLevel(displacementTex, dispSampler, in.uv, 0.0).r;

    // Calculate displacement amount (centered around midpoint)
    let dispAmount = (dispSample - displacement.midpoint) * displacement.amplitude;

    // Displace position along normal
    let displacedPos = in.position + in.normal * dispAmount;

    let worldPos = object.model * vec4f(displacedPos, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = scene.viewProj * worldPos;

    // Transform normal and tangent to world space
    out.normal = normalize((object.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.tangent = normalize((object.model * vec4f(in.tangent.xyz, 0.0)).xyz);
    out.bitangent = cross(out.normal, out.tangent) * in.tangent.w;

    out.uv = in.uv;
    out.color = in.color;

    return out;
}

// ============================================================================
// PBR Functions (same as pbr.wgsl)
// ============================================================================

fn sRGBToLinear(srgb: vec3f) -> vec3f {
    return pow(srgb, vec3f(2.2));
}

fn linearToSRGB(linear: vec3f) -> vec3f {
    return pow(linear, vec3f(1.0 / 2.2));
}

fn D_GGX(NdotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

fn G_SchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn G_Smith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

fn F_Schlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

fn perturbNormal(N: vec3f, T: vec3f, B: vec3f, uv: vec2f) -> vec3f {
    if (material.hasNormalTex == 0u) {
        return N;
    }
    var tangentNormal = textureSample(normalTex, texSampler, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    tangentNormal = normalize(tangentNormal);
    let TBN = mat3x3f(normalize(T), normalize(B), normalize(N));
    return normalize(TBN * tangentNormal);
}

fn calculateDirectLight(
    N: vec3f,
    V: vec3f,
    L: vec3f,
    lightColor: vec3f,
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    F0: vec3f
) -> vec3f {
    let H = normalize(V + L);
    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), EPSILON);
    let NdotH = max(dot(N, H), 0.0);
    let HdotV = max(dot(H, V), 0.0);

    let D = D_GGX(NdotH, roughness);
    let G = G_Smith(NdotV, NdotL, roughness);
    let F = F_Schlick(HdotV, F0);

    let numerator = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + EPSILON;
    let specular = numerator / denominator;

    let kS = F;
    var kD = vec3f(1.0) - kS;
    kD *= 1.0 - metallic;

    let diffuse = kD * albedo / PI;
    return (diffuse + specular) * lightColor * NdotL;
}

// ============================================================================
// Fragment Shader
// ============================================================================

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var baseColor = material.baseColor * in.color;
    if (material.hasBaseColorTex != 0u) {
        let texColor = textureSample(baseColorTex, texSampler, in.uv);
        baseColor *= vec4f(sRGBToLinear(texColor.rgb), texColor.a);
    }

    if (material.alphaMode == 1u) {
        if (baseColor.a < material.alphaCutoff) {
            discard;
        }
    }

    var metallic = material.metallic;
    var roughness = material.roughness;
    if (material.hasMetallicRoughnessTex != 0u) {
        let mrSample = textureSample(metallicRoughnessTex, texSampler, in.uv);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    let N = perturbNormal(normalize(in.normal), in.tangent, in.bitangent, in.uv);
    let V = normalize(scene.cameraPos - in.worldPos);

    let F0 = mix(vec3f(0.04), baseColor.rgb, metallic);

    var Lo = vec3f(0.0);
    let L = normalize(scene.lightDir);
    Lo += calculateDirectLight(N, V, L, scene.lightColor, baseColor.rgb, metallic, roughness, F0);

    let ambient = vec3f(0.03) * baseColor.rgb * scene.ambientIntensity;

    var ao = 1.0;
    if (material.hasOcclusionTex != 0u) {
        ao = textureSample(occlusionTex, texSampler, in.uv).r;
        ao = mix(1.0, ao, material.occlusionStrength);
    }

    var color = ambient * ao + Lo;

    var emissive = material.emissive;
    if (material.hasEmissiveTex != 0u) {
        emissive *= sRGBToLinear(textureSample(emissiveTex, texSampler, in.uv).rgb);
    }
    color += emissive * material.emissiveStrength;

    color = color / (color + vec3f(1.0));
    color = linearToSRGB(color);

    return vec4f(color, baseColor.a);
}
