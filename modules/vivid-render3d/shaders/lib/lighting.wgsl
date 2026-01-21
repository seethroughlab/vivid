// Vivid Render3D - Lighting Library
// Light types, structures, and attenuation functions
// @include "constants.wgsl"

// Light type constants
const LIGHT_DIRECTIONAL: u32 = 0u;
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;

// Maximum lights in a single pass
const MAX_LIGHTS: u32 = 4u;

// Light structure (must match C++ layout)
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

// Distance attenuation with smooth falloff
fn getAttenuation(distance: f32, range: f32) -> f32 {
    if (range <= 0.0) { return 1.0; }
    let d = max(distance, EPSILON);
    let att = 1.0 / (d * d);
    let falloff = saturate(1.0 - pow(d / range, 4.0));
    return att * falloff * falloff;
}

// Spotlight cone factor
fn getSpotFactor(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let cosAngle = dot(lightDir, spotDir);
    return saturate((cosAngle - outerAngle) / max(innerAngle - outerAngle, EPSILON));
}
