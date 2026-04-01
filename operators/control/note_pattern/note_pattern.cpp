#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "midi_helpers.h"
#include <algorithm>
#include <cmath>
#include "operator_api/thumbnail.h"

namespace note_insp {
static const char* kNoteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static const char* kChordAbbr[] = {"M","m","dim","aug","7","m7","M7"};
static constexpr float kTypeColors[7][3] = {
    {0.39f, 0.63f, 0.86f}, {0.63f, 0.39f, 0.78f}, {0.78f, 0.39f, 0.39f},
    {0.86f, 0.71f, 0.31f}, {0.31f, 0.71f, 0.63f}, {0.55f, 0.47f, 0.78f},
    {0.31f, 0.55f, 0.86f},
};
static constexpr float kPreviewH = 60.0f;
static constexpr float kLineH = 18.0f;
} // namespace note_insp
/**
 * @brief Per-step chord pattern sequencer with visual grid editor.
 *
 * Sequences up to 8 steps, each with a configurable root note and chord
 * type (major, minor, 7th, dim, aug, sus2, sus4). Outputs chord notes
 * as a polyphonic lane array with MIDI output.
 *
 * @see ChordProgression, Arpeggiator, PatternSeq
 */
struct NotePattern : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "NotePattern";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   steps        {"steps",          4, 1, 8};
    vivid::Param<int>   root_0       {"root_0",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_1       {"root_1",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_2       {"root_2",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_3       {"root_3",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_4       {"root_4",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_5       {"root_5",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_6       {"root_6",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_7       {"root_7",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   type_0       {"type_0",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_1       {"type_1",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_2       {"type_2",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_3       {"type_3",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_4       {"type_4",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_5       {"type_5",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_6       {"type_6",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_7       {"type_7",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   octave       {"octave",         4, 2, 7};
    vivid::Param<int>   beats_per_step{"beats_per_step", 4, 1, 16};
    vivid::Param<float> gate_length  {"gate_length",    0.8f, 0.01f, 1.0f};
    vivid::Param<float> velocity     {"velocity",       0.8f, 0.0f, 1.0f};
    vivid::Param<int>   midi_channel {"midi_channel",   1, 1, 16};

    NotePattern() {
        vivid::description(steps, "Number of active chord steps in the sequence, 1 to 8");
        vivid::description(root_0, "Root note for step 1");
        vivid::description(root_1, "Root note for step 2");
        vivid::description(root_2, "Root note for step 3");
        vivid::description(root_3, "Root note for step 4");
        vivid::description(root_4, "Root note for step 5");
        vivid::description(root_5, "Root note for step 6");
        vivid::description(root_6, "Root note for step 7");
        vivid::description(root_7, "Root note for step 8");
        vivid::description(type_0, "Chord type for step 1");
        vivid::description(type_1, "Chord type for step 2");
        vivid::description(type_2, "Chord type for step 3");
        vivid::description(type_3, "Chord type for step 4");
        vivid::description(type_4, "Chord type for step 5");
        vivid::description(type_5, "Chord type for step 6");
        vivid::description(type_6, "Chord type for step 7");
        vivid::description(type_7, "Chord type for step 8");
        vivid::description(octave, "Base octave for all chord notes, 2 to 7");
        vivid::description(beats_per_step, "Number of beats each chord step lasts");
        vivid::description(gate_length, "Fraction of each step where notes are held, 0 to 1");
        vivid::description(velocity, "MIDI velocity for all chord notes, 0 to 1");
        vivid::description(midi_channel, "MIDI channel for chord output, 1 to 16");
    }

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    // Internal state
    int beat_count_ = 0;
    float prev_phase_ = 0.0f;
    bool prev_gate_ = false;
    int prev_notes_[5] = {-1, -1, -1, -1, -1};
    int prev_note_count_ = 0;
    VividMidiBuffer midi_buf_ = {};

    // Chord interval tables: [type][interval_count], terminated by -1
    // 0=major, 1=minor, 2=dim, 3=aug, 4=dom7, 5=min7, 6=maj7
    static constexpr int kChordIntervals[7][5] = {
        {0, 4, 7, -1, -1},    // major
        {0, 3, 7, -1, -1},    // minor
        {0, 3, 6, -1, -1},    // dim
        {0, 4, 8, -1, -1},    // aug
        {0, 4, 7, 10, -1},    // dom7
        {0, 3, 7, 10, -1},    // min7
        {0, 4, 7, 11, -1},    // maj7
    };

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);
        // Hide step params — rendered by custom inspector
        size_t hidden_start = out.size();
        out.push_back(&root_0); out.push_back(&root_1);
        out.push_back(&root_2); out.push_back(&root_3);
        out.push_back(&root_4); out.push_back(&root_5);
        out.push_back(&root_6); out.push_back(&root_7);
        out.push_back(&type_0); out.push_back(&type_1);
        out.push_back(&type_2); out.push_back(&type_3);
        out.push_back(&type_4); out.push_back(&type_5);
        out.push_back(&type_6); out.push_back(&type_7);
        for (size_t i = hidden_start; i < out.size(); ++i)
            out[i]->display_hint = VIVID_DISPLAY_HIDDEN;
        out.push_back(&octave);
        out.push_back(&beats_per_step);
        out.push_back(&gate_length);
        out.push_back(&velocity);
        out.push_back(&midi_channel);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"notes",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        out.push_back({"velocities", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        out.push_back({"gates",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values, ctx->output_lanes,
                ctx->output_values, ctx->custom_outputs, ctx->custom_output_count);
    }

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values[0], ctx->param_values, ctx->output_lanes,
                ctx->output_float_values, ctx->custom_outputs, ctx->custom_output_count);
    }

private:
    void compute(float beat_phase, const float* params, VividLanePort* output_lanes,
                 float* output_values, void** custom_outputs, uint32_t custom_output_count) {
        int num_steps = steps.int_value();
        int oct = octave.int_value();
        int bps = beats_per_step.int_value();
        float gl = gate_length.value;
        float vel = velocity.value;

        // Detect beat_phase wraps (delta < -0.5) → increment beat_count_
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) {
            beat_count_++;
        }
        prev_phase_ = beat_phase;

        // Current step
        int current_step = (beat_count_ / bps) % num_steps;

        // Look up root and chord type for current step
        // Params are ordered: steps, root_0..root_7, type_0..type_7, octave, beats_per_step, ...
        // root_0 is param index 1, type_0 is param index 9
        int root = static_cast<int>(params[1 + current_step]);
        int chord_type = static_cast<int>(params[9 + current_step]);
        if (chord_type < 0) chord_type = 0;
        if (chord_type > 6) chord_type = 6;

        // Count chord intervals
        const int* intervals = kChordIntervals[chord_type];
        int chord_size = 0;
        for (int i = 0; i < 5 && intervals[i] >= 0; ++i) {
            chord_size++;
        }

        // Gate: 1.0 if beat_phase < gate_length, else 0.0
        float gate_val = (beat_phase < gl) ? 1.0f : 0.0f;

        // Write output lane arrays
        if (output_lanes) {
            auto& notes_lane = output_lanes[0];
            auto& velocity_lane   = output_lanes[1];
            auto& gates_lane = output_lanes[2];

            uint32_t len = static_cast<uint32_t>(chord_size);
            if (notes_lane.capacity >= len) {
                notes_lane.length = len;
                velocity_lane.length   = len;
                gates_lane.length = len;
                for (uint32_t i = 0; i < len; ++i) {
                    notes_lane.data[i] = static_cast<float>(root + oct * 12 + intervals[i]);
                    velocity_lane.data[i]   = vel;
                    gates_lane.data[i] = gate_val;
                }
            }
        }

        // MIDI output: polyphonic note-on/off on gate edges
        uint8_t ch = static_cast<uint8_t>(midi_channel.int_value() - 1);
        uint8_t midi_vel = static_cast<uint8_t>(std::clamp(static_cast<int>(vel * 127.0f), 0, 127));
        midi_buf_.count = 0;
        bool gate_high = (gate_val > 0.5f);
        if (gate_high && !prev_gate_) {
            // Gate rising: note-off previous chord, note-on new chord
            for (int i = 0; i < prev_note_count_; ++i) {
                if (prev_notes_[i] >= 0) {
                    vivid_sequencers::midi_note_off(midi_buf_,
                        static_cast<uint8_t>(prev_notes_[i]), ch);
                }
            }
            prev_note_count_ = chord_size;
            for (int i = 0; i < chord_size && i < 5; ++i) {
                int note = std::clamp(root + oct * 12 + intervals[i], 0, 127);
                vivid_sequencers::midi_note_on(midi_buf_,
                    static_cast<uint8_t>(note), midi_vel, ch);
                prev_notes_[i] = note;
            }
        } else if (!gate_high && prev_gate_) {
            // Gate falling: note-off all
            for (int i = 0; i < prev_note_count_; ++i) {
                if (prev_notes_[i] >= 0) {
                    vivid_sequencers::midi_note_off(midi_buf_,
                        static_cast<uint8_t>(prev_notes_[i]), ch);
                    prev_notes_[i] = -1;
                }
            }
            prev_note_count_ = 0;
        }
        prev_gate_ = gate_high;
        if (custom_outputs && custom_output_count > 0) {
            custom_outputs[0] = &midi_buf_;
        }

        // Scalar fallback: first note
        if (chord_size > 0 && output_values) {
            output_values[0] = static_cast<float>(root + oct * 12 + intervals[0]);
            output_values[1] = vel;
            output_values[2] = gate_val;
        }
    }
public:

    void draw_inspector(VividInspectorContext* ctx) override {
        namespace ni = note_insp;
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;

        float px = ctx->content_x;
        float py = ctx->content_y;
        float w = ctx->content_width;
        float h = ni::kPreviewH;

        int num_steps = (ctx->param_count > 0) ? static_cast<int>(ctx->param_values[0]) : 4;
        num_steps = std::max(1, std::min(8, num_steps));
        float cell_w = w / static_cast<float>(num_steps);

        // Detect current step from first output note (scalar fallback)
        int current_step = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 17) ? static_cast<int>(ctx->param_values[17]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                int root = static_cast<int>(ctx->param_values[1 + s]);
                int chord_type = std::clamp(static_cast<int>(ctx->param_values[9 + s]), 0, 6);
                float expected = static_cast<float>(root + oct * 12 + kChordIntervals[chord_type][0]);
                if (std::fabs(out_note - expected) < 0.5f) {
                    current_step = s;
                    break;
                }
            }
        }

        py += 4;

        // Dark background
        d.draw_rect(o, px, py, w, h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        for (int s = 0; s < num_steps; ++s) {
            int root = std::clamp(static_cast<int>(ctx->param_values[1 + s]), 0, 11);
            int chord_type = std::clamp(static_cast<int>(ctx->param_values[9 + s]), 0, 6);

            float cx = px + s * cell_w;
            bool is_current = (s == current_step);

            // Current step highlight
            if (is_current) {
                d.draw_rect(o, cx, py, cell_w, h, {0.18f, 0.22f, 0.30f, 0.6f});
            }

            // Note name centered
            const char* note = ni::kNoteNames[root];
            float nw = d.text_width(o, note, 1.0f);
            float text_x = cx + (cell_w - nw) * 0.5f;
            float text_y = py + 6;
            float bright = is_current ? 1.0f : 0.85f;
            d.draw_text(o, text_x, text_y, note, {bright, bright, bright, 1.0f}, 1.0f);

            // Chord abbreviation below in type color
            const char* chord = ni::kChordAbbr[chord_type];
            float cw2 = d.text_width(o, chord, 1.0f);
            float chord_x = cx + (cell_w - cw2) * 0.5f;
            float chord_y = text_y + ni::kLineH;
            const float* tc = ni::kTypeColors[chord_type];
            d.draw_text(o, chord_x, chord_y, chord,
                        {tc[0], tc[1], tc[2], is_current ? 1.0f : 0.7f}, 1.0f);

            // Colored bar at bottom
            float bar_h = 4.0f;
            float bar_y = py + h - bar_h - 2.0f;
            d.draw_rect(o, cx + 2, bar_y, cell_w - 4, bar_h,
                        {tc[0], tc[1], tc[2], is_current ? 0.9f : 0.6f});

            // Cell divider
            if (s > 0) {
                d.draw_rect(o, cx, py, 1, h,
                            {th.separator.r, th.separator.g, th.separator.b, 0.5f});
            }
        }

        ctx->consumed_height = 4.0f + h + 4.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "note_pattern thumbnail pipeline init failed");
            return;
        }

        // Pack uniforms
        struct Uniforms {
            float meta[4];       // num_steps, current_step, pad, pad
            float root_0123[4];
            float root_4567[4];
            float type_0123[4];
            float type_4567[4];
        } u{};

        int num_steps = (ctx->param_count > 0) ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[0]))) : 4;

        // Detect current step from output
        int current_step = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 17) ? static_cast<int>(ctx->param_values[17]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                int root = static_cast<int>(ctx->param_values[1 + s]);
                int chord_type = std::clamp(static_cast<int>(ctx->param_values[9 + s]), 0, 6);
                float expected = static_cast<float>(root + oct * 12 + kChordIntervals[chord_type][0]);
                if (std::fabs(out_note - expected) < 0.5f) {
                    current_step = s;
                    break;
                }
            }
        }

        u.meta[0] = static_cast<float>(num_steps);
        u.meta[1] = static_cast<float>(current_step);

        for (int i = 0; i < 8; ++i) {
            float root = (ctx->param_count > static_cast<uint32_t>(1 + i))
                ? ctx->param_values[1 + i] : 0.0f;
            float type = (ctx->param_count > static_cast<uint32_t>(9 + i))
                ? ctx->param_values[9 + i] : 0.0f;
            if (i < 4) {
                u.root_0123[i] = root;
                u.type_0123[i] = type;
            } else {
                u.root_4567[i - 4] = root;
                u.type_4567[i - 4] = type;
            }
        }

        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "NotePattern Thumb Pass");
    }

    ~NotePattern() override {
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
    info: vec4f,
    root_0123: vec4f,
    root_4567: vec4f,
    type_0123: vec4f,
    type_4567: vec4f,
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

fn get_root(idx: i32) -> f32 {
    if (idx < 4) { return uniforms.root_0123[idx]; }
    return uniforms.root_4567[idx - 4];
}

fn get_type(idx: i32) -> f32 {
    if (idx < 4) { return uniforms.type_0123[idx]; }
    return uniforms.type_4567[idx - 4];
}

fn type_color(t: i32) -> vec3f {
    // 7-color palette for chord types
    switch(t) {
        case 0: { return vec3f(100.0, 160.0, 220.0); }  // Major  — blue
        case 1: { return vec3f(160.0, 100.0, 200.0); }  // Minor  — purple
        case 2: { return vec3f(200.0, 100.0, 100.0); }  // Dim    — red
        case 3: { return vec3f(220.0, 180.0, 80.0); }   // Aug    — gold
        case 4: { return vec3f(80.0, 180.0, 160.0); }   // Dom7   — teal
        case 5: { return vec3f(140.0, 120.0, 200.0); }  // Min7   — lavender
        case 6: { return vec3f(80.0, 140.0, 220.0); }   // Maj7   — sky blue
        default: { return vec3f(100.0, 160.0, 220.0); }
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let highlight_bg = vec4f(35.0/255.0, 38.0/255.0, 45.0/255.0, 230.0/255.0);

    let num_steps = i32(uniforms.info.x);
    let current_step = i32(uniforms.info.y);
    if (num_steps < 1) { return bg; }

    let pad = 4.0 / 64.0;
    let plot_x = (uv.x - pad) / (1.0 - 2.0 * pad);
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);

    if (plot_x < 0.0 || plot_x > 1.0 || plot_y < 0.0 || plot_y > 1.0) {
        return bg;
    }

    let col_f = plot_x * f32(num_steps);
    let col_idx = i32(floor(col_f));
    if (col_idx >= num_steps) { return bg; }

    let is_current = (col_idx == current_step);
    let root = clamp(i32(get_root(col_idx)), 0, 11);
    let chord_type = clamp(i32(get_type(col_idx)), 0, 6);

    // Bar height from root: (root+1)/12
    let bar_frac = (f32(root) + 1.0) / 12.0;
    let bar_top = 1.0 - bar_frac;
    let frac = fract(col_f);

    var col = type_color(chord_type);
    if (is_current) {
        col = min(col + vec3f(60.0), vec3f(255.0));
    }

    if (plot_y >= bar_top && frac > 0.02 && frac < 0.98) {
        return vec4f(col / 255.0, 220.0/255.0);
    }

    if (is_current) {
        return highlight_bg;
    }

    return bg;
}
)";

        static constexpr uint64_t kUniformSize = sizeof(float) * 20;
        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "NotePattern Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, kUniformSize, "NotePattern Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, kUniformSize, "NotePattern Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "NotePattern Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, kUniformSize, "NotePattern Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "NotePattern Thumb Pipeline");
        if (!thumb_shader_ || !thumb_uniform_buf_ || !thumb_bind_layout_ || !thumb_pipe_layout_
            || !thumb_bind_group_ || !thumb_pipeline_) {
            vivid::gpu::release(thumb_pipeline_);
            vivid::gpu::release(thumb_bind_group_);
            vivid::gpu::release(thumb_bind_layout_);
            vivid::gpu::release(thumb_uniform_buf_);
            vivid::gpu::release(thumb_shader_);
            vivid::gpu::release(thumb_pipe_layout_);
            thumb_pipeline_format_ = WGPUTextureFormat_Undefined;
            return;
        }
        thumb_pipeline_format_ = ctx->thumbnail_format;
    }
};

// Legacy registration removed — use _fr/_au variants instead.
VIVID_THUMBNAIL(NotePattern)
VIVID_INSPECTOR(NotePattern)
