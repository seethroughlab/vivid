#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <cstdio>
#include <string>

static const char* kMetronomeVizFragment = R"(

struct Uniforms {
    resolution: vec2f,
    beat_phase: f32,
    bar_phase: f32,
    enabled: f32,
    line_width: f32,
    base_brightness: f32,
    accent_intensity: f32,
    color_r: f32,
    color_g: f32,
    color_b: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn pulse(x: f32, center: f32, width: f32) -> f32 {
    let d = abs(x - center);
    return 1.0 - smoothstep(width, width * 3.0, d);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let enabled = uniforms.enabled;
    let width = max(uniforms.line_width, 0.0015);
    let bg = mix(0.05, uniforms.base_brightness, enabled);

    let sweep = pulse(uv.x, uniforms.beat_phase, width);
    let scanline = pulse(uv.y, 0.5, width * 2.0);
    let beat_energy = sweep * (0.35 + 0.65 * scanline);
    let beat_flash = enabled * (1.0 - smoothstep(0.0, 0.16, uniforms.beat_phase));

    let bar_flash = enabled * (1.0 - smoothstep(0.0, 0.12, uniforms.bar_phase));
    let vignette = smoothstep(0.95, 0.15, distance(uv, vec2f(0.5, 0.5)));
    let accent = bar_flash * uniforms.accent_intensity * vignette;
    let beat_wash = beat_flash * (0.08 + 0.22 * vignette);

    let grid_x = 1.0 - smoothstep(0.0, 0.01, abs(fract(uv.x * 4.0) - 0.5));
    let grid_y = 1.0 - smoothstep(0.0, 0.015, abs(fract(uv.y * 8.0) - 0.5));
    let grid = (grid_x * 0.05 + grid_y * 0.03) * (0.25 + 0.75 * enabled);
    let meter_marks = (1.0 - smoothstep(0.0, 0.02, abs(fract(uv.x * 4.0 + uniforms.bar_phase * 4.0) - 0.5)))
        * 0.06 * enabled;

    let color = vec3f(uniforms.color_r, uniforms.color_g, uniforms.color_b);
    let base = vec3f(bg + grid + meter_marks) + color * beat_wash;
    let lit = base + color * beat_energy + color * accent;
    let alpha = clamp(0.28 + bg + beat_wash + beat_energy + accent, 0.0, 1.0);
    return vec4f(clamp(lit, vec3f(0.0), vec3f(1.0)), alpha);
}
)";

struct MetronomeVizUniforms {
    float resolution[2];
    float beat_phase;
    float bar_phase;
    float enabled;
    float line_width;
    float base_brightness;
    float accent_intensity;
    float color_r;
    float color_g;
    float color_b;
    float _pad0;
};

static_assert(sizeof(MetronomeVizUniforms) == 48,
              "MetronomeVizUniforms must match WGSL uniform block size");

/**
 * @brief Graph-metronome visualizer for transport-aware GPU patches.
 *
 * Renders a moving beat sweep plus a stronger bar accent directly from the
 * graph metronome fields on `VividGpuContext`. This operator demonstrates
 * GPU-domain access to graph-wide timing without any control wires.
 *
 * @tip Use this as a transport monitor in visual patches or demo graphs.
 * @see Clock, Lfo
 */
struct MetronomeViz : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "MetronomeViz";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> line_width{"line_width", 0.012f, 0.002f, 0.08f};
    vivid::Param<float> base_brightness{"base_brightness", 0.12f, 0.0f, 0.5f};
    vivid::Param<float> accent_intensity{"accent_intensity", 0.9f, 0.0f, 2.0f};
    vivid::Param<float> r{"r", 0.15f, 0.0f, 1.0f};
    vivid::Param<float> g{"g", 0.8f, 0.0f, 1.0f};
    vivid::Param<float> b{"b", 1.0f, 0.0f, 1.0f};

    MetronomeViz() {
        vivid::description(line_width, "Width of the moving beat sweep");
        vivid::description(base_brightness, "Background brightness under the transport overlay");
        vivid::description(accent_intensity, "Extra flash intensity on bar resets");
        vivid::description(r, "Red component of the sweep color");
        vivid::description(g, "Green component of the sweep color");
        vivid::description(b, "Blue component of the sweep color");

        vivid::semantic_tag(r, "color_rgba");
        vivid::semantic_shape(r, "scalar");
        vivid::semantic_intent(r, "color_red");
        vivid::semantic_tag(g, "color_rgba");
        vivid::semantic_shape(g, "scalar");
        vivid::semantic_intent(g, "color_green");
        vivid::semantic_tag(b, "color_rgba");
        vivid::semantic_shape(b, "scalar");
        vivid::semantic_intent(b, "color_blue");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(r, VIVID_DISPLAY_COLOR);
        display_hint(g, VIVID_DISPLAY_COLOR);
        display_hint(b, VIVID_DISPLAY_COLOR);

        out.push_back(&line_width);
        out.push_back(&base_brightness);
        out.push_back(&accent_intensity);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
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

        const vivid::MetronomeTransport transport = vivid::metronome_transport(ctx);

        MetronomeVizUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.beat_phase = transport.beat_phase;
        u.bar_phase = transport.bar_phase;
        u.enabled = 1.0f;
        u.line_width = line_width.value;
        u.base_brightness = base_brightness.value;
        u.accent_intensity = accent_intensity.value;
        u.color_r = r.value;
        u.color_g = g.value;
        u.color_b = b.value;
        u._pad0 = 0.0f;

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "MetronomeViz Pass");
    }

    ~MetronomeViz() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bind_group_ = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer uniform_buf_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUPipelineLayout pipe_layout_ = nullptr;
    bool init_failed_ = false;
    std::string shader_error_msg_;

    bool lazy_init(const VividGpuContext* ctx) {
        wgpuDevicePushErrorScope(ctx->device, WGPUErrorFilter_Validation);
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kMetronomeVizFragment;
        shader_ = vivid::gpu::create_shader(ctx->device, frag.c_str(), "MetronomeViz Shader");
        {
            WGPUPopErrorScopeCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowSpontaneous;
            cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                             WGPUStringView msg, void* ud, void*) {
                if (type != WGPUErrorType_NoError) {
                    auto* self = static_cast<MetronomeViz*>(ud);
                    self->shader_error_msg_ = msg.data
                        ? std::string(msg.data, msg.length) : "unknown WGSL error";
                    std::fprintf(stderr, "[metronome_viz] WGSL error — keeping black output. %s\n",
                                 self->shader_error_msg_.c_str());
                }
            };
            cb.userdata1 = this;
            wgpuDevicePopErrorScope(ctx->device, cb);
        }
        if (!shader_error_msg_.empty() || !shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            ctx->device, sizeof(MetronomeVizUniforms), "MetronomeViz Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(MetronomeVizUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("MetronomeViz BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MetronomeViz Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer = uniform_buf_;
        bg_entry.offset = 0;
        bg_entry.size = sizeof(MetronomeVizUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("MetronomeViz Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(
            ctx->device, shader_, pipe_layout_, ctx->output_format, "MetronomeViz Pipeline");
        return pipeline_ != nullptr;
    }
};

VIVID_DEFINE_OP(MetronomeViz) {
}

VIVID_REGISTER(MetronomeViz)
