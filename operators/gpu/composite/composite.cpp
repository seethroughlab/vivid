#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>

static constexpr int kMaxInputs = 6;
static constexpr int kParamsPerLayer = 6; // connected, opacity, x, y, scale, rotation

// =============================================================================
// Composite WGSL Fragment Shader
// =============================================================================

static const char* kCompositeFragment = R"(

struct Uniforms {
    blend_mode: i32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
    layer_a: vec4f,     // opacity, x, y, scale
    layer_b: vec4f,
    layer_c: vec4f,
    layer_d: vec4f,
    layer_e: vec4f,
    layer_f: vec4f,
    rot_abcd: vec4f,    // rotation a, b, c, d
    rot_ef__: vec4f,    // rotation e, f, pad, pad
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var texA: texture_2d<f32>;
@group(0) @binding(3) var texB: texture_2d<f32>;
@group(0) @binding(4) var texC: texture_2d<f32>;
@group(0) @binding(5) var texD: texture_2d<f32>;
@group(0) @binding(6) var texE: texture_2d<f32>;
@group(0) @binding(7) var texF: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn transform_uv(uv: vec2f, tx: f32, ty: f32, sc: f32, rot: f32) -> vec2f {
    var p = uv - vec2f(0.5);
    let angle = rot * 6.283185307;
    let c = cos(angle);
    let s = sin(angle);
    p = vec2f(p.x * c - p.y * s, p.x * s + p.y * c);
    p = p / max(sc, 0.001);
    p = p - vec2f(tx, ty);
    return p + vec2f(0.5);
}

fn do_blend(base: vec4f, overlay: vec4f, op: f32, mode: i32) -> vec4f {
    if (op <= 0.0) { return base; }

    switch mode {
        case 1: {
            // Add
            return vec4f(base.rgb + overlay.rgb * op, max(overlay.a * op, base.a));
        }
        case 2: {
            // Multiply
            return vec4f(base.rgb * mix(vec3f(1.0), overlay.rgb, op), max(overlay.a * op, base.a));
        }
        case 3: {
            // Screen
            let one = vec3f(1.0);
            return vec4f(one - (one - base.rgb) * (one - overlay.rgb * op), max(overlay.a * op, base.a));
        }
        case 4: {
            // Overlay
            let lo = 2.0 * overlay.rgb * base.rgb;
            let hi = vec3f(1.0) - 2.0 * (vec3f(1.0) - overlay.rgb) * (vec3f(1.0) - base.rgb);
            let ov = select(hi, lo, base.rgb < vec3f(0.5));
            return vec4f(mix(base.rgb, ov, op), max(overlay.a * op, base.a));
        }
        default: {
            // Normal (0)
            return vec4f(mix(base.rgb, overlay.rgb, overlay.a * op),
                         mix(base.a, 1.0, overlay.a * op));
        }
    }
}

fn sample_layer(uv: vec2f, layer: vec4f, rot: f32,
                tex: texture_2d<f32>, samp: sampler) -> vec4f {
    let tuv = transform_uv(uv, layer.y, layer.z, layer.w, rot);
    return textureSample(tex, samp, tuv);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let mode = uniforms.blend_mode;

    let f = sample_layer(uv, uniforms.layer_f, uniforms.rot_ef__.y, texF, texSampler);
    let e = sample_layer(uv, uniforms.layer_e, uniforms.rot_ef__.x, texE, texSampler);
    let d = sample_layer(uv, uniforms.layer_d, uniforms.rot_abcd.w, texD, texSampler);
    let c = sample_layer(uv, uniforms.layer_c, uniforms.rot_abcd.z, texC, texSampler);
    let b = sample_layer(uv, uniforms.layer_b, uniforms.rot_abcd.y, texB, texSampler);
    let a = sample_layer(uv, uniforms.layer_a, uniforms.rot_abcd.x, texA, texSampler);

    // Composite bottom-up: f (background) -> a (foreground)
    var result = vec4f(0.0, 0.0, 0.0, 0.0);
    result = do_blend(result, f, uniforms.layer_f.x, mode);
    result = do_blend(result, e, uniforms.layer_e.x, mode);
    result = do_blend(result, d, uniforms.layer_d.x, mode);
    result = do_blend(result, c, uniforms.layer_c.x, mode);
    result = do_blend(result, b, uniforms.layer_b.x, mode);
    result = do_blend(result, a, uniforms.layer_a.x, mode);
    return result;
}
)";

// =============================================================================
// Uniform struct matching WGSL (144 bytes, 16-byte aligned)
// =============================================================================

struct CompositeUniforms {
    int32_t blend_mode;
    float   _pad0, _pad1, _pad2;
    float   layer[6][4];     // [i] = {opacity, x, y, scale}
    float   rot_abcd[4];     // rotation a, b, c, d
    float   rot_ef__[4];     // rotation e, f, pad, pad
};
static_assert(sizeof(CompositeUniforms) == 144, "uniform buffer must be 144 bytes");

/**
 * @brief Composites up to 6 texture layers with per-layer transform and opacity.
 *
 * Each layer has opacity, translate (x/y), scale, and rotation controls.
 * Layers composite bottom-up: f (background) through a (foreground).
 * Controls for disconnected inputs are hidden automatically.
 * Supports Normal, Add, Multiply, Screen, and Overlay blend modes.
 *
 * @see Feedback, Bloom
 */
struct Composite : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Composite";
    static constexpr bool kTimeDependent = false;

    // -- Global params --------------------------------------------------------
    vivid::Param<int> blend_mode {"blend_mode", 0, {"Normal", "Add", "Multiply", "Screen", "Overlay"}};

    // -- Per-layer params (6 layers × 6 params each) --------------------------
    // Hidden controller params: written by process_gpu from input_connected
    vivid::Param<int> connected_a {"connected_a", 0, 0, 1};
    vivid::Param<int> connected_b {"connected_b", 0, 0, 1};
    vivid::Param<int> connected_c {"connected_c", 0, 0, 1};
    vivid::Param<int> connected_d {"connected_d", 0, 0, 1};
    vivid::Param<int> connected_e {"connected_e", 0, 0, 1};
    vivid::Param<int> connected_f {"connected_f", 0, 0, 1};

    vivid::Param<float> opacity_a  {"opacity_a",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> x_a        {"x_a",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> y_a        {"y_a",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> scale_a    {"scale_a",    1.0f, 0.01f, 4.0f};
    vivid::Param<float> rotation_a {"rotation_a", 0.0f, -1.0f, 1.0f};

    vivid::Param<float> opacity_b  {"opacity_b",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> x_b        {"x_b",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> y_b        {"y_b",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> scale_b    {"scale_b",    1.0f, 0.01f, 4.0f};
    vivid::Param<float> rotation_b {"rotation_b", 0.0f, -1.0f, 1.0f};

    vivid::Param<float> opacity_c  {"opacity_c",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> x_c        {"x_c",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> y_c        {"y_c",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> scale_c    {"scale_c",    1.0f, 0.01f, 4.0f};
    vivid::Param<float> rotation_c {"rotation_c", 0.0f, -1.0f, 1.0f};

    vivid::Param<float> opacity_d  {"opacity_d",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> x_d        {"x_d",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> y_d        {"y_d",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> scale_d    {"scale_d",    1.0f, 0.01f, 4.0f};
    vivid::Param<float> rotation_d {"rotation_d", 0.0f, -1.0f, 1.0f};

    vivid::Param<float> opacity_e  {"opacity_e",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> x_e        {"x_e",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> y_e        {"y_e",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> scale_e    {"scale_e",    1.0f, 0.01f, 4.0f};
    vivid::Param<float> rotation_e {"rotation_e", 0.0f, -1.0f, 1.0f};

    vivid::Param<float> opacity_f  {"opacity_f",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> x_f        {"x_f",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> y_f        {"y_f",        0.0f, -1.0f, 1.0f};
    vivid::Param<float> scale_f    {"scale_f",    1.0f, 0.01f, 4.0f};
    vivid::Param<float> rotation_f {"rotation_f", 0.0f, -1.0f, 1.0f};

    // Organized access
    vivid::Param<int>* connected_params[kMaxInputs] = {
        &connected_a, &connected_b, &connected_c,
        &connected_d, &connected_e, &connected_f
    };

    struct LayerParamSet {
        vivid::Param<float>* opacity;
        vivid::Param<float>* x;
        vivid::Param<float>* y;
        vivid::Param<float>* scale;
        vivid::Param<float>* rotation;
    };

    LayerParamSet layers[kMaxInputs] = {
        {&opacity_a, &x_a, &y_a, &scale_a, &rotation_a},
        {&opacity_b, &x_b, &y_b, &scale_b, &rotation_b},
        {&opacity_c, &x_c, &y_c, &scale_c, &rotation_c},
        {&opacity_d, &x_d, &y_d, &scale_d, &rotation_d},
        {&opacity_e, &x_e, &y_e, &scale_e, &rotation_e},
        {&opacity_f, &x_f, &y_f, &scale_f, &rotation_f},
    };

    Composite() {
        vivid::semantic_tag(blend_mode, "x_blend_mode");
        vivid::semantic_shape(blend_mode, "enum");
        vivid::description(blend_mode, "How layers are combined: Normal, Add, Multiply, Screen, or Overlay");

        static const char* labels[] = {"A", "B", "C", "D", "E", "F"};
        static const char* group_names[] = {
            "Layer A", "Layer B", "Layer C", "Layer D", "Layer E", "Layer F"
        };

        for (int i = 0; i < kMaxInputs; ++i) {
            // Hidden controller — drives visibility of the layer group
            vivid::display_hint(*connected_params[i], VIVID_DISPLAY_HIDDEN);

            auto& L = layers[i];

            // Group all layer params under a collapsible section
            vivid::param_group(*L.opacity,  group_names[i]);
            vivid::param_group(*L.x,        group_names[i]);
            vivid::param_group(*L.y,        group_names[i]);
            vivid::param_group(*L.scale,    group_names[i]);
            vivid::param_group(*L.rotation, group_names[i]);

            // Visibility: only show when connected
            vivid::visible_when_eq(*L.opacity,  *connected_params[i], {1});
            vivid::visible_when_eq(*L.x,        *connected_params[i], {1});
            vivid::visible_when_eq(*L.y,        *connected_params[i], {1});
            vivid::visible_when_eq(*L.scale,    *connected_params[i], {1});
            vivid::visible_when_eq(*L.rotation, *connected_params[i], {1});

            // Layout: opacity full-width, then x/y side by side, scale/rotation side by side
            vivid::layout_row(*L.x,        2, 0);
            vivid::layout_row(*L.y,        2, 1);
            vivid::layout_row(*L.scale,    2, 0);
            vivid::layout_row(*L.rotation, 2, 1);

            // Semantic metadata
            vivid::semantic_tag(*L.opacity, "probability_01");
            vivid::semantic_tag(*L.x, "position_xy");
            vivid::semantic_tag(*L.y, "position_xy");
            vivid::semantic_tag(*L.scale, "scale_factor");
            vivid::semantic_tag(*L.rotation, "angle_turns");

            for (auto* p : {L.opacity, L.x, L.y, L.scale, L.rotation})
                vivid::semantic_shape(*p, "scalar");

            char desc[96];
            std::snprintf(desc, sizeof(desc), "Opacity of layer %s", labels[i]);
            vivid::description(*L.opacity, desc);
            std::snprintf(desc, sizeof(desc), "Horizontal offset of layer %s", labels[i]);
            vivid::description(*L.x, desc);
            std::snprintf(desc, sizeof(desc), "Vertical offset of layer %s", labels[i]);
            vivid::description(*L.y, desc);
            std::snprintf(desc, sizeof(desc), "Scale of layer %s (1 = original size)", labels[i]);
            vivid::description(*L.scale, desc);
            std::snprintf(desc, sizeof(desc), "Rotation of layer %s in turns (-1 to 1)", labels[i]);
            vivid::description(*L.rotation, desc);
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&blend_mode);
        for (int i = 0; i < kMaxInputs; ++i) {
            out.push_back(connected_params[i]);
            out.push_back(layers[i].opacity);
            out.push_back(layers[i].x);
            out.push_back(layers[i].y);
            out.push_back(layers[i].scale);
            out.push_back(layers[i].rotation);
        }
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"b", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"c", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"d", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"e", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"f", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[composite] lazy_init FAILED\n");
                return;
            }
        }

        // Gather input texture views, fallback for disconnected
        WGPUTextureView tex[kMaxInputs]{};
        bool connected[kMaxInputs]{};
        for (int i = 0; i < kMaxInputs; ++i) {
            connected[i] = ctx->input_connected && ctx->input_connected[i];
            if (ctx->input_texture_views && i < (int)ctx->input_texture_count &&
                ctx->input_texture_views[i]) {
                tex[i] = ctx->input_texture_views[i];
                connected[i] = true;
            }
            if (!tex[i]) {
                if (!fallback_view_) create_fallback(ctx);
                tex[i] = fallback_view_;
            }
        }

        // Write connection state to hidden controller params for inspector visibility
        for (int i = 0; i < kMaxInputs; ++i) {
            int param_idx = 1 + i * kParamsPerLayer; // connected param index in collect_params order
            ctx->param_values[param_idx] = connected[i] ? 1.0f : 0.0f;
        }

        // Update uniforms
        CompositeUniforms u{};
        u.blend_mode = blend_mode.int_value();
        for (int i = 0; i < kMaxInputs; ++i) {
            u.layer[i][0] = connected[i] ? layers[i].opacity->value : 0.0f;
            u.layer[i][1] = layers[i].x->value;
            u.layer[i][2] = layers[i].y->value;
            u.layer[i][3] = layers[i].scale->value;
        }
        for (int i = 0; i < 4; ++i)
            u.rot_abcd[i] = layers[i].rotation->value;
        u.rot_ef__[0] = layers[4].rotation->value;
        u.rot_ef__[1] = layers[5].rotation->value;
        u.rot_ef__[2] = 0.0f;
        u.rot_ef__[3] = 0.0f;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // Recreate bind group when texture inputs change
        bool dirty = false;
        for (int i = 0; i < kMaxInputs; ++i) {
            const uint32_t w = (ctx->input_texture_widths &&
                                i < static_cast<int>(ctx->input_texture_count))
                ? ctx->input_texture_widths[i]
                : 0;
            const uint32_t h = (ctx->input_texture_heights &&
                                i < static_cast<int>(ctx->input_texture_count))
                ? ctx->input_texture_heights[i]
                : 0;
            if (tex[i] != cached_tex_[i] ||
                w != cached_tex_width_[i] ||
                h != cached_tex_height_[i]) {
                dirty = true;
                break;
            }
        }
        if (dirty) {
            if (cached_bind_group_)
                wgpuBindGroupRelease(cached_bind_group_);

            WGPUBindGroupEntry bg_entries[2 + kMaxInputs]{};
            bg_entries[0].binding = 0;
            bg_entries[0].buffer  = uniform_buf_;
            bg_entries[0].offset  = 0;
            bg_entries[0].size    = sizeof(CompositeUniforms);
            bg_entries[1].binding = 1;
            bg_entries[1].sampler = sampler_;
            for (int i = 0; i < kMaxInputs; ++i) {
                bg_entries[2 + i].binding = 2 + i;
                bg_entries[2 + i].textureView = tex[i];
            }

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = vivid_sv("Composite BG");
            bg_desc.layout = bind_layout_;
            bg_desc.entryCount = 2 + kMaxInputs;
            bg_desc.entries = bg_entries;
            cached_bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);
            for (int i = 0; i < kMaxInputs; ++i) {
                const uint32_t w = (ctx->input_texture_widths &&
                                    i < static_cast<int>(ctx->input_texture_count))
                    ? ctx->input_texture_widths[i]
                    : 0;
                const uint32_t h = (ctx->input_texture_heights &&
                                    i < static_cast<int>(ctx->input_texture_count))
                    ? ctx->input_texture_heights[i]
                    : 0;
                cached_tex_[i] = tex[i];
                cached_tex_width_[i] = w;
                cached_tex_height_[i] = h;
            }
        }

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, cached_bind_group_,
                             ctx->output_texture_view, "Composite Pass",
                             WGPUColor{0, 0, 0, 0});
    }

    ~Composite() override {
        vivid::gpu::release(cached_bind_group_);
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    WGPURenderPipeline  pipeline_      = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUBuffer          uniform_buf_   = nullptr;
    WGPUShaderModule    shader_        = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUSampler         sampler_       = nullptr;
    WGPUTexture         fallback_tex_  = nullptr;
    WGPUTextureView     fallback_view_ = nullptr;
    WGPUBindGroup       cached_bind_group_ = nullptr;
    WGPUTextureView     cached_tex_[kMaxInputs]{};
    uint32_t            cached_tex_width_[kMaxInputs]{};
    uint32_t            cached_tex_height_[kMaxInputs]{};

    void create_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Composite Fallback");
        td.size = { 1, 1, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = gpu->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format = gpu->output_format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        fallback_view_ = wgpuTextureCreateView(fallback_tex_, &vd);

        const uint8_t zero[8] = {};
        WGPUTexelCopyTextureInfo dest_info{};
        dest_info.texture = fallback_tex_;
        dest_info.mipLevel = 0;
        dest_info.origin = {0, 0, 0};
        dest_info.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest_info, zero, sizeof(zero), &layout, &extent);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kCompositeFragment, "Composite Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(CompositeUniforms), "Composite Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Composite Sampler");

        // Bind group layout: uniform(0) + sampler(1) + texA..texF(2..7)
        WGPUBindGroupLayoutEntry entries[2 + kMaxInputs]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(CompositeUniforms);

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        for (int i = 0; i < kMaxInputs; ++i) {
            entries[2 + i].binding = 2 + i;
            entries[2 + i].visibility = WGPUShaderStage_Fragment;
            entries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2 + i].texture.multisampled = false;
        }

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Composite BGL");
        bgl_desc.entryCount = 2 + kMaxInputs;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Composite Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_, gpu->output_format, "Composite Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(Composite)
