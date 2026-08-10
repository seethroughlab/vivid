#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/thumbnail_3d.h"
#include "linmath.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

// =============================================================================
// SDF3D — Raymarched Signed Distance Fields
// =============================================================================

// ---------------------------------------------------------------------------
// SDF params uniform (must be 192 bytes)
// ---------------------------------------------------------------------------

struct SDFParamsUniform {
    float shape_a_type;      // 0=sphere, 1=box, 2=torus, 3=cylinder, 4=cone
    float shape_b_type;      // -1=none, 0-4=same as above
    float operation;         // 0=none, 1=smooth_union, 2=smooth_subtract, 3=smooth_intersect
    float smooth_k;
    float size_a[4];         // xyz + pad
    float size_b[4];         // xyz + pad
    float pos_b[4];          // xyz + pad
    float color[4];          // rgba
    float roughness;
    float metallic;
    float emission;
    float flags;             // >0.5 = unlit
    float inv_model[16];     // inverse model matrix (world -> SDF local space)
    float max_steps;
    float surface_threshold;
    float instance_count;    // ADR-0053 demo: # of note-metaball spheres smooth-unioned into the field
    float instance_k;        // smooth-union blend radius for those instance spheres
    float shadow;            // soft SELF-shadow strength (0 = off); marches a shadow ray to light 0
    float _pad2[3];
};
static_assert(sizeof(SDFParamsUniform) == 192, "SDFParamsUniform must be 192 bytes");

// Note-metaball instance spheres: up to 32 packed as vec4 (xyz = LOCAL-space centre, w = radius). Fed
// from an InstanceArray3D input (e.g. a Notes signal), smooth-unioned into the SDF so the points MERGE
// into one gooey field instead of drawing as separate instances.
static constexpr uint32_t kMaxSDFInstances = 32;

// ---------------------------------------------------------------------------
// Lights uniform — ADR-0051 Phase 3: the shared vivid::gpu::LightsUniform, the CPU mirror of the
// LIGHTS_3D_WGSL preamble this shader now includes. The private copy that used to live here
// claimed to match Render3D's "exactly" but had drifted to 208 bytes (no spot_params), which is
// half of why a Light3D never reached SDF geometry.
// ---------------------------------------------------------------------------

using SDFLightsUniform = vivid::gpu::LightsUniform;

// ---------------------------------------------------------------------------
// WGSL shader
// ---------------------------------------------------------------------------

static const char* kSDF3DShader = R"(
struct SDFParams {
    shape_a_type:      f32,
    shape_b_type:      f32,
    operation:         f32,
    smooth_k:          f32,
    size_a:            vec4f,
    size_b:            vec4f,
    pos_b:             vec4f,
    color:             vec4f,
    roughness:         f32,
    metallic:          f32,
    emission:          f32,
    flags:             f32,
    inv_model:         mat4x4f,
    max_steps:         f32,
    surface_threshold: f32,
    instance_count:    f32,
    instance_k:        f32,
    shadow:            f32,
    _p2:               f32,
    _p3:               f32,
    _p4:               f32,
}

// Light / LightsUniform come from the shared LIGHTS_3D_WGSL preamble (ADR-0051 P3) — this shader
// used to declare its own, three-field version that could not represent a spot cone.

@group(0) @binding(0) var<uniform> camera: CustomCamera3D;
@group(0) @binding(1) var<uniform> params: SDFParams;
@group(0) @binding(2) var<uniform> lighting: LightsUniform;
struct MetaInstance {
    pos_r: vec4f,   // xyz = local centre, w = radius
    color: vec4f,   // per-instance rgba (from the InstanceArray3D — composable, not note-specific)
}
@group(0) @binding(3) var<storage, read> instances: array<MetaInstance, 32>;

struct SDFVertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@location(0) pos: vec3f,
           @location(1) normal: vec3f,
           @location(2) tangent: vec4f,
           @location(3) uv: vec2f) -> SDFVertexOutput {
    var out: SDFVertexOutput;
    out.position = vec4f(pos.xy, 0.5, 1.0);
    out.uv = pos.xy * 0.5 + 0.5;
    return out;
}

// --- SDF primitives ---

fn sdf_sphere(p: vec3f, r: f32) -> f32 {
    return length(p) - r;
}

fn sdf_box(p: vec3f, b: vec3f) -> f32 {
    let q = abs(p) - b;
    return length(max(q, vec3f(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
}

fn sdf_torus(p: vec3f, t: vec2f) -> f32 {
    let q = vec2f(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

fn sdf_cylinder(p: vec3f, r: f32, h: f32) -> f32 {
    let d = vec2f(length(p.xz) - r, abs(p.y) - h);
    return min(max(d.x, d.y), 0.0) + length(max(d, vec2f(0.0)));
}

fn sdf_cone(p: vec3f, r: f32, h: f32) -> f32 {
    let q = vec2f(length(p.xz), p.y);
    let tip = vec2f(0.0, h);
    let base = vec2f(r, -h);
    let ab = base - tip;
    let aq = q - tip;
    let t = clamp(dot(aq, ab) / dot(ab, ab), 0.0, 1.0);
    let nearest = tip + ab * t;
    var d = length(q - nearest);
    // Sign: inside if right of the edge
    if (ab.x * aq.y - ab.y * aq.x > 0.0) { d = -d; }
    // Cap at bottom
    let cap_d = q.y + h;
    if (cap_d < 0.0 && cap_d < -d) { d = -cap_d; }
    let cap_r = length(p.xz) - r * clamp((-p.y + h) / (2.0 * h), 0.0, 1.0);
    if (q.y < -h) { d = max(d, -cap_d); }
    return d;
}

fn eval_shape(shape_type: f32, p: vec3f, sz: vec3f) -> f32 {
    let t = i32(shape_type + 0.5);
    switch (t) {
        case 1: { return sdf_box(p, sz * 0.5); }
        case 2: { return sdf_torus(p, vec2f(sz.x * 0.5, sz.y * 0.25)); }
        case 3: { return sdf_cylinder(p, sz.x * 0.5, sz.y * 0.5); }
        case 4: { return sdf_cone(p, sz.x * 0.5, sz.y * 0.5); }
        default: { return sdf_sphere(p, sz.x * 0.5); }
    }
}

// --- CSG combinators (Inigo Quilez) ---

fn smooth_union(d1: f32, d2: f32, k: f32) -> f32 {
    let h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}

fn smooth_subtract(d1: f32, d2: f32, k: f32) -> f32 {
    let h = clamp(0.5 - 0.5 * (d2 + d1) / k, 0.0, 1.0);
    return mix(d1, -d2, h) + k * h * (1.0 - h);
}

fn smooth_intersect(d1: f32, d2: f32, k: f32) -> f32 {
    let h = clamp(0.5 - 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) + k * h * (1.0 - h);
}

fn scene_sdf(p: vec3f) -> f32 {
    // Base A (+ optional B via a CSG op). shape_a_type < -0.5 = no base shape → a pure instance field.
    var d: f32 = 1e9;
    if (params.shape_a_type > -0.5) {
        let d_a = eval_shape(params.shape_a_type, p, params.size_a.xyz);
        if (params.shape_b_type < -0.5) {
            d = d_a;
        } else {
            let p_b = p - params.pos_b.xyz;
            let d_b = eval_shape(params.shape_b_type, p_b, params.size_b.xyz);
            let op = i32(params.operation + 0.5);
            switch (op) {
                case 1: { d = smooth_union(d_a, d_b, params.smooth_k); }
                case 2: { d = smooth_subtract(d_a, d_b, params.smooth_k); }
                case 3: { d = smooth_intersect(d_a, d_b, params.smooth_k); }
                default: { d = d_a; }
            }
        }
    }
    // Note-metaball instances: smooth-union a sphere per active element, so the points MERGE (gooey necks
    // form where they cluster) instead of drawing separately. First union against d=1e9 returns the sphere.
    let n = i32(params.instance_count + 0.5);
    let k = max(params.instance_k, 0.001);
    for (var i: i32 = 0; i < n; i++) {
        let s = instances[i].pos_r;
        d = smooth_union(d, sdf_sphere(p - s.xyz, s.w), k);
    }
    return d;
}

// Blended surface COLOUR at p: replays the same smooth-union order as scene_sdf, mixing each instance's
// own colour by the same blend weight — so where two coloured metaballs merge, their colours melt too.
// Composable: the colours come from the InstanceArray3D, so this is per-INSTANCE, not note-specific.
fn scene_color(p: vec3f) -> vec3f {
    var d: f32 = 1e9;
    var col: vec3f = params.color.rgb;
    if (params.shape_a_type > -0.5) {
        let d_a = eval_shape(params.shape_a_type, p, params.size_a.xyz);
        if (params.shape_b_type < -0.5) {
            d = d_a;
        } else {
            let p_b = p - params.pos_b.xyz;
            let d_b = eval_shape(params.shape_b_type, p_b, params.size_b.xyz);
            let op = i32(params.operation + 0.5);
            switch (op) {
                case 1: { d = smooth_union(d_a, d_b, params.smooth_k); }
                case 2: { d = smooth_subtract(d_a, d_b, params.smooth_k); }
                case 3: { d = smooth_intersect(d_a, d_b, params.smooth_k); }
                default: { d = d_a; }
            }
        }
    }
    let n = i32(params.instance_count + 0.5);
    let k = max(params.instance_k, 0.001);
    for (var i: i32 = 0; i < n; i++) {
        let s  = instances[i].pos_r;
        let ic = instances[i].color.rgb;
        let ds = sdf_sphere(p - s.xyz, s.w);
        let h  = clamp(0.5 + 0.5 * (ds - d) / k, 0.0, 1.0);   // == smooth_union(d, ds, k)'s weight
        col = mix(ic, col, h);                                // h=1 → keep accumulated, h=0 → this instance
        d   = mix(ds, d, h) - k * h * (1.0 - h);
    }
    return col;
}

fn calc_normal(p: vec3f) -> vec3f {
    let e = vec2f(0.001, 0.0);
    return normalize(vec3f(
        scene_sdf(p + e.xyy) - scene_sdf(p - e.xyy),
        scene_sdf(p + e.yxy) - scene_sdf(p - e.yxy),
        scene_sdf(p + e.yyx) - scene_sdf(p - e.yyx),
    ));
}

// Soft SELF-shadow (Inigo Quilez): march from a surface point toward the light; the closer the ray
// grazes other geometry, the darker the point. All in the SDF's own field — the metaball lobes / merged
// notes shadow each other, which the shadow-MAP path can't do for a raymarched custom pipeline. `rd` is
// the toward-light direction in LOCAL space; `k` sets penumbra hardness.
fn soft_shadow(ro: vec3f, rd: vec3f, k: f32) -> f32 {
    var res: f32 = 1.0;
    var t: f32 = 0.04;
    for (var i: i32 = 0; i < 20; i++) {
        let h = scene_sdf(ro + rd * t);
        if (h < 0.001) { return 0.0; }
        res = min(res, k * h / t);
        t += clamp(h, 0.03, 0.5);
        if (t > 12.0) { break; }
    }
    return clamp(res, 0.0, 1.0);
}

struct SDFFragOutput {
    @location(0) color: vec4f,
    @builtin(frag_depth) depth: f32,
}

@fragment
fn fs_main(in: SDFVertexOutput) -> SDFFragOutput {
    // Reconstruct clip-space position from UV
    let ndc = vec4f(in.uv * 2.0 - 1.0, 0.0, 1.0);
    let ndc_far = vec4f(in.uv * 2.0 - 1.0, 1.0, 1.0);

    let world_near = camera.inverse_vp * ndc;
    let world_far  = camera.inverse_vp * ndc_far;

    let ray_origin = world_near.xyz / world_near.w;
    let ray_end    = world_far.xyz / world_far.w;
    let ray_dir    = normalize(ray_end - ray_origin);

    // Transform ray into SDF local space
    let local_origin = (params.inv_model * vec4f(ray_origin, 1.0)).xyz;
    let local_dir    = normalize((params.inv_model * vec4f(ray_dir, 0.0)).xyz);

    // Raymarch
    let max_steps = i32(params.max_steps + 0.5);
    let threshold = params.surface_threshold;
    let max_dist = camera.far_plane;

    var t: f32 = 0.0;
    var hit = false;
    for (var i: i32 = 0; i < max_steps; i++) {
        let p = local_origin + local_dir * t;
        let d = scene_sdf(p);
        if (d < threshold) {
            hit = true;
            break;
        }
        t += d;
        if (t > max_dist) { break; }
    }

    if (!hit) {
        discard;
    }

    let local_hit = local_origin + local_dir * t;

    // Transform hit point back to world space for depth + lighting
    // We need the inverse of inv_model, which is the original model matrix
    // For now, compute world hit from ray_origin + ray_dir * t_world
    // t_world needs scaling — compute world hit directly
    let world_hit_h = params.inv_model * vec4f(local_hit, 1.0);
    // Actually, we need model * local_hit. inv_model goes world->local,
    // so we need to go local->world. We reconstruct from the world-space ray:
    // Find t_world such that |world_origin + world_dir * t_world| projects to local_hit
    // Simpler: project local_hit along world ray
    let world_hit_dir = ray_origin + ray_dir * dot(local_hit - local_origin, local_dir) / length((params.inv_model * vec4f(ray_dir, 0.0)).xyz) * length(ray_dir);
    // ^ This is fragile. Better approach: reconstruct via the VP matrix from world ray.
    // Since local_dir may be scaled by inv_model, we track world-space t:
    let scale_factor = length((params.inv_model * vec4f(ray_dir, 0.0)).xyz);
    let t_world = t / scale_factor;
    let world_hit = ray_origin + ray_dir * t_world;

    // Compute frag_depth via VP projection
    let clip = camera.vp * vec4f(world_hit, 1.0);
    var depth = clip.z / clip.w;
    depth = clamp(depth, 0.0, 1.0);

    // Normal in local space, transform to world
    let local_normal = calc_normal(local_hit);
    // Normal transform: transpose(inv_model) * n = model^T * n
    // For uniform scale, just normalize(inv_model^T * n)
    let world_normal = normalize((vec4f(local_normal, 0.0) * params.inv_model).xyz);

    // Shading — when instances contribute, the base colour is the per-instance blend at the hit point
    // (so merged metaballs blend colour), else the op's own colour.
    var base_color = params.color.rgb;
    if (params.instance_count > 0.5) {
        base_color = scene_color(local_hit);
    }
    let alpha = params.color.a;

    var out: SDFFragOutput;

    if (params.flags > 0.5) {
        // Unlit
        out.color = vec4f(base_color * (1.0 + params.emission), alpha);
        out.depth = depth;
        return out;
    }

    let N = world_normal;
    let V = normalize(camera.camera_pos - world_hit);
    let ambient = vec3f(lighting.ambient_r, lighting.ambient_g, lighting.ambient_b);

    let diffuse_color  = base_color * (1.0 - params.metallic);
    let specular_color = mix(vec3f(0.04), base_color, params.metallic);

    let shininess = pow(2.0, (1.0 - params.roughness) * 7.0) + 2.0;

    var color = diffuse_color * ambient;
    for (var i: u32 = 0u; i < min(lighting.light_count, 4u); i++) {
        let light = lighting.lights[i];
        let light_color = light.color_and_radius.xyz;

        var L: vec3f;
        var attenuation: f32 = 1.0;
        if (light.position_and_type.w < 0.5) {
            L = normalize(light.direction_and_intensity.xyz);
        } else {
            let to_light = light.position_and_type.xyz - world_hit;
            let dist = length(to_light);
            L = to_light / max(dist, 0.001);
            let radius = light.color_and_radius.w;
            let ratio = dist / max(radius, 0.001);
            attenuation = saturate(1.0 - ratio * ratio);
        }

        let H = normalize(L + V);
        let intensity = light.direction_and_intensity.w;
        let diffuse  = max(dot(N, L), 0.0);
        let specular = pow(max(dot(N, H), 0.0), shininess);

        // Soft self-shadow from the KEY (light 0) only — one march per pixel, the big depth win; fill /
        // rim lights deliberately don't cast (they lift shadows). Marched in local space toward the light.
        var sh: f32 = 1.0;
        if (i == 0u && params.shadow > 0.001) {
            let L_local = normalize((params.inv_model * vec4f(L, 0.0)).xyz);
            let shade = soft_shadow(local_hit + local_normal * 0.04, L_local, 12.0);
            sh = mix(1.0, shade, params.shadow);
        }
        color += light_color * (diffuse_color * diffuse + specular_color * specular)
                 * intensity * attenuation * sh;
    }

    color += base_color * params.emission;

    out.color = vec4f(color, alpha);
    out.depth = depth;
    return out;
}
)";

// =============================================================================
// Thumbnail proxy meshes — position-only low-poly primitives that match the
// five shape_a options. Rendered by render_mesh() with pseudo-normal shading.
// The thumbnail is small (~140x88 px) so detail is intentionally low.
// =============================================================================

namespace {

struct ProxyVert { float pos[3]; };

struct ProxyGeom {
    std::vector<ProxyVert> verts;
    std::vector<uint32_t>  indices;
    float bmin[3];
    float bmax[3];
};

static void finalize_bounds(ProxyGeom& g) {
    g.bmin[0] = g.bmin[1] = g.bmin[2] =  1e9f;
    g.bmax[0] = g.bmax[1] = g.bmax[2] = -1e9f;
    for (const auto& v : g.verts) {
        for (int i = 0; i < 3; ++i) {
            if (v.pos[i] < g.bmin[i]) g.bmin[i] = v.pos[i];
            if (v.pos[i] > g.bmax[i]) g.bmax[i] = v.pos[i];
        }
    }
}

// Unit cube, extents [-0.5, 0.5]. CCW-from-outside winding.
static void build_cube_proxy(ProxyGeom& g) {
    g.verts = {
        {{-0.5f,-0.5f,-0.5f}}, {{ 0.5f,-0.5f,-0.5f}},
        {{ 0.5f, 0.5f,-0.5f}}, {{-0.5f, 0.5f,-0.5f}},
        {{-0.5f,-0.5f, 0.5f}}, {{ 0.5f,-0.5f, 0.5f}},
        {{ 0.5f, 0.5f, 0.5f}}, {{-0.5f, 0.5f, 0.5f}},
    };
    g.indices = {
        0,2,1, 0,3,2,   // -Z
        4,5,6, 4,6,7,   // +Z
        0,4,7, 0,7,3,   // -X
        1,2,6, 1,6,5,   // +X
        0,1,5, 0,5,4,   // -Y
        3,7,6, 3,6,2,   // +Y
    };
    finalize_bounds(g);
}

// UV sphere, radius 0.5, 6 stacks x 8 slices.
static void build_sphere_proxy(ProxyGeom& g) {
    const int stacks = 6, slices = 8;
    const float r = 0.5f;
    const float kPi = 3.14159265358979323846f;
    const float kTau = 6.28318530717958647692f;
    for (int i = 0; i <= stacks; ++i) {
        float phi = kPi * static_cast<float>(i) / static_cast<float>(stacks);
        float sp = std::sin(phi), cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j) {
            float th = kTau * static_cast<float>(j) / static_cast<float>(slices);
            float st = std::sin(th), ct = std::cos(th);
            g.verts.push_back({{ r * sp * ct, r * cp, r * sp * st }});
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j);
            uint32_t b = a + 1;
            uint32_t c = static_cast<uint32_t>((i + 1) * (slices + 1) + j);
            uint32_t d = c + 1;
            g.indices.push_back(a); g.indices.push_back(c); g.indices.push_back(d);
            g.indices.push_back(a); g.indices.push_back(d); g.indices.push_back(b);
        }
    }
    finalize_bounds(g);
}

// Torus, major R=0.35, minor r=0.15, 10 rings x 6 sides.
static void build_torus_proxy(ProxyGeom& g) {
    const int rings = 10, sides = 6;
    const float R = 0.35f, r = 0.15f;
    const float kTau = 6.28318530717958647692f;
    for (int i = 0; i <= rings; ++i) {
        float u = kTau * static_cast<float>(i) / static_cast<float>(rings);
        float cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= sides; ++j) {
            float v = kTau * static_cast<float>(j) / static_cast<float>(sides);
            float cv = std::cos(v), sv = std::sin(v);
            g.verts.push_back({{
                (R + r * cv) * cu,
                r * sv,
                (R + r * cv) * su,
            }});
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sides; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (sides + 1) + j);
            uint32_t b = a + 1;
            uint32_t c = static_cast<uint32_t>((i + 1) * (sides + 1) + j);
            uint32_t d = c + 1;
            g.indices.push_back(a); g.indices.push_back(b); g.indices.push_back(c);
            g.indices.push_back(b); g.indices.push_back(d); g.indices.push_back(c);
        }
    }
    finalize_bounds(g);
}

// Cylinder, radius 0.5, height 1.0, 8 slices, body + 2 caps.
static void build_cylinder_proxy(ProxyGeom& g) {
    const int slices = 8;
    const float r = 0.5f, h = 0.5f;
    const float kTau = 6.28318530717958647692f;
    // Body ring: bottom then top, wrap-duplicated.
    for (int i = 0; i <= 1; ++i) {
        float y = (i == 0) ? -h : h;
        for (int j = 0; j <= slices; ++j) {
            float th = kTau * static_cast<float>(j) / static_cast<float>(slices);
            g.verts.push_back({{ r * std::cos(th), y, r * std::sin(th) }});
        }
    }
    for (int j = 0; j < slices; ++j) {
        uint32_t a = static_cast<uint32_t>(j);
        uint32_t b = a + 1;
        uint32_t c = static_cast<uint32_t>(slices + 1 + j);
        uint32_t d = c + 1;
        g.indices.push_back(a); g.indices.push_back(c); g.indices.push_back(d);
        g.indices.push_back(a); g.indices.push_back(d); g.indices.push_back(b);
    }
    // Top cap
    uint32_t top_center = static_cast<uint32_t>(g.verts.size());
    g.verts.push_back({{ 0.0f, h, 0.0f }});
    uint32_t top_ring = static_cast<uint32_t>(g.verts.size());
    for (int j = 0; j <= slices; ++j) {
        float th = kTau * static_cast<float>(j) / static_cast<float>(slices);
        g.verts.push_back({{ r * std::cos(th), h, r * std::sin(th) }});
    }
    for (int j = 0; j < slices; ++j) {
        g.indices.push_back(top_center);
        g.indices.push_back(top_ring + static_cast<uint32_t>(j));
        g.indices.push_back(top_ring + static_cast<uint32_t>(j + 1));
    }
    // Bottom cap (reversed winding)
    uint32_t bot_center = static_cast<uint32_t>(g.verts.size());
    g.verts.push_back({{ 0.0f, -h, 0.0f }});
    uint32_t bot_ring = static_cast<uint32_t>(g.verts.size());
    for (int j = 0; j <= slices; ++j) {
        float th = kTau * static_cast<float>(j) / static_cast<float>(slices);
        g.verts.push_back({{ r * std::cos(th), -h, r * std::sin(th) }});
    }
    for (int j = 0; j < slices; ++j) {
        g.indices.push_back(bot_center);
        g.indices.push_back(bot_ring + static_cast<uint32_t>(j + 1));
        g.indices.push_back(bot_ring + static_cast<uint32_t>(j));
    }
    finalize_bounds(g);
}

// Cone, radius 0.5 base, height 1.0, apex at +Y, 8 slices + base cap.
static void build_cone_proxy(ProxyGeom& g) {
    const int slices = 8;
    const float r = 0.5f, h = 0.5f;
    const float kTau = 6.28318530717958647692f;
    // Apex (duplicated per slice for flat-ish shading via shared apex vert is fine here).
    uint32_t apex = static_cast<uint32_t>(g.verts.size());
    g.verts.push_back({{ 0.0f, h, 0.0f }});
    uint32_t base_ring = static_cast<uint32_t>(g.verts.size());
    for (int j = 0; j <= slices; ++j) {
        float th = kTau * static_cast<float>(j) / static_cast<float>(slices);
        g.verts.push_back({{ r * std::cos(th), -h, r * std::sin(th) }});
    }
    for (int j = 0; j < slices; ++j) {
        g.indices.push_back(apex);
        g.indices.push_back(base_ring + static_cast<uint32_t>(j));
        g.indices.push_back(base_ring + static_cast<uint32_t>(j + 1));
    }
    // Base cap (reversed winding so it faces -Y).
    uint32_t bot_center = static_cast<uint32_t>(g.verts.size());
    g.verts.push_back({{ 0.0f, -h, 0.0f }});
    uint32_t bot_ring = static_cast<uint32_t>(g.verts.size());
    for (int j = 0; j <= slices; ++j) {
        float th = kTau * static_cast<float>(j) / static_cast<float>(slices);
        g.verts.push_back({{ r * std::cos(th), -h, r * std::sin(th) }});
    }
    for (int j = 0; j < slices; ++j) {
        g.indices.push_back(bot_center);
        g.indices.push_back(bot_ring + static_cast<uint32_t>(j + 1));
        g.indices.push_back(bot_ring + static_cast<uint32_t>(j));
    }
    finalize_bounds(g);
}

} // namespace

// =============================================================================
// SDF3D Operator
// =============================================================================

static constexpr float kTAU = 6.28318530717958647692f;

/**
 * @brief Raymarches signed-distance 3D shapes into a rendered scene texture.
 *
 * SDF3D builds procedural forms from primary and secondary SDF shapes, supports smooth boolean
 * operations, and exposes material plus transform controls in one operator.
 *
 * @param shape Primary SDF primitive.
 * @param operation Optional boolean operation with the secondary shape.
 * @param shape_b Secondary SDF primitive.
 * @param smooth_k Blend amount for smooth boolean operations.
 * @param max_steps Raymarch step budget.
 * @param threshold Hit threshold for the distance field.
 */
struct SDF3D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "SDF3D";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr const char* kSummary = "Ray-marched signed-distance geometry: smooth blended primitives with no mesh.";
    static constexpr std::array<const char*, 3> kKeywords = {"3d", "sdf", "raymarch"};
    static constexpr bool kTimeDependent = false;

    // Shape A
    vivid::Param<int>   shape   {"shape",   0, {"Sphere", "Box", "Torus", "Cylinder", "Cone"}};
    vivid::Param<float> size_x  {"size_x",  1.0f, 0.01f, 10.0f};
    vivid::Param<float> size_y  {"size_y",  1.0f, 0.01f, 10.0f};
    vivid::Param<float> size_z  {"size_z",  1.0f, 0.01f, 10.0f};

    // CSG
    vivid::Param<int>   operation {"operation", 0, {"None", "Smooth Union", "Smooth Subtract", "Smooth Intersect"}};
    vivid::Param<int>   shape_b   {"shape_b",   0, {"None", "Sphere", "Box", "Torus", "Cylinder", "Cone"}};
    vivid::Param<float> size_bx   {"size_bx", 0.5f, 0.01f, 10.0f};
    vivid::Param<float> size_by   {"size_by", 0.5f, 0.01f, 10.0f};
    vivid::Param<float> size_bz   {"size_bz", 0.5f, 0.01f, 10.0f};
    vivid::Param<float> pos_bx    {"pos_bx",  0.5f, -10.0f, 10.0f};
    vivid::Param<float> pos_by    {"pos_by",  0.0f, -10.0f, 10.0f};
    vivid::Param<float> pos_bz    {"pos_bz",  0.0f, -10.0f, 10.0f};
    vivid::Param<float> smooth_k  {"smooth_k", 0.1f, 0.01f, 2.0f};

    // Note-metaball instances (optional `instances` input): each point becomes a sphere smooth-unioned
    // into the field, so they MERGE. instances_only hides the base A/B shape for a pure point field.
    vivid::Param<bool>  instances_only  {"instances_only", false};
    vivid::Param<float> instance_smooth {"instance_smooth", 0.35f, 0.01f, 2.0f};

    // Soft SELF-shadow strength (0 = off). Marches a shadow ray toward the key light (light 0) so the
    // metaball's own lobes / merged instances shadow each other — depth a raymarched op can't get from
    // the shadow-map path. Off by default, so existing SDF3D scenes are unchanged.
    vivid::Param<float> shadow {"shadow", 0.0f, 0.0f, 1.0f};

    // Color
    vivid::Param<float> r {"r", 0.9f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 0.2f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 1.0f, 0.0f, 1.0f};

    // Material
    vivid::Param<float> roughness {"roughness", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> metallic  {"metallic",  0.0f, 0.0f, 1.0f};
    vivid::Param<float> emission  {"emission",  0.0f, 0.0f, 5.0f};
    vivid::Param<int>   unlit     {"unlit",     0, {"Off", "On"}};

    // Transform
    vivid::Param<float> pos_x {"pos_x", 0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_y {"pos_y", 0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_z {"pos_z", 0.0f, -50.0f, 50.0f};
    vivid::Param<float> rot_x {"rot_x", 0.0f, -kTAU, kTAU};
    vivid::Param<float> rot_y {"rot_y", 0.0f, -kTAU, kTAU};
    vivid::Param<float> rot_z {"rot_z", 0.0f, -kTAU, kTAU};
    vivid::Param<float> scale {"scale", 1.0f, 0.01f, 10.0f};

    // Raymarching
    vivid::Param<int>   max_steps {"max_steps", 128, 16, 256};
    vivid::Param<float> threshold {"threshold", 0.001f, 0.0001f, 0.01f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(shape, "Shape");
        vivid::param_group(size_x, "Shape");
        vivid::param_group(size_y, "Shape");
        vivid::param_group(size_z, "Shape");

        vivid::param_group(operation, "CSG");
        vivid::param_group(shape_b, "CSG");
        vivid::param_group(size_bx, "CSG");
        vivid::param_group(size_by, "CSG");
        vivid::param_group(size_bz, "CSG");
        vivid::param_group(pos_bx, "CSG");
        vivid::param_group(pos_by, "CSG");
        vivid::param_group(pos_bz, "CSG");
        vivid::param_group(smooth_k, "CSG");
        vivid::param_group(instances_only, "Instances");
        vivid::param_group(instance_smooth, "Instances");
        vivid::param_group(shadow, "Instances");

        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);

        vivid::param_group(roughness, "Material");
        vivid::param_group(metallic, "Material");
        vivid::param_group(emission, "Material");
        vivid::param_group(unlit, "Material");

        vivid::param_group(pos_x, "Transform");
        vivid::param_group(pos_y, "Transform");
        vivid::param_group(pos_z, "Transform");
        vivid::param_group(rot_x, "Transform");
        vivid::param_group(rot_y, "Transform");
        vivid::param_group(rot_z, "Transform");
        vivid::param_group(scale, "Transform");

        vivid::param_group(max_steps, "Raymarching");
        vivid::param_group(threshold, "Raymarching");

        out.push_back(&shape);
        out.push_back(&size_x);
        out.push_back(&size_y);
        out.push_back(&size_z);
        out.push_back(&operation);
        out.push_back(&shape_b);
        out.push_back(&size_bx);
        out.push_back(&size_by);
        out.push_back(&size_bz);
        out.push_back(&pos_bx);
        out.push_back(&pos_by);
        out.push_back(&pos_bz);
        out.push_back(&smooth_k);
        out.push_back(&instances_only);
        out.push_back(&instance_smooth);
        out.push_back(&shadow);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
        out.push_back(&roughness);
        out.push_back(&metallic);
        out.push_back(&emission);
        out.push_back(&unlit);
        out.push_back(&pos_x);
        out.push_back(&pos_y);
        out.push_back(&pos_z);
        out.push_back(&rot_x);
        out.push_back(&rot_y);
        out.push_back(&rot_z);
        out.push_back(&scale);
        out.push_back(&max_steps);
        out.push_back(&threshold);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Optional note-metaball points (custom_inputs[0]): an InstanceArray3D whose elements become
        // smooth-unioned spheres in the field (see instances_only / instance_smooth).
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_INPUT, vivid::gpu::InstanceArray3D));
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_OUTPUT));
    }

    void draw_thumbnail(const VividThumbnailContext*) override { /* TODO(ADR-0041 Phase 1): reimplement vs trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[sdf3d] lazy_init FAILED\n");
                return;
            }
        }

        // Build model matrix from transform params
        mat4x4 model, inv_model;
        {
            mat4x4 t_mat, rx, ry, rz, s_mat, tmp1, tmp2;
            mat4x4_identity(t_mat);
            mat4x4_translate(t_mat, pos_x.value, pos_y.value, pos_z.value);
            mat4x4_identity(rx);
            mat4x4_rotate_X(rx, rx, rot_x.value);
            mat4x4_identity(ry);
            mat4x4_rotate_Y(ry, ry, rot_y.value);
            mat4x4_identity(rz);
            mat4x4_rotate_Z(rz, rz, rot_z.value);
            mat4x4_identity(s_mat);
            mat4x4_scale_aniso(s_mat, s_mat, scale.value, scale.value, scale.value);

            mat4x4_mul(tmp1, ry, rx);
            mat4x4_mul(tmp2, rz, tmp1);
            mat4x4_mul(tmp1, tmp2, s_mat);
            mat4x4_mul(model, t_mat, tmp1);
            mat4x4_invert(inv_model, model);
        }

        // Note-metaball instances (optional custom_inputs[0]): transform each point into SDF LOCAL space
        // and pack a MetaInstance = { pos.xyz, radius, rgba } (8 floats) for the shader's smooth-union +
        // per-instance colour blend. Always upload the full buffer so stale spheres are cleared to zero
        // (count gates the shader loop). Colour comes straight from the InstanceArray3D — composable.
        float sphere_data[kMaxSDFInstances * 8] = {0.f};
        uint32_t inst_n = 0;
        if (ctx->custom_input_count > 0 && ctx->custom_inputs && ctx->custom_inputs[0]) {
            const auto* arr = static_cast<const vivid::gpu::InstanceArray3D*>(ctx->custom_inputs[0]);
            if (arr && arr->data && arr->count > 0) {
                uint32_t n = arr->count > kMaxSDFInstances ? kMaxSDFInstances : arr->count;
                for (uint32_t i = 0; i < n; ++i) {
                    vec4 w = { arr->data[i].position[0], arr->data[i].position[1],
                               arr->data[i].position[2], 1.0f };
                    vec4 l; mat4x4_mul_vec4(l, inv_model, w);
                    sphere_data[i * 8 + 0] = l[0];
                    sphere_data[i * 8 + 1] = l[1];
                    sphere_data[i * 8 + 2] = l[2];
                    const float rad = arr->data[i].scale[0] * 0.5f;   // instance size → sphere radius
                    sphere_data[i * 8 + 3] = rad > 0.05f ? rad : 0.05f;
                    sphere_data[i * 8 + 4] = arr->data[i].color[0];    // per-instance rgba
                    sphere_data[i * 8 + 5] = arr->data[i].color[1];
                    sphere_data[i * 8 + 6] = arr->data[i].color[2];
                    sphere_data[i * 8 + 7] = arr->data[i].color[3];
                }
                inst_n = n;
            }
        }
        wgpuQueueWriteBuffer(ctx->queue, sphere_buf_, 0, sphere_data, kMaxSDFInstances * 32);

        // Upload SDF params
        SDFParamsUniform params_data{};
        params_data.shape_a_type = static_cast<float>(shape.int_value());
        // instances_only hides the base A/B shape (shape_a_type = -1) so the field is JUST the note points.
        if (instances_only.value) params_data.shape_a_type = -1.0f;
        params_data.instance_count = static_cast<float>(inst_n);
        params_data.instance_k     = instance_smooth.value;
        params_data.shadow         = shadow.value;
        params_data.shape_b_type = (shape_b.int_value() == 0) ? -1.0f
                                   : static_cast<float>(shape_b.int_value() - 1);
        params_data.operation    = static_cast<float>(operation.int_value());
        params_data.smooth_k     = smooth_k.value;
        params_data.size_a[0]    = size_x.value;
        params_data.size_a[1]    = size_y.value;
        params_data.size_a[2]    = size_z.value;
        params_data.size_a[3]    = 0.0f;
        params_data.size_b[0]    = size_bx.value;
        params_data.size_b[1]    = size_by.value;
        params_data.size_b[2]    = size_bz.value;
        params_data.size_b[3]    = 0.0f;
        params_data.pos_b[0]     = pos_bx.value;
        params_data.pos_b[1]     = pos_by.value;
        params_data.pos_b[2]     = pos_bz.value;
        params_data.pos_b[3]     = 0.0f;
        params_data.color[0]     = r.value;
        params_data.color[1]     = g.value;
        params_data.color[2]     = b.value;
        params_data.color[3]     = a.value;
        params_data.roughness    = roughness.value;
        params_data.metallic     = metallic.value;
        params_data.emission     = emission.value;
        params_data.flags        = (unlit.int_value() != 0) ? 1.0f : 0.0f;
        std::memcpy(params_data.inv_model, inv_model, 64);
        params_data.max_steps        = static_cast<float>(max_steps.int_value());
        params_data.surface_threshold = threshold.value;
        wgpuQueueWriteBuffer(ctx->queue, params_ubo_, 0, &params_data, sizeof(params_data));

        // ADR-0051 P3: a FALLBACK light, not the light. Render3D writes the scene's real lights
        // into this same buffer via `custom_lights_ubo` after every operator has run, so this only
        // shows through when the SDF is rendered by something that does not supply lights. Matches
        // Render3D's own no-light fallback, so the unlit-by-anything look is unchanged.
        SDFLightsUniform lights{};
        lights.light_count = 1;
        lights.ambient[0] = 0.15f;
        lights.ambient[1] = 0.15f;
        lights.ambient[2] = 0.15f;
        float dir[3] = {0.5f, 1.0f, 0.8f};
        float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        lights.lights[0].direction_and_intensity[0] = dir[0] / len;
        lights.lights[0].direction_and_intensity[1] = dir[1] / len;
        lights.lights[0].direction_and_intensity[2] = dir[2] / len;
        lights.lights[0].direction_and_intensity[3] = 1.0f;
        lights.lights[0].position_and_type[3] = 0.0f; // directional
        lights.lights[0].color_and_radius[0] = 1.0f;
        lights.lights[0].color_and_radius[1] = 1.0f;
        lights.lights[0].color_and_radius[2] = 1.0f;
        lights.lights[0].color_and_radius[3] = 10.0f;
        wgpuQueueWriteBuffer(ctx->queue, lights_ubo_, 0, &lights, sizeof(lights));

        // Output scene fragment
        vivid::gpu::scene_fragment_identity(fragment_);
        std::memcpy(fragment_.model_matrix, model, sizeof(mat4x4));
        fragment_.fragment_type    = vivid::gpu::VividSceneFragment::SDF;
        fragment_.vertex_buffer    = quad_vb_;
        fragment_.vertex_buf_size  = 4 * sizeof(vivid::gpu::Vertex3D);
        fragment_.index_buffer     = quad_ib_;
        fragment_.index_count      = 6;
        fragment_.pipeline         = pipeline_;
        fragment_.material_binds   = bind_group_;
        fragment_.custom_camera_ubo = camera_ubo_;
        fragment_.custom_lights_ubo = lights_ubo_;   // ADR-0051 P3: let Render3D light this SDF
        fragment_.depth_write      = true;

        fragment_.color[0] = r.value;
        fragment_.color[1] = g.value;
        fragment_.color[2] = b.value;
        fragment_.color[3] = a.value;
        fragment_.roughness = roughness.value;
        fragment_.metallic  = metallic.value;
        fragment_.emission  = emission.value;
        fragment_.unlit     = unlit.int_value() != 0;

        ctx->custom_outputs[0] = &fragment_;

        // Animated 3D thumbnail: a proxy sphere stands in for the raymarched SDF blob.
        const float col[3] = { r.value, g.value, b.value };
        vivid::thumb3d::render_proxy_sphere(ctx, thumb_, col, false);
    }

    ~SDF3D() override {
        vivid::thumb3d::destroy(thumb_);
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(camera_ubo_);
        vivid::gpu::release(params_ubo_);
        vivid::gpu::release(lights_ubo_);
        vivid::gpu::release(sphere_buf_);
        vivid::gpu::release(quad_vb_);
        vivid::gpu::release(quad_ib_);
        for (auto& p : proxy_) {
            vivid::gpu::release(p.vb);
            vivid::gpu::release(p.ib);
        }
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    vivid::thumb3d::State thumb_{};

    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;

    WGPUBuffer camera_ubo_ = nullptr;
    WGPUBuffer params_ubo_ = nullptr;
    WGPUBuffer lights_ubo_ = nullptr;
    WGPUBuffer sphere_buf_ = nullptr;   // storage: note-metaball instance spheres (kMaxSDFInstances vec4)
    WGPUBuffer quad_vb_    = nullptr;
    WGPUBuffer quad_ib_    = nullptr;

    // Thumbnail proxy buffers, one per shape_a value.
    static constexpr int kProxyCount = 5;
    struct ProxyBuffers {
        WGPUBuffer vb = nullptr;
        WGPUBuffer ib = nullptr;
        uint64_t   vb_size = 0;
        uint32_t   index_count = 0;
        float      bmin[3] = {-0.5f, -0.5f, -0.5f};
        float      bmax[3] = { 0.5f,  0.5f,  0.5f};
    };
    ProxyBuffers proxy_[kProxyCount]{};

    void ensure_proxy(const VividThumbnailContext*, int) { /* thumbnail proxy geometry; stubbed with draw_thumbnail (ADR-0041 Phase 1) */ }

    bool lazy_init(const VividGpuContext* ctx) {
        // Compile shader
        std::string src = std::string(vivid::gpu::CUSTOM_CAMERA_3D_WGSL)
                        + std::string(vivid::gpu::LIGHTS_3D_WGSL)   // ADR-0051 P3: the shared one
                        + kSDF3DShader;
        shader_ = vivid::gpu::create_wgsl_shader(ctx->device, src.c_str(), "SDF3D Shader");
        if (!shader_) return false;

        // UBO buffers
        camera_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, sizeof(vivid::gpu::CustomCamera3D), "SDF3D Camera UBO");
        params_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, sizeof(SDFParamsUniform), "SDF3D Params UBO");
        lights_ubo_ = vivid::gpu::create_uniform_buffer(
            ctx->device, sizeof(SDFLightsUniform), "SDF3D Lights UBO");
        {   // storage buffer for the note-metaball instance spheres (kMaxSDFInstances * vec4)
            WGPUBufferDescriptor bd{};
            bd.label = vivid_sv("SDF3D Instances");
            bd.size  = kMaxSDFInstances * 32;
            bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            sphere_buf_ = wgpuDeviceCreateBuffer(ctx->device, &bd);
        }

        // Bind group layout
        WGPUBindGroupLayoutEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(vivid::gpu::CustomCamera3D);

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].buffer.type = WGPUBufferBindingType_Uniform;
        entries[1].buffer.minBindingSize = sizeof(SDFParamsUniform);

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].buffer.type = WGPUBufferBindingType_Uniform;
        entries[2].buffer.minBindingSize = sizeof(SDFLightsUniform);

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Fragment;
        entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entries[3].buffer.minBindingSize = kMaxSDFInstances * 32;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("SDF3D BGL");
        bgl_desc.entryCount = 4;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("SDF3D Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);

        // Bind group
        WGPUBindGroupEntry bg_entries[4]{};
        bg_entries[0].binding = 0;
        bg_entries[0].buffer  = camera_ubo_;
        bg_entries[0].offset  = 0;
        bg_entries[0].size    = sizeof(vivid::gpu::CustomCamera3D);

        bg_entries[1].binding = 1;
        bg_entries[1].buffer  = params_ubo_;
        bg_entries[1].offset  = 0;
        bg_entries[1].size    = sizeof(SDFParamsUniform);

        bg_entries[2].binding = 2;
        bg_entries[2].buffer  = lights_ubo_;
        bg_entries[2].offset  = 0;
        bg_entries[2].size    = sizeof(SDFLightsUniform);

        bg_entries[3].binding = 3;
        bg_entries[3].buffer  = sphere_buf_;
        bg_entries[3].offset  = 0;
        bg_entries[3].size    = kMaxSDFInstances * 32;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("SDF3D Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 4;
        bg_desc.entries = bg_entries;
        bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);

        // Render pipeline
        WGPUVertexBufferLayout vbl = vivid::gpu::vertex3d_layout();
        vivid::gpu::Pipeline3DDesc pd{};
        pd.shader = shader_;
        pd.layout = pipe_layout_;
        pd.color_format = ctx->output_format;
        pd.vertex_layouts = &vbl;
        pd.vertex_layout_count = 1;
        pd.cull_mode = WGPUCullMode_None;
        pd.depth_write = true;
        pd.depth_compare = WGPUCompareFunction_Less;
        pd.label = "SDF3D Pipeline";
        pipeline_ = vivid::gpu::create_3d_pipeline(ctx->device, pd);
        if (!pipeline_) return false;

        // NDC fullscreen quad
        create_fullscreen_quad(ctx);

        return true;
    }

    void create_fullscreen_quad(const VividGpuContext* ctx) {
        vivid::gpu::Vertex3D verts[4]{};

        // (-1,-1,0) bottom-left
        verts[0].position[0] = -1.0f; verts[0].position[1] = -1.0f; verts[0].position[2] = 0.0f;
        verts[0].normal[0] = 0; verts[0].normal[1] = 0; verts[0].normal[2] = 1;
        verts[0].tangent[0] = 1; verts[0].tangent[1] = 0; verts[0].tangent[2] = 0; verts[0].tangent[3] = 1;
        verts[0].uv[0] = 0; verts[0].uv[1] = 0;

        // (1,-1,0) bottom-right
        verts[1].position[0] = 1.0f; verts[1].position[1] = -1.0f; verts[1].position[2] = 0.0f;
        verts[1].normal[0] = 0; verts[1].normal[1] = 0; verts[1].normal[2] = 1;
        verts[1].tangent[0] = 1; verts[1].tangent[1] = 0; verts[1].tangent[2] = 0; verts[1].tangent[3] = 1;
        verts[1].uv[0] = 1; verts[1].uv[1] = 0;

        // (1,1,0) top-right
        verts[2].position[0] = 1.0f; verts[2].position[1] = 1.0f; verts[2].position[2] = 0.0f;
        verts[2].normal[0] = 0; verts[2].normal[1] = 0; verts[2].normal[2] = 1;
        verts[2].tangent[0] = 1; verts[2].tangent[1] = 0; verts[2].tangent[2] = 0; verts[2].tangent[3] = 1;
        verts[2].uv[0] = 1; verts[2].uv[1] = 1;

        // (-1,1,0) top-left
        verts[3].position[0] = -1.0f; verts[3].position[1] = 1.0f; verts[3].position[2] = 0.0f;
        verts[3].normal[0] = 0; verts[3].normal[1] = 0; verts[3].normal[2] = 1;
        verts[3].tangent[0] = 1; verts[3].tangent[1] = 0; verts[3].tangent[2] = 0; verts[3].tangent[3] = 1;
        verts[3].uv[0] = 0; verts[3].uv[1] = 1;

        quad_vb_ = vivid::gpu::create_vertex_buffer(
            ctx->device, ctx->queue, verts, sizeof(verts), "SDF3D Quad VB");

        uint32_t indices[6] = { 0, 1, 2, 0, 2, 3 };
        quad_ib_ = vivid::gpu::create_index_buffer(
            ctx->device, ctx->queue, indices, 6, "SDF3D Quad IB");
    }
};

VIVID_REGISTER(SDF3D)
VIVID_THUMBNAIL(SDF3D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividSceneFragment)
