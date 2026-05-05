#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>

static constexpr int kMaxInputs = 16;
static constexpr int kParamsPerLayer = 6; // connected, opacity, x, y, scale, rotation

// =============================================================================
// WGSL Shader Generation — produces shader for exactly N active inputs
// =============================================================================

static std::string generate_composite_wgsl(int n) {
    std::ostringstream s;

    // Uniform struct: blend_mode + per-layer vec4f (opacity,x,y,scale) + rotation array
    s << "struct Uniforms {\n"
         "    blend_mode: i32,\n"
         "    _pad0: f32, _pad1: f32, _pad2: f32,\n";
    for (int i = 0; i < n; ++i)
        s << "    layer_" << i << ": vec4f,\n";
    // Rotations packed in vec4f groups
    int rot_vecs = (n + 3) / 4;
    for (int v = 0; v < rot_vecs; ++v)
        s << "    rot_" << v << ": vec4f,\n";
    s << "};\n\n";

    // Vertex output
    s << "struct VertexOutput {\n"
         "    @builtin(position) position: vec4f,\n"
         "    @location(0) uv: vec2f,\n"
         "}\n\n";

    // Bindings: uniform(0), sampler(1), textures(2..2+n-1)
    s << "@group(0) @binding(0) var<uniform> uniforms: Uniforms;\n"
         "@group(0) @binding(1) var texSampler: sampler;\n";
    for (int i = 0; i < n; ++i)
        s << "@group(0) @binding(" << (2 + i) << ") var tex_" << i << ": texture_2d<f32>;\n";
    s << "\n";

    // Vertex shader
    s << "@vertex\n"
         "fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {\n"
         "    let fs = fullscreenTriangle(vertexIndex, true);\n"
         "    var out: VertexOutput;\n"
         "    out.position = fs.position;\n"
         "    out.uv = fs.uv;\n"
         "    return out;\n"
         "}\n\n";

    // Transform UV helper
    s << "fn transform_uv(uv: vec2f, tx: f32, ty: f32, sc: f32, rot: f32) -> vec2f {\n"
         "    var p = uv - vec2f(0.5);\n"
         "    let angle = rot * 6.283185307;\n"
         "    let c = cos(angle);\n"
         "    let s = sin(angle);\n"
         "    p = vec2f(p.x * c - p.y * s, p.x * s + p.y * c);\n"
         "    p = p / max(sc, 0.001);\n"
         "    p = p - vec2f(tx, ty);\n"
         "    return p + vec2f(0.5);\n"
         "}\n\n";

    // Blend function
    s << "fn do_blend(base: vec4f, overlay: vec4f, op: f32, mode: i32) -> vec4f {\n"
         "    if (op <= 0.0) { return base; }\n"
         "    if (base.a <= 0.0) {\n"
         "        let a = overlay.a * op;\n"
         "        return vec4f(overlay.rgb * a, a);\n"
         "    }\n"
         "    switch mode {\n"
         "        case 1: {\n"
         "            return vec4f(base.rgb + overlay.rgb * op, max(overlay.a * op, base.a));\n"
         "        }\n"
         "        case 2: {\n"
         "            return vec4f(base.rgb * mix(vec3f(1.0), overlay.rgb, op), max(overlay.a * op, base.a));\n"
         "        }\n"
         "        case 3: {\n"
         "            let one = vec3f(1.0);\n"
         "            return vec4f(one - (one - base.rgb) * (one - overlay.rgb * op), max(overlay.a * op, base.a));\n"
         "        }\n"
         "        case 4: {\n"
         "            let lo = 2.0 * overlay.rgb * base.rgb;\n"
         "            let hi = vec3f(1.0) - 2.0 * (vec3f(1.0) - overlay.rgb) * (vec3f(1.0) - base.rgb);\n"
         "            let ov = select(hi, lo, base.rgb < vec3f(0.5));\n"
         "            return vec4f(mix(base.rgb, ov, op), max(overlay.a * op, base.a));\n"
         "        }\n"
         "        default: {\n"
         "            return vec4f(mix(base.rgb, overlay.rgb, overlay.a * op),\n"
         "                         mix(base.a, 1.0, overlay.a * op));\n"
         "        }\n"
         "    }\n"
         "}\n\n";

    // Sample layer helper
    s << "fn sample_layer(uv: vec2f, layer: vec4f, rot: f32,\n"
         "                tex: texture_2d<f32>, samp: sampler) -> vec4f {\n"
         "    let tuv = transform_uv(uv, layer.y, layer.z, layer.w, rot);\n"
         "    return textureSample(tex, samp, tuv);\n"
         "}\n\n";

    // Fragment shader — sample and composite bottom-up
    s << "@fragment\n"
         "fn fs_main(input: VertexOutput) -> @location(0) vec4f {\n"
         "    let uv = input.uv;\n"
         "    let mode = uniforms.blend_mode;\n\n";

    // Sample all layers
    for (int i = n - 1; i >= 0; --i) {
        int rv = i / 4;
        int rc = i % 4;
        const char* swizzle[] = {"x", "y", "z", "w"};
        s << "    let s_" << i << " = sample_layer(uv, uniforms.layer_" << i
          << ", uniforms.rot_" << rv << "." << swizzle[rc]
          << ", tex_" << i << ", texSampler);\n";
    }

    // Composite bottom-up: highest index (background) to 0 (foreground)
    s << "\n    var result = vec4f(0.0, 0.0, 0.0, 0.0);\n";
    for (int i = n - 1; i >= 0; --i)
        s << "    result = do_blend(result, s_" << i << ", uniforms.layer_" << i << ".x, mode);\n";
    s << "    return result;\n}\n";

    return s.str();
}

// =============================================================================
// Uniform buffer size for N active layers (16-byte aligned)
// =============================================================================

static uint32_t uniform_buffer_size(int n) {
    // 16 bytes (blend_mode + padding) + n * 16 (layer vec4f) + ceil(n/4) * 16 (rotation vec4f groups)
    uint32_t size = 16 + n * 16 + ((n + 3) / 4) * 16;
    return (size + 15) & ~15u;  // ensure 16-byte alignment
}

// =============================================================================
// Composite Operator
// =============================================================================

/**
 * @brief Composites up to 16 texture layers with per-layer transform and opacity.
 *
 * Each layer has opacity, translate (x/y), scale, and rotation controls.
 * Layers composite bottom-up: highest index (background) through 0 (foreground).
 * Controls for disconnected inputs are hidden automatically.
 * Uses repeat-group ports for grow-on-connect UI behavior.
 * Supports Normal, Add, Multiply, Screen, and Overlay blend modes.
 *
 * @see Feedback, Bloom
 */
struct Composite : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Composite";
    static constexpr bool kTimeDependent = false;

    // -- Global params --------------------------------------------------------
    vivid::Param<int> blend_mode {"blend_mode", 0, {"Normal", "Add", "Multiply", "Screen", "Overlay"}};

    // -- Per-layer params (generated in constructor) --------------------------
    // Storage for dynamically-named params (name strings must be stable)
    struct LayerParams {
        char connected_name[16];
        char opacity_name[16];
        char x_name[16];
        char y_name[16];
        char scale_name[16];
        char rotation_name[16];
        char group_name[16];

        vivid::Param<int>   connected{nullptr, 0, 0, 1};
        vivid::Param<float> opacity  {nullptr, 1.0f, 0.0f, 1.0f};
        vivid::Param<float> x        {nullptr, 0.0f, -1.0f, 1.0f};
        vivid::Param<float> y        {nullptr, 0.0f, -1.0f, 1.0f};
        vivid::Param<float> scale    {nullptr, 1.0f, 0.01f, 4.0f};
        vivid::Param<float> rotation {nullptr, 0.0f, -1.0f, 1.0f};
    };

    LayerParams lp_[kMaxInputs];

    // Port names (stable storage for VividPortDescriptor pointers)
    Composite() {
        vivid::semantic_tag(blend_mode, "x_blend_mode");
        vivid::semantic_shape(blend_mode, "enum");
        vivid::description(blend_mode, "How layers are combined: Normal, Add, Multiply, Screen, or Overlay");

        for (int i = 0; i < kMaxInputs; ++i) {
            auto& L = lp_[i];

            // Generate stable name strings
            std::snprintf(L.connected_name, sizeof(L.connected_name), "connected_%d", i);
            std::snprintf(L.opacity_name, sizeof(L.opacity_name), "opacity_%d", i);
            std::snprintf(L.x_name, sizeof(L.x_name), "x_%d", i);
            std::snprintf(L.y_name, sizeof(L.y_name), "y_%d", i);
            std::snprintf(L.scale_name, sizeof(L.scale_name), "scale_%d", i);
            std::snprintf(L.rotation_name, sizeof(L.rotation_name), "rotation_%d", i);
            std::snprintf(L.group_name, sizeof(L.group_name), "Layer %d", i);

            // Assign names to params
            L.connected.name = L.connected_name;
            L.opacity.name   = L.opacity_name;
            L.x.name         = L.x_name;
            L.y.name         = L.y_name;
            L.scale.name     = L.scale_name;
            L.rotation.name  = L.rotation_name;

            // Hidden controller — drives visibility of the layer group
            vivid::display_hint(L.connected, VIVID_DISPLAY_HIDDEN);

            // Group all layer params under a collapsible section
            vivid::param_group(L.opacity,  L.group_name);
            vivid::param_group(L.x,        L.group_name);
            vivid::param_group(L.y,        L.group_name);
            vivid::param_group(L.scale,    L.group_name);
            vivid::param_group(L.rotation, L.group_name);

            // Visibility: only show when connected
            vivid::visible_when_eq(L.opacity,  L.connected, {1});
            vivid::visible_when_eq(L.x,        L.connected, {1});
            vivid::visible_when_eq(L.y,        L.connected, {1});
            vivid::visible_when_eq(L.scale,    L.connected, {1});
            vivid::visible_when_eq(L.rotation, L.connected, {1});

            // Layout: opacity full-width, then x/y side by side, scale/rotation side by side
            vivid::layout_row(L.x,        2, 0);
            vivid::layout_row(L.y,        2, 1);
            vivid::layout_row(L.scale,    2, 0);
            vivid::layout_row(L.rotation, 2, 1);

            // Semantic metadata
            vivid::semantic_tag(L.opacity, "probability_01");
            vivid::semantic_tag(L.x, "position_xy");
            vivid::semantic_tag(L.y, "position_xy");
            vivid::semantic_tag(L.scale, "scale_factor");
            vivid::semantic_tag(L.rotation, "angle_turns");

            for (auto* p : {&L.opacity, &L.x, &L.y, &L.scale, &L.rotation})
                vivid::semantic_shape(*p, "scalar");

            // Repeat-group metadata
            uint16_t idx = static_cast<uint16_t>(i);
            vivid::repeat_group(L.connected, "layer", idx);
            vivid::repeat_group(L.opacity,   "layer", idx);
            vivid::repeat_group(L.x,         "layer", idx);
            vivid::repeat_group(L.y,         "layer", idx);
            vivid::repeat_group(L.scale,     "layer", idx);
            vivid::repeat_group(L.rotation,  "layer", idx);
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&blend_mode);
        for (int i = 0; i < kMaxInputs; ++i) {
            out.push_back(&lp_[i].connected);
            out.push_back(&lp_[i].opacity);
            out.push_back(&lp_[i].x);
            out.push_back(&lp_[i].y);
            out.push_back(&lp_[i].scale);
            out.push_back(&lp_[i].rotation);
        }
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"layer_0",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  0});
        out.push_back({"layer_1",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  1});
        out.push_back({"layer_2",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  2});
        out.push_back({"layer_3",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  3});
        out.push_back({"layer_4",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  4});
        out.push_back({"layer_5",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  5});
        out.push_back({"layer_6",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  6});
        out.push_back({"layer_7",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  7});
        out.push_back({"layer_8",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  8});
        out.push_back({"layer_9",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer",  9});
        out.push_back({"layer_10", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer", 10});
        out.push_back({"layer_11", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer", 11});
        out.push_back({"layer_12", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer", 12});
        out.push_back({"layer_13", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer", 13});
        out.push_back({"layer_14", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer", 14});
        out.push_back({"layer_15", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_TEXTURE, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "layer", 15});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        // Count active (connected) inputs
        int active = 0;
        bool connected[kMaxInputs]{};
        for (int i = 0; i < kMaxInputs; ++i) {
            connected[i] = ctx->input_connected && ctx->input_connected[i];
            if (ctx->input_texture_views && i < (int)ctx->input_texture_count &&
                ctx->input_texture_views[i]) {
                connected[i] = true;
            }
            if (connected[i]) active = i + 1;  // track highest connected index + 1
        }
        if (active == 0) active = 1;  // always render at least 1-input shader

        // Write connection state to hidden controller params for inspector visibility
        for (int i = 0; i < kMaxInputs; ++i) {
            int param_idx = 1 + i * kParamsPerLayer;
            ctx->param_values[param_idx] = connected[i] ? 1.0f : 0.0f;
        }

        // Rebuild pipeline if active input count changed
        if (active != cached_active_count_ || !pipeline_) {
            release_pipeline();
            if (!build_pipeline(ctx, active)) {
                std::fprintf(stderr, "[composite] build_pipeline(%d) FAILED\n", active);
                return;
            }
            cached_active_count_ = active;
        }

        // Gather texture views for active inputs
        WGPUTextureView tex[kMaxInputs]{};
        for (int i = 0; i < active; ++i) {
            if (ctx->input_texture_views && i < (int)ctx->input_texture_count &&
                ctx->input_texture_views[i]) {
                tex[i] = ctx->input_texture_views[i];
            }
            if (!tex[i]) {
                if (!fallback_view_) create_fallback(ctx);
                tex[i] = fallback_view_;
            }
        }

        // Update uniforms — dynamically sized for active inputs
        uint32_t usize = uniform_buffer_size(active);
        std::vector<uint8_t> ubuf(usize, 0);
        auto* base = ubuf.data();

        // blend_mode at offset 0
        int32_t bm = blend_mode.int_value();
        std::memcpy(base, &bm, 4);

        // Per-layer vec4f starting at offset 16
        for (int i = 0; i < active; ++i) {
            float layer[4] = {
                connected[i] ? lp_[i].opacity.value : 0.0f,
                lp_[i].x.value,
                lp_[i].y.value,
                lp_[i].scale.value
            };
            std::memcpy(base + 16 + i * 16, layer, 16);
        }

        // Rotation vec4f groups starting after layers
        uint32_t rot_offset = 16 + active * 16;
        int rot_vecs = (active + 3) / 4;
        for (int v = 0; v < rot_vecs; ++v) {
            float rot[4] = {0, 0, 0, 0};
            for (int c = 0; c < 4 && v * 4 + c < active; ++c)
                rot[c] = lp_[v * 4 + c].rotation.value;
            std::memcpy(base + rot_offset + v * 16, rot, 16);
        }

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, ubuf.data(), usize);

        // Recreate bind group when texture inputs change
        bool dirty = (active != cached_bind_count_);
        if (!dirty) {
            for (int i = 0; i < active; ++i) {
                uint32_t w = (ctx->input_texture_widths && i < (int)ctx->input_texture_count)
                    ? ctx->input_texture_widths[i] : 0;
                uint32_t h = (ctx->input_texture_heights && i < (int)ctx->input_texture_count)
                    ? ctx->input_texture_heights[i] : 0;
                if (tex[i] != cached_tex_[i] || w != cached_tex_width_[i] || h != cached_tex_height_[i]) {
                    dirty = true;
                    break;
                }
            }
        }
        if (dirty) {
            vivid::gpu::release(cached_bind_group_);
            cached_bind_group_ = nullptr;

            uint32_t entry_count = 2 + active;
            std::vector<WGPUBindGroupEntry> bg_entries(entry_count, WGPUBindGroupEntry{});
            bg_entries[0].binding = 0;
            bg_entries[0].buffer  = uniform_buf_;
            bg_entries[0].offset  = 0;
            bg_entries[0].size    = usize;
            bg_entries[1].binding = 1;
            bg_entries[1].sampler = sampler_;
            for (int i = 0; i < active; ++i) {
                bg_entries[2 + i].binding = 2 + i;
                bg_entries[2 + i].textureView = tex[i];
            }

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = vivid_sv("Composite BG");
            bg_desc.layout = bind_layout_;
            bg_desc.entryCount = entry_count;
            bg_desc.entries = bg_entries.data();
            cached_bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);

            cached_bind_count_ = active;
            for (int i = 0; i < active; ++i) {
                cached_tex_[i] = tex[i];
                cached_tex_width_[i] = (ctx->input_texture_widths && i < (int)ctx->input_texture_count)
                    ? ctx->input_texture_widths[i] : 0;
                cached_tex_height_[i] = (ctx->input_texture_heights && i < (int)ctx->input_texture_count)
                    ? ctx->input_texture_heights[i] : 0;
            }
        }

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, cached_bind_group_,
                             ctx->output_texture_view, "Composite Pass",
                             WGPUColor{0, 0, 0, 0});
    }

    ~Composite() override {
        release_pipeline();
        vivid::gpu::release(sampler_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    // Pipeline state (rebuilt when active input count changes)
    WGPURenderPipeline  pipeline_      = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUBuffer          uniform_buf_   = nullptr;
    WGPUShaderModule    shader_        = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    int                 cached_active_count_ = 0;

    // Shared state (not rebuilt)
    WGPUSampler         sampler_       = nullptr;
    WGPUTexture         fallback_tex_  = nullptr;
    WGPUTextureView     fallback_view_ = nullptr;

    // Bind group cache
    WGPUBindGroup       cached_bind_group_ = nullptr;
    int                 cached_bind_count_  = 0;
    WGPUTextureView     cached_tex_[kMaxInputs]{};
    uint32_t            cached_tex_width_[kMaxInputs]{};
    uint32_t            cached_tex_height_[kMaxInputs]{};

    void release_pipeline() {
        vivid::gpu::release(cached_bind_group_);
        cached_bind_group_ = nullptr;
        vivid::gpu::release(pipeline_);
        pipeline_ = nullptr;
        vivid::gpu::release(bind_layout_);
        bind_layout_ = nullptr;
        vivid::gpu::release(uniform_buf_);
        uniform_buf_ = nullptr;
        vivid::gpu::release(shader_);
        shader_ = nullptr;
        vivid::gpu::release(pipe_layout_);
        pipe_layout_ = nullptr;
    }

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

    bool build_pipeline(const VividGpuContext* gpu, int active) {
        std::string wgsl = generate_composite_wgsl(active);
        shader_ = vivid::gpu::create_shader(gpu->device, wgsl.c_str(), "Composite Shader");
        if (!shader_) return false;

        uint32_t usize = uniform_buffer_size(active);
        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, usize, "Composite Uniforms");
        if (!sampler_)
            sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Composite Sampler");

        // Bind group layout: uniform(0) + sampler(1) + tex_0..tex_{active-1}
        uint32_t entry_count = 2 + active;
        std::vector<WGPUBindGroupLayoutEntry> entries(entry_count, WGPUBindGroupLayoutEntry{});
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = usize;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        for (int i = 0; i < active; ++i) {
            entries[2 + i].binding = 2 + i;
            entries[2 + i].visibility = WGPUShaderStage_Fragment;
            entries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2 + i].texture.multisampled = false;
        }

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Composite BGL");
        bgl_desc.entryCount = entry_count;
        bgl_desc.entries = entries.data();
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Composite Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                gpu->output_format, "Composite Pipeline");
        return pipeline_ != nullptr;
    }
};

VIVID_DEFINE_OP(Composite) {
}

