// PBR shader with multi-light support

const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;
const MAX_LIGHTS: u32 = 4u;

const LIGHT_DIRECTIONAL: u32 = 0u;
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;

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
    baseColor: vec4f,
    metallic: f32,
    roughness: f32,
    lightCount: u32,
    receiveShadow: u32,
    lights: array<Light, 4>,
}

struct ShadowUniforms {
    lightViewProj: mat4x4f,
    shadowBias: f32,
    shadowMapSize: f32,
    shadowEnabled: u32,
    pointShadowEnabled: u32,
    pointLightPosAndRange: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@group(1) @binding(0) var<uniform> shadow: ShadowUniforms;
@group(1) @binding(1) var shadowMap: texture_depth_2d;
@group(1) @binding(2) var shadowSampler: sampler_comparison;
@group(1) @binding(3) var pointShadowAtlas: texture_2d<f32>;
@group(1) @binding(4) var pointShadowSampler: sampler;

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

fn sampleShadow(worldPos: vec3f) -> f32 {
    if (shadow.shadowEnabled == 0u) { return 1.0; }
    let lightSpacePos = shadow.lightViewProj * vec4f(worldPos, 1.0);
    var projCoords = lightSpacePos.xyz / lightSpacePos.w;
    let texCoordX = projCoords.x * 0.5 + 0.5;
    let texCoordY = 1.0 - (projCoords.y * 0.5 + 0.5);
    let texCoordZ = projCoords.z;
    if (texCoordX < 0.0 || texCoordX > 1.0 || texCoordY < 0.0 || texCoordY > 1.0 || texCoordZ < 0.0 || texCoordZ > 1.0) { return 1.0; }
    let currentDepth = texCoordZ - shadow.shadowBias;
    return textureSampleCompare(shadowMap, shadowSampler, vec2f(texCoordX, texCoordY), currentDepth);
}

fn samplePointShadow(worldPos: vec3f) -> f32 {
    if (shadow.pointShadowEnabled == 0u) { return 1.0; }
    let lightToFrag = worldPos - shadow.pointLightPosAndRange.xyz;
    let fragDist = length(lightToFrag);
    let absDir = abs(lightToFrag);
    var faceIndex: i32; var u: f32; var v: f32; var ma: f32;
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        ma = absDir.x;
        if (lightToFrag.x > 0.0) { faceIndex = 0; u = -lightToFrag.z; v = -lightToFrag.y; }
        else { faceIndex = 1; u = lightToFrag.z; v = -lightToFrag.y; }
    } else if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        ma = absDir.y;
        if (lightToFrag.y > 0.0) { faceIndex = 2; u = lightToFrag.x; v = lightToFrag.z; }
        else { faceIndex = 3; u = lightToFrag.x; v = -lightToFrag.z; }
    } else {
        ma = absDir.z;
        if (lightToFrag.z > 0.0) { faceIndex = 4; u = lightToFrag.x; v = -lightToFrag.y; }
        else { faceIndex = 5; u = -lightToFrag.x; v = -lightToFrag.y; }
    }
    let texU = (u / ma) * 0.5 + 0.5;
    let texV = 0.5 - (v / ma) * 0.5;
    let faceUV = vec2f(texU, texV);
    let col = f32(faceIndex % 3);
    let row = f32(faceIndex / 3);
    let atlasUV = (faceUV + vec2f(col, row)) / vec2f(3.0, 2.0);
    let sampledDepth = textureSample(pointShadowAtlas, pointShadowSampler, atlasUV).r;
    let normalizedFragDist = fragDist / shadow.pointLightPosAndRange.w;
    if (normalizedFragDist - shadow.shadowBias > sampledDepth) { return 0.0; }
    return 1.0;
}

fn calculateLightContribution(
    light: Light, lightIndex: u32, worldPos: vec3f, N: vec3f, V: vec3f,
    albedo: vec3f, metallic: f32, roughness: f32, F0: vec3f
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
        radiance = light.color * light.intensity * getAttenuation(dist, light.range);
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

    // Shadow factor (only for first light, only if receiving shadows)
    var shadowFactor: f32 = 1.0;
    if (lightIndex == 0u && uniforms.receiveShadow != 0u) {
        if (light.lightType == LIGHT_POINT) {
            shadowFactor = samplePointShadow(worldPos);
        } else {
            shadowFactor = sampleShadow(worldPos);
        }
    }

    let D = D_GGX(NdotH, roughness);
    let G = G_Smith(NdotV, NdotL, roughness);
    let F = F_Schlick(HdotV, F0);

    let specular = (D * G * F) / (4.0 * NdotV * NdotL + EPSILON);
    let kS = F;
    var kD = vec3f(1.0) - kS;
    kD *= 1.0 - metallic;

    return (kD * albedo / PI + specular) * radiance * NdotL * shadowFactor;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let N = normalize(in.worldNormal);
    let V = normalize(uniforms.cameraPos - in.worldPos);

    let albedo = uniforms.baseColor.rgb * in.color.rgb;
    let metallic = uniforms.metallic;
    let roughness = max(uniforms.roughness, 0.04);
    let F0 = mix(vec3f(0.04), albedo, metallic);

    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        Lo += calculateLightContribution(uniforms.lights[i], i, in.worldPos, N, V, albedo, metallic, roughness, F0);
    }

    let ambient = vec3f(0.03) * albedo * uniforms.ambientIntensity;
    var color = ambient + Lo;
    color = color / (color + vec3f(1.0));  // Reinhard
    color = pow(color, vec3f(1.0 / 2.2));  // Gamma

    return vec4f(color, uniforms.baseColor.a * in.color.a);
}
