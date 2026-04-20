#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>

// =============================================================================
// ColorBands WGSL Fragment Shader
// =============================================================================

static const char* kColorBandsFragment = R"(

struct Uniforms {
    resolution:    vec2f,
    time:          f32,
    scroll_speed:  f32,
    softness:      f32,
    band_count:    i32,
    orientation:   i32,
    _pad:          i32,
    palette:       array<vec4f, 6>,   // each .rgb = band color, .a unused
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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let n = clamp(u.band_count, 2, 6);
    let n_f = f32(n);

    // Pick the axis we vary along: 0 = horizontal stripes (vary by Y),
    // 1 = vertical stripes (vary by X).
    var coord = input.uv.y;
    if (u.orientation > 0) { coord = input.uv.x; }

    let pos    = fract(coord + u.time * u.scroll_speed);
    let band_f = pos * n_f;
    let band_i = i32(floor(band_f));
    let frac   = band_f - floor(band_f);

    let idx_a = ((band_i % n) + n) % n;
    let idx_b = (idx_a + 1) % n;
    let color_a = u.palette[idx_a].rgb;
    let color_b = u.palette[idx_b].rgb;

    // Soft edge: blend across the trailing softness fraction of each band.
    let soft = clamp(u.softness, 0.0, 1.0);
    let edge_threshold = 1.0 - soft * 0.5;
    let blend = smoothstep(edge_threshold, 1.0, frac);

    let color = mix(color_a, color_b, blend);
    return vec4f(color, 1.0);
}
)";

// =============================================================================
// Uniform struct matching the WGSL Uniforms (std140-ish layout)
// =============================================================================

struct ColorBandsUniforms {
    float resolution[2];   // 0
    float time;            // 8
    float scroll_speed;    // 12
    float softness;        // 16
    int   band_count;      // 20
    int   orientation;     // 24
    int   _pad;            // 28  (vec2f next needs align(16))
    // palette: array<vec4f, 6>  — starts at 32, each entry is 16 bytes
    float palette[6][4];
};
static_assert(sizeof(ColorBandsUniforms) == 32 + 6 * 16,
              "ColorBands uniform layout mismatch");

/**
 * @brief Solid color stripes from a 6-slot palette, horizontal or vertical.
 *
 * Source operator (no inputs) that fills the canvas with N evenly-spaced
 * bands using the first N entries of an embedded 6-color palette. Choose
 * horizontal (stripes vary by Y) or vertical (vary by X). Optional
 * scroll_speed scrolls the bands along their axis. Softness adds a
 * crossfade between adjacent bands at their boundary.
 *
 * Default palette is a cool magenta / cyan / violet / white set that
 * pairs well with Mirror + Bloom for projected wall-stripe aesthetics.
 *
 * @see Mirror, Scanlines, Bloom
 */
struct ColorBands : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "ColorBands";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   band_count   {"band_count",   5, 2, 6};
    vivid::Param<int>   orientation  {"orientation",  0, {"horizontal","vertical"}};
    vivid::Param<float> scroll_speed {"scroll_speed", 0.0f, -2.0f, 2.0f};
    vivid::Param<float> softness     {"softness",     0.0f, 0.0f, 1.0f};

    // 6-slot color palette. Defaults: cool magenta / cyan / violet / white.
    vivid::Param<float> c0_r{"c0_r", 0.05f, 0.0f, 1.0f};
    vivid::Param<float> c0_g{"c0_g", 0.05f, 0.0f, 1.0f};
    vivid::Param<float> c0_b{"c0_b", 0.18f, 0.0f, 1.0f};
    vivid::Param<float> c1_r{"c1_r", 0.45f, 0.0f, 1.0f};
    vivid::Param<float> c1_g{"c1_g", 0.10f, 0.0f, 1.0f};
    vivid::Param<float> c1_b{"c1_b", 0.55f, 0.0f, 1.0f};
    vivid::Param<float> c2_r{"c2_r", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c2_g{"c2_g", 0.20f, 0.0f, 1.0f};
    vivid::Param<float> c2_b{"c2_b", 0.75f, 0.0f, 1.0f};
    vivid::Param<float> c3_r{"c3_r", 0.75f, 0.0f, 1.0f};
    vivid::Param<float> c3_g{"c3_g", 0.55f, 0.0f, 1.0f};
    vivid::Param<float> c3_b{"c3_b", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c4_r{"c4_r", 0.30f, 0.0f, 1.0f};
    vivid::Param<float> c4_g{"c4_g", 0.85f, 0.0f, 1.0f};
    vivid::Param<float> c4_b{"c4_b", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c5_r{"c5_r", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c5_g{"c5_g", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c5_b{"c5_b", 1.00f, 0.0f, 1.0f};

    ColorBands() {
        vivid::description(band_count,
            "Number of stripes across the canvas (uses palette colors 0..N-1)");
        vivid::description(orientation,
            "Stripe direction: horizontal stripes vary by Y, vertical by X");
        vivid::description(scroll_speed,
            "Scroll speed along the band axis (negative reverses)");
        vivid::semantic_tag(scroll_speed, "frequency_hz");
        vivid::semantic_unit(scroll_speed, "Hz");
        vivid::description(softness,
            "Edge crossfade between adjacent bands (0 = hard, 1 = smooth)");

        for (auto* p : {&c0_r,&c0_g,&c0_b,&c1_r,&c1_g,&c1_b,
                        &c2_r,&c2_g,&c2_b,&c3_r,&c3_g,&c3_b,
                        &c4_r,&c4_g,&c4_b,&c5_r,&c5_g,&c5_b})
            vivid::semantic_shape(*p, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&band_count);
        out.push_back(&orientation);
        out.push_back(&scroll_speed);
        out.push_back(&softness);
        out.push_back(&c0_r); out.push_back(&c0_g); out.push_back(&c0_b);
        out.push_back(&c1_r); out.push_back(&c1_g); out.push_back(&c1_b);
        out.push_back(&c2_r); out.push_back(&c2_g); out.push_back(&c2_b);
        out.push_back(&c3_r); out.push_back(&c3_g); out.push_back(&c3_b);
        out.push_back(&c4_r); out.push_back(&c4_g); out.push_back(&c4_b);
        out.push_back(&c5_r); out.push_back(&c5_g); out.push_back(&c5_b);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (init_failed_) {
            vivid_report_gpu_error(ctx, shader_error_msg_.c_str());
            return;
        }
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                init_failed_ = true;
                return;
            }
        }

        ColorBandsUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.time          = static_cast<float>(ctx->time);
        u.scroll_speed  = scroll_speed.value;
        u.softness      = softness.value;
        u.band_count    = band_count.int_value();
        u.orientation   = orientation.int_value();
        u._pad          = 0;

        const vivid::Param<float>* palette[6][3] = {
            {&c0_r, &c0_g, &c0_b},
            {&c1_r, &c1_g, &c1_b},
            {&c2_r, &c2_g, &c2_b},
            {&c3_r, &c3_g, &c3_b},
            {&c4_r, &c4_g, &c4_b},
            {&c5_r, &c5_g, &c5_b},
        };
        for (int i = 0; i < 6; ++i) {
            u.palette[i][0] = palette[i][0]->value;
            u.palette[i][1] = palette[i][1]->value;
            u.palette[i][2] = palette[i][2]->value;
            u.palette[i][3] = 0.0f;
        }

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "ColorBands Pass");
    }

    ~ColorBands() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    bool                init_failed_       = false;
    std::string         shader_error_msg_;

    bool lazy_init(const VividGpuContext* gpu) {
        wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
        shader_ = vivid::gpu::create_shader(gpu->device, kColorBandsFragment,
                                            "ColorBands Shader");
        {
            WGPUPopErrorScopeCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowSpontaneous;
            cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                              WGPUStringView msg, void* ud, void*) {
                if (type != WGPUErrorType_NoError) {
                    auto* self = static_cast<ColorBands*>(ud);
                    self->shader_error_msg_ = msg.data
                        ? std::string(msg.data, msg.length) : "unknown WGSL error";
                    std::fprintf(stderr,
                                 "[color_bands] WGSL error \xe2\x80\x94 keeping black output. %s\n",
                                 self->shader_error_msg_.c_str());
                }
            };
            cb.userdata1 = this;
            wgpuDevicePopErrorScope(gpu->device, cb);
        }
        if (!shader_error_msg_.empty() || !shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(ColorBandsUniforms), "ColorBands Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(ColorBandsUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("ColorBands BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("ColorBands Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(ColorBandsUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("ColorBands Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, shader_, pipe_layout_,
            gpu->output_format, "ColorBands Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(ColorBands)
