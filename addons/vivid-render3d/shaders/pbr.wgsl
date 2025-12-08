// Vivid Render3D - PBR Shader (Metallic-Roughness Workflow)
// Based on Epic's PBR model with Cook-Torrance BRDF

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
    lightDir: vec3f,      // Primary directional light direction
    _pad1: f32,
    lightColor: vec3f,    // Primary light color * intensity
    ambientIntensity: f32,
}

struct ObjectUniforms {
    model: mat4x4f,
    normalMatrix: mat4x4f,  // transpose(inverse(model)) for correct normal transform
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
    alphaMode: u32,         // 0=Opaque, 1=Mask, 2=Blend
    hasBaseColorTex: u32,
    hasMetallicRoughnessTex: u32,
    hasNormalTex: u32,
    hasOcclusionTex: u32,
    hasEmissiveTex: u32,
    _pad: u32,
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

// IBL textures (optional - group 3)
@group(3) @binding(0) var iblSampler: sampler;
@group(3) @binding(1) var brdfLUT: texture_2d<f32>;
@group(3) @binding(2) var irradianceMap: texture_cube<f32>;
@group(3) @binding(3) var prefilteredMap: texture_cube<f32>;

// ============================================================================
// Vertex Shader
// ============================================================================

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,  // xyz = tangent, w = handedness
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

    let worldPos = object.model * vec4f(in.position, 1.0);
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
// PBR Functions
// ============================================================================

// Convert sRGB to linear color space
fn sRGBToLinear(srgb: vec3f) -> vec3f {
    return pow(srgb, vec3f(2.2));
}

// Convert linear to sRGB color space
fn linearToSRGB(linear: vec3f) -> vec3f {
    return pow(linear, vec3f(1.0 / 2.2));
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
fn D_GGX(NdotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Geometry function (Schlick-GGX)
fn G_SchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's method for geometry obstruction
fn G_Smith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Fresnel-Schlick approximation
fn F_Schlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Fresnel-Schlick with roughness for IBL
fn F_SchlickRoughness(cosTheta: f32, F0: vec3f, roughness: f32) -> vec3f {
    return F0 + (max(vec3f(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}

// Sample normal from tangent-space normal map
fn perturbNormal(N: vec3f, T: vec3f, B: vec3f, uv: vec2f) -> vec3f {
    if (material.hasNormalTex == 0u) {
        return N;
    }

    // Sample normal map and convert from [0,1] to [-1,1]
    var tangentNormal = textureSample(normalTex, texSampler, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;
    tangentNormal = normalize(tangentNormal);

    // TBN matrix
    let TBN = mat3x3f(normalize(T), normalize(B), normalize(N));
    return normalize(TBN * tangentNormal);
}

// Cook-Torrance BRDF for a single light
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

    // Cook-Torrance BRDF
    let D = D_GGX(NdotH, roughness);
    let G = G_Smith(NdotV, NdotL, roughness);
    let F = F_Schlick(HdotV, F0);

    // Specular reflection
    let numerator = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + EPSILON;
    let specular = numerator / denominator;

    // Energy conservation: diffuse + specular <= 1
    let kS = F;
    var kD = vec3f(1.0) - kS;
    kD *= 1.0 - metallic;  // Metals have no diffuse

    // Lambertian diffuse
    let diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightColor * NdotL;
}

// ============================================================================
// Fragment Shader
// ============================================================================

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample base color
    var baseColor = material.baseColor * in.color;
    if (material.hasBaseColorTex != 0u) {
        let texColor = textureSample(baseColorTex, texSampler, in.uv);
        // Convert sRGB texture to linear
        baseColor *= vec4f(sRGBToLinear(texColor.rgb), texColor.a);
    }

    // Alpha handling
    if (material.alphaMode == 1u) {
        // Mask mode
        if (baseColor.a < material.alphaCutoff) {
            discard;
        }
    }

    // Sample metallic-roughness
    var metallic = material.metallic;
    var roughness = material.roughness;
    if (material.hasMetallicRoughnessTex != 0u) {
        let mrSample = textureSample(metallicRoughnessTex, texSampler, in.uv);
        roughness *= mrSample.g;  // Green channel = roughness
        metallic *= mrSample.b;   // Blue channel = metallic
    }

    // Clamp roughness to avoid divide by zero
    roughness = clamp(roughness, 0.04, 1.0);

    // Get normal (possibly from normal map)
    let N = perturbNormal(normalize(in.normal), in.tangent, in.bitangent, in.uv);
    let V = normalize(scene.cameraPos - in.worldPos);

    // Calculate F0 (reflectance at normal incidence)
    // Dielectrics use 0.04, metals use albedo
    let F0 = mix(vec3f(0.04), baseColor.rgb, metallic);

    // Accumulate lighting
    var Lo = vec3f(0.0);

    // Primary directional light
    let L = normalize(scene.lightDir);
    Lo += calculateDirectLight(N, V, L, scene.lightColor, baseColor.rgb, metallic, roughness, F0);

    // Simple ambient (will be replaced with IBL when available)
    let ambient = vec3f(0.03) * baseColor.rgb * scene.ambientIntensity;

    // Sample ambient occlusion
    var ao = 1.0;
    if (material.hasOcclusionTex != 0u) {
        ao = textureSample(occlusionTex, texSampler, in.uv).r;
        ao = mix(1.0, ao, material.occlusionStrength);
    }

    // Combine lighting
    var color = ambient * ao + Lo;

    // Add emissive
    var emissive = material.emissive;
    if (material.hasEmissiveTex != 0u) {
        emissive *= sRGBToLinear(textureSample(emissiveTex, texSampler, in.uv).rgb);
    }
    color += emissive * material.emissiveStrength;

    // Tone mapping (simple Reinhard)
    color = color / (color + vec3f(1.0));

    // Gamma correction (linear to sRGB)
    color = linearToSRGB(color);

    return vec4f(color, baseColor.a);
}

// ============================================================================
// Fragment Shader with IBL
// ============================================================================

@fragment
fn fs_main_ibl(in: VertexOutput) -> @location(0) vec4f {
    // Sample base color
    var baseColor = material.baseColor * in.color;
    if (material.hasBaseColorTex != 0u) {
        let texColor = textureSample(baseColorTex, texSampler, in.uv);
        baseColor *= vec4f(sRGBToLinear(texColor.rgb), texColor.a);
    }

    // Alpha handling
    if (material.alphaMode == 1u) {
        if (baseColor.a < material.alphaCutoff) {
            discard;
        }
    }

    // Sample metallic-roughness
    var metallic = material.metallic;
    var roughness = material.roughness;
    if (material.hasMetallicRoughnessTex != 0u) {
        let mrSample = textureSample(metallicRoughnessTex, texSampler, in.uv);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }
    roughness = clamp(roughness, 0.04, 1.0);

    // Get normal and view direction
    let N = perturbNormal(normalize(in.normal), in.tangent, in.bitangent, in.uv);
    let V = normalize(scene.cameraPos - in.worldPos);
    let NdotV = max(dot(N, V), EPSILON);
    let R = reflect(-V, N);

    // Calculate F0
    let F0 = mix(vec3f(0.04), baseColor.rgb, metallic);

    // Accumulate direct lighting
    var Lo = vec3f(0.0);
    let L = normalize(scene.lightDir);
    Lo += calculateDirectLight(N, V, L, scene.lightColor, baseColor.rgb, metallic, roughness, F0);

    // IBL ambient lighting
    let F = F_SchlickRoughness(NdotV, F0, roughness);
    let kS = F;
    var kD = vec3f(1.0) - kS;
    kD *= 1.0 - metallic;

    // Diffuse IBL from irradiance map
    let irradiance = textureSample(irradianceMap, iblSampler, N).rgb;
    let diffuse = irradiance * baseColor.rgb;

    // Specular IBL from prefiltered environment map
    let MAX_REFLECTION_LOD = 4.0;
    let prefilteredColor = textureSampleLevel(
        prefilteredMap, iblSampler, R, roughness * MAX_REFLECTION_LOD
    ).rgb;
    let brdf = textureSample(brdfLUT, iblSampler, vec2f(NdotV, roughness)).rg;
    let specular = prefilteredColor * (F * brdf.x + brdf.y);

    let ambient = (kD * diffuse + specular) * scene.ambientIntensity;

    // Sample ambient occlusion
    var ao = 1.0;
    if (material.hasOcclusionTex != 0u) {
        ao = textureSample(occlusionTex, texSampler, in.uv).r;
        ao = mix(1.0, ao, material.occlusionStrength);
    }

    // Combine lighting
    var color = ambient * ao + Lo;

    // Add emissive
    var emissive = material.emissive;
    if (material.hasEmissiveTex != 0u) {
        emissive *= sRGBToLinear(textureSample(emissiveTex, texSampler, in.uv).rgb);
    }
    color += emissive * material.emissiveStrength;

    // Tone mapping
    color = color / (color + vec3f(1.0));

    // Gamma correction
    color = linearToSRGB(color);

    return vec4f(color, baseColor.a);
}
