#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_2d.h"
#include <cstdio>
#include <cstring>
#include <vector>

// =============================================================================
// Particles2D — GPU compute-shader particle simulator for the 2D drawable pipeline
// =============================================================================
//
// Architecture (mirrors Particles3D):
//   - Per-particle state (position, velocity, age, lifetime) stored in two
//     ping-pong storage buffers. One frame reads A/writes B, the next swaps.
//   - Compute shader runs one thread per particle: integrate physics, spawn
//     new particles into dead slots via an atomic counter, write per-particle
//     InstanceData2D records into an internal instance buffer.
//   - Emits a single VividDrawable2D of type SHAPE with the instance buffer
//     attached. Render2D's shape-instanced pipeline renders all N particles
//     via one DrawIndexed(6, N) call.
//
// Output: VividDrawable2D (SHAPE, blend=ADDITIVE). No upstream drawable input
// — Particles2D is a pure source. Future variants may thread a texture for
// sprite particles.
// =============================================================================

// ---------------------------------------------------------------------------
// Compute shader: particle simulation + instance data generation (2D)
// ---------------------------------------------------------------------------

static const char* kParticles2DCompute = R"(
struct Particle {
    position: vec2f,
    velocity: vec2f,
    age: f32,
    lifetime: f32,
    _pad0: f32,
    _pad1: f32,
    color: vec4f,        // birth color: the image color where this particle spawned
}

struct Params {
    max_count: u32,
    new_spawns: u32,
    dt: f32,
    gravity: f32,
    speed: f32,
    spread_rad: f32,
    lifetime: f32,
    size: f32,
    color: vec4f,
    seed: u32,
    noise_octaves: u32,
    noise_scale: f32,
    noise_speed: f32,
    curl_strength: f32,
    drag: f32,
    time: f32,
    bounds: f32,
    learning_mode: u32,
    // Mode selectors (see C++ enums). 0 = the legacy/default behavior.
    population: u32,     // 0 Stream, 1 Fixed
    emit_shape: u32,     // 0 Cone, 1 Point, 2 Ring, 3 Grid, 4 Image
    force_mode: u32,     // 0 Field, 1 Flock, 2 Image, 3 Flow
    color_mode: u32,     // 0 Solid, 1 Velocity, 2 Age, 3 Image
    render_shape: u32,   // 0 Circle, 1 Polygon, 2 Aligned, 3 Sprite
    blend: u32,          // 0 Additive, 1 Alpha
    color_amount: f32,   // Color=Image: blend toward sampled pixel color
    // Emission shapes
    emit_x: f32,
    emit_y: f32,
    ring_radius: f32,
    ring_thickness: f32,
    grid_cols: u32,
    grid_rows: u32,
    emit_gain: f32,
    // Image attraction force
    attract_strength: f32,
    attract_threshold: f32,
    grad_step: f32,
    // Flock
    view_radius: f32,
    sep_radius: f32,
    separation: f32,
    alignment: f32,
    cohesion: f32,
    max_speed: f32,
    min_speed: f32,
    wrap: u32,
    fixed_seed: u32,
    // Color modes (Velocity/Age) + Polygon render
    vel_scale: f32,
    age_r: f32,
    age_g: f32,
    age_b: f32,
    sides: u32,
    star_factor: f32,
    color_gain: f32,     // Color=Image: brightness boost for the birth color
    emit_threshold: f32, // Image emit: luma-key cutoff (0 = emit by raw brightness)
    emit_flow: u32,      // Image emit: 1 = spawn velocity from the flow vector (emit_mask RG)
    flow_strength: f32,  // scales the decoded flow into spawn velocity
    flow_force: f32,     // force_mode=Flow: continuous steering gain toward the flow vector
    _pad5: u32,
    _pad6: u32,
}

// Matches CPU-side InstanceData2D (48 bytes):
//   mat3x2f is 24 bytes + 8 bytes WGSL align pad = 32; color 16 → 48 total.
struct InstanceData {
    transform: mat3x2f,
    color:     vec4f,
}

@group(0) @binding(0) var<storage, read>       particles_in:  array<Particle>;
@group(0) @binding(1) var<storage, read_write> particles_out: array<Particle>;
@group(0) @binding(2) var<storage, read_write> instances_out: array<InstanceData>;
@group(0) @binding(3) var<uniform>             params: Params;
@group(0) @binding(4) var<storage, read_write> counter: atomic<u32>;
@group(0) @binding(5) var img: texture_2d<f32>;
@group(0) @binding(6) var emit_tex: texture_2d<f32>;

// Sample the input image at an NDC position (-1..1). Compute shaders can't use
// textureSample, so we textureLoad at the nearest texel. Y is flipped so the
// particle samples the pixel that appears under it on screen.
fn img_sample(ndc: vec2f) -> vec4f {
    let dims = vec2f(textureDimensions(img));
    let aspect = dims.x / dims.y;   // Render2D aspect-corrects X; undo it for uv
    let uv = vec2f(ndc.x / aspect, ndc.y) * vec2f(0.5, -0.5) + vec2f(0.5);
    let tc = vec2i(clamp(uv * dims, vec2f(0.0), dims - vec2f(1.0)));
    return textureLoad(img, tc, 0);
}

fn img_luma(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

fn img_load_uv(uv: vec2f) -> vec4f {
    let dims = vec2f(textureDimensions(img));
    let tc = vec2i(clamp(uv * dims, vec2f(0.0), dims - vec2f(1.0)));
    return textureLoad(img, tc, 0);
}

// Emission mask (binding 6). Disconnected -> bound to the color texture, so
// emission falls back to color luma. Connect a Motion/threshold texture to emit
// where it moves / is bright, independent of the birth color (still from img).
fn emit_load_uv(uv: vec2f) -> vec4f {
    let dims = vec2f(textureDimensions(emit_tex));
    let tc = vec2i(clamp(uv * dims, vec2f(0.0), dims - vec2f(1.0)));
    return textureLoad(emit_tex, tc, 0);
}

// Inverse of img_sample's NDC<->UV mapping (UV (0..1) -> NDC (-1..1)). Texel row 0
// is the top of the image -> top of screen (ndc.y = +1). X is multiplied by the
// texture aspect because Render2D divides particle X by aspect when drawing, so a
// spawn lands exactly on the bright/moving texel in screen space.
fn uv_to_ndc(uv: vec2f) -> vec2f {
    let dims = vec2f(textureDimensions(emit_tex));
    let aspect = dims.x / dims.y;
    return vec2f((uv.x - 0.5) * 2.0 * aspect, (uv.y - 0.5) * -2.0);
}

// ---------------------------------------------------------------------------
// 2D simplex noise (adapted from common 2D simplex implementations)
// ---------------------------------------------------------------------------

fn permute2(x: vec3f) -> vec3f {
    return (((x * 34.0) + 1.0) * x) % 289.0;
}

fn simplex2D(v: vec2f) -> f32 {
    let C = vec4f(0.211324865405187, 0.366025403784439,
                 -0.577350269189626, 0.024390243902439);
    var i  = floor(v + dot(v, C.yy));
    let x0 = v - i + dot(i, C.xx);
    var i1 = vec2f(0.0, 1.0);
    if (x0.x > x0.y) { i1 = vec2f(1.0, 0.0); }
    let x1 = x0 - i1 + C.xx;
    let x2 = x0 - vec2f(1.0, 1.0) + 2.0 * C.xx;
    i = i % 289.0;
    let p = permute2(permute2(vec3f(0.0, i1.y, 1.0) + i.y)
                     + vec3f(0.0, i1.x, 1.0) + i.x);
    var m = max(vec3f(0.5) - vec3f(dot(x0, x0), dot(x1, x1), dot(x2, x2)), vec3f(0.0));
    m = m * m;
    m = m * m;
    let x_ = 2.0 * fract(p * C.www) - 1.0;
    let h  = abs(x_) - 0.5;
    let ox = floor(x_ + 0.5);
    let a0 = x_ - ox;
    m = m * (1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h));
    var g: vec3f;
    g.x = a0.x * x0.x + h.x * x0.y;
    g.y = a0.y * x1.x + h.y * x1.y;
    g.z = a0.z * x2.x + h.z * x2.y;
    return 130.0 * dot(m, g);
}

fn fbm_simplex2D(p_in: vec2f, octaves: u32, lacunarity: f32, persistence: f32) -> f32 {
    var value = 0.0;
    var amplitude = 1.0;
    var frequency = 1.0;
    var max_value = 0.0;
    var p = p_in;
    for (var i = 0u; i < octaves; i++) {
        value += amplitude * simplex2D(p * frequency);
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return value / max_value;
}

fn curl_noise_2d(p: vec2f, octaves: u32) -> vec2f {
    // 2D curl of a scalar potential field F: curl = (dF/dy, -dF/dx).
    // Produces divergence-free advection, which looks organic for particles.
    let e = 0.01;
    let f_py = fbm_simplex2D(p + vec2f(0.0, e), octaves, 2.0, 0.5);
    let f_ny = fbm_simplex2D(p - vec2f(0.0, e), octaves, 2.0, 0.5);
    let f_px = fbm_simplex2D(p + vec2f(e, 0.0), octaves, 2.0, 0.5);
    let f_nx = fbm_simplex2D(p - vec2f(e, 0.0), octaves, 2.0, 0.5);
    let inv2e = 1.0 / (2.0 * e);
    return vec2f((f_py - f_ny) * inv2e, -(f_px - f_nx) * inv2e);
}

// ---------------------------------------------------------------------------
// PCG hash — deterministic PRNG
// ---------------------------------------------------------------------------

fn pcg_hash(input: u32) -> u32 {
    let state = input * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

fn rand_float(seed: u32) -> f32 {
    return f32(pcg_hash(seed)) / 4294967295.0;
}

// ---------------------------------------------------------------------------
// Compute entry point
// ---------------------------------------------------------------------------

@compute @workgroup_size(256)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let idx = gid.x;
    if (idx >= params.max_count) { return; }

    var p = particles_in[idx];
    let is_dead = p.lifetime <= 0.0;

    let fixed_pop = params.population == 1u;

    if (is_dead) {
        // Decide whether this slot spawns. Fixed population fills immediately
        // (persistent flock/field); Stream throttles via the spawn counter.
        var do_spawn = fixed_pop;
        if (!fixed_pop) {
            let slot = atomicAdd(&counter, 1u);
            do_spawn = slot < params.new_spawns;
        }

        if (do_spawn) {
            let s0 = pcg_hash(params.seed + idx * 3u);
            let s1 = pcg_hash(s0);
            let s2 = pcg_hash(s1);
            let s3 = pcg_hash(s2);
            let center = vec2f(params.emit_x, params.emit_y);
            var pos = center;
            var vel = vec2f(0.0, params.speed);
            var img_spawn_ok = true;   // Image emit: false if no pixel passed the luma key (don't birth at center)

            if (fixed_pop) {
                // Scatter a persistent population (flock seed).
                let f0 = pcg_hash(params.fixed_seed + idx * 7919u);
                let f1 = pcg_hash(f0);
                let f2 = pcg_hash(f1);
                pos = vec2f(rand_float(f0), rand_float(f1)) * 2.0 - 1.0;
                let a = rand_float(f2) * 6.28318530718;
                vel = vec2f(cos(a), sin(a)) * max(params.max_speed, 0.05);
                p.lifetime = 1.0e30;          // effectively immortal
            } else {
                let es = params.emit_shape;
                if (es == 0u) {               // Cone
                    let angle = (rand_float(s0) - 0.5) * params.spread_rad;
                    vel = vec2f(sin(angle), cos(angle)) * params.speed;
                } else if (es == 1u) {        // Point (omnidirectional)
                    let a = rand_float(s0) * 6.28318530718;
                    vel = vec2f(cos(a), sin(a)) * params.speed;
                } else if (es == 2u) {        // Ring
                    let a = rand_float(s0) * 6.28318530718;
                    let dir = vec2f(cos(a), sin(a));
                    let rr = params.ring_radius + (rand_float(s1) - 0.5) * params.ring_thickness;
                    pos = center + dir * rr;
                    vel = dir * params.speed;
                } else if (es == 3u) {        // Grid
                    let cx = floor(rand_float(s0) * f32(params.grid_cols));
                    let cy = floor(rand_float(s1) * f32(params.grid_rows));
                    let gx = (cx + 0.5) / f32(params.grid_cols) * 2.0 - 1.0;
                    let gy = (cy + 0.5) / f32(params.grid_rows) * 2.0 - 1.0;
                    pos = vec2f(gx, gy);
                    vel = vec2f(rand_float(s2) - 0.5, rand_float(s3) - 0.5) * params.speed;
                } else {                      // Image (brightness-weighted)
                    let mdims  = vec2f(textureDimensions(emit_tex));
                    let maspect = mdims.x / mdims.y;
                    var rs = s0;
                    var found = false;
                    var win_rg = vec2f(0.5);  // encoded flow vector at the winning sample
                    for (var t = 0u; t < 16u; t = t + 1u) {
                        let u = rand_float(rs); rs = pcg_hash(rs);
                        let v = rand_float(rs); rs = pcg_hash(rs);
                        let uv = vec2f(u, v);
                        let s      = emit_load_uv(uv);
                        // Flow mode keys "where" on the magnitude channel (B); else luma.
                        let lum    = select(img_luma(s.rgb), s.b, params.emit_flow == 1u);
                        let masked = max(lum - params.emit_threshold, 0.0) / max(1.0 - params.emit_threshold, 0.001);
                        let br     = masked * params.emit_gain;
                        let r = rand_float(rs); rs = pcg_hash(rs);
                        if (r < br) { pos = uv_to_ndc(uv); win_rg = s.rg; found = true; break; }
                    }
                    img_spawn_ok = found;   // no bright pixel found -> suppress, don't pile at center
                    if (params.emit_flow == 1u) {
                        // Spawn velocity along the decoded motion vector (Y-flip + aspect to screen space).
                        let f = (win_rg - vec2f(0.5)) * 2.0;
                        vel = vec2f(f.x * maspect, -f.y) * params.flow_strength;
                    } else {
                        let a = rand_float(s3) * 6.28318530718;
                        vel = vec2f(cos(a), sin(a)) * params.speed * 0.3;
                    }
                }
                p.lifetime = select(0.0, params.lifetime * (0.8 + 0.4 * rand_float(s2)), img_spawn_ok);
            }
            p.position = pos;
            p.velocity = vel;
            p.age = 0.0;
            p.color = img_sample(pos);  // birth color: the video color where this flake was shed
        } else {
            p.lifetime = 0.0;  // stay dead
        }
    } else {
        // ---- Universal forces ----------------------------------------------
        p.velocity.y += params.gravity * params.dt;

        let fm = params.force_mode;
        if (fm == 0u) {
            // Field: curl noise.
            if (params.curl_strength > 0.0) {
                let noise_pos = p.position * params.noise_scale
                              + vec2f(0.0, params.time * params.noise_speed);
                let curl = curl_noise_2d(noise_pos, params.noise_octaves);
                p.velocity += curl * params.curl_strength * params.dt;
            }
        } else if (fm == 1u) {
            // Flock: separation / alignment / cohesion (O(N^2) — only here).
            var sep = vec2f(0.0);
            var ali = vec2f(0.0);
            var coh = vec2f(0.0);
            var an = 0.0;
            var cn = 0.0;
            let vr2 = params.view_radius * params.view_radius;
            let sr2 = params.sep_radius * params.sep_radius;
            for (var j = 0u; j < params.max_count; j = j + 1u) {
                if (j == idx) { continue; }
                let o = particles_in[j];
                if (o.lifetime <= 0.0) { continue; }
                let diff = p.position - o.position;
                let d2 = dot(diff, diff);
                if (d2 > vr2 || d2 <= 0.00001) { continue; }
                ali += o.velocity; an += 1.0;
                coh += o.position; cn += 1.0;
                if (d2 < sr2) { sep += diff / max(sqrt(d2), 0.001); }
            }
            var steer = sep * params.separation;
            if (an > 0.0) { steer += (ali / an - p.velocity) * params.alignment; }
            if (cn > 0.0) { steer += (coh / cn - p.position) * params.cohesion; }
            p.velocity += steer * params.dt;
        } else if (fm == 2u) {
            // Image: climb the luminance gradient toward bright pixels.
            if (params.attract_strength > 0.0) {
                let dims = vec2f(textureDimensions(img));
                let aspect = dims.x / dims.y;
                let step = params.grad_step / max(dims, vec2f(1.0));
                let uv = vec2f(p.position.x / aspect, p.position.y) * vec2f(0.5, -0.5) + vec2f(0.5);
                if (img_luma(img_load_uv(uv).rgb) > params.attract_threshold) {
                    let rx = img_luma(img_load_uv(uv + vec2f(step.x, 0.0)).rgb);
                    let lx = img_luma(img_load_uv(uv - vec2f(step.x, 0.0)).rgb);
                    let uy = img_luma(img_load_uv(uv + vec2f(0.0, step.y)).rgb);
                    let dy = img_luma(img_load_uv(uv - vec2f(0.0, step.y)).rgb);
                    var g = vec2f(rx - lx, -(uy - dy));   // UV grad -> NDC (Y flip)
                    let gl = length(g);
                    if (gl > 0.0001) {
                        p.velocity += (g / gl) * params.attract_strength * params.dt;
                    }
                }
            }
        } else if (fm == 3u) {
            // Flow: steer along the motion vector sampled from emit_mask (Motion in Flow mode).
            let fdims   = vec2f(textureDimensions(emit_tex));
            let faspect = fdims.x / fdims.y;
            let fuv     = vec2f(p.position.x / faspect, p.position.y) * vec2f(0.5, -0.5) + vec2f(0.5);
            let fs      = emit_load_uv(fuv);
            if (fs.b > 0.02) {                       // only where there is motion magnitude
                let ff = (fs.rg - vec2f(0.5)) * 2.0;
                p.velocity += vec2f(ff.x * faspect, -ff.y) * params.flow_force * params.dt;
            }
        }

        // Drag (universal).
        if (params.drag > 0.0) {
            p.velocity *= 1.0 - params.drag * params.dt;
        }

        // Flock speed clamp.
        if (fm == 1u) {
            let sp = length(p.velocity);
            if (sp > params.max_speed) {
                p.velocity *= params.max_speed / sp;
            } else if (sp < params.min_speed && sp > 0.00001) {
                p.velocity *= params.min_speed / sp;
            }
        }

        p.position += p.velocity * params.dt;

        // Boundary handling: wrap (fixed) or kill-box (stream).
        if (fixed_pop) {
            if (params.wrap != 0u) {
                if (p.position.x >  1.0) { p.position.x -= 2.0; }
                if (p.position.x < -1.0) { p.position.x += 2.0; }
                if (p.position.y >  1.0) { p.position.y -= 2.0; }
                if (p.position.y < -1.0) { p.position.y += 2.0; }
            }
        } else {
            if (params.bounds > 0.0) {
                if (abs(p.position.x) > params.bounds ||
                    abs(p.position.y) > params.bounds) {
                    p.lifetime = 0.0;
                }
            }
            // Aging (stream only).
            p.age += params.dt;
            if (p.age >= p.lifetime) {
                p.lifetime = 0.0;
            }
        }
    }

    particles_out[idx] = p;

    // Write InstanceData2D for this slot.
    var inst: InstanceData;
    if (p.lifetime > 0.0) {
        let age_ratio   = p.age / p.lifetime;
        let size_factor = 1.0 - age_ratio * age_ratio;   // shrink over lifetime
        let alpha       = 1.0 - age_ratio;               // fade over lifetime
        let sz = params.size * size_factor;

        // Column-major mat3x2 = (scale, 0) / (0, scale) / (tx, ty).
        if (params.render_shape == 2u) {        // Aligned: orient to velocity
            let heading = atan2(p.velocity.y, p.velocity.x);
            let c = cos(heading);
            let s = sin(heading);
            let sx = sz * 1.8;                   // long axis along motion
            let sy = sz * 0.7;                   // narrow across motion
            inst.transform = mat3x2f(
                vec2f(c * sx, s * sx),
                vec2f(-s * sy, c * sy),
                vec2f(p.position.x, p.position.y)
            );
        } else {
            inst.transform = mat3x2f(
                vec2f(sz, 0.0),
                vec2f(0.0, sz),
                vec2f(p.position.x, p.position.y)
            );
        }
        var rgb = params.color.rgb;
        let cm = params.color_mode;
        if (cm == 1u) {                             // Velocity: brightness by speed
            rgb = rgb * clamp(length(p.velocity) * params.vel_scale, 0.15, 2.0);
        } else if (cm == 2u) {                      // Age: base -> age color over life
            rgb = mix(rgb, vec3f(params.age_r, params.age_g, params.age_b), age_ratio);
        } else if (cm == 3u) {                      // Image: birth color, brightness-boosted
            rgb = mix(rgb, p.color.rgb * params.color_gain, params.color_amount);
        }
        inst.color = vec4f(rgb, params.color.a * alpha);
    } else {
        // Dead particle — degenerate transform (zero scale, far off-screen).
        inst.transform = mat3x2f(
            vec2f(0.0, 0.0),
            vec2f(0.0, 0.0),
            vec2f(99999.0, 99999.0)
        );
        inst.color = vec4f(0.0);
    }
    instances_out[idx] = inst;
}
)";

// ---------------------------------------------------------------------------
// Params uniform (CPU-side mirror of the WGSL Params struct).
// ---------------------------------------------------------------------------

struct ParamsData {
    uint32_t max_count;       //   0
    uint32_t new_spawns;      //   4
    float    dt;              //   8
    float    gravity;         //  12
    float    speed;           //  16
    float    spread_rad;      //  20
    float    lifetime;        //  24
    float    size;            //  28
    float    color[4];        //  32
    uint32_t seed;            //  48
    uint32_t noise_octaves;   //  52
    float    noise_scale;     //  56
    float    noise_speed;     //  60
    float    curl_strength;   //  64
    float    drag;            //  68
    float    time;            //  72
    float    bounds;          //  76
    uint32_t learning_mode;   //  80
    uint32_t population;      //  84  0 Stream, 1 Fixed
    uint32_t emit_shape;     //  88  0 Cone, 1 Point, 2 Ring, 3 Grid, 4 Image
    uint32_t force_mode;     //  92  0 Field, 1 Flock, 2 Image, 3 Flow
    uint32_t color_mode;     //  96  0 Solid, 1 Velocity, 2 Age, 3 Image
    uint32_t render_shape;   // 100  0 Circle, 1 Polygon, 2 Aligned, 3 Sprite
    uint32_t blend;          // 104  0 Additive, 1 Alpha
    float    color_amount;   // 108  Color=Image blend factor
    // Emission shapes
    float    emit_x;            // 112
    float    emit_y;            // 116
    float    ring_radius;       // 120
    float    ring_thickness;    // 124
    uint32_t grid_cols;         // 128
    uint32_t grid_rows;         // 132
    float    emit_gain;         // 136
    // Image attraction force
    float    attract_strength;  // 140
    float    attract_threshold; // 144
    float    grad_step;         // 148
    // Flock
    float    view_radius;       // 152
    float    sep_radius;        // 156
    float    separation;        // 160
    float    alignment;         // 164
    float    cohesion;          // 168
    float    max_speed;         // 172
    float    min_speed;         // 176
    uint32_t wrap;              // 180
    uint32_t fixed_seed;        // 184
    // Color modes (Velocity/Age) + Polygon render
    float    vel_scale;         // 188
    float    age_r;             // 192
    float    age_g;             // 196
    float    age_b;             // 200
    uint32_t sides;             // 204
    float    star_factor;       // 208
    float    color_gain;        // 212  Color=Image: brightness boost for the birth color
    float    emit_threshold;    // 216  Image emit: luma-key cutoff (0 = emit by raw brightness)
    uint32_t emit_flow;         // 220  Image emit: 1 = spawn velocity from the flow vector (emit_mask RG)
    float    flow_strength;     // 224  scales the decoded flow into spawn velocity
    float    flow_force;        // 228  force_mode=Flow: continuous steering gain
    uint32_t _pad5;             // 232
    uint32_t _pad6;             // 236
};
static_assert(sizeof(ParamsData) == 240, "ParamsData must be 240 bytes");

// ---------------------------------------------------------------------------
// Particles2D operator
// ---------------------------------------------------------------------------

/**
 * @brief Modular GPU particle system — emission, forces, color and render as modes.
 *
 * One compute pass per frame over a ping-pong particle buffer, emitting an
 * instanced 2D drawable for Render2D. Behavior is selected by mode, so the same
 * operator covers ember fountains, flocking swarms, and image-driven particles:
 *
 *   - population:   Stream (emit/age/die) or Fixed (persistent scattered set)
 *   - emit_shape:   Cone, Point, Ring, Grid, or Image (brightness-weighted spawn)
 *   - force_mode:   Field (gravity + curl-noise), Flock (boids), or Image (climb
 *                   the luminance gradient toward bright pixels)
 *   - color_mode:   Solid, Velocity (by speed), Age (ramp over life), or Image
 *                   (adopt the pixel beneath each particle)
 *   - render_shape: Circle, Polygon, Aligned (oriented to velocity), or Sprite
 *
 * The optional `texture` input feeds the Image emit/force/color modes; leave it
 * disconnected for ordinary particles. The inspector reveals only the controls
 * for the active modes. Factory presets: Embers, Flocking, Fireflies, Image
 * Dust, Swarm to Light.
 *
 * @keywords particles, flock, boids, emitter, swarm, sparks, embers, image
 *
 * @param count          Maximum live particle count (1–100000).
 * @param emission_rate  New particles spawned per second.
 * @param lifetime       Lifetime of each particle (seconds).
 * @param speed          Base particle speed.
 * @param gravity        Y-axis acceleration.
 * @param spread         Emission cone width (degrees).
 * @param drag           Velocity damping.
 * @param curl_strength  Curl-noise force amplitude.
 * @param noise_scale    Spatial frequency of the noise field.
 * @param noise_speed    Rate at which the noise field evolves.
 * @param noise_octaves  Number of FBM octaves (1..4).
 * @param size           Base particle radius (NDC units).
 * @param softness       Edge softness of the SDF circle.
 * @param bounds         NDC-box clamp; 0 = disabled.
 * @param learning_mode  Advanced vs Beginner preset.
 * @param r / g / b / a  Base color (multiplied with per-particle alpha fade).
 *
 * @tip Feed directly into Render2D — the compute shader owns the instance buffer.
 * @tip Curl noise looks best at `curl_strength` 1–3 with small `curl_scale` (~2).
 * @recipe Particles2D -> Render2D -> Bloom -> video_out
 * @pitfall `count` > 100K will tax lower-end GPUs; start at 5000 and scale up.
 * @common_companions Render2D, Bloom, Feedback
 * @best_used_with Render2D
 * @family 2D drawable pipeline
 * @see Render2D, Flocking2D, Bloom
 */
struct Particles2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Particles2D";
    static constexpr bool kTimeDependent = true;

    // Emission
    vivid::Param<int>   count         {"count",         2000, 1, 100000};
    vivid::Param<float> emission_rate {"emission_rate", 500.0f, 0.0f, 10000.0f};
    vivid::Param<float> lifetime      {"lifetime",      2.0f, 0.1f, 30.0f};

    // Physics
    vivid::Param<float> speed   {"speed",   0.4f, 0.0f, 5.0f};
    vivid::Param<float> gravity {"gravity", -0.2f, -5.0f, 5.0f};
    vivid::Param<float> spread  {"spread",  45.0f, 0.0f, 360.0f};
    vivid::Param<float> drag    {"drag",    0.0f, 0.0f, 10.0f};

    // Curl noise
    vivid::Param<float> curl_strength {"curl_strength", 0.0f, 0.0f, 5.0f};
    vivid::Param<float> noise_scale   {"noise_scale",   1.0f, 0.01f, 10.0f};
    vivid::Param<float> noise_speed   {"noise_speed",   0.5f, 0.0f, 5.0f};
    vivid::Param<int>   noise_octaves {"noise_octaves", 2, 1, 4};

    // Appearance
    vivid::Param<float> size     {"size",     0.015f, 0.001f, 0.2f};
    vivid::Param<float> softness {"softness", 0.5f,   0.0f,   1.0f};
    vivid::Param<float> bounds   {"bounds",   2.0f,   0.0f,  10.0f};

    // Color (default warm ember)
    vivid::Param<float> r {"r", 1.0f,  0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.6f,  0.0f, 1.0f};
    vivid::Param<float> b {"b", 0.25f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 0.8f,  0.0f, 1.0f};

    // Learning mode (Advanced/Beginner)
    vivid::Param<int> learning_mode {"learning_mode", 0, {"Advanced", "Beginner"}};

    // -- Modes (index 0 = the legacy/default behavior) ------------------------
    // These select pluggable behavior; mode-specific params are revealed by the
    // inspector via visible_when. Behavior for non-default modes is added in
    // later build phases; today only the index-0 path is implemented.
    vivid::Param<int> population    {"population",    0, {"Stream", "Fixed"}};
    vivid::Param<int> emit_shape    {"emit_shape",    0, {"Cone", "Point", "Ring", "Grid", "Image"}};
    vivid::Param<int> force_mode    {"force_mode",    0, {"Field", "Flock", "Image", "Flow"}};
    vivid::Param<int> color_mode    {"color_mode",    0, {"Solid", "Velocity", "Age", "Image"}};
    vivid::Param<int> render_shape  {"render_shape",  0, {"Circle", "Polygon", "Aligned", "Sprite"}};
    vivid::Param<int> blend         {"blend",         0, {"Additive", "Alpha"}};

    // -- Texture coupling (active when the `texture` input is connected) -------
    vivid::Param<float> color_amount {"color_amount", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> color_gain   {"color_gain",   1.5f, 0.0f, 6.0f};

    // Emission shapes
    vivid::Param<float> emit_x         {"emit_x",         0.0f, -1.0f, 1.0f};
    vivid::Param<float> emit_y         {"emit_y",         0.0f, -1.0f, 1.0f};
    vivid::Param<float> ring_radius    {"ring_radius",    0.3f,  0.01f, 1.5f};
    vivid::Param<float> ring_thickness {"ring_thickness", 0.05f, 0.0f,  0.5f};
    vivid::Param<int>   grid_cols      {"grid_cols",      10, 1, 64};
    vivid::Param<int>   grid_rows      {"grid_rows",      10, 1, 64};
    vivid::Param<float> emit_gain      {"emit_gain",      1.0f,  0.0f, 8.0f};
    vivid::Param<float> emit_threshold {"emit_threshold", 0.0f,  0.0f, 1.0f};
    vivid::Param<int>   emit_flow      {"emit_flow",      0, {"Off", "On"}};
    vivid::Param<float> flow_strength  {"flow_strength",  0.4f,  0.0f, 3.0f};
    vivid::Param<float> flow_force     {"flow_force",     6.0f,  0.0f, 30.0f};

    // Image attraction force
    vivid::Param<float> attract_strength  {"attract_strength",  1.0f, 0.0f, 5.0f};
    vivid::Param<float> attract_threshold {"attract_threshold", 0.3f, 0.0f, 1.0f};
    vivid::Param<float> grad_step         {"grad_step",         2.0f, 0.5f, 8.0f};

    // Flock (boids)
    vivid::Param<float> view_radius {"view_radius", 0.25f, 0.01f, 2.0f};
    vivid::Param<float> sep_radius  {"sep_radius",  0.08f, 0.001f, 1.0f};
    vivid::Param<float> separation  {"separation",  1.5f,  0.0f, 10.0f};
    vivid::Param<float> alignment   {"alignment",   1.0f,  0.0f, 10.0f};
    vivid::Param<float> cohesion    {"cohesion",    1.0f,  0.0f, 10.0f};
    vivid::Param<float> max_speed   {"max_speed",   0.4f,  0.0f, 5.0f};
    vivid::Param<float> min_speed   {"min_speed",   0.1f,  0.0f, 5.0f};
    vivid::Param<int>   wrap        {"wrap",        1, {"Off", "On"}};
    vivid::Param<int>   fixed_seed  {"fixed_seed",  42, 0, 9999};

    // Color modes (Velocity / Age) + Polygon render
    vivid::Param<float> vel_scale   {"vel_scale",   2.0f, 0.0f, 20.0f};
    vivid::Param<float> age_r       {"age_r",       1.0f, 0.0f, 1.0f};
    vivid::Param<float> age_g       {"age_g",       0.1f, 0.0f, 1.0f};
    vivid::Param<float> age_b       {"age_b",       0.0f, 0.0f, 1.0f};
    vivid::Param<int>   sides       {"sides",       4, 3, 12};
    vivid::Param<float> star_factor {"star_factor", 0.0f, 0.0f, 1.0f};

    Particles2D() {
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);
        vivid::display_hint(age_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(age_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(age_b, VIVID_DISPLAY_COLOR);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        // -- Modes (always visible) -------------------------------------------
        vivid::param_group(population,   "Modes");
        vivid::param_group(emit_shape,   "Modes");
        vivid::param_group(force_mode,   "Modes");
        vivid::param_group(color_mode,   "Modes");
        vivid::param_group(render_shape, "Modes");
        vivid::param_group(blend,        "Modes");

        vivid::param_group(learning_mode, "Learning");

        // -- Emission ---------------------------------------------------------
        vivid::param_group(count,         "Emission");
        vivid::param_group(emission_rate, "Emission");
        vivid::param_group(lifetime,      "Emission");
        // emission_rate / lifetime only matter for the streaming population.
        vivid::visible_when_eq(emission_rate, population, {0});
        vivid::visible_when_eq(lifetime,      population, {0});
        // cone width applies to the Cone emitter.
        vivid::param_group(spread,  "Emission");
        vivid::visible_when_eq(spread, emit_shape, {0});

        // -- Motion -----------------------------------------------------------
        vivid::param_group(speed,   "Motion");
        vivid::param_group(gravity, "Motion");
        vivid::param_group(drag,    "Motion");

        // Curl-noise field (force_mode = Field).
        vivid::param_group(curl_strength, "Field Force");
        vivid::param_group(noise_scale,   "Field Force");
        vivid::param_group(noise_speed,   "Field Force");
        vivid::param_group(noise_octaves, "Field Force");
        vivid::visible_when_eq(curl_strength, force_mode, {0});
        vivid::visible_when_eq(noise_scale,   force_mode, {0});
        vivid::visible_when_eq(noise_speed,   force_mode, {0});
        vivid::visible_when_eq(noise_octaves, force_mode, {0});

        // -- Appearance -------------------------------------------------------
        vivid::param_group(size,     "Appearance");
        vivid::param_group(softness, "Appearance");
        vivid::param_group(bounds,   "Appearance");

        // Solid color (color_mode = Solid).
        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");
        vivid::visible_when_eq(r, color_mode, {0});
        vivid::visible_when_eq(g, color_mode, {0});
        vivid::visible_when_eq(b, color_mode, {0});

        // Image color: how strongly particles adopt the color of the video at birth.
        vivid::param_group(color_amount, "Color");
        vivid::description(color_amount, "Color=Image: blend toward the video color each particle was born with (needs the texture input)");
        vivid::visible_when_eq(color_amount, color_mode, {3});
        vivid::param_group(color_gain, "Color");
        vivid::description(color_gain, "Color=Image: brightness boost for the birth color, so shed flakes read vividly over dim footage");
        vivid::visible_when_eq(color_gain, color_mode, {3});

        // -- Emission shapes --------------------------------------------------
        vivid::param_group(emit_x,         "Emission");
        vivid::param_group(emit_y,         "Emission");
        vivid::param_group(ring_radius,    "Emission");
        vivid::param_group(ring_thickness, "Emission");
        vivid::param_group(grid_cols,      "Emission");
        vivid::param_group(grid_rows,      "Emission");
        vivid::param_group(emit_gain,      "Emission");
        vivid::description(emit_gain, "Emit=Image: brightness multiplier for where particles are born (needs the texture input)");
        vivid::param_group(emit_threshold, "Emission");
        vivid::description(emit_threshold, "Emit=Image: luma-key cutoff — only pixels brighter than this emit, so particles clearly come from the bright areas (0 = emit by raw brightness)");
        vivid::param_group(emit_flow, "Emission");
        vivid::description(emit_flow, "Emit=Image: with a Motion(Flow) mask on emit_mask, give each particle an initial velocity along the motion direction (keys 'where' on the magnitude channel)");
        vivid::param_group(flow_strength, "Emission");
        vivid::description(flow_strength, "How strongly the flow vector drives the spawn velocity");
        vivid::visible_when_eq(emit_x,         emit_shape, {0, 1, 2});
        vivid::visible_when_eq(emit_y,         emit_shape, {0, 1, 2});
        vivid::visible_when_eq(ring_radius,    emit_shape, {2});
        vivid::visible_when_eq(ring_thickness, emit_shape, {2});
        vivid::visible_when_eq(grid_cols,      emit_shape, {3});
        vivid::visible_when_eq(grid_rows,      emit_shape, {3});
        vivid::visible_when_eq(emit_gain,      emit_shape, {4});
        vivid::visible_when_eq(emit_threshold, emit_shape, {4});
        vivid::visible_when_eq(emit_flow,      emit_shape, {4});
        vivid::visible_when_eq(flow_strength,  emit_flow,  {1});

        // -- Image attraction force (force_mode = Image) ----------------------
        vivid::param_group(attract_strength,  "Image Force");
        vivid::param_group(attract_threshold, "Image Force");
        vivid::param_group(grad_step,         "Image Force");
        vivid::description(attract_strength, "Force=Image: pull toward brighter pixels (needs the texture input)");
        vivid::description(attract_threshold, "Only pixels brighter than this attract");
        vivid::visible_when_eq(attract_strength,  force_mode, {2});
        vivid::visible_when_eq(attract_threshold, force_mode, {2});
        vivid::visible_when_eq(grad_step,         force_mode, {2});

        // -- Flow steering (force_mode = Flow) --------------------------------
        vivid::param_group(flow_force, "Flow Force");
        vivid::description(flow_force, "Force=Flow: continuously steer particles along the motion vector from a Motion(Flow) mask on emit_mask");
        vivid::visible_when_eq(flow_force, force_mode, {3});

        // -- Flock (force_mode = Flock) ---------------------------------------
        vivid::param_group(view_radius, "Flock");
        vivid::param_group(sep_radius,  "Flock");
        vivid::param_group(separation,  "Flock");
        vivid::param_group(alignment,   "Flock");
        vivid::param_group(cohesion,    "Flock");
        vivid::param_group(max_speed,   "Flock");
        vivid::param_group(min_speed,   "Flock");
        vivid::param_group(wrap,        "Flock");
        vivid::visible_when_eq(view_radius, force_mode, {1});
        vivid::visible_when_eq(sep_radius,  force_mode, {1});
        vivid::visible_when_eq(separation,  force_mode, {1});
        vivid::visible_when_eq(alignment,   force_mode, {1});
        vivid::visible_when_eq(cohesion,    force_mode, {1});
        vivid::visible_when_eq(max_speed,   force_mode, {1});
        vivid::visible_when_eq(min_speed,   force_mode, {1});
        vivid::visible_when_eq(wrap,        force_mode, {1});

        // -- Population (Fixed) -----------------------------------------------
        vivid::param_group(fixed_seed, "Population");
        vivid::visible_when_eq(fixed_seed, population, {1});

        // -- Color modes (Velocity / Age) -------------------------------------
        vivid::param_group(vel_scale, "Color");
        vivid::param_group(age_r,     "Color");
        vivid::param_group(age_g,     "Color");
        vivid::param_group(age_b,     "Color");
        vivid::description(vel_scale, "Color=Velocity: how strongly speed maps to brightness");
        vivid::visible_when_eq(vel_scale, color_mode, {1});
        vivid::visible_when_eq(age_r,     color_mode, {2});
        vivid::visible_when_eq(age_g,     color_mode, {2});
        vivid::visible_when_eq(age_b,     color_mode, {2});

        // -- Polygon render ---------------------------------------------------
        vivid::param_group(sides,       "Appearance");
        vivid::param_group(star_factor, "Appearance");
        vivid::visible_when_eq(sides,       render_shape, {1});
        vivid::visible_when_eq(star_factor, render_shape, {1});

        out.push_back(&vel_scale);
        out.push_back(&age_r);
        out.push_back(&age_g);
        out.push_back(&age_b);
        out.push_back(&sides);
        out.push_back(&star_factor);

        out.push_back(&emit_x);
        out.push_back(&emit_y);
        out.push_back(&ring_radius);
        out.push_back(&ring_thickness);
        out.push_back(&grid_cols);
        out.push_back(&grid_rows);
        out.push_back(&emit_gain);
        out.push_back(&emit_threshold);
        out.push_back(&emit_flow);
        out.push_back(&flow_strength);
        out.push_back(&flow_force);
        out.push_back(&attract_strength);
        out.push_back(&attract_threshold);
        out.push_back(&grad_step);
        out.push_back(&view_radius);
        out.push_back(&sep_radius);
        out.push_back(&separation);
        out.push_back(&alignment);
        out.push_back(&cohesion);
        out.push_back(&max_speed);
        out.push_back(&min_speed);
        out.push_back(&wrap);
        out.push_back(&fixed_seed);

        out.push_back(&population);
        out.push_back(&emit_shape);
        out.push_back(&force_mode);
        out.push_back(&color_mode);
        out.push_back(&render_shape);
        out.push_back(&blend);
        out.push_back(&count);
        out.push_back(&emission_rate);
        out.push_back(&lifetime);
        out.push_back(&speed);
        out.push_back(&gravity);
        out.push_back(&spread);
        out.push_back(&drag);
        out.push_back(&curl_strength);
        out.push_back(&noise_scale);
        out.push_back(&noise_speed);
        out.push_back(&noise_octaves);
        out.push_back(&size);
        out.push_back(&softness);
        out.push_back(&bounds);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
        out.push_back(&color_amount);
        out.push_back(&color_gain);
        out.push_back(&learning_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Optional image the particles can read (Color=Image, Force=Image,
        // Emit=Image). Disconnected = treated as black (those modes no-op).
        out.push_back({"texture",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"emit_mask", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    ~Particles2D() override {
        vivid::gpu::release(compute_pipeline_);
        vivid::gpu::release(compute_shader_);
        vivid::gpu::release(compute_pipe_layout_);
        vivid::gpu::release(compute_bgl_);
        vivid::gpu::release(bind_group_a_);
        vivid::gpu::release(bind_group_b_);
        vivid::gpu::release(particle_buf_a_);
        vivid::gpu::release(particle_buf_b_);
        vivid::gpu::release(instance_buf_);
        vivid::gpu::release(params_ubo_);
        vivid::gpu::release(counter_buf_);
        vivid::gpu::release(fallback_view_);
        vivid::gpu::release(fallback_tex_);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        uint32_t max_count = static_cast<uint32_t>(count.int_value());
        if (max_count == 0) max_count = 1;

        // Rebuild (zero-fills -> all dead -> respawn) on count change, and on a
        // Fixed-population seed/mode change so the persistent scatter re-seeds.
        const int pop = population.int_value();
        const int fseed = fixed_seed.int_value();
        bool need_reset = (max_count != current_count_ || !compute_pipeline_);
        if (pop != last_population_) need_reset = true;
        if (pop == 1 && fseed != last_fixed_seed_) need_reset = true;
        if (need_reset) {
            rebuild_gpu_resources(ctx, max_count);
        }
        last_population_  = pop;
        last_fixed_seed_  = fseed;
        if (!compute_pipeline_) return;

        // Resolve the image input (or 1x1 fallback) and rebind if it changed.
        if (!fallback_view_) create_fallback(ctx);
        WGPUTextureView img = (ctx->input_texture_views && ctx->input_texture_count >= 1 &&
                               ctx->input_texture_views[0])
                              ? ctx->input_texture_views[0] : fallback_view_;
        // Emission mask = 2nd texture input; disconnected -> reuse the color view
        // so emission keys on color luma exactly as before.
        WGPUTextureView mask = (ctx->input_texture_views && ctx->input_texture_count >= 2 &&
                                ctx->input_texture_views[1])
                               ? ctx->input_texture_views[1] : img;
        if (img != cached_input_tex_ || mask != cached_mask_tex_) {
            rebuild_bind_groups(ctx, img, mask);
            cached_input_tex_ = img;
            cached_mask_tex_  = mask;
        }

        float dt = static_cast<float>(ctx->delta_time);

        // Fractional spawn accumulator.
        spawn_accumulator_ += emission_rate.value * dt;
        uint32_t new_spawns = static_cast<uint32_t>(spawn_accumulator_);
        spawn_accumulator_ -= static_cast<float>(new_spawns);
        if (new_spawns > max_count) new_spawns = max_count;

        // Upload params uniform.
        const bool beginner = (learning_mode.int_value() == 1);
        ParamsData params{};
        params.max_count     = max_count;
        params.new_spawns    = new_spawns;
        params.dt            = dt;
        params.gravity       = gravity.value;
        params.speed         = speed.value;
        params.spread_rad    = spread.value * (3.14159265358979f / 180.0f);
        params.lifetime      = lifetime.value;
        params.size          = size.value;
        params.color[0]      = r.value;
        params.color[1]      = g.value;
        params.color[2]      = b.value;
        params.color[3]      = a.value;
        params.seed          = frame_counter_++;
        params.noise_octaves = beginner ? 1u : static_cast<uint32_t>(noise_octaves.int_value());
        params.noise_scale   = beginner ? 1.0f : noise_scale.value;
        params.noise_speed   = beginner ? 0.0f : noise_speed.value;
        params.curl_strength = beginner ? 0.0f : curl_strength.value;
        params.drag          = beginner ? 0.05f : drag.value;
        elapsed_time_ += dt;
        params.time          = elapsed_time_;
        params.bounds        = bounds.value;
        params.learning_mode = beginner ? 1u : 0u;
        params.population    = static_cast<uint32_t>(population.int_value());
        params.emit_shape    = static_cast<uint32_t>(emit_shape.int_value());
        params.force_mode    = static_cast<uint32_t>(force_mode.int_value());
        params.color_mode    = static_cast<uint32_t>(color_mode.int_value());
        params.render_shape  = static_cast<uint32_t>(render_shape.int_value());
        params.blend         = static_cast<uint32_t>(blend.int_value());
        params.color_amount  = color_amount.value;
        params.color_gain    = color_gain.value;
        params.emit_x            = emit_x.value;
        params.emit_y            = emit_y.value;
        params.ring_radius       = ring_radius.value;
        params.ring_thickness    = ring_thickness.value;
        params.grid_cols         = static_cast<uint32_t>(grid_cols.int_value());
        params.grid_rows         = static_cast<uint32_t>(grid_rows.int_value());
        params.emit_gain         = emit_gain.value;
        params.emit_threshold    = emit_threshold.value;
        params.emit_flow         = static_cast<uint32_t>(emit_flow.int_value());
        params.flow_strength     = flow_strength.value;
        params.flow_force        = flow_force.value;
        params.attract_strength  = attract_strength.value;
        params.attract_threshold = attract_threshold.value;
        params.grad_step         = grad_step.value;
        params.view_radius       = view_radius.value;
        params.sep_radius        = sep_radius.value;
        params.separation        = separation.value;
        params.alignment         = alignment.value;
        params.cohesion          = cohesion.value;
        params.max_speed         = max_speed.value;
        params.min_speed         = min_speed.value;
        params.wrap              = static_cast<uint32_t>(wrap.int_value());
        params.fixed_seed        = static_cast<uint32_t>(fixed_seed.int_value());
        params.vel_scale         = vel_scale.value;
        params.age_r             = age_r.value;
        params.age_g             = age_g.value;
        params.age_b             = age_b.value;
        params.sides             = static_cast<uint32_t>(sides.int_value());
        params.star_factor       = star_factor.value;
        wgpuQueueWriteBuffer(ctx->queue, params_ubo_, 0, &params, sizeof(params));

        // Reset atomic counter.
        uint32_t zero = 0;
        wgpuQueueWriteBuffer(ctx->queue, counter_buf_, 0, &zero, sizeof(zero));

        // Compute pass.
        WGPUComputePassDescriptor cp{};
        cp.label = vivid_sv("Particles2D Compute");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(
            ctx->command_encoder, &cp);
        wgpuComputePassEncoderSetPipeline(pass, compute_pipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0,
            ping_ ? bind_group_a_ : bind_group_b_, 0, nullptr);
        uint32_t workgroups = (max_count + 255) / 256;
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroups, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);

        ping_ = !ping_;

        // Emit the drawable. Shape + blend follow the render/blend modes.
        const int rshape = render_shape.int_value();
        vivid::gpu::drawable_identity(output_);
        output_.type            = vivid::gpu::VIVID_DRAWABLE2D_SHAPE;
        output_.blend_mode      = (blend.int_value() == 1)
                                    ? vivid::gpu::VIVID_BLEND_ALPHA
                                    : vivid::gpu::VIVID_BLEND_ADDITIVE;
        // Circle/Sprite = 0 sides; Polygon = N sides; Aligned = triangle.
        int sides_out = 0;
        if (rshape == 1)      sides_out = sides.int_value();   // Polygon
        else if (rshape == 2) sides_out = 3;                   // Aligned
        output_.shape_sides     = sides_out;
        output_.shape_softness  = softness.value;
        output_.shape_star_factor = (rshape == 1) ? star_factor.value : 0.0f;
        output_.color[0] = 1.0f;
        output_.color[1] = 1.0f;
        output_.color[2] = 1.0f;
        output_.color[3] = 1.0f;
        output_.instance_buffer = instance_buf_;
        output_.instance_count  = max_count;
        ctx->custom_outputs[0]  = &output_;
    }

private:
    vivid::gpu::VividDrawable2D output_{};

    // Compute pipeline
    WGPUComputePipeline  compute_pipeline_    = nullptr;
    WGPUShaderModule     compute_shader_      = nullptr;
    WGPUPipelineLayout   compute_pipe_layout_ = nullptr;
    WGPUBindGroupLayout  compute_bgl_         = nullptr;

    // Ping-pong bind groups
    WGPUBindGroup bind_group_a_ = nullptr;
    WGPUBindGroup bind_group_b_ = nullptr;

    // GPU buffers
    WGPUBuffer particle_buf_a_ = nullptr;
    WGPUBuffer particle_buf_b_ = nullptr;
    WGPUBuffer instance_buf_   = nullptr;
    WGPUBuffer params_ubo_     = nullptr;
    WGPUBuffer counter_buf_    = nullptr;

    // Texture input (image coupling). A 1x1 black fallback is bound when the
    // `texture` input is disconnected so the compute shader always has a valid
    // binding (image modes then read black = no effect).
    WGPUTexture     fallback_tex_     = nullptr;
    WGPUTextureView fallback_view_    = nullptr;
    WGPUTextureView cached_input_tex_ = nullptr;
    WGPUTextureView cached_mask_tex_  = nullptr;
    uint64_t        particle_bytes_   = 0;

    uint32_t current_count_     = 0;
    bool     ping_              = true;
    float    spawn_accumulator_ = 0.0f;
    uint32_t frame_counter_     = 0;
    float    elapsed_time_      = 0.0f;
    int      last_population_    = -1;
    int      last_fixed_seed_    = -1;

    static constexpr uint64_t kParticleRecordBytes = 48;  // pos, vel, age, lifetime, 2 pad, color vec4f
    static constexpr uint64_t kInstanceRecordBytes = sizeof(vivid::gpu::InstanceData2D);

    void rebuild_gpu_resources(const VividGpuContext* gpu, uint32_t max_count) {
        vivid::gpu::release(compute_pipeline_);
        vivid::gpu::release(compute_shader_);
        vivid::gpu::release(compute_pipe_layout_);
        vivid::gpu::release(compute_bgl_);
        vivid::gpu::release(bind_group_a_);
        vivid::gpu::release(bind_group_b_);
        vivid::gpu::release(particle_buf_a_);
        vivid::gpu::release(particle_buf_b_);
        vivid::gpu::release(instance_buf_);
        vivid::gpu::release(params_ubo_);
        vivid::gpu::release(counter_buf_);

        current_count_ = max_count;
        ping_          = true;

        auto make_storage = [&](const char* label, uint64_t bytes) -> WGPUBuffer {
            WGPUBufferDescriptor d{};
            d.label = vivid_sv(label);
            d.size  = bytes;
            d.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            return wgpuDeviceCreateBuffer(gpu->device, &d);
        };

        uint64_t particle_bytes = static_cast<uint64_t>(max_count) * kParticleRecordBytes;
        if (particle_bytes < kParticleRecordBytes) particle_bytes = kParticleRecordBytes;
        particle_buf_a_ = make_storage("Particles2D Buf A", particle_bytes);
        particle_buf_b_ = make_storage("Particles2D Buf B", particle_bytes);

        uint64_t instance_bytes = static_cast<uint64_t>(max_count) * kInstanceRecordBytes;
        if (instance_bytes < kInstanceRecordBytes) instance_bytes = kInstanceRecordBytes;
        instance_buf_ = make_storage("Particles2D Instances", instance_bytes);

        // Zero-fill particle buffers — all particles start dead.
        std::vector<uint8_t> zeros(static_cast<size_t>(particle_bytes), 0);
        wgpuQueueWriteBuffer(gpu->queue, particle_buf_a_, 0, zeros.data(), particle_bytes);
        wgpuQueueWriteBuffer(gpu->queue, particle_buf_b_, 0, zeros.data(), particle_bytes);

        params_ubo_  = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(ParamsData),
                                                        "Particles2D Params");
        counter_buf_ = make_storage("Particles2D Counter", 4);

        // Compile compute shader.
        std::string wgsl = std::string(vivid::gpu::WGSL_CONSTANTS) + kParticles2DCompute;
        WGPUShaderSourceWGSL src{};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code        = vivid_sv(wgsl.c_str());
        WGPUShaderModuleDescriptor sm{};
        sm.nextInChain = &src.chain;
        sm.label       = vivid_sv("Particles2D Compute Shader");
        compute_shader_ = wgpuDeviceCreateShaderModule(gpu->device, &sm);
        if (!compute_shader_) {
            std::fprintf(stderr, "[particles_2d] shader compile failed\n");
            return;
        }

        // Bind group layout: 7 entries (5 buffers + color texture + emit_mask texture).
        WGPUBindGroupLayoutEntry entries[7]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].buffer.type = WGPUBufferBindingType_Storage;
        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].buffer.type = WGPUBufferBindingType_Storage;
        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].buffer.type = WGPUBufferBindingType_Uniform;
        entries[3].buffer.minBindingSize = sizeof(ParamsData);
        entries[4].binding = 4;
        entries[4].visibility = WGPUShaderStage_Compute;
        entries[4].buffer.type = WGPUBufferBindingType_Storage;
        entries[4].buffer.minBindingSize = 4;
        entries[5].binding = 5;
        entries[5].visibility = WGPUShaderStage_Compute;
        entries[5].texture.sampleType    = WGPUTextureSampleType_Float;
        entries[5].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[5].texture.multisampled  = false;
        entries[6].binding = 6;
        entries[6].visibility = WGPUShaderStage_Compute;
        entries[6].texture.sampleType    = WGPUTextureSampleType_Float;
        entries[6].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[6].texture.multisampled  = false;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label      = vivid_sv("Particles2D BGL");
        bgl_desc.entryCount = 7;
        bgl_desc.entries    = entries;
        compute_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label                = vivid_sv("Particles2D Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts     = &compute_bgl_;
        compute_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUComputePipelineDescriptor cp_desc{};
        cp_desc.label              = vivid_sv("Particles2D Compute Pipeline");
        cp_desc.layout             = compute_pipe_layout_;
        cp_desc.compute.module     = compute_shader_;
        cp_desc.compute.entryPoint = vivid_sv("cs_main");
        compute_pipeline_ = wgpuDeviceCreateComputePipeline(gpu->device, &cp_desc);
        if (!compute_pipeline_) {
            std::fprintf(stderr, "[particles_2d] compute pipeline creation failed\n");
            return;
        }

        particle_bytes_   = particle_bytes;
        create_fallback(gpu);
        cached_input_tex_ = fallback_view_;
        cached_mask_tex_  = fallback_view_;
        rebuild_bind_groups(gpu, fallback_view_, fallback_view_);
    }

    void create_bind_group(const VividGpuContext* gpu,
                            WGPUBuffer read_buf, WGPUBuffer write_buf,
                            uint64_t particle_bytes, WGPUTextureView img_view,
                            WGPUTextureView mask_view,
                            WGPUBindGroup* out_bg, const char* label) {
        uint64_t instance_bytes = static_cast<uint64_t>(current_count_) * kInstanceRecordBytes;
        if (instance_bytes < kInstanceRecordBytes) instance_bytes = kInstanceRecordBytes;

        WGPUBindGroupEntry entries[7]{};
        entries[0].binding = 0;
        entries[0].buffer  = read_buf;
        entries[0].size    = particle_bytes;
        entries[1].binding = 1;
        entries[1].buffer  = write_buf;
        entries[1].size    = particle_bytes;
        entries[2].binding = 2;
        entries[2].buffer  = instance_buf_;
        entries[2].size    = instance_bytes;
        entries[3].binding = 3;
        entries[3].buffer  = params_ubo_;
        entries[3].size    = sizeof(ParamsData);
        entries[4].binding = 4;
        entries[4].buffer  = counter_buf_;
        entries[4].size    = 4;
        entries[5].binding     = 5;
        entries[5].textureView = img_view;
        entries[6].binding     = 6;
        entries[6].textureView = mask_view;

        WGPUBindGroupDescriptor desc{};
        desc.label      = vivid_sv(label);
        desc.layout     = compute_bgl_;
        desc.entryCount = 7;
        desc.entries    = entries;
        *out_bg = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    // Create the 1x1 black fallback texture (bound when `texture` is disconnected).
    void create_fallback(const VividGpuContext* gpu) {
        if (fallback_view_) return;
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Particles2D Fallback");
        td.size = {1, 1, 1};
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);
        WGPUTextureViewDescriptor vd{};
        vd.format = WGPUTextureFormat_RGBA8Unorm;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        fallback_view_ = wgpuTextureCreateView(fallback_tex_, &vd);
        const uint8_t black[4] = {0, 0, 0, 255};
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = fallback_tex_;
        dst.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{};
        lay.bytesPerRow  = 4;
        lay.rowsPerImage = 1;
        WGPUExtent3D ext = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dst, black, 4, &lay, &ext);
    }

    // Rebuild both ping-pong bind groups with the given color + emit-mask textures.
    void rebuild_bind_groups(const VividGpuContext* gpu, WGPUTextureView img_view,
                             WGPUTextureView mask_view) {
        vivid::gpu::release(bind_group_a_);
        vivid::gpu::release(bind_group_b_);
        create_bind_group(gpu, particle_buf_a_, particle_buf_b_, particle_bytes_,
                          img_view, mask_view, &bind_group_a_, "Particles2D BG A");
        create_bind_group(gpu, particle_buf_b_, particle_buf_a_, particle_bytes_,
                          img_view, mask_view, &bind_group_b_, "Particles2D BG B");
    }
};

VIVID_DEFINE_OP(Particles2D) {
}


VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
