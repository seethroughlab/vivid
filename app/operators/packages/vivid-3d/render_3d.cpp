#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_3d.h"
#include <array>
#include <algorithm>   // std::clamp / std::max — clang pulls these in transitively, gcc does not
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
// Multi-light Blinn-Phong shader (non-instanced)
// =============================================================================

static const char* kRender3DFragment = R"(
struct Material {
    color: vec4f,
    roughness: f32,
    metallic: f32,
    emission: f32,
    flags: f32,
    shading_mode: f32,
    toon_levels: f32,
    _pad0: f32,
    _pad1: f32,
    fog_enabled: f32,
    fog_mode: f32, // 0=Linear, 1=Exp2
    fog_near: f32,
    fog_far: f32,
    fog_color: vec3f,
    fog_density: f32,
}

@group(0) @binding(0) var<uniform> camera: Camera3D;
@group(0) @binding(1) var<uniform> material: Material;
@group(0) @binding(2) var<uniform> lighting: LightsUniform;
@group(0) @binding(3) var<uniform> shadow: ShadowData;
@group(0) @binding(4) var shadow_cmp_sampler: sampler_comparison;
@group(0) @binding(5) var dir_shadow_map: texture_depth_2d_array;

fn apply_fog(color: vec3f, world_pos: vec3f) -> vec3f {
    if (material.fog_enabled < 0.5) {
        return color;
    }
    let dist = distance(camera.camera_pos, world_pos);
    var fog_factor = 0.0;
    if (material.fog_mode < 0.5) {
        let denom = max(material.fog_far - material.fog_near, 0.0001);
        fog_factor = saturate((dist - material.fog_near) / denom);
    } else {
        let x = dist * max(material.fog_density, 0.0);
        fog_factor = 1.0 - exp(-(x * x));
    }
    return mix(color, material.fog_color, saturate(fog_factor));
}

@vertex
fn vs_main(@location(0) pos: vec3f,
           @location(1) normal: vec3f,
           @location(2) tangent: vec4f,
           @location(3) uv: vec2f) -> Vertex3DOutput {
    return transform3d(camera, pos, normal, tangent, uv);
}

fn blinn_phong(N: vec3f, V: vec3f, light: Light, world_pos: vec3f) -> vec2f {
    var L: vec3f;
    var attenuation: f32 = 1.0;

    if (light.position_and_type.w < 0.5) {
        L = normalize(light.direction_and_intensity.xyz);
    } else if (light.position_and_type.w > 1.5) {
        let to_light = light.position_and_type.xyz - world_pos;
        let dist = length(to_light);
        L = to_light / max(dist, 0.001);
        let radius = light.color_and_radius.w;
        let ratio = dist / max(radius, 0.001);
        attenuation = saturate(1.0 - ratio * ratio);
        let cos_angle = dot(-L, normalize(light.direction_and_intensity.xyz));
        let cos_outer = light.spot_params.x;
        let cos_inner = light.spot_params.y;
        attenuation *= saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001));
    } else {
        let to_light = light.position_and_type.xyz - world_pos;
        let dist = length(to_light);
        L = to_light / max(dist, 0.001);
        let radius = light.color_and_radius.w;
        let ratio = dist / max(radius, 0.001);
        attenuation = saturate(1.0 - ratio * ratio);
    }

    let H = normalize(L + V);
    let intensity = light.direction_and_intensity.w;

    let shininess = pow(2.0, (1.0 - material.roughness) * 7.0) + 2.0;
    let diffuse  = max(dot(N, L), 0.0);
    let specular = pow(max(dot(N, H), 0.0), shininess);

    // Toon quantization
    var d = diffuse;
    var s = specular;
    if (material.shading_mode > 0.5) {
        let levels = max(material.toon_levels, 1.0);
        d = floor(d * levels + 0.5) / levels;
        s = step(0.5, s);
    }

    return vec2f(d, s) * intensity * attenuation;
}

@fragment
fn fs_main(in: Vertex3DOutput) -> @location(0) vec4f {
    let base_color = material.color.rgb;
    let alpha = material.color.a;

    // Unlit early-out
    if (material.flags > 0.5) {
        let unlit_color = apply_fog(base_color * (1.0 + material.emission), in.world_pos);
        return vec4f(unlit_color, alpha);
    }

    let N = normalize(in.normal);
    let V = normalize(camera.camera_pos - in.world_pos);
    let ambient = vec3f(lighting.ambient_r, lighting.ambient_g, lighting.ambient_b);

    let diffuse_color  = base_color * (1.0 - material.metallic);
    let specular_color = mix(vec3f(0.04), base_color, material.metallic);

    var color = diffuse_color * ambient;
    for (var i: u32 = 0u; i < min(lighting.light_count, 4u); i++) {
        let light = lighting.lights[i];
        let light_color = light.color_and_radius.xyz;
        let bp = blinn_phong(N, V, light, in.world_pos);

        // ADR-0051 P2: a light casts from its OWN array layer, or not at all (-1).
        var shadow_factor: f32 = 1.0;
        let sslot = shadow_slot_for(i, shadow);
        if (sslot >= 0) {
            shadow_factor = sample_shadow_dir(in.world_pos, sslot, shadow,
                                               dir_shadow_map, shadow_cmp_sampler);
        }

        color += light_color * (diffuse_color * bp.x + specular_color * bp.y) * shadow_factor;
    }

    // Emission
    color += base_color * material.emission;

    color = apply_fog(color, in.world_pos);
    return vec4f(color, alpha);
}
)";

// =============================================================================
// Instanced Blinn-Phong shader
// =============================================================================

static const char* kRender3DInstanced = R"(
struct Material {
    color: vec4f,
    roughness: f32,
    metallic: f32,
    emission: f32,
    flags: f32,
    shading_mode: f32,
    toon_levels: f32,
    _pad0: f32,
    _pad1: f32,
    fog_enabled: f32,
    fog_mode: f32, // 0=Linear, 1=Exp2
    fog_near: f32,
    fog_far: f32,
    fog_color: vec3f,
    fog_density: f32,
}

struct InstanceData {
    pos_rot:   vec4f,   // xyz=position, w=rotation_y (yaw)
    scale_pad: vec4f,   // xyz=scale, w=rotation_x (pitch)
    color:     vec4f,
}

@group(0) @binding(0) var<uniform> camera: Camera3D;
@group(0) @binding(1) var<uniform> material: Material;
@group(0) @binding(2) var<uniform> lighting: LightsUniform;
@group(0) @binding(3) var<uniform> shadow: ShadowData;
@group(0) @binding(4) var shadow_cmp_sampler: sampler_comparison;
@group(0) @binding(5) var dir_shadow_map: texture_depth_2d_array;
@group(1) @binding(0) var<storage, read> instances: array<InstanceData>;

fn apply_fog(color: vec3f, world_pos: vec3f) -> vec3f {
    if (material.fog_enabled < 0.5) {
        return color;
    }
    let dist = distance(camera.camera_pos, world_pos);
    var fog_factor = 0.0;
    if (material.fog_mode < 0.5) {
        let denom = max(material.fog_far - material.fog_near, 0.0001);
        fog_factor = saturate((dist - material.fog_near) / denom);
    } else {
        let x = dist * max(material.fog_density, 0.0);
        fog_factor = 1.0 - exp(-(x * x));
    }
    return mix(color, material.fog_color, saturate(fog_factor));
}

struct InstancedOutput {
    @builtin(position) position: vec4f,
    @location(0) world_pos: vec3f,
    @location(1) normal:    vec3f,
    @location(2) tangent:   vec4f,
    @location(3) uv:        vec2f,
    @location(4) instance_id: f32,
}

@vertex
fn vs_instanced(@location(0) pos: vec3f,
                @location(1) normal: vec3f,
                @location(2) tangent: vec4f,
                @location(3) uv: vec2f,
                @builtin(instance_index) iid: u32) -> InstancedOutput {
    let inst = instances[iid];
    let inst_pos = inst.pos_rot.xyz;
    let rot_y = inst.pos_rot.w;     // yaw
    let rot_x = inst.scale_pad.w;   // pitch
    let inst_scale = inst.scale_pad.xyz;

    // Apply non-uniform scale
    let scaled = pos * inst_scale;

    // Apply pitch (X rotation) first
    let cx = cos(rot_x);
    let sx = sin(rot_x);
    let pitched = vec3f(
        scaled.x,
        scaled.y * cx - scaled.z * sx,
        scaled.y * sx + scaled.z * cx
    );

    // Then yaw (Y rotation)
    let c = cos(rot_y);
    let s = sin(rot_y);
    let rotated = vec3f(
        pitched.x * c + pitched.z * s,
        pitched.y,
        -pitched.x * s + pitched.z * c
    );

    let local_pos = rotated + inst_pos;
    let world = camera.model * vec4f(local_pos, 1.0);

    // Normal transform: Ry * Rx * (normal * inverse_scale), then normalize
    let inv_scale = vec3f(1.0 / max(inst_scale.x, 0.0001),
                          1.0 / max(inst_scale.y, 0.0001),
                          1.0 / max(inst_scale.z, 0.0001));
    let n_scaled = normal * inv_scale;
    // Pitch
    let n_pitched = vec3f(
        n_scaled.x,
        n_scaled.y * cx - n_scaled.z * sx,
        n_scaled.y * sx + n_scaled.z * cx
    );
    // Yaw
    let n_rotated = vec3f(
        n_pitched.x * c + n_pitched.z * s,
        n_pitched.y,
        -n_pitched.x * s + n_pitched.z * c
    );
    let world_normal = normalize((camera.normal_matrix * vec4f(n_rotated, 0.0)).xyz);

    // Tangent transform: Ry * Rx (rotation only, no inverse-scale)
    let t_pitched = vec3f(
        tangent.x,
        tangent.y * cx - tangent.z * sx,
        tangent.y * sx + tangent.z * cx
    );
    let t_rotated = vec3f(
        t_pitched.x * c + t_pitched.z * s,
        t_pitched.y,
        -t_pitched.x * s + t_pitched.z * c
    );
    let world_tangent = normalize((camera.normal_matrix * vec4f(t_rotated, 0.0)).xyz);

    var out: InstancedOutput;
    out.position  = camera.mvp * vec4f(local_pos, 1.0);
    out.world_pos = world.xyz;
    out.normal    = world_normal;
    out.tangent   = vec4f(world_tangent, tangent.w);
    out.uv        = uv;
    out.instance_id = f32(iid);
    return out;
}

fn blinn_phong_i(N: vec3f, V: vec3f, light: Light, world_pos: vec3f) -> vec2f {
    var L: vec3f;
    var attenuation: f32 = 1.0;

    if (light.position_and_type.w < 0.5) {
        L = normalize(light.direction_and_intensity.xyz);
    } else if (light.position_and_type.w > 1.5) {
        let to_light = light.position_and_type.xyz - world_pos;
        let dist = length(to_light);
        L = to_light / max(dist, 0.001);
        let radius = light.color_and_radius.w;
        let ratio = dist / max(radius, 0.001);
        attenuation = saturate(1.0 - ratio * ratio);
        let cos_angle = dot(-L, normalize(light.direction_and_intensity.xyz));
        let cos_outer = light.spot_params.x;
        let cos_inner = light.spot_params.y;
        attenuation *= saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001));
    } else {
        let to_light = light.position_and_type.xyz - world_pos;
        let dist = length(to_light);
        L = to_light / max(dist, 0.001);
        let radius = light.color_and_radius.w;
        let ratio = dist / max(radius, 0.001);
        attenuation = saturate(1.0 - ratio * ratio);
    }

    let H = normalize(L + V);
    let intensity = light.direction_and_intensity.w;

    let shininess = pow(2.0, (1.0 - material.roughness) * 7.0) + 2.0;
    let diffuse  = max(dot(N, L), 0.0);
    let specular = pow(max(dot(N, H), 0.0), shininess);

    // Toon quantization
    var d = diffuse;
    var s = specular;
    if (material.shading_mode > 0.5) {
        let levels = max(material.toon_levels, 1.0);
        d = floor(d * levels + 0.5) / levels;
        s = step(0.5, s);
    }

    return vec2f(d, s) * intensity * attenuation;
}

@fragment
fn fs_instanced(in: InstancedOutput) -> @location(0) vec4f {
    let inst = instances[u32(floor(in.instance_id))];
    let inst_color = inst.color;
    let base_color = inst_color.rgb;
    let alpha = inst_color.a;

    // Unlit early-out
    if (material.flags > 0.5) {
        let unlit_color = apply_fog(base_color * (1.0 + material.emission), in.world_pos);
        return vec4f(unlit_color, alpha);
    }

    let N = normalize(in.normal);
    let V = normalize(camera.camera_pos - in.world_pos);
    let ambient = vec3f(lighting.ambient_r, lighting.ambient_g, lighting.ambient_b);

    let diffuse_color  = base_color * (1.0 - material.metallic);
    let specular_color = mix(vec3f(0.04), base_color, material.metallic);

    var color = diffuse_color * ambient;
    for (var i: u32 = 0u; i < min(lighting.light_count, 4u); i++) {
        let light = lighting.lights[i];
        let light_color = light.color_and_radius.xyz;
        let bp = blinn_phong_i(N, V, light, in.world_pos);

        // ADR-0051 P2: a light casts from its OWN array layer, or not at all (-1).
        var shadow_factor: f32 = 1.0;
        let sslot = shadow_slot_for(i, shadow);
        if (sslot >= 0) {
            shadow_factor = sample_shadow_dir(in.world_pos, sslot, shadow,
                                               dir_shadow_map, shadow_cmp_sampler);
        }

        color += light_color * (diffuse_color * bp.x + specular_color * bp.y) * shadow_factor;
    }

    // Emission
    color += base_color * material.emission;

    color = apply_fog(color, in.world_pos);
    return vec4f(color, alpha);
}
)";

// =============================================================================
// Billboard shader (camera-facing quads for particle instancing)
// =============================================================================

static const char* kRender3DBillboard = R"(
struct Material {
    color: vec4f,
    roughness: f32,
    metallic: f32,
    emission: f32,
    flags: f32,
    shading_mode: f32,
    toon_levels: f32,
    _pad0: f32,
    _pad1: f32,
    fog_enabled: f32,
    fog_mode: f32, // 0=Linear, 1=Exp2
    fog_near: f32,
    fog_far: f32,
    fog_color: vec3f,
    fog_density: f32,
}

struct InstanceData {
    pos_rot:   vec4f,   // xyz=position, w=rotation_y
    scale_pad: vec4f,   // xyz=scale, w=unused
    color:     vec4f,
}

@group(0) @binding(0) var<uniform> camera: Camera3D;
@group(0) @binding(1) var<uniform> material: Material;
@group(0) @binding(2) var<uniform> lighting: LightsUniform;
@group(0) @binding(3) var<uniform> shadow: ShadowData;
@group(0) @binding(4) var shadow_cmp_sampler: sampler_comparison;
@group(0) @binding(5) var dir_shadow_map: texture_depth_2d_array;
@group(1) @binding(0) var<storage, read> instances: array<InstanceData>;

fn apply_fog(color: vec3f, world_pos: vec3f) -> vec3f {
    if (material.fog_enabled < 0.5) {
        return color;
    }
    let dist = distance(camera.camera_pos, world_pos);
    var fog_factor = 0.0;
    if (material.fog_mode < 0.5) {
        let denom = max(material.fog_far - material.fog_near, 0.0001);
        fog_factor = saturate((dist - material.fog_near) / denom);
    } else {
        let x = dist * max(material.fog_density, 0.0);
        fog_factor = 1.0 - exp(-(x * x));
    }
    return mix(color, material.fog_color, saturate(fog_factor));
}

struct BillboardOutput {
    @builtin(position) position: vec4f,
    @location(0) world_pos: vec3f,
    @location(1) color:     vec4f,
    @location(2) uv:        vec2f,
}

@vertex
fn vs_billboard(@location(0) pos: vec3f,
                @location(1) normal: vec3f,
                @location(2) tangent: vec4f,
                @location(3) uv: vec2f,
                @builtin(instance_index) iid: u32) -> BillboardOutput {
    let inst = instances[iid];
    let inst_position = inst.pos_rot.xyz;
    let billboard_size = inst.scale_pad.x;  // uniform size for billboards
    let elong = inst.scale_pad.y;           // >1 → stretch into a streak (1 = round)
    let vdir = vec3f(inst.pos_rot.w, inst.scale_pad.z, inst.scale_pad.w);  // velocity dir (when stretched)
    let inst_pos = (camera.model * vec4f(inst_position, 1.0)).xyz;

    // Camera-facing basis vectors
    var to_camera = normalize(camera.camera_pos - inst_pos);
    let world_up = vec3f(0.0, 1.0, 0.0);
    var right = normalize(cross(world_up, to_camera));
    // Degenerate fallback when looking straight down
    if (length(cross(world_up, to_camera)) < 0.001) {
        right = vec3f(1.0, 0.0, 0.0);
    }
    let up = cross(to_camera, right);

    // Offset from instance position along billboard axes. When elongated, stretch the quad along the
    // velocity direction projected onto the billboard plane (a screen-aligned motion-streak).
    var offset = right * pos.x * billboard_size + up * pos.y * billboard_size;
    let vr = dot(vdir, right);
    let vu = dot(vdir, up);
    let vlen = length(vec2f(vr, vu));
    if (elong > 1.0 && vlen > 1e-4) {
        let sdir = vec2f(vr, vu) / vlen;
        let perp = vec2f(-sdir.y, sdir.x);
        let along = dot(pos.xy, sdir) * elong;
        let across = dot(pos.xy, perp);
        let q = sdir * along + perp * across;
        offset = (right * q.x + up * q.y) * billboard_size;
    }
    let world = inst_pos + offset;

    var out: BillboardOutput;
    out.position  = camera.mvp * vec4f(inst_position + (camera.normal_matrix * vec4f(offset, 0.0)).xyz, 1.0);
    out.world_pos = world;
    out.color     = inst.color;
    out.uv        = uv;
    return out;
}

@fragment
fn fs_billboard(in: BillboardOutput) -> @location(0) vec4f {
    // Soft circle SDF for round particles
    let center = in.uv - vec2f(0.5, 0.5);
    let dist = length(center) * 2.0;
    let alpha = in.color.a * saturate(1.0 - dist);
    if (alpha < 0.01) { discard; }
    let lit = in.color.rgb * (1.0 + material.emission);
    let color = apply_fog(lit, in.world_pos);
    return vec4f(color, alpha);
}
)";

// =============================================================================
// Textured PBR shader (Cook-Torrance BRDF, non-instanced)
// =============================================================================

static const char* kRender3DTextured = R"(
struct Material {
    color: vec4f,
    roughness: f32,
    metallic: f32,
    emission: f32,
    flags: f32,
    shading_mode: f32,
    toon_levels: f32,
    _pad0: f32,
    _pad1: f32,
    fog_enabled: f32,
    fog_mode: f32, // 0=Linear, 1=Exp2
    fog_near: f32,
    fog_far: f32,
    fog_color: vec3f,
    fog_density: f32,
}

@group(0) @binding(0) var<uniform> camera: Camera3D;
@group(0) @binding(1) var<uniform> material: Material;
@group(0) @binding(2) var<uniform> lighting: LightsUniform;
@group(0) @binding(3) var<uniform> shadow: ShadowData;
@group(0) @binding(4) var shadow_cmp_sampler: sampler_comparison;
@group(0) @binding(5) var dir_shadow_map: texture_depth_2d_array;

@group(1) @binding(0) var pbr_sampler: sampler;
@group(1) @binding(1) var albedo_map: texture_2d<f32>;
@group(1) @binding(2) var normal_map: texture_2d<f32>;
@group(1) @binding(3) var roughness_metallic_map: texture_2d<f32>;
@group(1) @binding(4) var emission_map: texture_2d<f32>;

fn apply_fog(color: vec3f, world_pos: vec3f) -> vec3f {
    if (material.fog_enabled < 0.5) {
        return color;
    }
    let dist = distance(camera.camera_pos, world_pos);
    var fog_factor = 0.0;
    if (material.fog_mode < 0.5) {
        let denom = max(material.fog_far - material.fog_near, 0.0001);
        fog_factor = saturate((dist - material.fog_near) / denom);
    } else {
        let x = dist * max(material.fog_density, 0.0);
        fog_factor = 1.0 - exp(-(x * x));
    }
    return mix(color, material.fog_color, saturate(fog_factor));
}

@vertex
fn vs_textured(@location(0) pos: vec3f,
               @location(1) normal: vec3f,
               @location(2) tangent: vec4f,
               @location(3) uv: vec2f) -> Vertex3DOutput {
    return transform3d(camera, pos, normal, tangent, uv);
}

@fragment
fn fs_textured(in: Vertex3DOutput) -> @location(0) vec4f {
    // Sample textures
    let albedo_tex = textureSample(albedo_map, pbr_sampler, in.uv);
    let normal_tex = textureSample(normal_map, pbr_sampler, in.uv);
    let rm_tex     = textureSample(roughness_metallic_map, pbr_sampler, in.uv);
    let emit_tex   = textureSample(emission_map, pbr_sampler, in.uv);

    // Material params modulated by textures
    let base_color = material.color.rgb * albedo_tex.rgb;
    // Treat PBR textured surfaces as opaque by default; many albedo maps
    // are authored without meaningful alpha and can cause unintended see-through.
    let alpha      = material.color.a;
    let roughness  = material.roughness * rm_tex.r;
    let metallic   = material.metallic + rm_tex.g;  // additive — rm_tex.g=0 by default
    let emit_color = base_color * material.emission * max(emit_tex.r, max(emit_tex.g, emit_tex.b));

    // Unlit early-out
    if (material.flags > 0.5) {
        let unlit_color = apply_fog(base_color + emit_color, in.world_pos);
        return vec4f(unlit_color, alpha);
    }

    // Normal mapping (TBN)
    let geom_N = normalize(in.normal);
    let T = normalize(in.tangent.xyz);
    let B = cross(geom_N, T) * in.tangent.w;
    let TBN = mat3x3f(T, B, geom_N);
    let map_n = normal_tex.rgb * 2.0 - 1.0;
    let N = normalize(TBN * map_n);

    let V = normalize(camera.camera_pos - in.world_pos);
    let NdotV = max(dot(N, V), 0.001);
    let ambient = vec3f(lighting.ambient_r, lighting.ambient_g, lighting.ambient_b);

    let F0 = mix(vec3f(0.04), base_color, metallic);
    let diffuse_color = base_color * (1.0 - metallic);

    var color = diffuse_color * ambient;
    for (var i: u32 = 0u; i < min(lighting.light_count, 4u); i++) {
        let light = lighting.lights[i];
        let light_color = light.color_and_radius.xyz;
        let intensity = light.direction_and_intensity.w;

        var L: vec3f;
        var attenuation: f32 = 1.0;
        if (light.position_and_type.w < 0.5) {
            L = normalize(light.direction_and_intensity.xyz);
        } else if (light.position_and_type.w > 1.5) {
            let to_light = light.position_and_type.xyz - in.world_pos;
            let dist = length(to_light);
            L = to_light / max(dist, 0.001);
            let radius = light.color_and_radius.w;
            let ratio = dist / max(radius, 0.001);
            attenuation = saturate(1.0 - ratio * ratio);
            let cos_angle = dot(-L, normalize(light.direction_and_intensity.xyz));
            let cos_outer = light.spot_params.x;
            let cos_inner = light.spot_params.y;
            attenuation *= saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001));
        } else {
            let to_light = light.position_and_type.xyz - in.world_pos;
            let dist = length(to_light);
            L = to_light / max(dist, 0.001);
            let radius = light.color_and_radius.w;
            let ratio = dist / max(radius, 0.001);
            attenuation = saturate(1.0 - ratio * ratio);
        }

        let H = normalize(L + V);
        let NdotL = max(dot(N, L), 0.0);
        let NdotH = max(dot(N, H), 0.0);
        let HdotV = max(dot(H, V), 0.0);

        // Cook-Torrance specular BRDF
        let D = D_GGX(NdotH, roughness);
        let G = G_Smith(NdotV, NdotL, roughness);
        let F = F_Schlick(HdotV, F0);
        let spec = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

        let kD = (1.0 - F) * (1.0 - metallic);

        // ADR-0051 P2: a light casts from its OWN array layer, or not at all (-1).
        var shadow_factor: f32 = 1.0;
        let sslot = shadow_slot_for(i, shadow);
        if (sslot >= 0) {
            shadow_factor = sample_shadow_dir(in.world_pos, sslot, shadow,
                                               dir_shadow_map, shadow_cmp_sampler);
        }

        var contrib = (kD * diffuse_color / PI + spec) * light_color * NdotL * intensity * attenuation * shadow_factor;

        // Toon quantization
        if (material.shading_mode > 0.5) {
            let levels = max(material.toon_levels, 1.0);
            let lum = dot(contrib, vec3f(0.2126, 0.7152, 0.0722));
            let q = floor(lum * levels + 0.5) / levels;
            if (lum > 0.001) {
                contrib = contrib * (q / lum);
            }
        }

        color += contrib;
    }

    // Emission
    color += emit_color;

    color = apply_fog(color, in.world_pos);
    return vec4f(color, alpha);
}
)";

// =============================================================================
// IBL WGSL preamble — bind group 2 declarations (Phase 6f)
// =============================================================================

static const char* kIBLBindingsWGSL = R"(
@group(2) @binding(0) var ibl_sampler: sampler;
@group(2) @binding(1) var irradiance_map: texture_cube<f32>;
@group(2) @binding(2) var prefiltered_map: texture_cube<f32>;
@group(2) @binding(3) var brdf_lut: texture_2d<f32>;
struct IBLParams { intensity: f32, has_environment: f32, _pad0: f32, _pad1: f32 }
@group(2) @binding(4) var<uniform> ibl: IBLParams;
)";

// =============================================================================
// Non-textured Blinn-Phong + IBL shader (Phase 6f)
// =============================================================================

static const char* kRender3DFragmentIBL = R"(
struct Material {
    color: vec4f,
    roughness: f32,
    metallic: f32,
    emission: f32,
    flags: f32,
    shading_mode: f32,
    toon_levels: f32,
    _pad0: f32,
    _pad1: f32,
    fog_enabled: f32,
    fog_mode: f32, // 0=Linear, 1=Exp2
    fog_near: f32,
    fog_far: f32,
    fog_color: vec3f,
    fog_density: f32,
}

@group(0) @binding(0) var<uniform> camera: Camera3D;
@group(0) @binding(1) var<uniform> material: Material;
@group(0) @binding(2) var<uniform> lighting: LightsUniform;
@group(0) @binding(3) var<uniform> shadow: ShadowData;
@group(0) @binding(4) var shadow_cmp_sampler: sampler_comparison;
@group(0) @binding(5) var dir_shadow_map: texture_depth_2d_array;

fn apply_fog(color: vec3f, world_pos: vec3f) -> vec3f {
    if (material.fog_enabled < 0.5) {
        return color;
    }
    let dist = distance(camera.camera_pos, world_pos);
    var fog_factor = 0.0;
    if (material.fog_mode < 0.5) {
        let denom = max(material.fog_far - material.fog_near, 0.0001);
        fog_factor = saturate((dist - material.fog_near) / denom);
    } else {
        let x = dist * max(material.fog_density, 0.0);
        fog_factor = 1.0 - exp(-(x * x));
    }
    return mix(color, material.fog_color, saturate(fog_factor));
}

@vertex
fn vs_main(@location(0) pos: vec3f,
           @location(1) normal: vec3f,
           @location(2) tangent: vec4f,
           @location(3) uv: vec2f) -> Vertex3DOutput {
    return transform3d(camera, pos, normal, tangent, uv);
}

fn blinn_phong(N: vec3f, V: vec3f, light: Light, world_pos: vec3f) -> vec2f {
    var L: vec3f;
    var attenuation: f32 = 1.0;

    if (light.position_and_type.w < 0.5) {
        L = normalize(light.direction_and_intensity.xyz);
    } else if (light.position_and_type.w > 1.5) {
        let to_light = light.position_and_type.xyz - world_pos;
        let dist = length(to_light);
        L = to_light / max(dist, 0.001);
        let radius = light.color_and_radius.w;
        let ratio = dist / max(radius, 0.001);
        attenuation = saturate(1.0 - ratio * ratio);
        let cos_angle = dot(-L, normalize(light.direction_and_intensity.xyz));
        let cos_outer = light.spot_params.x;
        let cos_inner = light.spot_params.y;
        attenuation *= saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001));
    } else {
        let to_light = light.position_and_type.xyz - world_pos;
        let dist = length(to_light);
        L = to_light / max(dist, 0.001);
        let radius = light.color_and_radius.w;
        let ratio = dist / max(radius, 0.001);
        attenuation = saturate(1.0 - ratio * ratio);
    }

    let H = normalize(L + V);
    let intensity = light.direction_and_intensity.w;

    let shininess = pow(2.0, (1.0 - material.roughness) * 7.0) + 2.0;
    let diffuse  = max(dot(N, L), 0.0);
    let specular = pow(max(dot(N, H), 0.0), shininess);

    // Toon quantization
    var d = diffuse;
    var s = specular;
    if (material.shading_mode > 0.5) {
        let levels = max(material.toon_levels, 1.0);
        d = floor(d * levels + 0.5) / levels;
        s = step(0.5, s);
    }

    return vec2f(d, s) * intensity * attenuation;
}

@fragment
fn fs_main(in: Vertex3DOutput) -> @location(0) vec4f {
    let base_color = material.color.rgb;
    let alpha = material.color.a;

    // Unlit early-out
    if (material.flags > 0.5) {
        let unlit_color = apply_fog(base_color * (1.0 + material.emission), in.world_pos);
        return vec4f(unlit_color, alpha);
    }

    let N = normalize(in.normal);
    let V = normalize(camera.camera_pos - in.world_pos);
    let NdotV = max(dot(N, V), 0.001);
    let ambient = vec3f(lighting.ambient_r, lighting.ambient_g, lighting.ambient_b);

    let diffuse_color  = base_color * (1.0 - material.metallic);
    let specular_color = mix(vec3f(0.04), base_color, material.metallic);

    var color: vec3f;
    if (ibl.has_environment > 0.5) {
        let irradiance = textureSample(irradiance_map, ibl_sampler, N).rgb;
        let ambient_diffuse = irradiance * diffuse_color * ibl.intensity;
        let R = reflect(-V, N);
        let prefiltered = textureSampleLevel(prefiltered_map, ibl_sampler, R, material.roughness * 4.0).rgb;
        let brdf = textureSample(brdf_lut, ibl_sampler, vec2f(NdotV, material.roughness)).rg;
        let ambient_specular = prefiltered * (specular_color * brdf.x + brdf.y) * ibl.intensity;
        color = ambient_diffuse + ambient_specular;
    } else {
        color = diffuse_color * ambient;
    }

    for (var i: u32 = 0u; i < min(lighting.light_count, 4u); i++) {
        let light = lighting.lights[i];
        let light_color = light.color_and_radius.xyz;
        let bp = blinn_phong(N, V, light, in.world_pos);

        // ADR-0051 P2: a light casts from its OWN array layer, or not at all (-1).
        var shadow_factor: f32 = 1.0;
        let sslot = shadow_slot_for(i, shadow);
        if (sslot >= 0) {
            shadow_factor = sample_shadow_dir(in.world_pos, sslot, shadow,
                                               dir_shadow_map, shadow_cmp_sampler);
        }

        color += light_color * (diffuse_color * bp.x + specular_color * bp.y) * shadow_factor;
    }

    // Emission
    color += base_color * material.emission;

    color = apply_fog(color, in.world_pos);
    return vec4f(color, alpha);
}
)";

// =============================================================================
// Textured PBR + IBL shader (Phase 6f)
// =============================================================================

static const char* kRender3DTexturedIBL = R"(
struct Material {
    color: vec4f,
    roughness: f32,
    metallic: f32,
    emission: f32,
    flags: f32,
    shading_mode: f32,
    toon_levels: f32,
    _pad0: f32,
    _pad1: f32,
    fog_enabled: f32,
    fog_mode: f32, // 0=Linear, 1=Exp2
    fog_near: f32,
    fog_far: f32,
    fog_color: vec3f,
    fog_density: f32,
}

@group(0) @binding(0) var<uniform> camera: Camera3D;
@group(0) @binding(1) var<uniform> material: Material;
@group(0) @binding(2) var<uniform> lighting: LightsUniform;
@group(0) @binding(3) var<uniform> shadow: ShadowData;
@group(0) @binding(4) var shadow_cmp_sampler: sampler_comparison;
@group(0) @binding(5) var dir_shadow_map: texture_depth_2d_array;

@group(1) @binding(0) var pbr_sampler: sampler;
@group(1) @binding(1) var albedo_map: texture_2d<f32>;
@group(1) @binding(2) var normal_map: texture_2d<f32>;
@group(1) @binding(3) var roughness_metallic_map: texture_2d<f32>;
@group(1) @binding(4) var emission_map: texture_2d<f32>;

fn apply_fog(color: vec3f, world_pos: vec3f) -> vec3f {
    if (material.fog_enabled < 0.5) {
        return color;
    }
    let dist = distance(camera.camera_pos, world_pos);
    var fog_factor = 0.0;
    if (material.fog_mode < 0.5) {
        let denom = max(material.fog_far - material.fog_near, 0.0001);
        fog_factor = saturate((dist - material.fog_near) / denom);
    } else {
        let x = dist * max(material.fog_density, 0.0);
        fog_factor = 1.0 - exp(-(x * x));
    }
    return mix(color, material.fog_color, saturate(fog_factor));
}

@vertex
fn vs_textured(@location(0) pos: vec3f,
               @location(1) normal: vec3f,
               @location(2) tangent: vec4f,
               @location(3) uv: vec2f) -> Vertex3DOutput {
    return transform3d(camera, pos, normal, tangent, uv);
}

@fragment
fn fs_textured(in: Vertex3DOutput) -> @location(0) vec4f {
    // Sample textures
    let albedo_tex = textureSample(albedo_map, pbr_sampler, in.uv);
    let normal_tex = textureSample(normal_map, pbr_sampler, in.uv);
    let rm_tex     = textureSample(roughness_metallic_map, pbr_sampler, in.uv);
    let emit_tex   = textureSample(emission_map, pbr_sampler, in.uv);

    // Material params modulated by textures
    let base_color = material.color.rgb * albedo_tex.rgb;
    // Treat PBR textured surfaces as opaque by default; many albedo maps
    // are authored without meaningful alpha and can cause unintended see-through.
    let alpha      = material.color.a;
    let roughness  = material.roughness * rm_tex.r;
    let metallic   = material.metallic + rm_tex.g;
    let emit_color = base_color * material.emission * max(emit_tex.r, max(emit_tex.g, emit_tex.b));

    // Unlit early-out
    if (material.flags > 0.5) {
        let unlit_color = apply_fog(base_color + emit_color, in.world_pos);
        return vec4f(unlit_color, alpha);
    }

    // Normal mapping (TBN)
    let geom_N = normalize(in.normal);
    let T = normalize(in.tangent.xyz);
    let B = cross(geom_N, T) * in.tangent.w;
    let TBN = mat3x3f(T, B, geom_N);
    let map_n = normal_tex.rgb * 2.0 - 1.0;
    let N = normalize(TBN * map_n);

    let V = normalize(camera.camera_pos - in.world_pos);
    let NdotV = max(dot(N, V), 0.001);
    let ambient = vec3f(lighting.ambient_r, lighting.ambient_g, lighting.ambient_b);

    let F0 = mix(vec3f(0.04), base_color, metallic);
    let diffuse_color = base_color * (1.0 - metallic);

    var color: vec3f;
    if (ibl.has_environment > 0.5) {
        let irradiance = textureSample(irradiance_map, ibl_sampler, N).rgb;
        let ambient_diffuse = irradiance * diffuse_color * ibl.intensity;
        let R = reflect(-V, N);
        let prefiltered = textureSampleLevel(prefiltered_map, ibl_sampler, R, roughness * 4.0).rgb;
        let brdf = textureSample(brdf_lut, ibl_sampler, vec2f(NdotV, roughness)).rg;
        let ambient_specular = prefiltered * (F0 * brdf.x + brdf.y) * ibl.intensity;
        color = ambient_diffuse + ambient_specular;
    } else {
        color = diffuse_color * ambient;
    }

    for (var i: u32 = 0u; i < min(lighting.light_count, 4u); i++) {
        let light = lighting.lights[i];
        let light_color = light.color_and_radius.xyz;
        let intensity = light.direction_and_intensity.w;

        var L: vec3f;
        var attenuation: f32 = 1.0;
        if (light.position_and_type.w < 0.5) {
            L = normalize(light.direction_and_intensity.xyz);
        } else if (light.position_and_type.w > 1.5) {
            let to_light = light.position_and_type.xyz - in.world_pos;
            let dist = length(to_light);
            L = to_light / max(dist, 0.001);
            let radius = light.color_and_radius.w;
            let ratio = dist / max(radius, 0.001);
            attenuation = saturate(1.0 - ratio * ratio);
            let cos_angle = dot(-L, normalize(light.direction_and_intensity.xyz));
            let cos_outer = light.spot_params.x;
            let cos_inner = light.spot_params.y;
            attenuation *= saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 0.001));
        } else {
            let to_light = light.position_and_type.xyz - in.world_pos;
            let dist = length(to_light);
            L = to_light / max(dist, 0.001);
            let radius = light.color_and_radius.w;
            let ratio = dist / max(radius, 0.001);
            attenuation = saturate(1.0 - ratio * ratio);
        }

        let H = normalize(L + V);
        let NdotL = max(dot(N, L), 0.0);
        let NdotH = max(dot(N, H), 0.0);
        let HdotV = max(dot(H, V), 0.0);

        // Cook-Torrance specular BRDF
        let D = D_GGX(NdotH, roughness);
        let G = G_Smith(NdotV, NdotL, roughness);
        let F = F_Schlick(HdotV, F0);
        let spec = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

        let kD = (1.0 - F) * (1.0 - metallic);

        // ADR-0051 P2: a light casts from its OWN array layer, or not at all (-1).
        var shadow_factor: f32 = 1.0;
        let sslot = shadow_slot_for(i, shadow);
        if (sslot >= 0) {
            shadow_factor = sample_shadow_dir(in.world_pos, sslot, shadow,
                                               dir_shadow_map, shadow_cmp_sampler);
        }

        var contrib = (kD * diffuse_color / PI + spec) * light_color * NdotL * intensity * attenuation * shadow_factor;

        // Toon quantization
        if (material.shading_mode > 0.5) {
            let levels = max(material.toon_levels, 1.0);
            let lum = dot(contrib, vec3f(0.2126, 0.7152, 0.0722));
            let q = floor(lum * levels + 0.5) / levels;
            if (lum > 0.001) {
                contrib = contrib * (q / lum);
            }
        }

        color += contrib;
    }

    // Emission
    color += emit_color;

    color = apply_fog(color, in.world_pos);
    return vec4f(color, alpha);
}
)";

// =============================================================================
// Uniform struct layouts
// =============================================================================

// Phase 6f: IBL params uniform
struct IBLUniform {
    float intensity;       // 4 bytes
    float has_environment;  // 4 bytes
    float _pad[2];         // 8 bytes → 16 total
};
static_assert(sizeof(IBLUniform) == 16, "IBLUniform must be 16 bytes");

struct CameraUniform {
    float mvp[16];
    float model[16];
    float normal_mat[16];
    float camera_pos[3];
    float _pad;
};
static_assert(sizeof(CameraUniform) == 208, "Camera3D uniform must be 208 bytes");

struct MaterialUniform {
    float color[4];       // 16 bytes
    float roughness;      //  4 bytes
    float metallic;       //  4 bytes
    float emission;       //  4 bytes
    float flags;          //  4 bytes — >0.5 = unlit
    float shading_mode;   //  4 bytes — 0=default, 1=toon
    float toon_levels;    //  4 bytes — quantization bands
    float _pad[2];        //  8 bytes
    float fog_enabled;    //  4 bytes
    float fog_mode;       //  4 bytes — 0=Linear, 1=Exp2
    float fog_near;       //  4 bytes
    float fog_far;        //  4 bytes
    float fog_color[3];   // 12 bytes
    float fog_density;    //  4 bytes
};
static_assert(sizeof(MaterialUniform) == 80, "MaterialUniform must be 80 bytes");

// ADR-0051 Phase 3: LightData / LightsUniform are the CANONICAL pair in gpu_3d.h, next to the
// LIGHTS_3D_WGSL preamble they mirror. Aliased here so the rest of this file reads unchanged.
using LightData     = vivid::gpu::LightData;
using LightsUniform = vivid::gpu::LightsUniform;

struct ShadowUniform {
    float light_vp[4][16];      // 4 casters × 64 bytes = 256 bytes
    float shadow_bias;           // 4 bytes
    uint32_t shadow_count_dir;   // 4 bytes — number of occupied caster slots
    float _pad[2];               // 8 bytes → 272
    int32_t shadow_slot[4];      // 16 bytes — light index -> caster slot, -1 = casts none
};
static_assert(sizeof(ShadowUniform) == 288, "ShadowUniform must be 288 bytes");

// =============================================================================
// Shadow caster shader (vertex-only, depth-only pass)
// =============================================================================

static const char* kShadowCasterShader = R"(
struct Camera3D {
    mvp:            mat4x4f,
    model:          mat4x4f,
    normal_matrix:  mat4x4f,
    camera_pos:     vec3f,
    _pad:           f32,
}

@group(0) @binding(0) var<uniform> camera: Camera3D;

@vertex
fn vs_shadow(@location(0) pos: vec3f,
             @location(1) normal: vec3f,
             @location(2) tangent: vec4f,
             @location(3) uv: vec2f) -> @builtin(position) vec4f {
    return camera.mvp * vec4f(pos, 1.0);
}

@fragment
fn fs_shadow() -> @location(0) vec4f {
    return vec4f(0.0, 0.0, 0.0, 1.0);
}
)";

// =============================================================================
// Multi-draw constants
// =============================================================================

static constexpr uint32_t kMaxDrawSlots       = 128;
static constexpr uint32_t kMaxLights          = 4;
// ADR-0051 Phase 2: shadow-casting lights each own a layer of the depth array and a block of
// shadow-camera UBO slots. Equal to kMaxLights today (every light may cast), but named separately
// because the two ceilings are independent — raising kMaxLights should not multiply shadow memory.
static constexpr uint32_t kMaxShadowCasters   = 4;
static constexpr uint64_t kCameraSlotStride   = 256;  // 256-byte aligned for dynamic offsets
static constexpr uint64_t kMaterialSlotStride = 256;

// =============================================================================
// Tree walk data structures
// =============================================================================

struct DrawCall {
    const vivid::gpu::VividSceneFragment* frag;
    mat4x4 composed_model;
    WGPUBuffer instance_buffer;  // nullptr for non-instanced
    uint32_t   instance_count;   // 0 or 1 for non-instanced
    const vivid::gpu::VividSceneFragment* material_override;  // Phase 6c
};

struct CollectedLight {
    float position[3];
    float light_type;
    float direction[3];
    float intensity;
    float color[3];
    float radius;
    float spot_angle_cos;   // cos(outer cone angle)
    float spot_inner_cos;   // cos(inner cone angle)
    bool  cast_shadow;      // ADR-0051 P2: does this light cast at all
};

// ADR-0051 Phase 3: ambient is a LIGHT you place in the scene, not a constant buried in the
// renderer. Ambient lights don't occupy one of the four shading slots and never cast — they just
// sum. `any` distinguishes "the author set the ambient to black" from "the author never said",
// which is what keeps the old hardcoded grey working as a fallback and only as a fallback.
struct CollectedAmbient {
    bool  any = false;
    float rgb[3] = {0.f, 0.f, 0.f};
};

// The fill Render3D uses when a scene places no Ambient light. Matches what was hardcoded here (and
// in SDF3D) before ADR-0051, so an existing scene is unchanged.
static constexpr float kDefaultAmbient = 0.15f;

// ADR-0051 Phase 1: a light's aim, in world space — `light_direction` rotated by the upper-3x3 of
// its composed transform (so parenting a light under a Transform3D swings its beam) and normalized.
// A degenerate aim — an all-zero direction, or a scale-0 parent that collapses the rotation — falls
// back to straight down instead of propagating normalize(0) = NaN into the shader, where it would
// silently poison `max(dot(N, L), 0)` and drop the light to ambient-only.
static void aim_from_fragment(const vivid::gpu::VividSceneFragment* node,
                              const mat4x4 composed, float out_aim[3]) {
    const float dx = node->light_direction[0];
    const float dy = node->light_direction[1];
    const float dz = node->light_direction[2];
    float rx = composed[0][0]*dx + composed[1][0]*dy + composed[2][0]*dz;
    float ry = composed[0][1]*dx + composed[1][1]*dy + composed[2][1]*dz;
    float rz = composed[0][2]*dx + composed[1][2]*dy + composed[2][2]*dz;
    const float len = std::sqrt(rx*rx + ry*ry + rz*rz);
    if (len > 1e-8f) {
        rx /= len; ry /= len; rz /= len;
    } else {
        rx = 0.0f; ry = -1.0f; rz = 0.0f;   // canonical fallback: shining straight down
    }
    out_aim[0] = rx; out_aim[1] = ry; out_aim[2] = rz;
}

static void collect_fragments(const vivid::gpu::VividSceneFragment* node,
                               const mat4x4 parent_transform,
                               std::vector<DrawCall>& draws,
                               std::vector<CollectedLight>& lights,
                               CollectedAmbient& ambient,
                               uint32_t& lights_seen,
                               const vivid::gpu::VividSceneFragment* material = nullptr,
                               const vivid::gpu::VividSceneFragment** out_env = nullptr) {
    if (!node) return;

    mat4x4 composed;
    mat4x4_mul(composed, parent_transform, node->model_matrix);

    // Phase 6c: material override inheritance
    const vivid::gpu::VividSceneFragment* active_material = material;
    if (node->material_texture_binds) {
        active_material = node;
    }

    if (node->fragment_type == vivid::gpu::VividSceneFragment::LIGHT
        && node->light_type > 2.5f) {
        // ADR-0051 P3: Ambient — sums into the scene fill, costs no shading slot.
        ambient.any = true;
        for (int c = 0; c < 3; ++c)
            ambient.rgb[c] += node->light_color[c] * node->light_intensity;
    } else if (node->fragment_type == vivid::gpu::VividSceneFragment::LIGHT) {
        ++lights_seen;   // ADR-0051 P4: count them ALL, so we can say how many were dropped
        if (lights.size() < kMaxLights) {
            CollectedLight cl{};
            cl.light_type = node->light_type;
            cl.intensity  = node->light_intensity;
            cl.color[0]   = node->light_color[0];
            cl.color[1]   = node->light_color[1];
            cl.color[2]   = node->light_color[2];
            cl.radius     = node->light_radius;
            cl.cast_shadow = node->light_cast_shadow;

            // ADR-0051 Phase 1: ONE rule for aim — `light_direction` is the direction the light
            // SHINES (away from it), for every light type that has an aim. The uniform carries the
            // vector pointing TOWARD the light (what every shader's `L` expects), so directional
            // negates the aim; spot keeps it (its cone test dots against -L).
            float aim[3];
            aim_from_fragment(node, composed, aim);

            if (node->light_type < 0.5f) {
                // Directional: no position — only the aim matters. Toward-light = -aim.
                cl.direction[0] = -aim[0];
                cl.direction[1] = -aim[1];
                cl.direction[2] = -aim[2];
            } else if (node->light_type > 1.5f) {
                // Spot: position from the translation column, cone aimed along `aim`.
                cl.position[0] = composed[3][0];
                cl.position[1] = composed[3][1];
                cl.position[2] = composed[3][2];
                cl.direction[0] = aim[0];
                cl.direction[1] = aim[1];
                cl.direction[2] = aim[2];
                float outer_deg = node->light_spot_angle;
                float blend = node->light_spot_blend;
                float outer_rad = outer_deg * 3.14159265358979f / 180.0f;
                float inner_rad = outer_rad * (1.0f - blend);
                cl.spot_angle_cos = std::cos(outer_rad);
                cl.spot_inner_cos = std::cos(inner_rad);
            } else {
                // Point: position from translation column
                cl.position[0] = composed[3][0];
                cl.position[1] = composed[3][1];
                cl.position[2] = composed[3][2];
            }
            lights.push_back(cl);
        }
    } else if (node->fragment_type == vivid::gpu::VividSceneFragment::ENVIRONMENT) {
        // Phase 6f: last environment wins
        if (out_env) *out_env = node;
    } else if (node->vertex_buffer && node->index_count > 0) {
        if (draws.size() < kMaxDrawSlots) {
            DrawCall dc{};
            dc.frag = node;
            std::memcpy(dc.composed_model, composed, sizeof(mat4x4));
            dc.instance_buffer = node->instance_buffer;
            dc.instance_count  = node->instance_count;
            dc.material_override = active_material;
            draws.push_back(dc);
        }
    }

    for (uint32_t i = 0; i < node->child_count; ++i) {
        collect_fragments(node->children[i], composed, draws, lights, ambient, lights_seen,
                          active_material, out_env);
    }
}

// =============================================================================
// Render3D Operator
// =============================================================================

/**
 * @brief Renders a composed 3D scene into a texture with camera, lighting, and post controls.
 *
 * Render3D is the main scene renderer for vivid-3d. It consumes scene data, applies camera and
 * environment settings, and produces the final 3D image plus depth-aware effects like fog.
 *
 * @param cam_x Camera X position.
 * @param cam_y Camera Y position.
 * @param cam_z Camera Z position.
 * @param fov Camera field of view.
 * @param shadow_enabled Enables shadow rendering.
 * @param fog_enabled Enables fog in the final scene render.
 */
struct Render3D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Render3D";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_RENDERER;   // ADR-0046
    static constexpr const char* kSummary = "Renders a composed 3D scene to a texture: camera, lights, shadows, fog and IBL.";
    static constexpr std::array<const char*, 3> kKeywords = {"3d", "render", "camera"};
    static constexpr bool kTimeDependent = false;

    // Camera params
    vivid::Param<float> cam_x    {"cam_x",     0.0f,  -50.0f, 50.0f};
    vivid::Param<float> cam_y    {"cam_y",     2.0f,  -50.0f, 50.0f};
    vivid::Param<float> cam_z    {"cam_z",     5.0f,  -50.0f, 50.0f};
    vivid::Param<float> target_x {"target_x",  0.0f,  -50.0f, 50.0f};
    vivid::Param<float> target_y {"target_y",  0.0f,  -50.0f, 50.0f};
    vivid::Param<float> target_z {"target_z",  0.0f,  -50.0f, 50.0f};
    vivid::Param<float> fov      {"fov",      60.0f,    1.0f, 170.0f};
    vivid::Param<float> near_p   {"near",      0.1f,   0.001f, 10.0f};
    vivid::Param<float> far_p    {"far",     100.0f,    1.0f, 10000.0f};

    // Auto-orbit (Phase 3 OrbitCamera helper): when orbit>0.5, the eye is placed on a circle around
    // the target instead of using cam_*. Gives sin/cos orbital motion a single linear mapping can't —
    // wire transport.bar_phase -> orbit_phase for a bar-synced camera orbit + parallax (legibility).
    vivid::Param<float> orbit        {"orbit",        0.0f, 0.0f, 1.0f};
    vivid::Param<float> orbit_phase  {"orbit_phase",  0.0f, 0.0f, 1.0f};   // 0..1 -> 0..2pi
    vivid::Param<float> orbit_radius {"orbit_radius", 6.0f, 0.5f, 50.0f};
    vivid::Param<float> orbit_height {"orbit_height", 2.0f, -50.0f, 50.0f};

    // Camera eye: orbital override when enabled, else the raw cam_* params. Shared by every view build.
    void compute_eye(float out[3]) const {
        if (orbit.value > 0.5f) {
            const float a = orbit_phase.value * 6.28318530718f;
            out[0] = target_x.value + std::cos(a) * orbit_radius.value;
            out[1] = orbit_height.value;
            out[2] = target_z.value + std::sin(a) * orbit_radius.value;
        } else {
            out[0] = cam_x.value; out[1] = cam_y.value; out[2] = cam_z.value;
        }
    }

    // Background params
    vivid::Param<float> bg_r     {"bg_r",      0.0f,   0.0f, 1.0f};
    vivid::Param<float> bg_g     {"bg_g",      0.0f,   0.0f, 1.0f};
    vivid::Param<float> bg_b     {"bg_b",      0.0f,   0.0f, 1.0f};
    vivid::Param<float> bg_a     {"bg_a",      1.0f,   0.0f, 1.0f};

    // Shadow params (Phase 6d)
    vivid::Param<float> shadow_enabled    {"shadow_enabled",    1.0f, 0.0f, 1.0f};
    vivid::Param<float> shadow_resolution {"shadow_resolution", 1024.0f, 256.0f, 4096.0f};
    vivid::Param<float> shadow_bias       {"shadow_bias",       0.005f, 0.0f, 0.05f};

    // Fog params (opt-in)
    vivid::Param<float> fog_enabled {"fog_enabled", 0.0f, 0.0f, 1.0f};
    vivid::Param<int>   fog_mode    {"fog_mode",    0, {"Linear", "Exp2"}};
    vivid::Param<float> fog_color_r {"fog_color_r", 0.12f, 0.0f, 1.0f};
    vivid::Param<float> fog_color_g {"fog_color_g", 0.14f, 0.0f, 1.0f};
    vivid::Param<float> fog_color_b {"fog_color_b", 0.18f, 0.0f, 1.0f};
    vivid::Param<float> fog_near    {"fog_near",    4.0f, 0.0f, 1000.0f};
    vivid::Param<float> fog_far     {"fog_far",     30.0f, 0.01f, 2000.0f};
    vivid::Param<float> fog_density {"fog_density", 0.04f, 0.0f, 2.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(cam_x, "Camera");
        vivid::param_group(cam_y, "Camera");
        vivid::param_group(cam_z, "Camera");
        vivid::param_group(target_x, "Camera");
        vivid::param_group(target_y, "Camera");
        vivid::param_group(target_z, "Camera");
        vivid::param_group(fov, "Camera");
        vivid::param_group(near_p, "Camera");
        vivid::param_group(far_p, "Camera");
        vivid::param_group(orbit, "Camera");
        vivid::param_group(orbit_phase, "Camera");
        vivid::param_group(orbit_radius, "Camera");
        vivid::param_group(orbit_height, "Camera");

        vivid::param_group(bg_r, "Background");
        vivid::param_group(bg_g, "Background");
        vivid::param_group(bg_b, "Background");
        vivid::param_group(bg_a, "Background");
        vivid::display_hint(bg_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(bg_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(bg_b, VIVID_DISPLAY_COLOR);

        vivid::param_group(shadow_enabled, "Shadows");
        vivid::param_group(shadow_resolution, "Shadows");
        vivid::param_group(shadow_bias, "Shadows");

        vivid::param_group(fog_enabled, "Fog");
        vivid::param_group(fog_mode, "Fog");
        vivid::param_group(fog_color_r, "Fog");
        vivid::param_group(fog_color_g, "Fog");
        vivid::param_group(fog_color_b, "Fog");
        vivid::param_group(fog_near, "Fog");
        vivid::param_group(fog_far, "Fog");
        vivid::param_group(fog_density, "Fog");
        vivid::display_hint(fog_color_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(fog_color_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(fog_color_b, VIVID_DISPLAY_COLOR);

        out.push_back(&cam_x);
        out.push_back(&cam_y);
        out.push_back(&cam_z);
        out.push_back(&target_x);
        out.push_back(&target_y);
        out.push_back(&target_z);
        out.push_back(&fov);
        out.push_back(&near_p);
        out.push_back(&far_p);
        out.push_back(&orbit);
        out.push_back(&orbit_phase);
        out.push_back(&orbit_radius);
        out.push_back(&orbit_height);
        out.push_back(&bg_r);
        out.push_back(&bg_g);
        out.push_back(&bg_b);
        out.push_back(&bg_a);
        out.push_back(&shadow_enabled);
        out.push_back(&shadow_resolution);
        out.push_back(&shadow_bias);
        out.push_back(&fog_enabled);
        out.push_back(&fog_mode);
        out.push_back(&fog_color_r);
        out.push_back(&fog_color_g);
        out.push_back(&fog_color_b);
        out.push_back(&fog_near);
        out.push_back(&fog_far);
        out.push_back(&fog_density);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_INPUT));
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
        out.push_back({"depth",   VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (cached_format_ == WGPUTextureFormat_Undefined) {
            if (!lazy_init(ctx)) return;
        }

        // Invalidate pipeline cache if output format changed
        if (ctx->output_format != cached_format_) {
            for (auto& [f, p] : pipeline_cache_) vivid::gpu::release(p);
            pipeline_cache_.clear();
            cached_format_ = ctx->output_format;
        }

        uint32_t w = ctx->output_width;
        uint32_t h = ctx->output_height;

        // Recreate depth buffer if output size changed
        if (w != cached_w_ || h != cached_h_) {
            vivid::gpu::release(depth_view_);
            vivid::gpu::release(depth_tex_);
            // Phase 6e: use shadow_map variant for TextureBinding usage (needed for depth blit)
            depth_tex_ = vivid::gpu::create_shadow_map_texture(ctx->device, w, h, "Render3D Depth");
            depth_view_ = vivid::gpu::create_depth_view(depth_tex_, "Render3D Depth View");

            // Phase 6e: R32Float depth output texture
            vivid::gpu::release(depth_out_view_);
            vivid::gpu::release(depth_out_tex_);
            {
                WGPUTextureDescriptor td{};
                td.label = vivid_sv("Render3D Depth Out R32F");
                td.size = { w, h, 1 };
                td.mipLevelCount = 1;
                td.sampleCount = 1;
                td.dimension = WGPUTextureDimension_2D;
                td.format = WGPUTextureFormat_R32Float;
                td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
                         | WGPUTextureUsage_CopySrc;
                depth_out_tex_ = wgpuDeviceCreateTexture(ctx->device, &td);

                WGPUTextureViewDescriptor vd{};
                vd.label = vivid_sv("Render3D Depth Out R32F View");
                vd.format = WGPUTextureFormat_R32Float;
                vd.dimension = WGPUTextureViewDimension_2D;
                vd.mipLevelCount = 1;
                vd.arrayLayerCount = 1;
                depth_out_view_ = wgpuTextureCreateView(depth_out_tex_, &vd);
            }
            depth_blit_bg_dirty_ = true;

            cached_w_ = w;
            cached_h_ = h;
        }

        // Build camera matrices
        float eye[3];   compute_eye(eye);
        float target[3] = { target_x.value, target_y.value, target_z.value };
        float up[3]     = { 0.0f, 1.0f, 0.0f };

        mat4x4 view, proj, vp;
        mat4x4_look_at(view, eye, target, up);

        float fov_rad = fov.value * (3.14159265358979323846f / 180.0f);
        float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
        vivid::gpu::perspective_wgpu(proj, fov_rad, aspect, near_p.value, far_p.value);
        mat4x4_mul(vp, proj, view);

        WGPUColor clear_color = { bg_r.value, bg_g.value, bg_b.value, bg_a.value };

        // Check if we have a scene connected
        bool has_scene = vivid::gpu::scene_input(ctx, 0) != nullptr;

        // Lambda to run depth blit and set output (shared by early returns + main path)
        auto blit_depth_output = [&]() {
            if (depth_blit_pipeline_ && depth_out_view_) {
                rebuild_depth_blit_bg_if_needed(ctx);
                if (depth_blit_bg_) {
                    vivid::gpu::run_pass(ctx->command_encoder, depth_blit_pipeline_,
                                         depth_blit_bg_, depth_out_view_,
                                         "Depth Blit", {0, 0, 0, 0});
                }
                if (ctx->aux_output_texture_count > 0)
                    ctx->aux_output_texture_views[0] = depth_out_view_;
            }
        };

        if (!has_scene) {
            WGPURenderPassEncoder pass = vivid::gpu::begin_3d_pass(
                ctx->command_encoder, ctx->output_texture_view, depth_view_,
                "Render3D Clear", clear_color);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            blit_depth_output();
            return;
        }

        // Tree walk: collect geometry, lights, and environment
        std::vector<DrawCall> draws;
        std::vector<CollectedLight> collected_lights;
        CollectedAmbient collected_ambient;
        const vivid::gpu::VividSceneFragment* env = nullptr;
        mat4x4 identity;
        mat4x4_identity(identity);
        uint32_t lights_seen = 0;
        collect_fragments(vivid::gpu::scene_input(ctx, 0), identity, draws, collected_lights,
                         collected_ambient, lights_seen, nullptr, &env);

        // ADR-0051 P4 / ADR-0019: lights past the ceiling used to vanish with no word to anyone —
        // you added a fifth Light3D, the picture didn't change, and nothing said why. Say it on the
        // node. `light_limit_msg_` owns the storage, as vivid_report_gpu_error requires.
        if (lights_seen > kMaxLights) {
            light_limit_msg_ = "scene has " + std::to_string(lights_seen) + " lights; only "
                             + std::to_string(kMaxLights) + " are shaded ("
                             + std::to_string(lights_seen - kMaxLights) + " ignored)";
            vivid_report_gpu_error(ctx, light_limit_msg_.c_str());
        }

        if (draws.empty()) {
            WGPURenderPassEncoder pass = vivid::gpu::begin_3d_pass(
                ctx->command_encoder, ctx->output_texture_view, depth_view_,
                "Render3D Clear", clear_color);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);

            // Phase 6f: still render skybox even with no geometry
            bool has_ibl = env && env->ibl_irradiance && env->ibl_prefiltered && env->ibl_brdf_lut;
            if (has_ibl && skybox_pipeline_ && env->ibl_prefiltered) {
                mat4x4 view, proj, vp;
                float eye_pos[3];   compute_eye(eye_pos);
                vec3 veye = {eye_pos[0], eye_pos[1], eye_pos[2]};
                vec3 vtgt = {target_x.value, target_y.value, target_z.value};
                vec3 vup  = {0.f, 1.f, 0.f};
                mat4x4_look_at(view, veye, vtgt, vup);
                float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
                vivid::gpu::perspective_wgpu(proj, fov.value * 3.14159265f / 180.f,
                                              aspect, near_p.value, far_p.value);
                mat4x4_mul(vp, proj, view);
                render_skybox(ctx, env, vp, eye_pos);
            }

            blit_depth_output();
            return;
        }

        // Default light fallback — matches the previous hardcoded directional light. Suppressed
        // when the scene placed an Ambient light (ADR-0051 P3): ambient-only is a deliberate flat
        // look, and conjuring a key light on top of it would override the author.
        if (collected_lights.empty() && !collected_ambient.any) {
            CollectedLight def{};
            float dir[3] = {0.5f, 1.0f, 0.8f};
            float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
            def.direction[0] = dir[0] / len;
            def.direction[1] = dir[1] / len;
            def.direction[2] = dir[2] / len;
            def.light_type = 0.0f;
            def.intensity  = 1.0f;
            def.color[0] = 1.0f; def.color[1] = 1.0f; def.color[2] = 1.0f;
            def.radius = 10.0f;
            def.cast_shadow = true;   // value-init would leave this false and silently kill the
                                      // shadow in every scene that never adds a Light3D
            collected_lights.push_back(def);
        }

        // Write lights uniform (shared across all draws)
        LightsUniform lights_data{};
        lights_data.light_count = static_cast<uint32_t>(collected_lights.size());
        // ADR-0051 P3: an Ambient Light3D drives the fill; the old constant is now only the
        // fallback for a scene that places none.
        for (int c = 0; c < 3; ++c)
            lights_data.ambient[c] = collected_ambient.any ? collected_ambient.rgb[c]
                                                           : kDefaultAmbient;
        for (uint32_t i = 0; i < lights_data.light_count && i < kMaxLights; ++i) {
            auto& cl = collected_lights[i];
            auto& ld = lights_data.lights[i];
            ld.position_and_type[0] = cl.position[0];
            ld.position_and_type[1] = cl.position[1];
            ld.position_and_type[2] = cl.position[2];
            ld.position_and_type[3] = cl.light_type;
            ld.direction_and_intensity[0] = cl.direction[0];
            ld.direction_and_intensity[1] = cl.direction[1];
            ld.direction_and_intensity[2] = cl.direction[2];
            ld.direction_and_intensity[3] = cl.intensity;
            ld.color_and_radius[0] = cl.color[0];
            ld.color_and_radius[1] = cl.color[1];
            ld.color_and_radius[2] = cl.color[2];
            ld.color_and_radius[3] = cl.radius;
            ld.spot_params[0] = cl.spot_angle_cos;
            ld.spot_params[1] = cl.spot_inner_cos;
            ld.spot_params[2] = 0.0f;
            ld.spot_params[3] = 0.0f;
        }
        wgpuQueueWriteBuffer(ctx->queue, lights_ubo_, 0, &lights_data, sizeof(lights_data));

        // ---- Phase 6d: Shadow pre-pass ----
        ShadowUniform shadow_data{};
        std::memset(&shadow_data, 0, sizeof(shadow_data));
        shadow_data.shadow_bias = shadow_bias.value;

        bool shadows_active = shadow_enabled.value > 0.5f && !collected_lights.empty();
        uint32_t dir_shadow_count = 0;
        for (auto& s : shadow_data.shadow_slot) s = -1;   // default: this light casts nothing

        if (shadows_active) {
            uint32_t sres = static_cast<uint32_t>(shadow_resolution.value);
            if (sres < 64) sres = 64;
            ensure_shadow_maps(ctx, sres);

            // ADR-0051 Phase 2: every light that HAS an aim can cast — directional (ortho) and
            // spot (perspective from the cone). Point lights still can't: an omni shadow needs six
            // cube faces and a distance compare (Phase 2c). Each caster gets its own array layer
            // AND its own block of shadow-camera UBO slots: all wgpuQueueWriteBuffer calls land
            // before any command in the frame's single submit, so sharing one block would leave
            // every pass rendering the LAST caster's matrices.
            for (uint32_t li = 0; li < lights_data.light_count && li < kMaxLights; ++li) {
                auto& cl = collected_lights[li];
                const bool is_point = (cl.light_type > 0.5f && cl.light_type < 1.5f);
                if (is_point || !cl.cast_shadow) continue;
                if (dir_shadow_count >= kMaxShadowCasters) break;

                const uint32_t slot = dir_shadow_count;
                mat4x4 light_vp;
                if (cl.light_type > 1.5f) {
                    compute_spot_light_vp(light_vp, cl);
                } else {
                    compute_directional_light_vp(light_vp, cl.direction, eye, target, fov_rad,
                                                  aspect, near_p.value, far_p.value, draws);
                }

                std::memcpy(shadow_data.light_vp[slot], light_vp, 64);
                shadow_data.shadow_slot[li] = static_cast<int32_t>(slot);

                // Write this caster's per-draw matrices into ITS OWN slot block.
                const uint64_t block = static_cast<uint64_t>(slot) * kMaxDrawSlots * kCameraSlotStride;
                for (uint32_t di = 0; di < static_cast<uint32_t>(draws.size()); ++di) {
                    auto& dc = draws[di];
                    mat4x4 shadow_mvp;
                    mat4x4_mul(shadow_mvp, light_vp, dc.composed_model);

                    CameraUniform shadow_cam{};
                    std::memcpy(shadow_cam.mvp, shadow_mvp, 64);
                    std::memcpy(shadow_cam.model, dc.composed_model, 64);
                    wgpuQueueWriteBuffer(ctx->queue, shadow_camera_ubo_,
                                         block + di * kCameraSlotStride,
                                         &shadow_cam, sizeof(shadow_cam));
                }

                render_shadow_pass(ctx, draws, dir_shadow_layer_views_[slot], sres, sres, slot,
                                   "Render3D Shadow Pass");

                dir_shadow_count++;
            }
        }

        shadow_data.shadow_count_dir = dir_shadow_count;
        wgpuQueueWriteBuffer(ctx->queue, shadow_ubo_, 0, &shadow_data, sizeof(shadow_data));

        // Phase 6f: IBL environment data
        bool has_ibl = env && env->ibl_irradiance && env->ibl_prefiltered && env->ibl_brdf_lut;
        {
            IBLUniform ibl_data{};
            ibl_data.intensity = has_ibl ? env->ibl_intensity : 0.0f;
            ibl_data.has_environment = has_ibl ? 1.0f : 0.0f;
            wgpuQueueWriteBuffer(ctx->queue, ibl_ubo_, 0, &ibl_data, sizeof(ibl_data));
        }
        if (has_ibl) rebuild_ibl_bind_group(ctx, env);

        // Write per-draw uniforms (camera UBO with real camera data for main pass)
        for (uint32_t i = 0; i < static_cast<uint32_t>(draws.size()); ++i) {
            auto& dc = draws[i];

            mat4x4 mvp;
            mat4x4_mul(mvp, vp, dc.composed_model);

            mat4x4 nm;
            vivid::gpu::normal_matrix(nm, dc.composed_model);

            CameraUniform cam{};
            std::memcpy(cam.mvp,        mvp,               16 * sizeof(float));
            std::memcpy(cam.model,      dc.composed_model, 16 * sizeof(float));
            std::memcpy(cam.normal_mat, nm,                16 * sizeof(float));
            cam.camera_pos[0] = eye[0];
            cam.camera_pos[1] = eye[1];
            cam.camera_pos[2] = eye[2];
            cam._pad = 0.0f;
            wgpuQueueWriteBuffer(ctx->queue, camera_ubo_,
                                 i * kCameraSlotStride, &cam, sizeof(cam));

            // Phase 6c: material override provides properties when present
            const auto* mat_src = dc.material_override ? dc.material_override : dc.frag;
            MaterialUniform mat{};
            std::memcpy(mat.color, mat_src->color, 4 * sizeof(float));
            mat.roughness = mat_src->roughness;
            mat.metallic  = mat_src->metallic;
            mat.emission  = mat_src->emission;
            mat.flags        = mat_src->unlit ? 1.0f : 0.0f;
            mat.shading_mode = mat_src->shading_mode;
            mat.toon_levels  = mat_src->toon_levels;
            mat.fog_enabled  = fog_enabled.value > 0.5f ? 1.0f : 0.0f;
            mat.fog_mode     = static_cast<float>(fog_mode.int_value());
            mat.fog_near     = fog_near.value;
            mat.fog_far      = fog_far.value;
            mat.fog_color[0] = fog_color_r.value;
            mat.fog_color[1] = fog_color_g.value;
            mat.fog_color[2] = fog_color_b.value;
            mat.fog_density  = fog_density.value;
            wgpuQueueWriteBuffer(ctx->queue, material_ubo_,
                                 i * kMaterialSlotStride, &mat, sizeof(mat));

            // Custom pipeline camera injection (SDF3D etc.)
            if (dc.frag->pipeline && dc.frag->custom_camera_ubo) {
                vivid::gpu::CustomCamera3D ccam{};
                mat4x4 inv_vp;
                mat4x4_invert(inv_vp, vp);
                std::memcpy(ccam.inverse_vp, inv_vp, 64);
                std::memcpy(ccam.vp, vp, 64);
                ccam.camera_pos[0] = eye[0];
                ccam.camera_pos[1] = eye[1];
                ccam.camera_pos[2] = eye[2];
                ccam.near_plane    = near_p.value;
                ccam.far_plane     = far_p.value;
                ccam.resolution[0] = static_cast<float>(w);
                ccam.resolution[1] = static_cast<float>(h);
                wgpuQueueWriteBuffer(ctx->queue, dc.frag->custom_camera_ubo,
                                     0, &ccam, sizeof(ccam));
            }

            // ADR-0051 P3: custom pipeline LIGHT injection, the same channel as the camera above.
            // SDF3D used to upload a hardcoded directional light of its own, so a Light3D changed
            // the mesh geometry in a scene and left the SDF geometry beside it untouched. This is
            // the byte-identical LightsUniform every other shading path already gets, so both are
            // lit by one rig. Ordered after SDF3D's own default write (operators run before
            // Render3D in graph order), so this wins.
            if (dc.frag->pipeline && dc.frag->custom_lights_ubo) {
                wgpuQueueWriteBuffer(ctx->queue, dc.frag->custom_lights_ubo,
                                     0, &lights_data, sizeof(lights_data));
            }
        }

        // Rotate the instanced bind-group pools: release the set retired last frame (its command buffer has
        // been submitted and consumed), then queue the previous frame's set for release next frame. The
        // one-frame delay guarantees a bind group outlives the pass that referenced it.
        for (auto bg : retire_inst_bgs_) vivid::gpu::release(bg);
        retire_inst_bgs_.clear();
        for (auto& kv : frame_inst_bgs_) retire_inst_bgs_.push_back(kv.second);
        frame_inst_bgs_.clear();

        // One render pass with multiple draws using dynamic offsets
        WGPURenderPassEncoder pass = vivid::gpu::begin_3d_pass(
            ctx->command_encoder, ctx->output_texture_view, depth_view_,
            "Render3D Pass", clear_color);

        for (uint32_t i = 0; i < static_cast<uint32_t>(draws.size()); ++i) {
            auto& dc = draws[i];

            // Custom pipeline override (SDF3D etc.)
            if (dc.frag->pipeline) {
                wgpuRenderPassEncoderSetPipeline(pass, dc.frag->pipeline);
                if (dc.frag->material_binds)
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, dc.frag->material_binds, 0, nullptr);
                // ADR-0060 P2: bind the scene IBL at group 1 for custom pipelines that declare it (SDF3D),
                // so SDF surfaces reflect the environment — the real env, or the black fallback otherwise.
                if (dc.frag->custom_ibl)
                    wgpuRenderPassEncoderSetBindGroup(pass, 1,
                        has_ibl ? ibl_bind_group_ : fallback_ibl_bg_, 0, nullptr);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, dc.frag->vertex_buffer,
                                                      0, dc.frag->vertex_buf_size);
                wgpuRenderPassEncoderSetIndexBuffer(pass, dc.frag->index_buffer,
                                                     WGPUIndexFormat_Uint32, 0,
                                                     static_cast<uint64_t>(dc.frag->index_count) * sizeof(uint32_t));
                wgpuRenderPassEncoderDrawIndexed(pass, dc.frag->index_count, 1, 0, 0, 0);
                continue;
            }

            // >= 1 (not > 1): a draw with a SINGLE instance must still go through the instanced path so its
            // per-instance transform (position/scale/colour from the instance buffer) is applied — otherwise
            // a one-instance Instancer3D/Particles3D draw renders at the base shape's origin, ignoring the
            // instance's position (e.g. a lone note-orb snapping to the centre).
            bool instanced = dc.instance_count >= 1 && dc.instance_buffer;
            bool is_billboard = instanced && dc.frag->billboard;

            uint32_t dynamic_offsets[2] = {
                static_cast<uint32_t>(i * kCameraSlotStride),
                static_cast<uint32_t>(i * kMaterialSlotStride),
            };

            // Phase 6c: use material override's pipeline_flags when available
            const auto* flag_src = dc.material_override ? dc.material_override : dc.frag;
            uint32_t flags = flag_src->pipeline_flags;
            if (is_billboard)        flags |= vivid::gpu::kPipelineInstanced | vivid::gpu::kPipelineBillboard;
            else if (instanced)      flags |= vivid::gpu::kPipelineInstanced;

            // Phase 6c: strip textured flag when combined with instanced (deferred)
            bool is_textured = (flags & vivid::gpu::kPipelineTextured) != 0;
            if (is_textured && instanced) {
                flags &= ~vivid::gpu::kPipelineTextured;
                is_textured = false;
            }

            // Phase 6f: add IBL flag for non-instanced, non-billboard draws
            if (has_ibl && !instanced && !is_billboard) {
                flags |= vivid::gpu::kPipelineIBL;
            }

            auto active = get_or_create_pipeline(ctx, flags);
            if (!active) continue;

            wgpuRenderPassEncoderSetPipeline(pass, active);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group_, 2, dynamic_offsets);

            if (instanced) {
                // Per-draw instanced bind group, retired a frame later (see rotation before the pass).
                // The instance storage buffer is reallocated whenever an instancer's count changes, so a
                // single cached bind group would (a) go stale as the buffer grows and (b) be released while
                // last frame's submitted pass may still reference it — dropping instances. Pooling per draw
                // and deferring the release fixes both, and lets multiple instanced draws share one pass.
                WGPUBindGroup ibg = frame_instanced_bind_group(ctx, dc.instance_buffer);
                wgpuRenderPassEncoderSetBindGroup(pass, 1, ibg, 0, nullptr);
            } else if (is_textured) {
                // Phase 6c: bind PBR textures at group 1
                const auto* tex_src = dc.material_override ? dc.material_override : dc.frag;
                WGPUBindGroup tex_bg = tex_src->material_texture_binds;
                if (!tex_bg) tex_bg = fallback_tex_bg_;
                wgpuRenderPassEncoderSetBindGroup(pass, 1, tex_bg, 0, nullptr);
            }

            // Phase 6f: bind IBL at group 2
            if (flags & vivid::gpu::kPipelineIBL) {
                // Non-textured IBL needs empty group 1 placeholder
                if (!is_textured && !instanced) {
                    wgpuRenderPassEncoderSetBindGroup(pass, 1, ibl_empty_bg_, 0, nullptr);
                }
                wgpuRenderPassEncoderSetBindGroup(pass, 2,
                    has_ibl ? ibl_bind_group_ : fallback_ibl_bg_, 0, nullptr);
            }

            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, dc.frag->vertex_buffer,
                                                  0, dc.frag->vertex_buf_size);
            wgpuRenderPassEncoderSetIndexBuffer(pass, dc.frag->index_buffer,
                                                 WGPUIndexFormat_Uint32, 0,
                                                 static_cast<uint64_t>(dc.frag->index_count) * sizeof(uint32_t));

            uint32_t inst_count = instanced ? dc.instance_count : 1;
            wgpuRenderPassEncoderDrawIndexed(pass, dc.frag->index_count, inst_count, 0, 0, 0);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Phase 6f: skybox pass (after geometry, before depth blit)
        if (has_ibl && skybox_pipeline_ && env->ibl_prefiltered) {
            render_skybox(ctx, env, vp, eye);
        }

        // Phase 6e: blit depth and set output
        blit_depth_output();
    }

    ~Render3D() override {
        for (auto& [f, p] : pipeline_cache_)
            vivid::gpu::release(p);
        vivid::gpu::release(bind_group_);
        for (auto& kv : frame_inst_bgs_)  vivid::gpu::release(kv.second);
        for (auto bg : retire_inst_bgs_)  vivid::gpu::release(bg);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(inst_bind_layout_);
        vivid::gpu::release(camera_ubo_);
        vivid::gpu::release(material_ubo_);
        vivid::gpu::release(lights_ubo_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(instanced_shader_);
        vivid::gpu::release(billboard_shader_);
        vivid::gpu::release(textured_shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(inst_pipe_layout_);
        vivid::gpu::release(tex_pipe_layout_);
        vivid::gpu::release(tex_bind_layout_);
        vivid::gpu::release(pbr_sampler_);
        vivid::gpu::release(fallback_tex_bg_);
        for (int i = 0; i < 4; ++i) {
            vivid::gpu::release(fallback_views_[i]);
            vivid::gpu::release(fallback_textures_[i]);
        }
        vivid::gpu::release(depth_view_);
        vivid::gpu::release(depth_tex_);
        // Phase 6e: depth blit resources
        vivid::gpu::release(depth_out_view_);
        vivid::gpu::release(depth_out_tex_);
        vivid::gpu::release(depth_blit_bg_);
        vivid::gpu::release(depth_blit_pipeline_);
        vivid::gpu::release(depth_blit_pipe_layout_);
        vivid::gpu::release(depth_blit_bgl_);
        vivid::gpu::release(depth_blit_shader_);
        vivid::gpu::release(depth_blit_sampler_);
        // Shadow resources (Phase 6d)
        vivid::gpu::release(shadow_shader_);
        vivid::gpu::release(shadow_pipe_layout_);
        vivid::gpu::release(shadow_camera_bgl_);
        vivid::gpu::release(shadow_camera_bg_);
        vivid::gpu::release(shadow_camera_ubo_);
        vivid::gpu::release(dir_shadow_tex_);
        for (auto& v : dir_shadow_layer_views_) vivid::gpu::release(v);
        vivid::gpu::release(dir_shadow_sample_);
        vivid::gpu::release(shadow_sampler_);
        vivid::gpu::release(shadow_ubo_);
        vivid::gpu::release(fallback_shadow_tex_);
        vivid::gpu::release(fallback_shadow_view_);
        vivid::gpu::release(shadow_color_view_);
        vivid::gpu::release(shadow_color_tex_);
        // Phase 6f: IBL resources
        vivid::gpu::release(ibl_shader_);
        vivid::gpu::release(textured_ibl_shader_);
        vivid::gpu::release(ibl_pipe_layout_);
        vivid::gpu::release(tex_ibl_pipe_layout_);
        vivid::gpu::release(ibl_bind_layout_);
        vivid::gpu::release(ibl_ubo_);
        vivid::gpu::release(ibl_bind_group_);
        vivid::gpu::release(fallback_ibl_bg_);
        vivid::gpu::release(fallback_ibl_cube_tex_);
        vivid::gpu::release(fallback_ibl_cube_view_);
        vivid::gpu::release(fallback_ibl_lut_tex_);
        vivid::gpu::release(fallback_ibl_lut_view_);
        vivid::gpu::release(ibl_sampler_);
        vivid::gpu::release(ibl_empty_bg_);
        vivid::gpu::release(ibl_empty_bgl_);
        // Phase 6f: skybox resources
        vivid::gpu::release(skybox_shader_);
        vivid::gpu::release(skybox_pipeline_);
        vivid::gpu::release(skybox_pipe_layout_);
        vivid::gpu::release(skybox_bgl_);
        vivid::gpu::release(skybox_bg_);
        vivid::gpu::release(skybox_camera_ubo_);
    }

private:
    // Pipeline variant cache (keyed on PipelineFeatureFlags)
    std::unordered_map<uint32_t, WGPURenderPipeline> pipeline_cache_;

    // Shader modules (persistent, shared across variants)
    WGPUShaderModule    shader_            = nullptr;  // non-instanced
    WGPUShaderModule    instanced_shader_  = nullptr;
    WGPUShaderModule    billboard_shader_  = nullptr;
    WGPUShaderModule    textured_shader_   = nullptr;  // Phase 6c: PBR textured
    WGPUShaderModule    ibl_shader_        = nullptr;  // Phase 6f: non-textured + IBL
    WGPUShaderModule    textured_ibl_shader_ = nullptr;  // Phase 6f: textured + IBL

    // Pipeline layouts (2 distinct: 1-group and 2-group)
    WGPUPipelineLayout  pipe_layout_       = nullptr;  // 1 bind group (non-instanced)
    WGPUPipelineLayout  inst_pipe_layout_  = nullptr;  // 2 bind groups (instanced + billboard)
    WGPUPipelineLayout  tex_pipe_layout_   = nullptr;  // Phase 6c: 2 groups (uniforms + textures)
    WGPUPipelineLayout  ibl_pipe_layout_   = nullptr;  // Phase 6f: group 0 + group 2
    WGPUPipelineLayout  tex_ibl_pipe_layout_ = nullptr;  // Phase 6f: group 0 + group 1 + group 2

    // Phase 6f: IBL bind group (group 2)
    WGPUBindGroupLayout ibl_bind_layout_      = nullptr;
    WGPUBuffer          ibl_ubo_              = nullptr;
    WGPUBindGroup       ibl_bind_group_       = nullptr;
    WGPUBindGroup       fallback_ibl_bg_      = nullptr;
    WGPUTexture         fallback_ibl_cube_tex_ = nullptr;
    WGPUTextureView     fallback_ibl_cube_view_ = nullptr;
    WGPUTexture         fallback_ibl_lut_tex_  = nullptr;
    WGPUTextureView     fallback_ibl_lut_view_ = nullptr;
    WGPUSampler         ibl_sampler_          = nullptr;
    WGPUBindGroupLayout ibl_empty_bgl_        = nullptr;  // empty group 1 placeholder
    WGPUBindGroup       ibl_empty_bg_         = nullptr;

    // Phase 6f: skybox resources
    WGPUShaderModule    skybox_shader_        = nullptr;
    WGPURenderPipeline  skybox_pipeline_      = nullptr;
    WGPUPipelineLayout  skybox_pipe_layout_   = nullptr;
    WGPUBindGroupLayout skybox_bgl_           = nullptr;
    WGPUBindGroup       skybox_bg_            = nullptr;
    WGPUBuffer          skybox_camera_ubo_    = nullptr;

    // Phase 6c: textured pipeline resources
    WGPUBindGroupLayout tex_bind_layout_     = nullptr;
    WGPUSampler         pbr_sampler_         = nullptr;
    WGPUTexture         fallback_textures_[4] = {};
    WGPUTextureView     fallback_views_[4]    = {};
    WGPUBindGroup       fallback_tex_bg_     = nullptr;

    // Output format (for cache invalidation)
    WGPUTextureFormat   cached_format_     = WGPUTextureFormat_Undefined;

    // Shared bind group (group 0): camera + material + lights + shadow
    WGPUBindGroup       bind_group_   = nullptr;
    WGPUBindGroupLayout bind_layout_  = nullptr;
    WGPUBuffer          camera_ubo_   = nullptr;
    WGPUBuffer          material_ubo_ = nullptr;
    WGPUBuffer          lights_ubo_   = nullptr;

    // Instanced bind group (group 1): storage buffer. One bind group per instanced draw, pooled per frame
    // (frame_inst_bgs_) and retired a frame later (retire_inst_bgs_) so multiple instanced draws coexist
    // and a bind group is never released while an in-flight pass still references it.
    WGPUBindGroupLayout inst_bind_layout_  = nullptr;
    std::vector<std::pair<WGPUBuffer, WGPUBindGroup>> frame_inst_bgs_;   // created this frame (buffer → bind group)
    std::vector<WGPUBindGroup>                        retire_inst_bgs_;  // previous frame's, released next frame
    WGPUDevice          cached_device_     = nullptr;

    WGPUTexture         depth_tex_    = nullptr;
    WGPUTextureView     depth_view_   = nullptr;
    uint32_t            cached_w_     = 0;
    uint32_t            cached_h_     = 0;

    // Phase 6e: depth blit resources (Depth32Float → R32Float)
    WGPUTexture         depth_out_tex_          = nullptr;
    WGPUTextureView     depth_out_view_         = nullptr;
    WGPUShaderModule    depth_blit_shader_      = nullptr;
    WGPURenderPipeline  depth_blit_pipeline_    = nullptr;
    WGPUPipelineLayout  depth_blit_pipe_layout_ = nullptr;
    WGPUBindGroupLayout depth_blit_bgl_         = nullptr;
    WGPUBindGroup       depth_blit_bg_          = nullptr;
    WGPUSampler         depth_blit_sampler_     = nullptr;
    bool                depth_blit_bg_dirty_    = true;

    // Phase 6d: shadow mapping resources
    WGPUShaderModule    shadow_shader_         = nullptr;
    WGPUPipelineLayout  shadow_pipe_layout_    = nullptr;
    WGPUBindGroupLayout shadow_camera_bgl_     = nullptr;
    WGPUBindGroup       shadow_camera_bg_      = nullptr;
    WGPUBuffer          shadow_camera_ubo_     = nullptr;   // dedicated buffer for shadow pass
    std::string         light_limit_msg_;                  // ADR-0051 P4: owns the reported message
    WGPUTexture         dir_shadow_tex_        = nullptr;  // depth ARRAY, one layer per caster
    WGPUTextureView     dir_shadow_layer_views_[kMaxShadowCasters] = {};  // per-caster render targets
    WGPUTextureView     dir_shadow_sample_     = nullptr;  // 2d-array view, for sampling in fragment
    WGPUSampler         shadow_sampler_        = nullptr;   // comparison sampler
    WGPUBuffer          shadow_ubo_            = nullptr;
    uint32_t            cached_shadow_res_     = 0;
    WGPUTexture         fallback_shadow_tex_   = nullptr;   // 1x1 dummy depth texture
    WGPUTextureView     fallback_shadow_view_  = nullptr;
    WGPUTexture         shadow_color_tex_      = nullptr;   // scratch color target for shadow pass
    WGPUTextureView     shadow_color_view_     = nullptr;

    // Phase 6e: rebuild depth blit bind group when depth view changes
    void rebuild_depth_blit_bg_if_needed(const VividGpuContext* ctx) {
        if (!depth_blit_bg_dirty_ && depth_blit_bg_) return;

        vivid::gpu::release(depth_blit_bg_);

        WGPUTextureViewDescriptor sample_vd{};
        sample_vd.label = vivid_sv("Render3D Depth Blit Sample View");
        sample_vd.format = vivid::gpu::kDepthFormat;
        sample_vd.dimension = WGPUTextureViewDimension_2D;
        sample_vd.mipLevelCount = 1;
        sample_vd.arrayLayerCount = 1;
        sample_vd.aspect = WGPUTextureAspect_DepthOnly;
        WGPUTextureView depth_sample = wgpuTextureCreateView(depth_tex_, &sample_vd);

        WGPUBindGroupEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].textureView = depth_sample;
        entries[1].binding = 1;
        entries[1].sampler = depth_blit_sampler_;

        WGPUBindGroupDescriptor desc{};
        desc.label = vivid_sv("Render3D Depth Blit BG");
        desc.layout = depth_blit_bgl_;
        desc.entryCount = 2;
        desc.entries = entries;
        depth_blit_bg_ = wgpuDeviceCreateBindGroup(ctx->device, &desc);

        wgpuTextureViewRelease(depth_sample);
        depth_blit_bg_dirty_ = false;
    }

    // A per-frame instanced bind group for `inst_buf`, reused within the frame if the same buffer recurs.
    // Created bind groups are retired one frame later (see the rotation before the pass), so any number of
    // instanced draws can coexist in a single render pass — each references its own still-live bind group.
    WGPUBindGroup frame_instanced_bind_group(const VividGpuContext* ctx, WGPUBuffer inst_buf) {
        for (auto& kv : frame_inst_bgs_)
            if (kv.first == inst_buf) return kv.second;

        uint64_t buf_size = wgpuBufferGetSize(inst_buf);

        WGPUBindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer  = inst_buf;
        entry.offset  = 0;
        entry.size    = buf_size;

        WGPUBindGroupDescriptor desc{};
        desc.label      = vivid_sv("Render3D Instanced BG");
        desc.layout     = inst_bind_layout_;
        desc.entryCount = 1;
        desc.entries    = &entry;
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(ctx->device, &desc);
        frame_inst_bgs_.push_back({ inst_buf, bg });
        return bg;
    }

    // Phase 6f: rebuild IBL bind group with environment's texture views
    void rebuild_ibl_bind_group(const VividGpuContext* ctx,
                                 const vivid::gpu::VividSceneFragment* env) {
        vivid::gpu::release(ibl_bind_group_);

        WGPUBindGroupEntry entries[5]{};
        entries[0].binding = 0;
        entries[0].sampler = env->ibl_sampler;
        entries[1].binding = 1;
        entries[1].textureView = env->ibl_irradiance;
        entries[2].binding = 2;
        entries[2].textureView = env->ibl_prefiltered;
        entries[3].binding = 3;
        entries[3].textureView = env->ibl_brdf_lut;
        entries[4].binding = 4;
        entries[4].buffer = ibl_ubo_;
        entries[4].offset = 0;
        entries[4].size = sizeof(IBLUniform);

        WGPUBindGroupDescriptor desc{};
        desc.label = vivid_sv("Render3D IBL BG");
        desc.layout = ibl_bind_layout_;
        desc.entryCount = 5;
        desc.entries = entries;
        ibl_bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &desc);
    }

    // Phase 6d: Ensure shadow map textures are the right size
    void ensure_shadow_maps(const VividGpuContext* ctx, uint32_t res) {
        if (res == cached_shadow_res_ && dir_shadow_tex_) return;

        vivid::gpu::release(dir_shadow_tex_);
        for (auto& v : dir_shadow_layer_views_) vivid::gpu::release(v);
        vivid::gpu::release(dir_shadow_sample_);
        vivid::gpu::release(shadow_color_tex_);
        vivid::gpu::release(shadow_color_view_);

        // ADR-0051 Phase 2: ONE LAYER PER CASTER. This used to be a single 2D map that every
        // shadow pass cleared and re-rendered, so with two casters the last one's depth was all
        // that survived while the shader still sampled it per-light — the first light's shadow
        // came out projected from the wrong viewpoint.
        dir_shadow_tex_ = vivid::gpu::create_shadow_map_texture(
            ctx->device, res, res, "Render3D Dir Shadow Map", kMaxShadowCasters);
        for (uint32_t i = 0; i < kMaxShadowCasters; ++i) {
            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("Render3D Dir Shadow Layer View");
            vd.format = vivid::gpu::kDepthFormat;
            vd.dimension = WGPUTextureViewDimension_2D;   // a single layer, as a render target
            vd.baseArrayLayer = i;
            vd.arrayLayerCount = 1;
            vd.mipLevelCount = 1;
            vd.aspect = WGPUTextureAspect_All;            // depth render target: all aspects
            dir_shadow_layer_views_[i] = wgpuTextureCreateView(dir_shadow_tex_, &vd);
        }
        {
            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("Render3D Dir Shadow Sample View");
            vd.format = vivid::gpu::kDepthFormat;
            vd.dimension = WGPUTextureViewDimension_2DArray;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = kMaxShadowCasters;
            vd.aspect = WGPUTextureAspect_DepthOnly;
            dir_shadow_sample_ = wgpuTextureCreateView(dir_shadow_tex_, &vd);
        }

        // Scratch color target for shadow pass (workaround: some wgpu-native
        // backends need at least one color attachment for depth writes to work)
        {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv("Render3D Shadow Scratch Color");
            td.size = { res, res, 1 };
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_R8Unorm;
            td.usage = WGPUTextureUsage_RenderAttachment;
            shadow_color_tex_ = wgpuDeviceCreateTexture(ctx->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("Render3D Shadow Scratch Color View");
            vd.format = WGPUTextureFormat_R8Unorm;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            shadow_color_view_ = wgpuTextureCreateView(shadow_color_tex_, &vd);
        }

        cached_shadow_res_ = res;
        rebuild_bind_group(ctx);
    }

    // Phase 6d: Recreate bind_group_ with current shadow texture view
    void rebuild_bind_group(const VividGpuContext* ctx) {
        vivid::gpu::release(bind_group_);

        WGPUTextureView shadow_view = dir_shadow_sample_ ? dir_shadow_sample_ : fallback_shadow_view_;

        WGPUBindGroupEntry bg_entries[6]{};
        bg_entries[0].binding = 0;
        bg_entries[0].buffer  = camera_ubo_;
        bg_entries[0].offset  = 0;
        bg_entries[0].size    = sizeof(CameraUniform);

        bg_entries[1].binding = 1;
        bg_entries[1].buffer  = material_ubo_;
        bg_entries[1].offset  = 0;
        bg_entries[1].size    = sizeof(MaterialUniform);

        bg_entries[2].binding = 2;
        bg_entries[2].buffer  = lights_ubo_;
        bg_entries[2].offset  = 0;
        bg_entries[2].size    = sizeof(LightsUniform);

        bg_entries[3].binding = 3;
        bg_entries[3].buffer  = shadow_ubo_;
        bg_entries[3].offset  = 0;
        bg_entries[3].size    = sizeof(ShadowUniform);

        bg_entries[4].binding = 4;
        bg_entries[4].sampler = shadow_sampler_;

        bg_entries[5].binding = 5;
        bg_entries[5].textureView = shadow_view;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Render3D Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 6;
        bg_desc.entries = bg_entries;
        bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);
    }

    // Phase 6d: Compute directional light VP (ortho projection fitting scene geometry)
    void compute_directional_light_vp(mat4x4 out, const float* light_dir,
                                       const float* eye, const float* target,
                                       float fov_rad, float aspect,
                                       float near, float far,
                                       const std::vector<DrawCall>& draws) {
        // Compute scene world-space AABB from draw call geometry.
        // Use tight OBB→AABB: for each axis, the extent is the sum of absolute
        // model matrix column components scaled by the local half-extent (0.5
        // for unit cube Shape3D geometry).
        float scene_min[3] = {  1e30f,  1e30f,  1e30f };
        float scene_max[3] = { -1e30f, -1e30f, -1e30f };
        for (auto& dc : draws) {
            if (dc.frag->pipeline) continue;
            for (int a = 0; a < 3; a++) {
                float center = dc.composed_model[3][a];
                float ext = 0.5f * (std::fabs(dc.composed_model[0][a]) +
                                     std::fabs(dc.composed_model[1][a]) +
                                     std::fabs(dc.composed_model[2][a]));
                if (center - ext < scene_min[a]) scene_min[a] = center - ext;
                if (center + ext > scene_max[a]) scene_max[a] = center + ext;
            }
        }

        // Scene center and extent
        float center[3] = { (scene_min[0] + scene_max[0]) * 0.5f,
                             (scene_min[1] + scene_max[1]) * 0.5f,
                             (scene_min[2] + scene_max[2]) * 0.5f };
        float extent = 0;
        for (int a = 0; a < 3; a++)
            extent = std::max(extent, scene_max[a] - scene_min[a]);
        if (extent < 1.0f) extent = 1.0f;

        // Light "eye" is offset from scene center along light direction
        float light_dist = extent;
        float light_eye[3] = { center[0] + light_dir[0] * light_dist,
                                center[1] + light_dir[1] * light_dist,
                                center[2] + light_dir[2] * light_dist };

        // Choose stable up vector for look-at
        float up[3] = { 0.0f, 1.0f, 0.0f };
        float abs_dot = std::fabs(light_dir[0]*up[0] + light_dir[1]*up[1] + light_dir[2]*up[2]);
        if (abs_dot > 0.99f) {
            up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
        }

        mat4x4 light_view;
        mat4x4_look_at(light_view, light_eye, center, up);

        // Transform scene AABB corners to light view space for tight ortho bounds
        float min_x =  1e30f, max_x = -1e30f;
        float min_y =  1e30f, max_y = -1e30f;
        float min_z =  1e30f, max_z = -1e30f;
        for (int i = 0; i < 8; ++i) {
            float wx = (i & 1) ? scene_max[0] : scene_min[0];
            float wy = (i & 2) ? scene_max[1] : scene_min[1];
            float wz = (i & 4) ? scene_max[2] : scene_min[2];
            vec4 c = { wx, wy, wz, 1.0f };
            vec4 lv;
            mat4x4_mul_vec4(lv, light_view, c);
            if (lv[0] < min_x) min_x = lv[0]; if (lv[0] > max_x) max_x = lv[0];
            if (lv[1] < min_y) min_y = lv[1]; if (lv[1] > max_y) max_y = lv[1];
            if (lv[2] < min_z) min_z = lv[2]; if (lv[2] > max_z) max_z = lv[2];
        }

        // Small padding
        float pad = extent * 0.05f;
        min_x -= pad; max_x += pad;
        min_y -= pad; max_y += pad;
        min_z -= pad; max_z += pad;

        // ortho_wgpu expects positive near/far distances.
        // In view space (right-handed look-at), objects are at negative Z.
        float ortho_near = -max_z;
        float ortho_far  = -min_z;
        if (ortho_near < 0.001f) ortho_near = 0.001f;
        if (ortho_far <= ortho_near) ortho_far = ortho_near + 1.0f;

        mat4x4 light_proj;
        vivid::gpu::ortho_wgpu(light_proj, min_x, max_x, min_y, max_y, ortho_near, ortho_far);
        mat4x4_mul(out, light_proj, light_view);
    }

    // ADR-0051 Phase 2b: a SPOT light's view-projection — a perspective frustum straight down the
    // cone, which is exactly what a spot already is, so unlike an omni light it needs no cube map.
    // The fov is the full cone angle with a small margin so the penumbra at the rim still has depth
    // behind it; far tracks the light's influence radius, since past it the light contributes zero.
    void compute_spot_light_vp(mat4x4 out, const CollectedLight& cl) {
        vec3 eye = { cl.position[0], cl.position[1], cl.position[2] };
        vec3 tgt = { cl.position[0] + cl.direction[0],
                     cl.position[1] + cl.direction[1],
                     cl.position[2] + cl.direction[2] };
        // Any up-vector works except one parallel to the aim; swap axes when shining near-vertically
        // (a spot pointing straight down is the common case, and would make look_at degenerate).
        vec3 up = { 0.f, 1.f, 0.f };
        if (std::fabs(cl.direction[1]) > 0.99f) { up[1] = 0.f; up[2] = 1.f; }

        mat4x4 view, proj;
        mat4x4_look_at(view, eye, tgt, up);

        // spot_angle_cos is cos(outer half-angle); recover the angle and double it for the fov.
        const float outer = std::acos(std::clamp(cl.spot_angle_cos, -1.0f, 1.0f));
        float fov = 2.0f * outer * 1.05f;                       // 5% margin for the soft rim
        fov = std::clamp(fov, 0.05f, 3.0f);                     // keep well inside a degenerate pi
        const float far_plane = std::max(cl.radius, 0.2f);
        // The near plane has to be pushed OUT, not pulled in. A perspective depth buffer spends
        // most of its precision just past `near`, so a tiny near (0.05 against a far of 30) crushes
        // the whole scene into ~0.002 of depth range — less than the default 0.005 shadow_bias, and
        // every occluder silently stops registering. Trading occluders within `near` of the lamp for
        // a usable depth range is the right trade: geometry that close is inside the fixture.
        const float near_plane = std::clamp(far_plane * 0.05f, 0.1f, 5.0f);
        vivid::gpu::perspective_wgpu(proj, fov, 1.0f, near_plane, far_plane);
        mat4x4_mul(out, proj, view);
    }

    // Phase 6d: Run shadow render pass (depth + scratch color attachment)
    // `slot` selects this caster's block of shadow-camera UBO slots (ADR-0051 Phase 2).
    void render_shadow_pass(const VividGpuContext* ctx, const std::vector<DrawCall>& draws,
                            WGPUTextureView depth_target, uint32_t w, uint32_t h,
                            uint32_t slot, const char* label) {
        auto shadow_pipeline = get_or_create_pipeline(ctx, vivid::gpu::kPipelineShadowCaster);
        if (!shadow_pipeline) return;

        WGPURenderPassDepthStencilAttachment depth_att{};
        depth_att.view = depth_target;
        depth_att.depthLoadOp = WGPULoadOp_Clear;
        depth_att.depthStoreOp = WGPUStoreOp_Store;
        depth_att.depthClearValue = 1.0f;

        // Scratch color attachment — workaround for wgpu-native depth-only pass issue
        WGPURenderPassColorAttachment color_att{};
        color_att.view = shadow_color_view_;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.loadOp = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Discard;
        color_att.clearValue = {0, 0, 0, 0};

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv(label);
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;
        rp_desc.depthStencilAttachment = &depth_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            ctx->command_encoder, &rp_desc);

        wgpuRenderPassEncoderSetPipeline(pass, shadow_pipeline);

        for (uint32_t i = 0; i < static_cast<uint32_t>(draws.size()); ++i) {
            auto& dc = draws[i];

            // Skip custom pipelines, non-shadow-casters, billboards
            if (dc.frag->pipeline) continue;
            if (!dc.frag->cast_shadow) continue;
            if (dc.frag->billboard) continue;

            uint32_t dynamic_offset = static_cast<uint32_t>(
                (static_cast<uint64_t>(slot) * kMaxDrawSlots + i) * kCameraSlotStride);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, shadow_camera_bg_, 1, &dynamic_offset);

            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, dc.frag->vertex_buffer,
                                                  0, dc.frag->vertex_buf_size);
            wgpuRenderPassEncoderSetIndexBuffer(pass, dc.frag->index_buffer,
                                                 WGPUIndexFormat_Uint32, 0,
                                                 static_cast<uint64_t>(dc.frag->index_count) * sizeof(uint32_t));

            uint32_t inst_count = (dc.instance_count >= 1 && dc.instance_buffer) ? dc.instance_count : 1;
            wgpuRenderPassEncoderDrawIndexed(pass, dc.frag->index_count, inst_count, 0, 0, 0);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    // Phase 6f: skybox shader + pipeline initialization
    bool init_skybox(const VividGpuContext* ctx) {
        static const char* kSkyboxShader = R"(
struct SkyboxCamera {
    inverse_vp: mat4x4f,
}

@group(0) @binding(0) var<uniform> skybox_cam: SkyboxCamera;
@group(0) @binding(1) var skybox_sampler: sampler;
@group(0) @binding(2) var skybox_cube: texture_cube<f32>;

struct SkyboxOutput {
    @builtin(position) position: vec4f,
    @location(0) ndc: vec2f,
}

@vertex
fn vs_skybox(@builtin(vertex_index) vid: u32) -> SkyboxOutput {
    // Fullscreen triangle with z=1 (at far plane)
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: SkyboxOutput;
    let pos = positions[vid];
    out.position = vec4f(pos, 1.0, 1.0);
    out.ndc = pos;
    return out;
}

@fragment
fn fs_skybox(in: SkyboxOutput) -> @location(0) vec4f {
    // Reconstruct world-space ray direction from NDC via inverse VP
    let ndc = vec4f(in.ndc.x, in.ndc.y, 1.0, 1.0);
    let world = skybox_cam.inverse_vp * ndc;
    let dir = normalize(world.xyz / world.w);
    let color = textureSampleLevel(skybox_cube, skybox_sampler, dir, 0.0).rgb;
    return vec4f(color, 1.0);
}
)";
skybox_shader_ = vivid::gpu::create_wgsl_shader(
            ctx->device, kSkyboxShader, "Render3D Skybox Shader");
        if (!skybox_shader_) return false;

        // Bind group layout: camera UBO + sampler + cubemap
        WGPUBindGroupLayoutEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = 64;  // mat4x4f

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Render3D Skybox BGL");
        bgl_desc.entryCount = 3;
        bgl_desc.entries = entries;
        skybox_bgl_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Render3D Skybox PL");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &skybox_bgl_;
        skybox_pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);

        // Camera UBO (64 bytes — inverse VP matrix)
        skybox_camera_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, 64, "Render3D Skybox Camera UBO");

        // Pipeline: depth test LessEqual, no depth write, no blend
        vivid::gpu::Pipeline3DDesc pd{};
        pd.shader       = skybox_shader_;
        pd.layout       = skybox_pipe_layout_;
        pd.color_format = ctx->output_format;
        pd.vertex_layouts      = nullptr;
        pd.vertex_layout_count = 0;
        pd.cull_mode    = WGPUCullMode_None;
        pd.depth_write  = false;
        pd.depth_compare = WGPUCompareFunction_LessEqual;
        pd.vs_entry     = "vs_skybox";
        pd.fs_entry     = "fs_skybox";
        pd.label        = "Render3D Skybox Pipeline";
        skybox_pipeline_ = vivid::gpu::create_3d_pipeline(ctx->device, pd);
        return skybox_pipeline_ != nullptr;
    }

    // Phase 6f: render skybox pass
    void render_skybox(const VividGpuContext* ctx,
                       const vivid::gpu::VividSceneFragment* env,
                       const mat4x4 vp, const float* eye) {
        // Write inverse VP to skybox camera UBO
        mat4x4 inv_vp;
        mat4x4_invert(inv_vp, vp);
        wgpuQueueWriteBuffer(ctx->queue, skybox_camera_ubo_, 0, inv_vp, 64);

        // Rebuild skybox bind group (environment cubemap may change)
        vivid::gpu::release(skybox_bg_);
        WGPUBindGroupEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].buffer = skybox_camera_ubo_;
        entries[0].offset = 0;
        entries[0].size = 64;
        entries[1].binding = 1;
        entries[1].sampler = env->ibl_sampler;
        entries[2].binding = 2;
        entries[2].textureView = env->ibl_prefiltered;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Render3D Skybox BG");
        bg_desc.layout = skybox_bgl_;
        bg_desc.entryCount = 3;
        bg_desc.entries = entries;
        skybox_bg_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);

        // Begin pass — load existing color + depth (no clear)
        WGPURenderPassColorAttachment color_att{};
        color_att.view = ctx->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.loadOp = WGPULoadOp_Load;
        color_att.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDepthStencilAttachment depth_att{};
        depth_att.view = depth_view_;
        depth_att.depthLoadOp = WGPULoadOp_Load;
        depth_att.depthStoreOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("Render3D Skybox Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;
        rp_desc.depthStencilAttachment = &depth_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            ctx->command_encoder, &rp_desc);
        wgpuRenderPassEncoderSetPipeline(pass, skybox_pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, skybox_bg_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    WGPURenderPipeline get_or_create_pipeline(const VividGpuContext* ctx, uint32_t flags) {
        auto it = pipeline_cache_.find(flags);
        if (it != pipeline_cache_.end()) return it->second;

        bool is_shadow_caster = (flags & vivid::gpu::kPipelineShadowCaster) != 0;

        // Shadow caster: depth-only pipeline with separate minimal layout
        if (is_shadow_caster) {
            WGPUVertexBufferLayout vbl = vivid::gpu::vertex3d_layout();

            // Depth stencil state
            WGPUDepthStencilState depth_stencil{};
            depth_stencil.format = vivid::gpu::kDepthFormat;
            depth_stencil.depthWriteEnabled = WGPUOptionalBool_True;
            depth_stencil.depthCompare = WGPUCompareFunction_Less;
            depth_stencil.stencilFront.compare     = WGPUCompareFunction_Always;
            depth_stencil.stencilFront.failOp      = WGPUStencilOperation_Keep;
            depth_stencil.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depth_stencil.stencilFront.passOp      = WGPUStencilOperation_Keep;
            depth_stencil.stencilBack              = depth_stencil.stencilFront;
            depth_stencil.stencilReadMask          = 0xFF;
            depth_stencil.stencilWriteMask         = 0xFF;

            // Fragment state with scratch color target (workaround for wgpu-native
            // depth-only pass issue — needs at least one color attachment)
            WGPUColorTargetState shadow_ct{};
            shadow_ct.format = WGPUTextureFormat_R8Unorm;
            shadow_ct.writeMask = WGPUColorWriteMask_None;  // don't actually write

            WGPUFragmentState shadow_frag{};
            shadow_frag.module = shadow_shader_;
            shadow_frag.entryPoint = vivid_sv("fs_shadow");
            shadow_frag.targetCount = 1;
            shadow_frag.targets = &shadow_ct;

            WGPURenderPipelineDescriptor rp_desc{};
            rp_desc.label = vivid_sv("Render3D Shadow Caster Pipeline");
            rp_desc.layout = shadow_pipe_layout_;
            rp_desc.vertex.module = shadow_shader_;
            rp_desc.vertex.entryPoint = vivid_sv("vs_shadow");
            rp_desc.vertex.bufferCount = 1;
            rp_desc.vertex.buffers = &vbl;
            rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
            rp_desc.primitive.cullMode = WGPUCullMode_Front;
            rp_desc.depthStencil = &depth_stencil;
            rp_desc.multisample.count = 1;
            rp_desc.multisample.mask = 0xFFFFFFFF;
            rp_desc.fragment = &shadow_frag;

            auto pipeline = wgpuDeviceCreateRenderPipeline(ctx->device, &rp_desc);
            if (pipeline) pipeline_cache_[flags] = pipeline;
            return pipeline;
        }

        bool is_instanced = (flags & vivid::gpu::kPipelineInstanced) != 0;
        bool is_billboard  = (flags & vivid::gpu::kPipelineBillboard) != 0;
        bool is_ibl       = (flags & vivid::gpu::kPipelineIBL) != 0;

        // Select shader, layout, entry points based on flags
        WGPUShaderModule shader;
        WGPUPipelineLayout layout;
        const char *vs_entry, *fs_entry;

        bool is_textured  = (flags & vivid::gpu::kPipelineTextured) != 0;

        if (is_billboard) {
            shader   = billboard_shader_;
            layout   = inst_pipe_layout_;
            vs_entry = "vs_billboard";
            fs_entry = "fs_billboard";
        } else if (is_textured && is_ibl && !is_instanced) {
            shader   = textured_ibl_shader_;
            layout   = tex_ibl_pipe_layout_;
            vs_entry = "vs_textured";
            fs_entry = "fs_textured";
        } else if (is_textured && !is_instanced) {
            shader   = textured_shader_;
            layout   = tex_pipe_layout_;
            vs_entry = "vs_textured";
            fs_entry = "fs_textured";
        } else if (is_ibl && !is_instanced) {
            shader   = ibl_shader_;
            layout   = ibl_pipe_layout_;
            vs_entry = "vs_main";
            fs_entry = "fs_main";
        } else if (is_instanced) {
            shader   = instanced_shader_;
            layout   = inst_pipe_layout_;
            vs_entry = "vs_instanced";
            fs_entry = "fs_instanced";
        } else {
            shader   = shader_;
            layout   = pipe_layout_;
            vs_entry = "vs_main";
            fs_entry = "fs_main";
        }

        static WGPUBlendState bb_blend{};
        bb_blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        bb_blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        bb_blend.color.operation = WGPUBlendOperation_Add;
        bb_blend.alpha.srcFactor = WGPUBlendFactor_One;
        bb_blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        bb_blend.alpha.operation = WGPUBlendOperation_Add;

        WGPUVertexBufferLayout vbl = vivid::gpu::vertex3d_layout();

        vivid::gpu::Pipeline3DDesc pd{};
        pd.shader = shader;
        pd.layout = layout;
        pd.color_format = cached_format_;
        pd.vertex_layouts = &vbl;
        pd.vertex_layout_count = 1;
        pd.vs_entry = vs_entry;
        pd.fs_entry = fs_entry;

        if (is_billboard) {
            pd.cull_mode = WGPUCullMode_None;
            pd.depth_write = false;
            pd.blend = &bb_blend;
            pd.label = "Render3D Billboard Pipeline";
        } else if (is_instanced) {
            pd.label = "Render3D Instanced Pipeline";
        } else {
            pd.label = "Render3D Pipeline";
        }

        auto pipeline = vivid::gpu::create_3d_pipeline(ctx->device, pd);
        if (pipeline) pipeline_cache_[flags] = pipeline;
        return pipeline;
    }

    bool lazy_init(const VividGpuContext* ctx) {
        cached_device_ = ctx->device;

        // ---- Compile shader modules ----
        std::string src = std::string(vivid::gpu::VERTEX_3D_WGSL)
                        + std::string(vivid::gpu::LIGHTS_3D_WGSL)
                        + std::string(vivid::gpu::SHADOW_3D_WGSL)
                        + kRender3DFragment;
        shader_ = vivid::gpu::create_shader(ctx->device, src.c_str(), "Render3D Shader");
        if (!shader_) return false;

        std::string inst_src = std::string(vivid::gpu::VERTEX_3D_WGSL)
                             + std::string(vivid::gpu::LIGHTS_3D_WGSL)
                             + std::string(vivid::gpu::SHADOW_3D_WGSL)
                             + kRender3DInstanced;
        instanced_shader_ = vivid::gpu::create_shader(ctx->device, inst_src.c_str(),
                                                       "Render3D Instanced Shader");
        if (!instanced_shader_) return false;

        std::string bb_src = std::string(vivid::gpu::VERTEX_3D_WGSL)
                           + std::string(vivid::gpu::LIGHTS_3D_WGSL)
                           + std::string(vivid::gpu::SHADOW_3D_WGSL)
                           + kRender3DBillboard;
        billboard_shader_ = vivid::gpu::create_shader(ctx->device, bb_src.c_str(),
                                                       "Render3D Billboard Shader");
        if (!billboard_shader_) return false;

        // Phase 6c: textured PBR shader
        std::string tex_src = std::string(vivid::gpu::VERTEX_3D_WGSL)
                            + std::string(vivid::gpu::LIGHTS_3D_WGSL)
                            + std::string(vivid::gpu::SHADOW_3D_WGSL)
                            + std::string(vivid::gpu::PBR_BRDF_WGSL)
                            + kRender3DTextured;
        textured_shader_ = vivid::gpu::create_shader(ctx->device, tex_src.c_str(),
                                                       "Render3D Textured Shader");
        if (!textured_shader_) return false;

        // Phase 6d: shadow caster shader (standalone, no preamble needed)
        shadow_shader_ = vivid::gpu::create_wgsl_shader(ctx->device, kShadowCasterShader,
                                                          "Render3D Shadow Caster Shader");
        if (!shadow_shader_) return false;

        // ---- Uniform buffers ----
        camera_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, kMaxDrawSlots * kCameraSlotStride, "Render3D Camera UBO");
        material_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, kMaxDrawSlots * kMaterialSlotStride, "Render3D Material UBO");
        lights_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, sizeof(LightsUniform), "Render3D Lights UBO");
        shadow_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, sizeof(ShadowUniform), "Render3D Shadow UBO");

        // ---- Phase 6d: comparison sampler for shadow mapping ----
        {
            WGPUSamplerDescriptor sd{};
            sd.label = vivid_sv("Render3D Shadow Comparison Sampler");
            sd.addressModeU = WGPUAddressMode_ClampToEdge;
            sd.addressModeV = WGPUAddressMode_ClampToEdge;
            sd.addressModeW = WGPUAddressMode_ClampToEdge;
            sd.magFilter    = WGPUFilterMode_Linear;
            sd.minFilter    = WGPUFilterMode_Linear;
            sd.compare      = WGPUCompareFunction_Less;
            sd.maxAnisotropy = 1;
            shadow_sampler_ = wgpuDeviceCreateSampler(ctx->device, &sd);
        }

        // ---- Phase 6d: fallback 1x1 depth texture for shadow sampling ----
        // ADR-0051 Phase 2: an ARRAY of kMaxShadowCasters layers — the bind group layout now
        // declares 2d-array, and a plain 2D view would fail validation when no shadows are active.
        fallback_shadow_tex_ = vivid::gpu::create_shadow_map_texture(
            ctx->device, 1, 1, "Render3D Fallback Shadow Tex", kMaxShadowCasters);
        {
            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("Render3D Fallback Shadow View");
            vd.format = vivid::gpu::kDepthFormat;
            vd.dimension = WGPUTextureViewDimension_2DArray;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = kMaxShadowCasters;
            vd.aspect = WGPUTextureAspect_DepthOnly;
            fallback_shadow_view_ = wgpuTextureCreateView(fallback_shadow_tex_, &vd);
        }

        // ---- Bind group layout 0: camera(dyn) + material(dyn) + lights + shadow + sampler + depth tex ----
        WGPUBindGroupLayoutEntry entries[6]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(CameraUniform);

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].buffer.type = WGPUBufferBindingType_Uniform;
        entries[1].buffer.hasDynamicOffset = true;
        entries[1].buffer.minBindingSize = sizeof(MaterialUniform);

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].buffer.type = WGPUBufferBindingType_Uniform;
        entries[2].buffer.minBindingSize = sizeof(LightsUniform);

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Fragment;
        entries[3].buffer.type = WGPUBufferBindingType_Uniform;
        entries[3].buffer.minBindingSize = sizeof(ShadowUniform);

        entries[4].binding = 4;
        entries[4].visibility = WGPUShaderStage_Fragment;
        entries[4].sampler.type = WGPUSamplerBindingType_Comparison;

        entries[5].binding = 5;
        entries[5].visibility = WGPUShaderStage_Fragment;
        entries[5].texture.sampleType = WGPUTextureSampleType_Depth;
        entries[5].texture.viewDimension = WGPUTextureViewDimension_2DArray;  // ADR-0051 P2

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Render3D BGL");
        bgl_desc.entryCount = 6;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);

        // ---- Bind group layout 1: storage buffer for instancing ----
        WGPUBindGroupLayoutEntry inst_entry{};
        inst_entry.binding = 0;
        inst_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        inst_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        inst_entry.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor inst_bgl_desc{};
        inst_bgl_desc.label = vivid_sv("Render3D Instance BGL");
        inst_bgl_desc.entryCount = 1;
        inst_bgl_desc.entries = &inst_entry;
        inst_bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &inst_bgl_desc);

        // ---- Pipeline layout: non-instanced (group 0 only) ----
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Render3D Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);

        // ---- Pipeline layout: instanced + billboard (group 0 + group 1) ----
        WGPUBindGroupLayout inst_layouts[2] = { bind_layout_, inst_bind_layout_ };
        WGPUPipelineLayoutDescriptor inst_pl_desc{};
        inst_pl_desc.label = vivid_sv("Render3D Instanced Pipeline Layout");
        inst_pl_desc.bindGroupLayoutCount = 2;
        inst_pl_desc.bindGroupLayouts = inst_layouts;
        inst_pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &inst_pl_desc);

        // ---- Phase 6c: textured pipeline layout (group 0 + PBR textures) ----
        tex_bind_layout_ = vivid::gpu::create_pbr_texture_bind_layout(ctx->device);
        WGPUBindGroupLayout tex_layouts[2] = { bind_layout_, tex_bind_layout_ };
        WGPUPipelineLayoutDescriptor tex_pl_desc{};
        tex_pl_desc.label = vivid_sv("Render3D Textured Pipeline Layout");
        tex_pl_desc.bindGroupLayoutCount = 2;
        tex_pl_desc.bindGroupLayouts = tex_layouts;
        tex_pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &tex_pl_desc);

        // ---- Phase 6d: shadow caster pipeline layout (camera-only, 1 entry) ----
        {
            WGPUBindGroupLayoutEntry sc_entry{};
            sc_entry.binding = 0;
            sc_entry.visibility = WGPUShaderStage_Vertex;
            sc_entry.buffer.type = WGPUBufferBindingType_Uniform;
            sc_entry.buffer.hasDynamicOffset = true;
            sc_entry.buffer.minBindingSize = sizeof(CameraUniform);

            WGPUBindGroupLayoutDescriptor sc_bgl_desc{};
            sc_bgl_desc.label = vivid_sv("Render3D Shadow Camera BGL");
            sc_bgl_desc.entryCount = 1;
            sc_bgl_desc.entries = &sc_entry;
            shadow_camera_bgl_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &sc_bgl_desc);

            WGPUPipelineLayoutDescriptor sc_pl_desc{};
            sc_pl_desc.label = vivid_sv("Render3D Shadow Pipeline Layout");
            sc_pl_desc.bindGroupLayoutCount = 1;
            sc_pl_desc.bindGroupLayouts = &shadow_camera_bgl_;
            shadow_pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &sc_pl_desc);

            // Dedicated shadow camera UBO (separate from main camera_ubo_ to avoid
            // queue write ordering issues — all wgpuQueueWriteBuffer calls complete
            // before any command buffer executes)
            // One block of draw slots PER CASTER (ADR-0051 Phase 2) — see the shadow loop for why
            // the blocks cannot be shared.
            shadow_camera_ubo_ = vivid::gpu::create_uniform_buffer(
                ctx->device, kMaxShadowCasters * kMaxDrawSlots * kCameraSlotStride,
                "Render3D Shadow Camera UBO");

            // Shadow caster bind group (uses dedicated shadow_camera_ubo_)
            WGPUBindGroupEntry sc_bg_entry{};
            sc_bg_entry.binding = 0;
            sc_bg_entry.buffer  = shadow_camera_ubo_;
            sc_bg_entry.offset  = 0;
            sc_bg_entry.size    = sizeof(CameraUniform);

            WGPUBindGroupDescriptor sc_bg_desc{};
            sc_bg_desc.label      = vivid_sv("Render3D Shadow Camera BG");
            sc_bg_desc.layout     = shadow_camera_bgl_;
            sc_bg_desc.entryCount = 1;
            sc_bg_desc.entries    = &sc_bg_entry;
            shadow_camera_bg_ = wgpuDeviceCreateBindGroup(ctx->device, &sc_bg_desc);
        }

        // ---- Phase 6c: PBR sampler + fallback textures ----
        pbr_sampler_ = vivid::gpu::create_repeat_sampler(ctx->device, "Render3D PBR Sampler");

        // Fallback 1x1 RGBA8Unorm textures
        struct FallbackSpec { const char* label; uint8_t rgba[4]; };
        FallbackSpec specs[4] = {
            {"Render3D Fallback Albedo",   {255, 255, 255, 255}},  // white
            {"Render3D Fallback Normal",   {128, 128, 255, 255}},  // flat identity
            {"Render3D Fallback R/M",      {255,   0,   0,   0}},  // roughness=1, metallic=0
            {"Render3D Fallback Emission", {  0,   0,   0,   0}},  // black
        };
        for (int i = 0; i < 4; ++i) {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv(specs[i].label);
            td.size = {1, 1, 1};
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_RGBA8Unorm;
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            fallback_textures_[i] = wgpuDeviceCreateTexture(ctx->device, &td);

            WGPUTexelCopyTextureInfo dst{};
            dst.texture = fallback_textures_[i];
            dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout layout_info{};
            layout_info.bytesPerRow = 4;
            layout_info.rowsPerImage = 1;
            WGPUExtent3D extent = {1, 1, 1};
            wgpuQueueWriteTexture(ctx->queue, &dst, specs[i].rgba, 4, &layout_info, &extent);

            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv(specs[i].label);
            vd.format = WGPUTextureFormat_RGBA8Unorm;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            fallback_views_[i] = wgpuTextureCreateView(fallback_textures_[i], &vd);
        }

        // Fallback textured bind group
        WGPUBindGroupEntry tex_bg_entries[5]{};
        tex_bg_entries[0].binding = 0;
        tex_bg_entries[0].sampler = pbr_sampler_;
        for (int i = 0; i < 4; ++i) {
            tex_bg_entries[i + 1].binding = static_cast<uint32_t>(i + 1);
            tex_bg_entries[i + 1].textureView = fallback_views_[i];
        }
        WGPUBindGroupDescriptor fb_bg_desc{};
        fb_bg_desc.label = vivid_sv("Render3D Fallback Tex BG");
        fb_bg_desc.layout = tex_bind_layout_;
        fb_bg_desc.entryCount = 5;
        fb_bg_desc.entries = tex_bg_entries;
        fallback_tex_bg_ = wgpuDeviceCreateBindGroup(ctx->device, &fb_bg_desc);

        // ---- Bind group 0 (6 entries: camera, material, lights, shadow, sampler, depth tex) ----
        WGPUBindGroupEntry bg_entries[6]{};
        bg_entries[0].binding = 0;
        bg_entries[0].buffer  = camera_ubo_;
        bg_entries[0].offset  = 0;
        bg_entries[0].size    = sizeof(CameraUniform);

        bg_entries[1].binding = 1;
        bg_entries[1].buffer  = material_ubo_;
        bg_entries[1].offset  = 0;
        bg_entries[1].size    = sizeof(MaterialUniform);

        bg_entries[2].binding = 2;
        bg_entries[2].buffer  = lights_ubo_;
        bg_entries[2].offset  = 0;
        bg_entries[2].size    = sizeof(LightsUniform);

        bg_entries[3].binding = 3;
        bg_entries[3].buffer  = shadow_ubo_;
        bg_entries[3].offset  = 0;
        bg_entries[3].size    = sizeof(ShadowUniform);

        bg_entries[4].binding = 4;
        bg_entries[4].sampler = shadow_sampler_;

        bg_entries[5].binding = 5;
        bg_entries[5].textureView = fallback_shadow_view_;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Render3D Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 6;
        bg_desc.entries = bg_entries;
        bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);

        // ---- Record output format for pipeline cache ----
        cached_format_ = ctx->output_format;

        // ---- Initial depth buffer (with TextureBinding for depth blit) ----
        depth_tex_ = vivid::gpu::create_shadow_map_texture(
            ctx->device, ctx->output_width, ctx->output_height, "Render3D Depth");
        depth_view_ = vivid::gpu::create_depth_view(depth_tex_, "Render3D Depth View");
        cached_w_ = ctx->output_width;
        cached_h_ = ctx->output_height;

        // ---- Phase 6e: depth blit pipeline (Depth32Float → R32Float) ----
        {
            static const char* kDepthBlitShader = R"(
struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

fn fullscreenTriangle(vertexIndex: u32) -> FullscreenOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

@group(0) @binding(0) var depth_tex: texture_depth_2d;
@group(0) @binding(1) var depth_samp: sampler;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> FullscreenOutput {
    return fullscreenTriangle(vertexIndex);
}

@fragment
fn fs_depth_blit(in: FullscreenOutput) -> @location(0) f32 {
    let dims = textureDimensions(depth_tex);
    let px = vec2i(in.uv * vec2f(f32(dims.x), f32(dims.y)));
    return textureLoad(depth_tex, clamp(px, vec2i(0), vec2i(dims) - vec2i(1)), 0);
}
)";
            depth_blit_shader_ = vivid::gpu::create_wgsl_shader(
                ctx->device, kDepthBlitShader, "Render3D Depth Blit Shader");
            if (!depth_blit_shader_) return false;

            // Nearest sampler (non-comparison)
            WGPUSamplerDescriptor sd{};
            sd.label = vivid_sv("Render3D Depth Blit Sampler");
            sd.addressModeU = WGPUAddressMode_ClampToEdge;
            sd.addressModeV = WGPUAddressMode_ClampToEdge;
            sd.addressModeW = WGPUAddressMode_ClampToEdge;
            sd.magFilter = WGPUFilterMode_Nearest;
            sd.minFilter = WGPUFilterMode_Nearest;
            sd.maxAnisotropy = 1;
            depth_blit_sampler_ = wgpuDeviceCreateSampler(ctx->device, &sd);

            // Bind group layout: depth_tex(0) + sampler(1)
            WGPUBindGroupLayoutEntry blit_entries[2]{};
            blit_entries[0].binding = 0;
            blit_entries[0].visibility = WGPUShaderStage_Fragment;
            blit_entries[0].texture.sampleType = WGPUTextureSampleType_Depth;
            blit_entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

            blit_entries[1].binding = 1;
            blit_entries[1].visibility = WGPUShaderStage_Fragment;
            blit_entries[1].sampler.type = WGPUSamplerBindingType_NonFiltering;

            WGPUBindGroupLayoutDescriptor blit_bgl_desc{};
            blit_bgl_desc.label = vivid_sv("Render3D Depth Blit BGL");
            blit_bgl_desc.entryCount = 2;
            blit_bgl_desc.entries = blit_entries;
            depth_blit_bgl_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &blit_bgl_desc);

            // Pipeline layout
            WGPUPipelineLayoutDescriptor blit_pl_desc{};
            blit_pl_desc.label = vivid_sv("Render3D Depth Blit Pipeline Layout");
            blit_pl_desc.bindGroupLayoutCount = 1;
            blit_pl_desc.bindGroupLayouts = &depth_blit_bgl_;
            depth_blit_pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &blit_pl_desc);

            // Pipeline: R32Float target, no blend, no depth stencil
            WGPUColorTargetState blit_ct{};
            blit_ct.format = WGPUTextureFormat_R32Float;
            blit_ct.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState blit_frag{};
            blit_frag.module = depth_blit_shader_;
            blit_frag.entryPoint = vivid_sv("fs_depth_blit");
            blit_frag.targetCount = 1;
            blit_frag.targets = &blit_ct;

            WGPURenderPipelineDescriptor blit_rp{};
            blit_rp.label = vivid_sv("Render3D Depth Blit Pipeline");
            blit_rp.layout = depth_blit_pipe_layout_;
            blit_rp.vertex.module = depth_blit_shader_;
            blit_rp.vertex.entryPoint = vivid_sv("vs_main");
            blit_rp.vertex.bufferCount = 0;
            blit_rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            blit_rp.primitive.frontFace = WGPUFrontFace_CCW;
            blit_rp.primitive.cullMode = WGPUCullMode_None;
            blit_rp.multisample.count = 1;
            blit_rp.multisample.mask = 0xFFFFFFFF;
            blit_rp.fragment = &blit_frag;

            depth_blit_pipeline_ = wgpuDeviceCreateRenderPipeline(ctx->device, &blit_rp);
            if (!depth_blit_pipeline_) return false;
        }

        // ---- Phase 6e: initial R32Float depth output texture ----
        {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv("Render3D Depth Out R32F");
            td.size = { ctx->output_width, ctx->output_height, 1 };
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_R32Float;
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
                     | WGPUTextureUsage_CopySrc;
            depth_out_tex_ = wgpuDeviceCreateTexture(ctx->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("Render3D Depth Out R32F View");
            vd.format = WGPUTextureFormat_R32Float;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            depth_out_view_ = wgpuTextureCreateView(depth_out_tex_, &vd);
        }
        depth_blit_bg_dirty_ = true;

        // ---- Phase 6f: IBL bind group layout (group 2) ----
        {
            WGPUBindGroupLayoutEntry ibl_entries[5]{};
            // binding 0: sampler
            ibl_entries[0].binding = 0;
            ibl_entries[0].visibility = WGPUShaderStage_Fragment;
            ibl_entries[0].sampler.type = WGPUSamplerBindingType_Filtering;
            // binding 1: irradiance cubemap
            ibl_entries[1].binding = 1;
            ibl_entries[1].visibility = WGPUShaderStage_Fragment;
            ibl_entries[1].texture.sampleType = WGPUTextureSampleType_Float;
            ibl_entries[1].texture.viewDimension = WGPUTextureViewDimension_Cube;
            // binding 2: prefiltered cubemap
            ibl_entries[2].binding = 2;
            ibl_entries[2].visibility = WGPUShaderStage_Fragment;
            ibl_entries[2].texture.sampleType = WGPUTextureSampleType_Float;
            ibl_entries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;
            // binding 3: BRDF LUT
            ibl_entries[3].binding = 3;
            ibl_entries[3].visibility = WGPUShaderStage_Fragment;
            ibl_entries[3].texture.sampleType = WGPUTextureSampleType_Float;
            ibl_entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
            // binding 4: IBL params UBO
            ibl_entries[4].binding = 4;
            ibl_entries[4].visibility = WGPUShaderStage_Fragment;
            ibl_entries[4].buffer.type = WGPUBufferBindingType_Uniform;
            ibl_entries[4].buffer.minBindingSize = sizeof(IBLUniform);

            WGPUBindGroupLayoutDescriptor ibl_bgl_desc{};
            ibl_bgl_desc.label = vivid_sv("Render3D IBL BGL");
            ibl_bgl_desc.entryCount = 5;
            ibl_bgl_desc.entries = ibl_entries;
            ibl_bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &ibl_bgl_desc);
        }

        // ---- Phase 6f: IBL UBO ----
        ibl_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, sizeof(IBLUniform), "Render3D IBL UBO");

        // ---- Phase 6f: IBL sampler (linear, clamp) ----
        ibl_sampler_ = vivid::gpu::create_clamp_linear_sampler(
            ctx->device, "Render3D IBL Sampler");

        // ---- Phase 6f: fallback IBL cubemap (1x1x6 black RGBA16Float) ----
        {
            fallback_ibl_cube_tex_ = vivid::gpu::create_cubemap_texture(
                ctx->device, 1, 1, WGPUTextureFormat_RGBA16Float, "Render3D Fallback IBL Cube");
            fallback_ibl_cube_view_ = vivid::gpu::create_cubemap_view(
                fallback_ibl_cube_tex_, WGPUTextureFormat_RGBA16Float, 1,
                "Render3D Fallback IBL Cube View");

            // Write black to each face
            uint16_t black_rgba16[4] = { 0, 0, 0, 0x3C00 }; // 0,0,0,1 in half-float
            for (uint32_t face = 0; face < 6; ++face) {
                WGPUTexelCopyTextureInfo dst{};
                dst.texture = fallback_ibl_cube_tex_;
                dst.origin = { 0, 0, face };
                dst.aspect = WGPUTextureAspect_All;
                WGPUTexelCopyBufferLayout bl{};
                bl.bytesPerRow = 8;
                bl.rowsPerImage = 1;
                WGPUExtent3D ext = { 1, 1, 1 };
                wgpuQueueWriteTexture(ctx->queue, &dst, black_rgba16, 8, &bl, &ext);
            }
        }

        // ---- Phase 6f: fallback BRDF LUT (1x1 RG16Float) ----
        {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv("Render3D Fallback BRDF LUT");
            td.size = { 1, 1, 1 };
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_RG16Float;
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            fallback_ibl_lut_tex_ = wgpuDeviceCreateTexture(ctx->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("Render3D Fallback BRDF LUT View");
            vd.format = WGPUTextureFormat_RG16Float;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            fallback_ibl_lut_view_ = wgpuTextureCreateView(fallback_ibl_lut_tex_, &vd);

            uint16_t zero_rg16[2] = { 0, 0 };
            WGPUTexelCopyTextureInfo dst{};
            dst.texture = fallback_ibl_lut_tex_;
            dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout bl{};
            bl.bytesPerRow = 4;
            bl.rowsPerImage = 1;
            WGPUExtent3D ext = { 1, 1, 1 };
            wgpuQueueWriteTexture(ctx->queue, &dst, zero_rg16, 4, &bl, &ext);
        }

        // ---- Phase 6f: fallback IBL bind group (has_environment=0) ----
        {
            IBLUniform ibl_zero{};
            ibl_zero.has_environment = 0.0f;
            wgpuQueueWriteBuffer(ctx->queue, ibl_ubo_, 0, &ibl_zero, sizeof(ibl_zero));

            WGPUBindGroupEntry entries[5]{};
            entries[0].binding = 0;
            entries[0].sampler = ibl_sampler_;
            entries[1].binding = 1;
            entries[1].textureView = fallback_ibl_cube_view_;
            entries[2].binding = 2;
            entries[2].textureView = fallback_ibl_cube_view_;
            entries[3].binding = 3;
            entries[3].textureView = fallback_ibl_lut_view_;
            entries[4].binding = 4;
            entries[4].buffer = ibl_ubo_;
            entries[4].offset = 0;
            entries[4].size = sizeof(IBLUniform);

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = vivid_sv("Render3D Fallback IBL BG");
            bg_desc.layout = ibl_bind_layout_;
            bg_desc.entryCount = 5;
            bg_desc.entries = entries;
            fallback_ibl_bg_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);
        }

        // ---- Phase 6f: IBL pipeline layouts ----
        // Non-textured IBL needs an empty group 1 placeholder so @group(2) maps correctly
        {
            WGPUBindGroupLayoutDescriptor empty_desc{};
            empty_desc.label = vivid_sv("Render3D Empty BGL (IBL gap)");
            empty_desc.entryCount = 0;
            WGPUBindGroupLayout empty_bgl = wgpuDeviceCreateBindGroupLayout(ctx->device, &empty_desc);

            // ibl_pipe_layout_: group 0 + empty group 1 + group 2
            WGPUBindGroupLayout ibl_layouts[3] = { bind_layout_, empty_bgl, ibl_bind_layout_ };
            WGPUPipelineLayoutDescriptor desc{};
            desc.label = vivid_sv("Render3D IBL Pipeline Layout");
            desc.bindGroupLayoutCount = 3;
            desc.bindGroupLayouts = ibl_layouts;
            ibl_pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &desc);

            // Create empty bind group for the placeholder group 1
            WGPUBindGroupDescriptor ebg_desc{};
            ebg_desc.label = vivid_sv("Render3D Empty IBL Gap BG");
            ebg_desc.layout = empty_bgl;
            ebg_desc.entryCount = 0;
            ibl_empty_bg_ = wgpuDeviceCreateBindGroup(ctx->device, &ebg_desc);

            ibl_empty_bgl_ = empty_bgl;
        }
        {
            // tex_ibl_pipe_layout_: group 0 + group 1 + group 2
            WGPUBindGroupLayout tex_ibl_layouts[3] = { bind_layout_, tex_bind_layout_, ibl_bind_layout_ };
            WGPUPipelineLayoutDescriptor desc{};
            desc.label = vivid_sv("Render3D Textured IBL Pipeline Layout");
            desc.bindGroupLayoutCount = 3;
            desc.bindGroupLayouts = tex_ibl_layouts;
            tex_ibl_pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &desc);
        }

        // ---- Phase 6f: IBL shader modules ----
        {
            std::string src = std::string(vivid::gpu::VERTEX_3D_WGSL)
                            + std::string(vivid::gpu::LIGHTS_3D_WGSL)
                            + std::string(vivid::gpu::SHADOW_3D_WGSL)
                            + std::string(kIBLBindingsWGSL)
                            + kRender3DFragmentIBL;
            ibl_shader_ = vivid::gpu::create_shader(ctx->device, src.c_str(),
                                                      "Render3D IBL Shader");
            if (!ibl_shader_) return false;
        }
        {
            std::string src = std::string(vivid::gpu::VERTEX_3D_WGSL)
                            + std::string(vivid::gpu::LIGHTS_3D_WGSL)
                            + std::string(vivid::gpu::SHADOW_3D_WGSL)
                            + std::string(vivid::gpu::PBR_BRDF_WGSL)
                            + std::string(kIBLBindingsWGSL)
                            + kRender3DTexturedIBL;
            textured_ibl_shader_ = vivid::gpu::create_shader(ctx->device, src.c_str(),
                                                               "Render3D Textured IBL Shader");
            if (!textured_ibl_shader_) return false;
        }

        // ---- Phase 6f: skybox ----
        if (!init_skybox(ctx)) return false;

        return true;
    }
};

VIVID_REGISTER(Render3D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividSceneFragment)
