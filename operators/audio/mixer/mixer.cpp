#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include <cstring>

static constexpr int kMaxInputs = 16;

/**
 * @brief Summing mixer with per-channel gain for up to 16 audio inputs.
 *
 * Sums connected audio inputs with independent gain controls.
 * Disconnected inputs contribute silence. Uses repeat-group ports
 * for grow-on-connect UI behavior.
 *
 * @see Gain, Composite
 */
struct Mixer : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Mixer";
    static constexpr bool kTimeDependent = false;

    // Per-input gain params (generated in constructor)
    struct InputParams {
        char gain_name[16];
        char desc[80];
        vivid::Param<float> gain{nullptr, 1.0f, 0.0f, 2.0f};
    };

    InputParams ip_[kMaxInputs];
    char port_names_[kMaxInputs][16];

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    Mixer() {
        for (int i = 0; i < kMaxInputs; ++i) {
            auto& I = ip_[i];
            std::snprintf(I.gain_name, sizeof(I.gain_name), "gain_%d", i);
            std::snprintf(port_names_[i], sizeof(port_names_[i]), "input_%d", i);
            std::snprintf(I.desc, sizeof(I.desc),
                          "Level multiplier for input %d (0 = silent, 1 = unity, 2 = double)", i);

            I.gain.name = I.gain_name;

            vivid::display_hint(I.gain, VIVID_DISPLAY_KNOB);
            vivid::layout_row(I.gain, 2, i % 2);
            vivid::semantic_tag(I.gain, "amplitude_linear");
            vivid::semantic_shape(I.gain, "scalar");
            vivid::description(I.gain, I.desc);
            vivid::repeat_group(I.gain, "input", static_cast<uint16_t>(i));
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        for (int i = 0; i < kMaxInputs; ++i)
            out.push_back(&ip_[i].gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        for (int i = 0; i < kMaxInputs; ++i) {
            VividPortDescriptor pd{};
            pd.name = port_names_[i];
            pd.type = VIVID_PORT_AUDIO_BUFFER;
            pd.direction = VIVID_PORT_INPUT;
            pd.transport = VIVID_PORT_TRANSPORT_AUDIO_BUFFER;
            pd.channels = 1;
            pd.repeat_group = "input";
            pd.repeat_group_idx = static_cast<uint16_t>(i);
            out.push_back(pd);
        }
        VividPortDescriptor out_port{};
        out_port.name = "output";
        out_port.type = VIVID_PORT_AUDIO_BUFFER;
        out_port.direction = VIVID_PORT_OUTPUT;
        out_port.transport = VIVID_PORT_TRANSPORT_AUDIO_BUFFER;
        out_port.channels = 1;
        out.push_back(out_port);
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        const uint32_t n = ctx->buffer_size;

        // Clear output
        std::memset(out, 0, n * sizeof(float));

        // Accumulate each input × gain
        for (int i = 0; i < kMaxInputs; ++i) {
            const float* in = ctx->input_buffers[i];
            if (!in) continue;
            float g = ip_[i].gain.value;
            if (g == 0.0f) continue;
            for (uint32_t s = 0; s < n; ++s)
                out[s] += in[s] * g;
        }
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
