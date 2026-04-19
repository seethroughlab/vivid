// ShapeField — GPU SDF geometry with owned internal LFO pools.
//
// Renders N instances of a chosen SDF shape (circle, triangle, square, pentagon,
// hexagon, star) in configurable spatial layouts. Three owned LFO pools
// (scale, rotation, color_mod) provide per-instance modulation via ChildOp<LFO>.

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static constexpr int kMaxInstances = 64;

// Lane-array input port indices. Phase 5 added the first five; Phase 6
// appended `rotation` and `shape_idx` (keep Phase-5 ordinals unchanged so
// no consumer breaks).
static constexpr int kLanePosX       = 0;
static constexpr int kLanePosY       = 1;
static constexpr int kLaneSize       = 2;
static constexpr int kLaneHue        = 3;
static constexpr int kLaneBrightness = 4;
static constexpr int kLaneRotation   = 5;
static constexpr int kLaneShapeIdx   = 6;
static constexpr int kLaneInputCount = 7;

// Simple LCG hash for deterministic pseudo-random layout positions
static float hash_float(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f; // [0, 1)
}

// Convert HSV (h,s,v all in [0,1]) to RGB. Used when the `hue` lane is
// connected so each instance gets a full-saturation rainbow slot.
static void hsv_to_rgb(float h, float s, float v, float* r, float* g, float* b) {
    h = h - std::floor(h);                        // wrap to [0,1)
    const float hp = h * 6.0f;
    const float c  = v * s;
    const float x  = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float rr = 0.0f, gg = 0.0f, bb = 0.0f;
    if      (hp < 1.0f) { rr = c; gg = x; }
    else if (hp < 2.0f) { rr = x; gg = c; }
    else if (hp < 3.0f) {         gg = c; bb = x; }
    else if (hp < 4.0f) {         gg = x; bb = c; }
    else if (hp < 5.0f) { rr = x;          bb = c; }
    else                { rr = c;          bb = x; }
    const float m = v - c;
    *r = rr + m; *g = gg + m; *b = bb + m;
}

// ── WGSL fragment shader ────────────────────────────────────────────────

static const char* kShapeFieldFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    active_count: f32,
    softness: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
    instances_geo:   array<vec4f, 64>,   // xy=position, z=size, w=rotation
    instances_color: array<vec4f, 64>,   // rgb=color, a=alpha
    instances_shape: array<vec4f, 16>,   // 4 shape indices per vec4, 64 total
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

// Phase 6: per-instance shape dispatch. `shape_idx` in [0,5] selects a
// primitive; unknown values fall through to Square.
fn sd_for_shape(p_in: vec2f, size: f32, rot: f32, shape_idx: i32) -> f32 {
    // Apply per-instance rotation
    let c = cos(rot);
    let s = sin(rot);
    let p = vec2f(c * p_in.x + s * p_in.y, -s * p_in.x + c * p_in.y);

    // Circle has no polygon math — early-out.
    if (shape_idx == 0) {
        return length(p) - size;
    }

    var sides = 4.0;
    var sf    = 0.0;
    switch (shape_idx) {
        case 1: { sides = 3.0; }              // Triangle
        case 2: { sides = 4.0; }              // Square
        case 3: { sides = 5.0; }              // Pentagon
        case 4: { sides = 6.0; }              // Hexagon
        case 5: { sides = 5.0; sf = 0.5; }    // Star (5-pointed)
        default: { sides = 4.0; }
    }

    let an = PI / sides;
    let angle = atan2(p.y, p.x);

    if (sf < 0.001) {
        // Regular polygon via sector folding
        let sector = round(angle / (2.0 * an));
        let folded = angle - sector * 2.0 * an;
        let q = length(p) * vec2f(cos(folded), abs(sin(folded)));
        return q.x - size * cos(an);
    } else {
        // Star — alternating outer/inner vertices
        let inner_r = size * (1.0 - sf);
        let sector = round(angle / (2.0 * an));
        let folded = abs(angle - sector * 2.0 * an);

        let lp = length(p);
        let q = vec2f(lp * cos(folded), lp * sin(folded));

        let v0 = vec2f(size, 0.0);
        let v1 = vec2f(inner_r * cos(an), inner_r * sin(an));
        let edge = v1 - v0;
        let normal = normalize(vec2f(edge.y, -edge.x));
        return dot(q - v0, normal);
    }
}

// Unpack shape index for instance `i` from the vec4-packed array.
// Explicit switch avoids any uniform-vector dynamic-indexing portability gotcha.
fn get_shape_idx(i: i32) -> i32 {
    let v = u.instances_shape[i / 4];
    switch (i % 4) {
        case 0:  { return i32(v.x); }
        case 1:  { return i32(v.y); }
        case 2:  { return i32(v.z); }
        default: { return i32(v.w); }
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = u.resolution.x / u.resolution.y;
    let uv = vec2f(input.uv.x * aspect, input.uv.y);
    let n = i32(u.active_count);

    var accum = vec4f(0.0);

    for (var i = 0; i < n; i++) {
        let geo = u.instances_geo[i];
        let col = u.instances_color[i];
        let sidx = get_shape_idx(i);

        let ppos = vec2f(geo.x * aspect, geo.y);
        let d = sd_for_shape(uv - ppos, geo.z, geo.w, sidx);
        let alpha = (1.0 - smoothstep(-u.softness, u.softness, d)) * col.a;

        accum += vec4f(col.rgb * alpha, alpha);
    }

    return vec4f(min(accum.rgb, vec3f(1.0)), min(accum.a, 1.0));
}
)";

// ── Uniform struct (must match WGSL layout exactly) ─────────────────────

struct ShapeFieldUniforms {
    float resolution[2];
    float time;
    float active_count;
    float softness;
    float _pad0;
    float _pad1;
    float _pad2;
    float instances_geo[kMaxInstances * 4];    // array<vec4f, 64>
    float instances_color[kMaxInstances * 4];  // array<vec4f, 64>
    float instances_shape[kMaxInstances];      // flat 64 f32; WGSL array<vec4f, 16>
};

// ── Instance state ──────────────────────────────────────────────────────

struct Instance {
    float x = 0.5f, y = 0.5f;   // base position in [0,1] UV space
    float phase = 0.0f;          // per-instance animation phase offset
};

// ── Owned LFO pool ─────────────────────────────────────────────────────

struct LfoPool {
    std::vector<vivid::ChildOp<LFO>> instances;
    int cached_count = 0;
};

// ── Waveform index mapping ─────────────────────────────────────────────
// Host param enum:  0=Sine, 1=Triangle, 2=Saw, 3=Square
// LFO waveform enum: 0=sine, 1=saw, 2=square, 3=triangle
static int host_waveform_to_lfo(int host) {
    switch (host) {
        case 0: return 0; // Sine   → sine
        case 1: return 3; // Triangle → triangle
        case 2: return 1; // Saw    → saw
        case 3: return 2; // Square → square
        default: return 0;
    }
}
/**
 * @brief Renders N SDF shapes with per-instance layout and LFO modulation.
 *
 * Draws up to 64 instances of a chosen SDF shape (circle, polygon, star)
 * arranged in grid, circle, line, or random layouts. Per-instance LFO
 * modulation on scale, rotation, and color.
 *
 * @param layout Arrangement: Random, Grid, Circle, or Line.
 *
 * @tip Self-contained — no Instancer2D needed. Outputs a texture directly.
 * @tip For more flexible instancing, use Shape2D → Instancer2D with an InstanceArray2D bundle.
 * @recipe ShapeField -> Bloom -> video_out
 * @common_companions Bloom, video_out
 * @best_used_with video_out, Bloom
 * @see Shape2D, Instancer2D, Particles2D
 */
struct ShapeField : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "ShapeField";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   count     {"count",     16,    1,     kMaxInstances};
    vivid::Param<int>   shape     {"shape",     0,     {"Circle", "Triangle", "Square", "Pentagon", "Hexagon", "Star"}};
    vivid::Param<float> base_size {"base_size", 0.08f, 0.01f, 0.5f};
    vivid::Param<float> softness  {"softness",  0.0f,   0.0f, 0.05f};
    vivid::Param<float> color_r   {"color_r",   1.0f,  0.0f,  1.0f};
    vivid::Param<float> color_g   {"color_g",   1.0f,  0.0f,  1.0f};
    vivid::Param<float> color_b   {"color_b",   1.0f,  0.0f,  1.0f};
    vivid::Param<int>   layout    {"layout",    0,     {"Random", "Grid", "Circle", "Line"}};
    vivid::Param<int>   animate   {"animate",   0,     {"Off", "On"}};
    vivid::Param<float> speed     {"speed",     1.0f,  0.0f,  5.0f};

    // ── Scale Modulation ────────────────────────────────────────
    vivid::Param<int>   scale_enabled  {"scale_enabled",  0, {"Off", "On"}};
    vivid::Param<float> scale_amount   {"scale_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> scale_rate     {"scale_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   scale_waveform {"scale_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> scale_offset   {"scale_offset",   0.0f, -1.0f, 1.0f};
    // ── Rotation Modulation ─────────────────────────────────────
    vivid::Param<int>   rotation_enabled  {"rotation_enabled",  0, {"Off", "On"}};
    vivid::Param<float> rotation_amount   {"rotation_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> rotation_rate     {"rotation_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   rotation_waveform {"rotation_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> rotation_offset   {"rotation_offset",   0.0f, -1.0f, 1.0f};
    // ── Color Mod Modulation ────────────────────────────────────
    vivid::Param<int>   color_mod_enabled  {"color_mod_enabled",  0, {"Off", "On"}};
    vivid::Param<float> color_mod_amount   {"color_mod_amount",   1.0f, 0.0f, 2.0f};
    vivid::Param<float> color_mod_rate     {"color_mod_rate",     1.0f, 0.01f, 20.0f};
    vivid::Param<int>   color_mod_waveform {"color_mod_waveform", 0, {"Sine", "Triangle", "Saw", "Square"}};
    vivid::Param<float> color_mod_offset   {"color_mod_offset",   0.0f, -1.0f, 1.0f};

    ShapeField() {
        vivid::description(count, "Number of shape instances to render, 1-64");
        vivid::description(shape, "SDF shape type: Circle, Triangle, Square, Pentagon, Hexagon, or Star");
        vivid::description(base_size, "Base radius of each shape instance");
        vivid::description(softness, "Edge softness of the shape rendering");
        vivid::description(color_r, "Red component of the shape color");
        vivid::description(color_g, "Green component of the shape color");
        vivid::description(color_b, "Blue component of the shape color");
        vivid::description(layout, "Spatial arrangement: Random, Grid, Circle, or Line");
        vivid::description(animate, "Enable built-in layout animation");
        vivid::description(speed, "Speed of the layout animation");
        vivid::description(scale_enabled, "Enable per-instance LFO modulation of scale");
        vivid::description(scale_amount, "Strength of the scale modulation");
        vivid::description(scale_rate, "Frequency of the scale modulation LFO in Hz");
        vivid::description(scale_waveform, "Waveform shape for the scale modulation LFO");
        vivid::description(scale_offset, "DC offset added to the scale modulation LFO");
        vivid::description(rotation_enabled, "Enable per-instance LFO modulation of rotation");
        vivid::description(rotation_amount, "Strength of the rotation modulation");
        vivid::description(rotation_rate, "Frequency of the rotation modulation LFO in Hz");
        vivid::description(rotation_waveform, "Waveform shape for the rotation modulation LFO");
        vivid::description(rotation_offset, "DC offset added to the rotation modulation LFO");
        vivid::description(color_mod_enabled, "Enable per-instance LFO modulation of brightness");
        vivid::description(color_mod_amount, "Strength of the color brightness modulation");
        vivid::description(color_mod_rate, "Frequency of the color modulation LFO in Hz");
        vivid::description(color_mod_waveform, "Waveform shape for the color modulation LFO");
        vivid::description(color_mod_offset, "DC offset added to the color modulation LFO");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(count,     2, 0);
        vivid::layout_row(shape,     2, 1);
        // base_size, softness, layout: full-width sliders
        vivid::display_hint(color_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_b, VIVID_DISPLAY_COLOR);
        // color: compound widget handles COLOR triplet automatically
        vivid::layout_row(animate,   2, 0);
        vivid::layout_row(speed,     2, 1);
        out.push_back(&count);
        out.push_back(&shape);
        out.push_back(&base_size);
        out.push_back(&softness);
        out.push_back(&layout);
        out.push_back(&color_r);
        out.push_back(&color_g);
        out.push_back(&color_b);
        out.push_back(&animate);
        out.push_back(&speed);

        // Scale modulation params
        vivid::layout_row(scale_enabled,  2, 0);
        vivid::layout_row(scale_waveform, 2, 1);
        out.push_back(&scale_enabled);
        out.push_back(&scale_amount);
        out.push_back(&scale_rate);
        out.push_back(&scale_waveform);
        out.push_back(&scale_offset);

        // Rotation modulation params
        vivid::layout_row(rotation_enabled,  2, 0);
        vivid::layout_row(rotation_waveform, 2, 1);
        out.push_back(&rotation_enabled);
        out.push_back(&rotation_amount);
        out.push_back(&rotation_rate);
        out.push_back(&rotation_waveform);
        out.push_back(&rotation_offset);

        // Color mod modulation params
        vivid::layout_row(color_mod_enabled,  2, 0);
        vivid::layout_row(color_mod_waveform, 2, 1);
        out.push_back(&color_mod_enabled);
        out.push_back(&color_mod_amount);
        out.push_back(&color_mod_rate);
        out.push_back(&color_mod_waveform);
        out.push_back(&color_mod_offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Phase 5 — lane-array inputs. Each is optional; unconnected lanes
        // fall back to the static/LFO-driven values computed elsewhere, so
        // pre-Phase-5 graphs render bit-identically.
        VividPortDescriptor pos_x_port{"pos_x", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(pos_x_port, "position_xy");
        vivid::semantic_shape(pos_x_port, "lane_array");
        vivid::semantic_intent(pos_x_port, "x_component");
        vivid::description(pos_x_port,
            "Per-instance x position in [0, 1]. When connected, overrides layout.");
        out.push_back(pos_x_port);   // 0 = kLanePosX

        VividPortDescriptor pos_y_port{"pos_y", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(pos_y_port, "position_xy");
        vivid::semantic_shape(pos_y_port, "lane_array");
        vivid::semantic_intent(pos_y_port, "y_component");
        vivid::description(pos_y_port,
            "Per-instance y position in [0, 1]. When connected, overrides layout.");
        out.push_back(pos_y_port);   // 1 = kLanePosY

        VividPortDescriptor size_port{"size", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(size_port, "amplitude_linear");
        vivid::semantic_shape(size_port, "lane_array");
        vivid::semantic_intent(size_port, "scale_multiplier");
        vivid::description(size_port,
            "Per-instance size multiplier on base_size. 1.0 = default.");
        out.push_back(size_port);    // 2 = kLaneSize

        VividPortDescriptor hue_port{"hue", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(hue_port, "color_hue");
        vivid::semantic_shape(hue_port, "lane_array");
        vivid::semantic_intent(hue_port, "hue_cycles");
        vivid::description(hue_port,
            "Per-instance hue in [0, 1]. HSV(hue, 1, 1) replaces color_r/g/b when connected.");
        out.push_back(hue_port);     // 3 = kLaneHue

        VividPortDescriptor bright_port{"brightness", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(bright_port, "amplitude_linear");
        vivid::semantic_shape(bright_port, "lane_array");
        vivid::semantic_intent(bright_port, "per_note_amplitude");
        vivid::description(bright_port,
            "Per-instance intensity in [0, 1]. Multiplies RGB and alpha.");
        out.push_back(bright_port);  // 4 = kLaneBrightness

        // Phase 6 lane inputs ----------------------------------------------
        VividPortDescriptor rot_port{"rotation", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(rot_port, "rotation_radians");
        vivid::semantic_shape(rot_port, "lane_array");
        vivid::semantic_intent(rot_port, "angle_turns");
        vivid::description(rot_port,
            "Per-instance rotation in turns [0, 1]. Overrides the rotation LFO when connected.");
        out.push_back(rot_port);     // 5 = kLaneRotation

        VividPortDescriptor shape_port{"shape_idx", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT};
        vivid::semantic_tag(shape_port, "enum_index");
        vivid::semantic_shape(shape_port, "lane_array");
        vivid::semantic_intent(shape_port, "shape_selector");
        vivid::description(shape_port,
            "Per-instance shape index: 0=Circle, 1=Triangle, 2=Square, 3=Pentagon, 4=Hexagon, 5=Star. "
            "Overrides the global shape param when connected; clamped to [0, 5].");
        out.push_back(shape_port);   // 6 = kLaneShapeIdx

        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }



    // Fetch lane data + length for input port `idx`. Returns data=nullptr
    // when the port is disconnected or the lane is empty.
    struct LaneSlot { const float* data = nullptr; uint32_t length = 0; };
    static LaneSlot lane_slot(const VividGpuContext* ctx, int idx) {
        if (!ctx->input_lanes) return {};
        const auto& lane = ctx->input_lanes[idx];
        if (!lane.data || lane.length == 0) return {};
        return {lane.data, lane.length};
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_ && !lazy_init(ctx)) return;

        // ── Phase 5: lane-driven effective count ─────────────────────
        // When any lane input is connected and non-empty, the max of its
        // lengths wins. Users can drive just one lane (e.g. hue) without
        // touching count; or drive pos_x with 8 lanes and render 8 shapes
        // regardless of the static count.
        const LaneSlot lane_pos_x    = lane_slot(ctx, kLanePosX);
        const LaneSlot lane_pos_y    = lane_slot(ctx, kLanePosY);
        const LaneSlot lane_size     = lane_slot(ctx, kLaneSize);
        const LaneSlot lane_hue      = lane_slot(ctx, kLaneHue);
        const LaneSlot lane_bright   = lane_slot(ctx, kLaneBrightness);
        const LaneSlot lane_rotation = lane_slot(ctx, kLaneRotation);
        const LaneSlot lane_shape    = lane_slot(ctx, kLaneShapeIdx);

        uint32_t lane_max = std::max({lane_pos_x.length, lane_pos_y.length,
                                      lane_size.length,  lane_hue.length,
                                      lane_bright.length,
                                      lane_rotation.length, lane_shape.length});
        int n = (lane_max > 0) ? static_cast<int>(lane_max) : count.int_value();
        if (n < 1) n = 1;
        if (n > kMaxInstances) n = kMaxInstances;

        // ── Sync LFO pools ──────────────────────────────────────────
        sync_lfo_pool(scale_pool_,     n, scale_enabled,     scale_rate,     scale_waveform,     scale_offset);
        sync_lfo_pool(rotation_pool_,  n, rotation_enabled,  rotation_rate,  rotation_waveform,  rotation_offset);
        sync_lfo_pool(color_mod_pool_, n, color_mod_enabled, color_mod_rate, color_mod_waveform, color_mod_offset);

        // ── Compute layout positions ─────────────────────────────────
        int layout_mode = layout.int_value();
        bool animating = animate.int_value() != 0;
        float t = static_cast<float>(ctx->time);
        float spd = speed.value;

        if (n != prev_count_ || layout_mode != prev_layout_) {
            compute_base_layout(n, layout_mode);
            prev_count_  = n;
            prev_layout_ = layout_mode;
        }

        // ── Pack uniforms ────────────────────────────────────────────
        // Phase 6: shape is per-instance. The global `shape` param is the
        // fallback when the shape_idx lane isn't connected; per-instance
        // dispatch now lives in the shader's sd_for_shape().
        int default_shape_idx = std::clamp(shape.int_value(), 0, 5);

        ShapeFieldUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.time          = t;
        u.active_count  = static_cast<float>(n);
        u.softness      = softness.value;

        // Shared process context for all child LFO instances
        VividFrameContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        // Process all LFO pools
        float scale_vals[kMaxInstances];
        float rotation_vals[kMaxInstances];
        float color_mod_vals[kMaxInstances];

        process_lfo_pool(scale_pool_,     &ctrl_ctx, n, scale_amount.value,     scale_vals);
        process_lfo_pool(rotation_pool_,  &ctrl_ctx, n, rotation_amount.value,  rotation_vals);
        process_lfo_pool(color_mod_pool_, &ctrl_ctx, n, color_mod_amount.value, color_mod_vals);

        float cr = color_r.value;
        float cg = color_g.value;
        float cb = color_b.value;
        float bs = base_size.value;

        for (int i = 0; i < n; ++i) {
            auto& inst = instances_[i];

            // Animated position
            float px = inst.x;
            float py = inst.y;
            if (animating) {
                switch (layout_mode) {
                    case 0: // Random: slow drift
                        px += std::cos(inst.phase + t * spd) * 0.02f;
                        py += std::sin(inst.phase + t * spd) * 0.02f;
                        break;
                    case 1: // Grid: oscillation
                        px += std::sin(inst.phase + t * spd) * 0.015f;
                        py += std::cos(inst.phase + t * spd * 0.7f) * 0.015f;
                        break;
                    case 2: { // Circle: rotate ring
                        float angle = inst.phase + t * spd * 0.5f;
                        float radius = 0.3f;
                        px = 0.5f + radius * std::cos(angle);
                        py = 0.5f + radius * std::sin(angle);
                        break;
                    }
                    case 3: // Line: sine wave
                        py += std::sin(inst.phase + t * spd) * 0.05f;
                        break;
                }
            }

            // Phase 5 — per-instance lane overrides. Each override fires
            // only if the corresponding lane is connected and has an entry
            // for this instance index. Connected lanes bypass the animated
            // layout; partial connections compose cleanly.
            if (lane_pos_x.data && static_cast<uint32_t>(i) < lane_pos_x.length) {
                px = lane_pos_x.data[i];
            }
            if (lane_pos_y.data && static_cast<uint32_t>(i) < lane_pos_y.length) {
                py = lane_pos_y.data[i];
            }

            // Pack geometry: xy=position, z=size, w=rotation
            // scale_vals: bipolar [-1,1] → remap to [0,2] so 0 = default size
            float sz = bs * std::max(0.0f, 1.0f + scale_vals[i]);
            if (lane_size.data && static_cast<uint32_t>(i) < lane_size.length) {
                sz = bs * std::max(0.0f, lane_size.data[i]);
            }
            // Rotation: lane drives in turns [0,1]; LFO pool drives in turns
            // already. Convert to radians for the shader either way.
            float rot_turns = rotation_vals[i];
            if (lane_rotation.data && static_cast<uint32_t>(i) < lane_rotation.length) {
                rot_turns = lane_rotation.data[i];
            }
            u.instances_geo[i * 4 + 0] = px;
            u.instances_geo[i * 4 + 1] = py;
            u.instances_geo[i * 4 + 2] = sz;
            u.instances_geo[i * 4 + 3] = rot_turns * 6.2831853f;

            // Phase 6: per-instance shape dispatch.
            int shape_idx_i = default_shape_idx;
            if (lane_shape.data && static_cast<uint32_t>(i) < lane_shape.length) {
                shape_idx_i = std::clamp(
                    static_cast<int>(lane_shape.data[i]), 0, 5);
            }
            u.instances_shape[i] = static_cast<float>(shape_idx_i);

            // Pack color: rgb=modulated color, a=alpha
            // color_mod_vals: bipolar [-1,1] → brightness range [0.5, 1.5]
            float mod = std::max(0.0f, 1.0f + color_mod_vals[i] * 0.5f);
            float cr_i = cr, cg_i = cg, cb_i = cb;
            if (lane_hue.data && static_cast<uint32_t>(i) < lane_hue.length) {
                hsv_to_rgb(lane_hue.data[i], 1.0f, 1.0f, &cr_i, &cg_i, &cb_i);
            }
            float bright_i = 1.0f;
            if (lane_bright.data && static_cast<uint32_t>(i) < lane_bright.length) {
                bright_i = std::max(0.0f, std::min(1.0f, lane_bright.data[i]));
            }
            u.instances_color[i * 4 + 0] = cr_i * mod * bright_i;
            u.instances_color[i * 4 + 1] = cg_i * mod * bright_i;
            u.instances_color[i * 4 + 2] = cb_i * mod * bright_i;
            u.instances_color[i * 4 + 3] = bright_i;
        }

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "ShapeField Pass");
    }

    ~ShapeField() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    // Instance state
    std::vector<Instance> instances_;
    int prev_count_  = -1;
    int prev_layout_ = -1;

    // Owned LFO pools
    LfoPool scale_pool_;
    LfoPool rotation_pool_;
    LfoPool color_mod_pool_;

    // GPU handles
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    bool lazy_init(const VividGpuContext* gpu) {
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kShapeFieldFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "ShapeField Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(ShapeFieldUniforms), "ShapeField Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(ShapeFieldUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("ShapeField BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("ShapeField Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(ShapeFieldUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("ShapeField Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, shader_, pipe_layout_, gpu->output_format, "ShapeField Pipeline");
        if (!pipeline_) return false;

        instances_.resize(kMaxInstances);
        return true;
    }

    // ── Layout computation ──────────────────────────────────────────

    void compute_base_layout(int n, int layout_mode) {
        constexpr float TAU = 6.2831853f;
        constexpr float margin = 0.1f;

        for (int i = 0; i < n; ++i) {
            instances_[i].phase = TAU * static_cast<float>(i) / static_cast<float>(n);
        }

        switch (layout_mode) {
            case 0: { // Random — deterministic from fixed seed
                uint32_t seed = 42;
                for (int i = 0; i < n; ++i) {
                    instances_[i].x = margin + hash_float(seed) * (1.0f - 2.0f * margin);
                    instances_[i].y = margin + hash_float(seed) * (1.0f - 2.0f * margin);
                }
                break;
            }
            case 1: { // Grid
                int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(n))));
                int rows = (n + cols - 1) / cols;
                for (int i = 0; i < n; ++i) {
                    int col = i % cols;
                    int row = i / cols;
                    instances_[i].x = (cols > 1)
                        ? margin + static_cast<float>(col) * (1.0f - 2.0f * margin) / static_cast<float>(cols - 1)
                        : 0.5f;
                    instances_[i].y = (rows > 1)
                        ? margin + static_cast<float>(row) * (1.0f - 2.0f * margin) / static_cast<float>(rows - 1)
                        : 0.5f;
                }
                break;
            }
            case 2: { // Circle
                float radius = 0.3f;
                for (int i = 0; i < n; ++i) {
                    float angle = TAU * static_cast<float>(i) / static_cast<float>(n);
                    instances_[i].x = 0.5f + radius * std::cos(angle);
                    instances_[i].y = 0.5f + radius * std::sin(angle);
                }
                break;
            }
            case 3: { // Line
                for (int i = 0; i < n; ++i) {
                    instances_[i].x = (n > 1)
                        ? margin + static_cast<float>(i) * (1.0f - 2.0f * margin) / static_cast<float>(n - 1)
                        : 0.5f;
                    instances_[i].y = 0.5f;
                }
                break;
            }
        }
    }

    // ── LFO pool management ─────────────────────────────────────────

    void sync_lfo_pool(LfoPool& pool, int n,
                       const vivid::Param<int>& enabled_param,
                       const vivid::Param<float>& rate_param,
                       const vivid::Param<int>& waveform_param,
                       const vivid::Param<float>& offset_param) {
        if (enabled_param.int_value() == 0) {
            if (!pool.instances.empty()) {
                pool.instances.clear();
                pool.cached_count = 0;
            }
            return;
        }

        // Resize pool if instance count changed
        if (n != pool.cached_count) {
            pool.instances.clear();
            pool.instances.resize(n);
            pool.cached_count = n;

            // Set staggered phase offset on each new instance
            for (int i = 0; i < n; ++i) {
                pool.instances[i].set_param("phase_offset",
                    static_cast<float>(i) / static_cast<float>(n));
            }
        }

        // Sync host params → child LFO params each frame
        float freq = rate_param.value;
        int   wf   = host_waveform_to_lfo(waveform_param.int_value());
        float off  = offset_param.value;

        for (int i = 0; i < n; ++i) {
            pool.instances[i].set_param("frequency", freq);
            pool.instances[i].set_param("waveform",  static_cast<float>(wf));
            pool.instances[i].set_param("offset",    off);
        }
    }

    void process_lfo_pool(LfoPool& pool, const VividFrameContext* ctx,
                          int n, float amount, float* out_values) {
        if (pool.instances.empty()) {
            for (int i = 0; i < n; ++i) out_values[i] = 0.0f;
            return;
        }

        for (int i = 0; i < n; ++i) {
            pool.instances[i].process(ctx);
            out_values[i] = pool.instances[i].output("value") * amount;
        }
    }
};

VIVID_REGISTER(ShapeField)
