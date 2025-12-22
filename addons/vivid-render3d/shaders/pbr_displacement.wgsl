// PBR with displacement shader - extends textured PBR with vertex displacement

const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;
const MAX_LIGHTS: u32 = 4u;

const LIGHT_DIRECTIONAL: u32 = 0u;
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;

const ALPHA_OPAQUE: u32 = 0u;
const ALPHA_MASK: u32 = 1u;
const ALPHA_BLEND: u32 = 2u;

struct Light {
    position: vec3f,
    range: f32,
    direction: vec3f,
    spotAngle: f32,
    color: vec3f,
    intensity: f32,
    lightType: u32,
    spotBlend: f32,
    _pad: vec2f,
}

struct Uniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    normalMatrix: mat4x4f,
    cameraPos: vec3f,
    ambientIntensity: f32,
    baseColorFactor: vec4f,
    metallicFactor: f32,
    roughnessFactor: f32,
    normalScale: f32,
    aoStrength: f32,
    emissiveFactor: vec3f,
    emissiveStrength: f32,
    textureFlags: u32,
    lightCount: u32,
    alphaCutoff: f32,
    alphaMode: u32,
    lights: array<Light, 4>,
}

struct DisplacementUniforms {
    amplitude: f32,
    midpoint: f32,
    _pad0: f32,
    _pad1: f32,
}

// Group 0: Material (same as textured PBR)
@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var materialSampler: sampler;
@group(0) @binding(2) var baseColorMap: texture_2d<f32>;
@group(0) @binding(3) var normalMap: texture_2d<f32>;
@group(0) @binding(4) var metallicMap: texture_2d<f32>;
@group(0) @binding(5) var roughnessMap: texture_2d<f32>;
@group(0) @binding(6) var aoMap: texture_2d<f32>;
@group(0) @binding(7) var emissiveMap: texture_2d<f32>;

// Group 1: Displacement
@group(1) @binding(0) var<uniform> displacement: DisplacementUniforms;
@group(1) @binding(1) var displacementSampler: sampler;
@group(1) @binding(2) var displacementMap: texture_2d<f32>;

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
    @location(2) worldTangent: vec3f,
    @location(3) worldBitangent: vec3f,
    @location(4) uv: vec2f,
    @location(5) color: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    // Sample displacement map at vertex UV using LOD 0
    let dispSample = textureSampleLevel(displacementMap, displacementSampler, in.uv, 0.0).r;

    // Calculate displacement: (sample - midpoint) * amplitude
    let dispAmount = (dispSample - displacement.midpoint) * displacement.amplitude;

    // Displace position along normal
    let displacedPos = in.position + in.normal * dispAmount;

    // Use displaced position for rendering
    let worldPos = uniforms.model * vec4f(displacedPos, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = uniforms.mvp * vec4f(displacedPos, 1.0);

    let N = normalize((uniforms.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    let T = normalize((uniforms.model * vec4f(in.tangent.xyz, 0.0)).xyz);
    let B = cross(N, T) * in.tangent.w;

    out.worldNormal = N;
    out.worldTangent = T;
    out.worldBitangent = B;
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

fn D_GGX(NdotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + EPSILON);
}

fn G_SchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + EPSILON);
}

fn G_Smith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

fn F_Schlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn getAttenuation(distance: f32, range: f32) -> f32 {
    if (range <= 0.0) { return 1.0; }
    let d = max(distance, EPSILON);
    let att = 1.0 / (d * d);
    let falloff = saturate(1.0 - pow(d / range, 4.0));
    return att * falloff * falloff;
}

fn getSpotFactor(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let cosAngle = dot(lightDir, spotDir);
    return saturate((cosAngle - outerAngle) / max(innerAngle - outerAngle, EPSILON));
}

fn calculateLightContribution(
    light: Light,
    worldPos: vec3f,
    N: vec3f,
    V: vec3f,
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    F0: vec3f
) -> vec3f {
    var L: vec3f;
    var radiance: vec3f;

    if (light.lightType == LIGHT_DIRECTIONAL) {
        // Negate: light.direction points from light to scene, we need surface to light
        L = -normalize(light.direction);
        radiance = light.color * light.intensity;
    } else if (light.lightType == LIGHT_POINT) {
        let lightVec = light.position - worldPos;
        let dist = length(lightVec);
        L = lightVec / max(dist, EPSILON);
        let att = getAttenuation(dist, light.range);
        radiance = light.color * light.intensity * att;
    } else {
        let lightVec = light.position - worldPos;
        let dist = length(lightVec);
        L = lightVec / max(dist, EPSILON);
        let att = getAttenuation(dist, light.range);
        let spot = getSpotFactor(-L, normalize(light.direction), light.spotBlend, light.spotAngle);
        radiance = light.color * light.intensity * att * spot;
    }

    let H = normalize(V + L);
    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), EPSILON);
    let NdotH = max(dot(N, H), 0.0);
    let HdotV = max(dot(H, V), 0.0);

    if (NdotL <= 0.0) { return vec3f(0.0); }

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
    return (diffuse + specular) * radiance * NdotL;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample textures
    let baseColorSample = textureSample(baseColorMap, materialSampler, in.uv);
    let baseColor = baseColorSample.rgb * uniforms.baseColorFactor.rgb * in.color.rgb;
    var finalAlpha = baseColorSample.a * uniforms.baseColorFactor.a * in.color.a;

    // Alpha test for mask mode
    if (uniforms.alphaMode == ALPHA_MASK && finalAlpha < uniforms.alphaCutoff) {
        discard;
    }

    let metallicSample = textureSample(metallicMap, materialSampler, in.uv).b;
    let roughnessSample = textureSample(roughnessMap, materialSampler, in.uv).g;
    let metallic = metallicSample * uniforms.metallicFactor;
    let roughness = max(roughnessSample * uniforms.roughnessFactor, 0.04);

    let aoSample = textureSample(aoMap, materialSampler, in.uv).r;
    let ao = mix(1.0, aoSample, uniforms.aoStrength);

    let emissiveSample = textureSample(emissiveMap, materialSampler, in.uv).rgb;
    let emissive = emissiveSample * uniforms.emissiveFactor * uniforms.emissiveStrength;

    // Normal mapping
    var tangentNormal = textureSample(normalMap, materialSampler, in.uv).xyz * 2.0 - 1.0;
    tangentNormal = vec3f(tangentNormal.x * uniforms.normalScale, tangentNormal.y * uniforms.normalScale, tangentNormal.z);
    tangentNormal = normalize(tangentNormal);

    // Use baseColor directly (no sRGB conversion to match original PBR shader)
    let albedo = baseColor;

    let TBN = mat3x3f(
        normalize(in.worldTangent),
        normalize(in.worldBitangent),
        normalize(in.worldNormal)
    );
    let N = normalize(TBN * tangentNormal);
    let V = normalize(uniforms.cameraPos - in.worldPos);
    let F0 = mix(vec3f(0.04), albedo, metallic);

    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        Lo += calculateLightContribution(
            uniforms.lights[i], in.worldPos, N, V, albedo, metallic, roughness, F0
        );
    }

    let ambient = vec3f(0.03) * albedo * uniforms.ambientIntensity * ao;

    var color = ambient + Lo + emissive;
    color = color / (color + vec3f(1.0));
    color = pow(color, vec3f(1.0 / 2.2));

    var outAlpha = finalAlpha;
    if (uniforms.alphaMode == ALPHA_OPAQUE) {
        outAlpha = 1.0;
    }

    return vec4f(color, outAlpha);
}
