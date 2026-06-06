// ShapeField — self-contained SDF emitter for the 2D drawable pipeline.
//
// Emits a tree of VividDrawable2D records (one leaf per SDF shape kind) so
// Render2D's shape-instanced pipeline draws all N instances in ≤ 6 draw calls.
// Keeps the operator's unique capabilities: owned ChildOp<LFO> pools for per-
// instance scale/rotation/color modulation, plus 7 lane-array overrides.
//
// 64-instance cap is intentional — positions ShapeField as "self-contained SDF
// field with internal modulation" vs the Shape2D + Instancer2D + external LFO
// composition path which scales to 4096 but requires explicit wiring.

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_2d.h"
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

static constexpr int kMaxInstances = 64;
static constexpr int kShapeBuckets = 6;   // Circle / Triangle / Square / Pentagon / Hexagon / Star

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

// Shape-idx → (shape_sides, shape_star_factor) for Render2D's SDF. Matches
// the mapping the fragment shader used historically; see docstring above the
// struct.
static void shape_idx_to_sdf_params(int shape_idx,
                                    uint32_t* out_sides, float* out_star_factor) {
    switch (shape_idx) {
        case 0: *out_sides = 0; *out_star_factor = 0.0f; return;  // Circle
        case 1: *out_sides = 3; *out_star_factor = 0.0f; return;  // Triangle
        case 2: *out_sides = 4; *out_star_factor = 0.0f; return;  // Square
        case 3: *out_sides = 5; *out_star_factor = 0.0f; return;  // Pentagon
        case 4: *out_sides = 6; *out_star_factor = 0.0f; return;  // Hexagon
        case 5: *out_sides = 5; *out_star_factor = 0.5f; return;  // Star
        default:*out_sides = 4; *out_star_factor = 0.0f; return;  // fallback = square
    }
}

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
 * @brief Self-contained SDF field: emits up to 64 shape instances as a drawable tree.
 *
 * Unlike the Shape2D → Instancer2D recipe, ShapeField owns three `ChildOp<LFO>`
 * pools (scale, rotation, color_mod) that produce independent per-instance
 * modulation — one LFO per instance, each with its own phase. Lane-array inputs
 * override the LFO values when connected. Output is a VividDrawable2D tree;
 * Render2D draws each of the six SDF shape kinds (circle, triangle, square,
 * pentagon, hexagon, star) with a single instanced draw call per kind.
 *
 * @param layout Arrangement: Random, Grid, Circle, or Line.
 *
 * @tip Drop-in drawable source — wire `drawable` straight into Render2D.
 * @tip For large instance counts or compute-driven sources, use Particles2D /
 *      Shape2D → Instancer2D instead; ShapeField caps at 64 on purpose.
 * @recipe ShapeField -> Render2D -> Bloom -> video_out
 * @common_companions Render2D, Bloom
 * @best_used_with Render2D
 * @family 2D drawable pipeline
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

        instances_.resize(kMaxInstances);

        // Wire the tree: children[] points at child drawables in a fixed order
        // matching shape_idx. Unused buckets keep instance_count=0 so Render2D
        // skips them (collect_recursive only draws leaves with non-zero work).
        for (int k = 0; k < kShapeBuckets; ++k) {
            children_ptrs_[k] = &output_children_[k];
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(count,     2, 0);
        vivid::layout_row(shape,     2, 1);
        vivid::display_hint(color_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_b, VIVID_DISPLAY_COLOR);
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

        vivid::layout_row(scale_enabled,  2, 0);
        vivid::layout_row(scale_waveform, 2, 1);
        out.push_back(&scale_enabled);
        out.push_back(&scale_amount);
        out.push_back(&scale_rate);
        out.push_back(&scale_waveform);
        out.push_back(&scale_offset);

        vivid::layout_row(rotation_enabled,  2, 0);
        vivid::layout_row(rotation_waveform, 2, 1);
        out.push_back(&rotation_enabled);
        out.push_back(&rotation_amount);
        out.push_back(&rotation_rate);
        out.push_back(&rotation_waveform);
        out.push_back(&rotation_offset);

        vivid::layout_row(color_mod_enabled,  2, 0);
        vivid::layout_row(color_mod_waveform, 2, 1);
        out.push_back(&color_mod_enabled);
        out.push_back(&color_mod_amount);
        out.push_back(&color_mod_rate);
        out.push_back(&color_mod_waveform);
        out.push_back(&color_mod_offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"pos_x",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, "position_xy", "lane_array", "x_component", "Per-instance x position in [0, 1]. When connected, overrides layout."});
        out.push_back({"pos_y",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, "position_xy", "lane_array", "y_component", "Per-instance y position in [0, 1]. When connected, overrides layout."});
        out.push_back({"size",       VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, "amplitude_linear", "lane_array", "scale_multiplier", "Per-instance size multiplier on base_size. 1.0 = default."});
        out.push_back({"hue",        VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, "color_hue", "lane_array", "hue_cycles", "Per-instance hue in [0, 1]. HSV(hue, 1, 1) replaces color_r/g/b when connected."});
        out.push_back({"brightness", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, "amplitude_linear", "lane_array", "per_note_amplitude", "Per-instance intensity in [0, 1]. Multiplies RGB and alpha."});
        out.push_back({"rotation",   VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, "rotation_radians", "lane_array", "angle_turns", "Per-instance rotation in turns [0, 1]. Overrides the rotation LFO when connected."});
        out.push_back({"shape_idx",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, "enum_index", "lane_array", "shape_selector", "Per-instance shape index: 0=Circle, 1=Triangle, 2=Square, 3=Pentagon, 4=Hexagon, 5=Star. Overrides the global shape param when connected; clamped to [0, 5]."});
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    // Fetch lane data + length for input port `idx`. Returns data=nullptr
    // when the port is disconnected or the value is empty.
    struct LaneSlot { const float* data = nullptr; uint32_t length = 0; };
    static LaneSlot lane_slot(const VividGpuContext* ctx, int idx) {
        if (!ctx->values) return {};
        const VividValueView* v = &ctx->values[idx];
        const float* data = vivid_value_floats(v);
        const uint32_t length = vivid_value_count(v);
        if (!data || length == 0) return {};
        return {data, length};
    }

    void process_gpu(const VividGpuContext* ctx) override {
        // ── Phase 5: lane-driven effective count ─────────────────────
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

        int default_shape_idx = std::clamp(shape.int_value(), 0, 5);

        // Process all LFO pools
        float scale_vals[kMaxInstances];
        float rotation_vals[kMaxInstances];
        float color_mod_vals[kMaxInstances];
        VividFrameContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;
        process_lfo_pool(scale_pool_,     &ctrl_ctx, n, scale_amount.value,     scale_vals);
        process_lfo_pool(rotation_pool_,  &ctrl_ctx, n, rotation_amount.value,  rotation_vals);
        process_lfo_pool(color_mod_pool_, &ctrl_ctx, n, color_mod_amount.value, color_mod_vals);

        // ── Clear per-bucket scratch vectors ─────────────────────────
        for (int k = 0; k < kShapeBuckets; ++k) bucket_instances_[k].clear();

        const float cr = color_r.value;
        const float cg = color_g.value;
        const float cb = color_b.value;
        const float bs = base_size.value;

        // ── Compose N instances, bucket each into its shape-kind array ─
        for (int i = 0; i < n; ++i) {
            const auto& inst = instances_[i];

            // Animated position (UV [0,1] space).
            float px = inst.x;
            float py = inst.y;
            if (animating) {
                switch (layout_mode) {
                    case 0:
                        px += std::cos(inst.phase + t * spd) * 0.02f;
                        py += std::sin(inst.phase + t * spd) * 0.02f;
                        break;
                    case 1:
                        px += std::sin(inst.phase + t * spd) * 0.015f;
                        py += std::cos(inst.phase + t * spd * 0.7f) * 0.015f;
                        break;
                    case 2: {
                        float angle = inst.phase + t * spd * 0.5f;
                        float radius = 0.3f;
                        px = 0.5f + radius * std::cos(angle);
                        py = 0.5f + radius * std::sin(angle);
                        break;
                    }
                    case 3:
                        py += std::sin(inst.phase + t * spd) * 0.05f;
                        break;
                }
            }

            // Lane overrides (stay in [0, 1] UV space here; NDC conversion below).
            if (lane_pos_x.data && static_cast<uint32_t>(i) < lane_pos_x.length)
                px = lane_pos_x.data[i];
            if (lane_pos_y.data && static_cast<uint32_t>(i) < lane_pos_y.length)
                py = lane_pos_y.data[i];

            // Size in UV scale. scale_vals is bipolar [-1,1] → remap to [0,2].
            float sz = bs * std::max(0.0f, 1.0f + scale_vals[i]);
            if (lane_size.data && static_cast<uint32_t>(i) < lane_size.length)
                sz = bs * std::max(0.0f, lane_size.data[i]);

            // Rotation. Lane gives turns [0,1]; LFO pool outputs turns too.
            float rot_turns = rotation_vals[i];
            if (lane_rotation.data && static_cast<uint32_t>(i) < lane_rotation.length)
                rot_turns = lane_rotation.data[i];
            const float rot_rad = rot_turns * 6.2831853f;

            // Colour (brightness × HSV/RGB × color_mod).
            float mod = std::max(0.0f, 1.0f + color_mod_vals[i] * 0.5f);
            float cr_i = cr, cg_i = cg, cb_i = cb;
            if (lane_hue.data && static_cast<uint32_t>(i) < lane_hue.length)
                hsv_to_rgb(lane_hue.data[i], 1.0f, 1.0f, &cr_i, &cg_i, &cb_i);
            float bright_i = 1.0f;
            if (lane_bright.data && static_cast<uint32_t>(i) < lane_bright.length)
                bright_i = std::max(0.0f, std::min(1.0f, lane_bright.data[i]));

            // Per-instance shape_idx.
            int shape_idx_i = default_shape_idx;
            if (lane_shape.data && static_cast<uint32_t>(i) < lane_shape.length)
                shape_idx_i = std::clamp(static_cast<int>(lane_shape.data[i]), 0, 5);

            // ── Convert UV → NDC + build mat3x2 transform ─────────────
            // UV (y grows down) → NDC (y up). Size in UV units maps to a
            // half-extent of 2*sz in NDC (unit quad is [-1,1] = 2 units wide).
            const float ndc_x = (px - 0.5f) * 2.0f;
            const float ndc_y = (0.5f - py) * 2.0f;
            const float half  = sz * 2.0f;
            const float c_ = std::cos(rot_rad);
            const float s_ = std::sin(rot_rad);

            vivid::gpu::InstanceData2D rec{};
            // T · R · S, column-major mat3x2:
            // linear block = R · S = [[c*sx, -s*sy], [s*sx, c*sy]]
            rec.transform[0] =  c_ * half;  // col0.x (a = coeff of local x in world x)
            rec.transform[1] =  s_ * half;  // col0.y (c = coeff of local x in world y)
            rec.transform[2] = -s_ * half;  // col1.x (b)
            rec.transform[3] =  c_ * half;  // col1.y (d)
            rec.transform[4] = ndc_x;
            rec.transform[5] = ndc_y;
            rec.color[0] = cr_i * mod * bright_i;
            rec.color[1] = cg_i * mod * bright_i;
            rec.color[2] = cb_i * mod * bright_i;
            rec.color[3] = bright_i;

            bucket_instances_[shape_idx_i].push_back(rec);
        }

        // ── Upload per-bucket instance buffers + wire child drawables ─
        uint32_t child_n = 0;
        for (int k = 0; k < kShapeBuckets; ++k) {
            auto& bucket = bucket_instances_[k];
            if (bucket.empty()) {
                output_children_[k].instance_count = 0;
                continue;  // will be skipped in parent's children[] below
            }

            ensure_bucket_capacity(ctx, k, static_cast<uint32_t>(bucket.size()));
            wgpuQueueWriteBuffer(ctx->queue, bucket_buffers_[k], 0,
                                 bucket.data(),
                                 bucket.size() * sizeof(vivid::gpu::InstanceData2D));

            uint32_t sides = 0;
            float star_factor = 0.0f;
            shape_idx_to_sdf_params(k, &sides, &star_factor);

            auto& child = output_children_[k];
            vivid::gpu::drawable_identity(child);
            child.type              = vivid::gpu::VIVID_DRAWABLE2D_SHAPE;
            child.blend_mode        = vivid::gpu::VIVID_BLEND_ALPHA;
            child.shape_sides       = sides;
            child.shape_star_factor = star_factor;
            child.shape_softness    = softness.value;
            child.instance_buffer   = bucket_buffers_[k];
            child.instance_count    = static_cast<uint32_t>(bucket.size());

            live_children_[child_n++] = &output_children_[k];
        }

        // Root: composition node (no own geometry — child_count > 0 signals
        // Render2D's collect_recursive to walk and skip).
        vivid::gpu::drawable_identity(output_root_);
        output_root_.children    = live_children_;
        output_root_.child_count = child_n;

        ctx->custom_outputs[0] = &output_root_;
    }

    ~ShapeField() override {
        for (int k = 0; k < kShapeBuckets; ++k) {
            vivid::gpu::release(bucket_buffers_[k]);
        }
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

    // Drawable tree output: 1 root + 6 children (one per shape kind).
    vivid::gpu::VividDrawable2D output_root_{};
    vivid::gpu::VividDrawable2D output_children_[kShapeBuckets]{};
    // Stable pointer array used by output_root_.children. Populated each frame
    // with the subset of children_ptrs_ whose instance_count > 0.
    vivid::gpu::VividDrawable2D* children_ptrs_[kShapeBuckets]{};  // all children, stable order
    vivid::gpu::VividDrawable2D* live_children_[kShapeBuckets]{};  // per-frame live subset

    // Per-bucket GPU storage buffer for InstanceData2D[N]. Grown with slack.
    WGPUBuffer bucket_buffers_[kShapeBuckets]   = {};
    uint32_t   bucket_capacities_[kShapeBuckets] = {};

    // Per-bucket CPU scratch for the per-frame instance partition.
    std::vector<vivid::gpu::InstanceData2D> bucket_instances_[kShapeBuckets];

    void ensure_bucket_capacity(const VividGpuContext* ctx, int bucket, uint32_t needed) {
        if (bucket_buffers_[bucket] && bucket_capacities_[bucket] >= needed) return;
        vivid::gpu::release(bucket_buffers_[bucket]);
        uint32_t new_cap = needed + 8;  // small slack to avoid churn on small deltas
        bucket_capacities_[bucket] = new_cap;
        WGPUBufferDescriptor bd{};
        bd.label = vivid_sv("ShapeField bucket buffer");
        bd.size  = static_cast<uint64_t>(new_cap) * sizeof(vivid::gpu::InstanceData2D);
        bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        bucket_buffers_[bucket] = wgpuDeviceCreateBuffer(ctx->device, &bd);
    }

    // ── Layout computation ──────────────────────────────────────────
    void compute_base_layout(int n, int layout_mode) {
        constexpr float TAU = 6.2831853f;
        constexpr float margin = 0.1f;

        for (int i = 0; i < n; ++i) {
            instances_[i].phase = TAU * static_cast<float>(i) / static_cast<float>(n);
        }

        switch (layout_mode) {
            case 0: {
                uint32_t seed = 42;
                for (int i = 0; i < n; ++i) {
                    instances_[i].x = margin + hash_float(seed) * (1.0f - 2.0f * margin);
                    instances_[i].y = margin + hash_float(seed) * (1.0f - 2.0f * margin);
                }
                break;
            }
            case 1: {
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
            case 2: {
                float radius = 0.3f;
                for (int i = 0; i < n; ++i) {
                    float angle = TAU * static_cast<float>(i) / static_cast<float>(n);
                    instances_[i].x = 0.5f + radius * std::cos(angle);
                    instances_[i].y = 0.5f + radius * std::sin(angle);
                }
                break;
            }
            case 3: {
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

        if (n != pool.cached_count) {
            pool.instances.clear();
            pool.instances.resize(n);
            pool.cached_count = n;
            for (int i = 0; i < n; ++i) {
                pool.instances[i].set_param("phase_offset",
                    static_cast<float>(i) / static_cast<float>(n));
            }
        }

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

VIVID_DEFINE_OP(ShapeField) {
}


VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
