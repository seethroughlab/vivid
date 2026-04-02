#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"

struct GainThumbState {
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup bind_group = nullptr;
    WGPUBindGroupLayout bind_layout = nullptr;
    WGPUBuffer uniform_buf = nullptr;
    WGPUShaderModule shader = nullptr;
    WGPUPipelineLayout pipe_layout = nullptr;
    WGPUTextureFormat pipeline_format = WGPUTextureFormat_Undefined;

    void release_all() {
        vivid::gpu::release(pipeline);
        vivid::gpu::release(bind_group);
        vivid::gpu::release(bind_layout);
        vivid::gpu::release(uniform_buf);
        vivid::gpu::release(shader);
        vivid::gpu::release(pipe_layout);
    }
};

/**
 * @brief Simple amplitude multiplier with CV modulation.
 *
 * Scales the input signal by a gain factor. Connect a control signal
 * to the amplitude CV input for dynamic volume control.
 *
 * @see Mixer, Compressor, Limiter
 */
struct Gain : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Gain";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 2.0f};

    GainThumbState* thumb_state_ = nullptr;

    ~Gain() override {
        if (thumb_state_) { thumb_state_->release_all(); delete thumb_state_; }
    }

    Gain() {
        vivid::semantic_tag(gain, "amplitude_linear");
        vivid::semantic_shape(gain, "scalar");
        vivid::semantic_intent(gain, "input_gain");
        vivid::description(gain, "Scales the input signal amplitude (0 = silence, 1 = unity)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",        VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",       VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"amplitude_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 1.0f});
        vivid::append_analysis_ports(out);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_state_) thumb_state_ = new GainThumbState();
        if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
            vivid_report_thumbnail_error(ctx, "gain thumbnail pipeline init failed");
            return;
        }
        struct Uniforms { float gain_val, pad0, pad1, pad2; } u{};
        u.gain_val = ctx->param_values[0];
        wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Gain Thumb Pass");
    }

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
        thumb_state_->release_all();
        static const char* kShader = R"(
struct Uniforms { data: vec4f, };
struct VertexOutput { @builtin(position) position: vec4f, @location(0) uv: vec2f, }
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
    let g = clamp(uniforms.data.x / 2.0, 0.0, 1.0);  // normalize 0-2 to 0-1

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);

    // Vertical bar in center third of thumbnail
    let bar_left = 0.3;
    let bar_right = 0.7;
    if (uv.x < bar_left || uv.x > bar_right) { return bg; }

    let pad = 0.08;
    let fill_top = 1.0 - g * (1.0 - 2.0 * pad) - pad;

    // Bar background (dim)
    let bar_bg = vec4f(40.0/255.0, 42.0/255.0, 48.0/255.0, 200.0/255.0);

    if (uv.y < pad || uv.y > 1.0 - pad) { return bg; }

    if (uv.y < fill_top) { return bar_bg; }

    // Color gradient: green at low, yellow at unity (0.5), red at boost
    let norm_y = 1.0 - (uv.y - pad) / (1.0 - 2.0 * pad);
    var col = vec3f(0.0);
    if (norm_y < 0.5) {
        col = mix(vec3f(80.0/255.0, 190.0/255.0, 100.0/255.0),
                  vec3f(220.0/255.0, 200.0/255.0, 60.0/255.0), norm_y * 2.0);
    } else {
        col = mix(vec3f(220.0/255.0, 200.0/255.0, 60.0/255.0),
                  vec3f(220.0/255.0, 80.0/255.0, 60.0/255.0), (norm_y - 0.5) * 2.0);
    }
    return vec4f(col, 220.0/255.0);
}
)";
        thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kShader, "Gain Thumb Shader");
        thumb_state_->uniform_buf =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Gain Thumb Uniforms");
        thumb_state_->bind_layout =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Gain Thumb BGL");
        thumb_state_->pipe_layout =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Gain Thumb Layout");
        thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "Gain Thumb BG");
        thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Gain Thumb Pipeline");
        thumb_state_->pipeline_format = ctx->thumbnail_format;
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        float amp_cv_val = 1.0f;
        float g = gain.value * amp_cv_val;

        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in[i] * g;
    }
};

VIVID_REGISTER(Gain)
VIVID_THUMBNAIL(Gain)
