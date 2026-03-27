#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"

struct Mixer : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Mixer";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain1{"gain_1", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> gain2{"gain_2", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> gain3{"gain_3", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> gain4{"gain_4", 1.0f, 0.0f, 2.0f};

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    Mixer() {
        // 2×2 knob grid
        vivid::layout_row(gain1, 2, 0);  vivid::display_hint(gain1, VIVID_DISPLAY_KNOB);
        vivid::layout_row(gain2, 2, 1);  vivid::display_hint(gain2, VIVID_DISPLAY_KNOB);
        vivid::layout_row(gain3, 2, 0);  vivid::display_hint(gain3, VIVID_DISPLAY_KNOB);
        vivid::layout_row(gain4, 2, 1);  vivid::display_hint(gain4, VIVID_DISPLAY_KNOB);
        for (auto* p : {&gain1, &gain2, &gain3, &gain4}) {
            vivid::semantic_tag(*p, "amplitude_linear");
            vivid::semantic_shape(*p, "scalar");
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain1);
        out.push_back(&gain2);
        out.push_back(&gain3);
        out.push_back(&gain4);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input_1", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"input_2", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"input_3", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"input_4", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",  VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in1 = ctx->input_buffers[0];
        const float* in2 = ctx->input_buffers[1];
        const float* in3 = ctx->input_buffers[2];
        const float* in4 = ctx->input_buffers[3];
        float*       out = ctx->output_buffers[0];
        float g1 = gain1.value, g2 = gain2.value;
        float g3 = gain3.value, g4 = gain4.value;

        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in1[i] * g1 + in2[i] * g2 + in3[i] * g3 + in4[i] * g4;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "mixer thumbnail pipeline init failed");
            return;
        }

        struct Uniforms { float pad[4]; } u{};
        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "Mixer Thumb Pass");
    }

    ~Mixer() override {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);
    }

private:
    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);

        static const char* kThumbFragment = R"(
struct Uniforms {
    data: vec4f,
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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let dim_col = vec4f(80.0/255.0, 100.0/255.0, 140.0/255.0, 160.0/255.0);
    let bright_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 230.0/255.0);

    // 4 input lines converging to single output
    let x = uv.x;
    let y = uv.y;

    // Input positions (left side, spread vertically)
    let in_y0 = 0.15;
    let in_y1 = 0.38;
    let in_y2 = 0.62;
    let in_y3 = 0.85;
    let out_y = 0.5;  // output at center

    // Quadratic interpolation from input to output
    let t = x * x;  // quadratic ease
    let line0 = in_y0 + (out_y - in_y0) * t;
    let line1 = in_y1 + (out_y - in_y1) * t;
    let line2 = in_y2 + (out_y - in_y2) * t;
    let line3 = in_y3 + (out_y - in_y3) * t;

    let thickness = 0.018;

    // Check each input line (dim)
    if (abs(y - line0) < thickness) { return dim_col; }
    if (abs(y - line1) < thickness) { return dim_col; }
    if (abs(y - line2) < thickness) { return dim_col; }
    if (abs(y - line3) < thickness) { return dim_col; }

    // Output line on right side (bright, only after convergence)
    if (x > 0.85 && abs(y - out_y) < thickness * 1.5) {
        return bright_col;
    }

    return bg;
}
)";

        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Mixer Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Mixer Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Mixer Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "Mixer Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, sizeof(float) * 4, "Mixer Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "Mixer Thumb Pipeline");
        thumb_pipeline_format_ = ctx->thumbnail_format;
    }
};

VIVID_REGISTER(Mixer)
VIVID_THUMBNAIL(Mixer)
