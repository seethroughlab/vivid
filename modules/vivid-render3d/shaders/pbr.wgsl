// Vivid Render3D - PBR Shader with Multi-Light Support

// @include "lib/constants.wgsl"
// @include "lib/pbr.wgsl"
// @include "lib/lighting.wgsl"
// @include "lib/tonemapping.wgsl"

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

// @include "lib/shadow.wgsl"

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
    color = reinhard(color);
    color = gammaCorrect(color);

    return vec4f(color, uniforms.baseColor.a * in.color.a);
}
