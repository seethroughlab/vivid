#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"
#include "operator_api/thumbnail.h"

/**
 * @brief Colored noise generator with five spectral profiles.
 *
 * Generates white, pink, brown, blue, or violet noise. Each color has
 * a distinct spectral slope -- white is flat, pink falls at 3 dB/octave,
 * brown at 6 dB/octave, blue rises at 3 dB/octave, violet at 6 dB/octave.
 *
 * @tip Pink noise is useful for testing frequency response. Brown works well as a modulation source.
 * @see Oscillator, SpreadNoise
 */
struct Noise : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Noise";
    static constexpr bool kTimeDependent = false;  // uses PRNG state, not ctx->time

    vivid::Param<int>   color    {"color",     0, {"White", "Pink", "Brown", "Blue", "Violet"}};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};

    audio_dsp::WhiteNoise  white_;
    audio_dsp::PinkNoise   pink_;
    audio_dsp::BrownNoise  brown_;
    audio_dsp::BlueNoise   blue_;
    audio_dsp::VioletNoise violet_;

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    Noise() {
        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::description(color, "Spectral profile of the noise (white = flat, pink = -3 dB/oct, brown = -6 dB/oct)");
        vivid::description(amplitude, "Output level of the noise signal");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&color);
        out.push_back(&amplitude);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float*   out = ctx->output_buffers[0];
        float    amp = amplitude.value;
        int      col = color.int_value();

        switch (col) {
            case 1:  // Pink
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = pink_.next() * amp;
                break;
            case 2:  // Brown
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = brown_.next() * amp;
                break;
            case 3:  // Blue
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = blue_.next() * amp;
                break;
            case 4:  // Violet
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = violet_.next() * amp;
                break;
            default: // White
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = white_.next() * amp;
                break;
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "noise thumbnail pipeline init failed");
            return;
        }

        struct Uniforms { float seed; float pad[3]; } u{};
        u.seed = 42.0f;
        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "Noise Thumb Pass");
    }

    ~Noise() override {
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

fn pcg_hash(inp: u32) -> u32 {
    var state = inp * 747796405u + 2891336453u;
    var word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let col = vec4f(80.0/255.0, 200.0/255.0, 220.0/255.0, 220.0/255.0);

    let seed = u32(uniforms.data.x);
    let col_idx = u32(uv.x * 64.0);
    let hash = pcg_hash(col_idx + seed * 1000u);
    let bar_height = f32(hash % 1000u) / 1000.0;

    // Bar centered on midline
    let mid = 0.5;
    let half_h = bar_height * 0.4;
    let top = mid - half_h;
    let bot = mid + half_h;

    let y = uv.y;
    if (y > top && y < bot) {
        return col;
    }

    // Midline
    if (abs(y - 0.5) < 0.005) {
        return vec4f(60.0/255.0, 65.0/255.0, 75.0/255.0, 150.0/255.0);
    }

    return bg;
}
)";

        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Noise Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Noise Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Noise Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "Noise Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, sizeof(float) * 4, "Noise Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "Noise Thumb Pipeline");
        thumb_pipeline_format_ = ctx->thumbnail_format;
    }
};

VIVID_REGISTER(Noise)
VIVID_THUMBNAIL(Noise)
