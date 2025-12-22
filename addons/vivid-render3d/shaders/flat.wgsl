// Flat/Gouraud/Unlit shader with multi-light support and shadow mapping

const MAX_LIGHTS: u32 = 4u;
const LIGHT_DIRECTIONAL: u32 = 0u;
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;
const EPSILON: f32 = 0.0001;

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
    worldPos: vec3f,
    _pad0: f32,
    baseColor: vec4f,
    ambient: f32,
    shadingMode: u32,
    lightCount: u32,
    toonLevels: u32,
    receiveShadow: u32, // 1=receive shadows, 0=ignore shadows
    _pad1: vec3f,       // Padding to align lights array
    lights: array<Light, 4>,
};

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
// Point shadow uses 3x2 atlas texture
// Layout: +X(0,0), -X(1,0), +Y(2,0), -Y(0,1), +Z(1,1), -Z(2,1)
@group(1) @binding(3) var pointShadowAtlas: texture_2d<f32>;
@group(1) @binding(4) var pointShadowSampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
    @location(4) lighting: vec3f,
};

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
    // Convert to [0,1] UV within the face
    let texU = (u / ma) * 0.5 + 0.5;
    let texV = 0.5 - (v / ma) * 0.5;
    let faceUV = vec2f(texU, texV);

    // Calculate atlas UV based on face index
    // Layout: +X(0,0), -X(1,0), +Y(2,0), -Y(0,1), +Z(1,1), -Z(2,1)
    let col = f32(faceIndex % 3);
    let row = f32(faceIndex / 3);
    let atlasUV = (faceUV + vec2f(col, row)) / vec2f(3.0, 2.0);

    let sampledDepth = textureSample(pointShadowAtlas, pointShadowSampler, atlasUV).r;
    let normalizedFragDist = fragDist / shadow.pointLightPosAndRange.w;
    if (normalizedFragDist - shadow.shadowBias > sampledDepth) { return 0.0; }
    return 1.0;
}

fn calculateSimpleLightingNoShadow(worldPos: vec3f, N: vec3f) -> vec3f {
    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        let light = uniforms.lights[i];
        var L: vec3f; var attenuation: f32 = 1.0;
        if (light.lightType == LIGHT_DIRECTIONAL) { L = -normalize(light.direction); }
        else if (light.lightType == LIGHT_POINT) {
            let lightVec = light.position - worldPos;
            let dist = length(lightVec);
            L = lightVec / max(dist, EPSILON);
            attenuation = getAttenuation(dist, light.range);
        } else {
            let lightVec = light.position - worldPos;
            let dist = length(lightVec);
            L = lightVec / max(dist, EPSILON);
            attenuation = getAttenuation(dist, light.range);
            attenuation *= getSpotFactor(-L, normalize(light.direction), light.spotBlend, light.spotAngle);
        }
        Lo += light.color * light.intensity * max(dot(N, L), 0.0) * attenuation;
    }
    return Lo;
}

fn calculateSimpleLighting(worldPos: vec3f, N: vec3f) -> vec3f {
    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    // Skip shadow sampling if object doesn't receive shadows
    let shadowFactor = select(1.0, sampleShadow(worldPos), uniforms.receiveShadow != 0u);
    for (var i = 0u; i < lightCount; i++) {
        let light = uniforms.lights[i];
        var L: vec3f; var attenuation: f32 = 1.0; var lightShadow: f32 = 1.0;
        if (light.lightType == LIGHT_DIRECTIONAL) {
            L = -normalize(light.direction);
            if (i == 0u) { lightShadow = shadowFactor; }
        } else if (light.lightType == LIGHT_POINT) {
            let lightVec = light.position - worldPos;
            let dist = length(lightVec);
            L = lightVec / max(dist, EPSILON);
            attenuation = getAttenuation(dist, light.range);
            // Apply point light shadow (skip if not receiving shadows)
            if (i == 0u && uniforms.receiveShadow != 0u) { lightShadow = samplePointShadow(worldPos); }
        } else {
            let lightVec = light.position - worldPos;
            let dist = length(lightVec);
            L = lightVec / max(dist, EPSILON);
            attenuation = getAttenuation(dist, light.range);
            attenuation *= getSpotFactor(-L, normalize(light.direction), light.spotBlend, light.spotAngle);
            if (i == 0u) { lightShadow = shadowFactor; }
        }
        Lo += light.color * light.intensity * max(dot(N, L), 0.0) * attenuation * lightShadow;
    }
    return Lo;
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos4 = uniforms.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos4.xyz;
    out.position = uniforms.mvp * vec4f(in.position, 1.0);
    out.worldNormal = normalize((uniforms.model * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    out.color = in.color;
    if (uniforms.shadingMode == 2u || uniforms.shadingMode == 3u) {
        out.lighting = calculateSimpleLightingNoShadow(out.worldPos, out.worldNormal);
    } else {
        out.lighting = vec3f(1.0);
    }
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var finalColor = uniforms.baseColor * in.color;
    if (uniforms.shadingMode == 0u) { return finalColor; }
    else if (uniforms.shadingMode == 1u) {
        let N = normalize(in.worldNormal);
        let lighting = calculateSimpleLighting(in.worldPos, N);
        return vec4f(finalColor.rgb * (vec3f(uniforms.ambient) + lighting), finalColor.a);
    } else if (uniforms.shadingMode == 2u) {
        return vec4f(finalColor.rgb * (vec3f(uniforms.ambient) + in.lighting), finalColor.a);
    } else if (uniforms.shadingMode == 3u) {
        return vec4f(finalColor.rgb * in.lighting, finalColor.a);
    } else if (uniforms.shadingMode == 4u) {
        let N = normalize(in.worldNormal);
        let lighting = calculateSimpleLighting(in.worldPos, N);
        let luminance = dot(lighting, vec3f(0.299, 0.587, 0.114));
        let levels = f32(uniforms.toonLevels);
        let quantized = floor(luminance * levels + 0.5) / levels;
        return vec4f(finalColor.rgb * (uniforms.ambient + quantized), finalColor.a);
    }
    return vec4f(finalColor.rgb * (vec3f(uniforms.ambient) + in.lighting), finalColor.a);
}
