#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "midi_helpers.h"
#include <algorithm>
#include <cmath>
#include "operator_api/thumbnail.h"
#include <cstring>

// ChordProgression — dual-cadence diatonic chord sequencer.
//
// Beat-phase wrap detection + diatonic chord building + gate window + MIDI output.
// All timing logic is cadence-agnostic.
//
struct ChordProgression : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "ChordProgression";
    static constexpr bool kTimeDependent = true;

    // --- Global parameters ---
    vivid::Param<int>   steps          {"steps",          4, 1, 8};
    vivid::Param<int>   key_root       {"key_root",       0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   mode           {"mode",           0, {"Major","Minor","Dorian","Mixolydian","Harm Min","Mel Min"}};
    vivid::Param<int>   octave         {"octave",         4, 2, 7};
    vivid::Param<int>   beats_per_step {"beats_per_step", 4, 1, 16};
    vivid::Param<float> gate_length    {"gate_length",    0.8f, 0.01f, 1.0f};
    vivid::Param<float> velocity       {"velocity",       0.8f, 0.0f, 1.0f};

    // --- Per-step degree (0=I .. 6=VII) ---
    vivid::Param<int>   degree_0 {"degree_0", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_1 {"degree_1", 3, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_2 {"degree_2", 4, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_3 {"degree_3", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_4 {"degree_4", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_5 {"degree_5", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_6 {"degree_6", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_7 {"degree_7", 0, {"I","II","III","IV","V","VI","VII"}};

    // --- Per-step voicing ---
    vivid::Param<int>   voicing_0 {"voicing_0", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_1 {"voicing_1", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_2 {"voicing_2", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_3 {"voicing_3", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_4 {"voicing_4", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_5 {"voicing_5", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_6 {"voicing_6", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_7 {"voicing_7", 0, {"Root","Inv1","Inv2","Drop2"}};

    // --- Per-step extension ---
    vivid::Param<int>   ext_0 {"ext_0", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_1 {"ext_1", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_2 {"ext_2", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_3 {"ext_3", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_4 {"ext_4", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_5 {"ext_5", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_6 {"ext_6", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_7 {"ext_7", 0, {"Triad","7th","Add9"}};

    vivid::Param<int> midi_channel {"midi_channel", 1, 1, 16};

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

    // Semitone intervals from key root for each scale degree
    static constexpr int kScaleIntervals[6][7] = {
        {0, 2, 4, 5, 7, 9, 11},  // Major (Ionian)
        {0, 2, 3, 5, 7, 8, 10},  // Natural Minor (Aeolian)
        {0, 2, 3, 5, 7, 9, 10},  // Dorian
        {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
        {0, 2, 3, 5, 7, 8, 11},  // Harmonic Minor
        {0, 2, 3, 5, 7, 9, 11},  // Melodic Minor (ascending)
    };

    // Chromatic note number -> circle-of-fifths position (for thumbnail)
    static constexpr int kChromaticToFifths[12] = {
        0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5
    };

    // Circle-of-fifths position -> chromatic note number (for thumbnail)
    static constexpr int kFifthsOrder[12] = {
        0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5
    };

    // Param indices:
    //  0       = steps
    //  1       = key_root
    //  2       = mode
    //  3       = octave
    //  4       = beats_per_step
    //  5       = gate_length
    //  6       = velocity
    //  7..14   = degree_0..degree_7
    //  15..22  = voicing_0..voicing_7
    //  23..30  = ext_0..ext_7
    //  31      = midi_channel

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);           // 0
        out.push_back(&key_root);        // 1
        out.push_back(&mode);            // 2
        out.push_back(&octave);          // 3
        out.push_back(&beats_per_step);  // 4
        out.push_back(&gate_length);     // 5
        out.push_back(&velocity);        // 6
        out.push_back(&degree_0);  out.push_back(&degree_1);   // 7..14
        out.push_back(&degree_2);  out.push_back(&degree_3);
        out.push_back(&degree_4);  out.push_back(&degree_5);
        out.push_back(&degree_6);  out.push_back(&degree_7);
        out.push_back(&voicing_0); out.push_back(&voicing_1);  // 15..22
        out.push_back(&voicing_2); out.push_back(&voicing_3);
        out.push_back(&voicing_4); out.push_back(&voicing_5);
        out.push_back(&voicing_6); out.push_back(&voicing_7);
        out.push_back(&ext_0);    out.push_back(&ext_1);       // 23..30
        out.push_back(&ext_2);    out.push_back(&ext_3);
        out.push_back(&ext_4);    out.push_back(&ext_5);
        out.push_back(&ext_6);    out.push_back(&ext_7);
        out.push_back(&midi_channel); // 31
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"notes",      VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"velocities", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"gates",      VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"note",       VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});
        out.push_back({"vel",        VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});
        out.push_back({"gate",       VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    // Build chord intervals relative to the chord root using diatonic third-stacking.
    // Returns the number of notes written to out_intervals[].
    static int build_chord(int scale_mode, int degree, int ext,
                           int* out_intervals, int max_notes) {
        if (scale_mode < 0 || scale_mode > 5) scale_mode = 0;
        if (degree < 0 || degree > 6) degree = 0;

        const int* scale = kScaleIntervals[scale_mode];
        int root_st = scale[degree];
        int count = 0;

        // Root
        out_intervals[count++] = 0;

        // Third (degree + 2)
        if (count < max_notes) {
            int interval = scale[(degree + 2) % 7] - root_st;
            if (interval <= 0) interval += 12;
            out_intervals[count++] = interval;
        }

        // Fifth (degree + 4)
        if (count < max_notes) {
            int interval = scale[(degree + 4) % 7] - root_st;
            if (interval <= 0) interval += 12;
            out_intervals[count++] = interval;
        }

        if (ext == 1 && count < max_notes) {
            // 7th (degree + 6)
            int interval = scale[(degree + 6) % 7] - root_st;
            if (interval <= 0) interval += 12;
            out_intervals[count++] = interval;
        } else if (ext == 2 && count < max_notes) {
            // Add9: 2nd scale degree above root, shifted up an octave
            int interval = scale[(degree + 1) % 7] - root_st;
            if (interval <= 0) interval += 12;
            interval += 12;
            out_intervals[count++] = interval;
        }

        return count;
    }

    // Apply voicing transformation to chord intervals (in-place).
    static void apply_voicing(int voicing, int* intervals, int count) {
        if (count < 2) return;
        std::sort(intervals, intervals + count);

        switch (voicing) {
            case 0: // Root position
                break;
            case 1: // First inversion: move bottom note up 12
                intervals[0] += 12;
                std::sort(intervals, intervals + count);
                break;
            case 2: // Second inversion: move two bottom notes up 12
                intervals[0] += 12;
                if (count > 1) intervals[1] += 12;
                std::sort(intervals, intervals + count);
                break;
            case 3: // Drop 2: move 2nd-from-top down 12
                if (count >= 2) {
                    intervals[count - 2] -= 12;
                    std::sort(intervals, intervals + count);
                }
                break;
        }
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values, ctx->output_spreads,
                ctx->output_values, ctx->custom_outputs, ctx->custom_output_count);
    }

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values[0], ctx->param_values, ctx->output_spreads,
                ctx->output_float_values, ctx->custom_outputs, ctx->custom_output_count);
    }

    void compute(float beat_phase, const float* params, VividSpreadPort* out_spreads,
                 float* output_values, void** custom_outputs, uint32_t custom_output_count) {
        int num_steps = steps.int_value();
        int kr = key_root.int_value();
        int m  = mode.int_value();
        int oct = octave.int_value();
        int bps = beats_per_step.int_value();
        float gl  = gate_length.value;
        float vel = velocity.value;

        if (kr < 0) kr = 0; if (kr > 11) kr = 11;
        if (m < 0) m = 0;   if (m > 5) m = 5;

        // Detect beat_phase wraps (delta < -0.5) -> increment beat_count_
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) {
            beat_count_++;
        }
        prev_phase_ = beat_phase;

        int current_step = (beat_count_ / bps) % num_steps;

        // Per-step params via param_values indices
        int degree  = static_cast<int>(params[7 + current_step]);
        int voicing = static_cast<int>(params[15 + current_step]);
        int ext     = static_cast<int>(params[23 + current_step]);
        if (degree < 0)  degree = 0;  if (degree > 6)  degree = 6;
        if (voicing < 0) voicing = 0; if (voicing > 3) voicing = 3;
        if (ext < 0)     ext = 0;     if (ext > 2)     ext = 2;

        // Build chord
        int intervals[5];
        int chord_size = build_chord(m, degree, ext, intervals, 5);
        apply_voicing(voicing, intervals, chord_size);

        // MIDI base note for the chord root
        int base_note = kr + oct * 12 + kScaleIntervals[m][degree];

        float gate_val = (beat_phase < gl) ? 1.0f : 0.0f;

        // Write output spreads
        if (out_spreads) {
            auto& notes_sp = out_spreads[0];
            auto& vel_sp   = out_spreads[1];
            auto& gates_sp = out_spreads[2];

            uint32_t len = static_cast<uint32_t>(chord_size);
            if (notes_sp.capacity >= len) {
                notes_sp.length = len;
                vel_sp.length   = len;
                gates_sp.length = len;
                for (uint32_t i = 0; i < len; ++i) {
                    notes_sp.data[i] = static_cast<float>(base_note + intervals[i]);
                    vel_sp.data[i]   = vel;
                    gates_sp.data[i] = gate_val;
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
                int note = std::clamp(base_note + intervals[i], 0, 127);
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

        // Scalar fallback: first note of chord
        if (chord_size > 0 && output_values) {
            output_values[0] = static_cast<float>(base_note + intervals[0]);
            output_values[1] = vel;
            output_values[2] = gate_val;
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "chord_progression thumbnail pipeline init failed");
            return;
        }

        struct Uniforms {
            float meta[4];  // key_root (0-11), chord_root (0-11, -1=none), scale_mask (bitcast u32), pad
            float pad[4];
        } u{};

        int kr = (ctx->param_count > 1)
            ? std::max(0, std::min(11, static_cast<int>(ctx->param_values[1]))) : 0;
        int m = (ctx->param_count > 2)
            ? std::max(0, std::min(5, static_cast<int>(ctx->param_values[2]))) : 0;
        int num_steps = (ctx->param_count > 0)
            ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[0]))) : 4;

        // Compute scale mask (12-bit bitmask)
        uint32_t scale_mask = 0;
        for (int d = 0; d < 7; ++d) {
            scale_mask |= (1u << ((kr + kScaleIntervals[m][d]) % 12));
        }

        // Detect current chord root
        int current_chord_root = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 3) ? static_cast<int>(ctx->param_values[3]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                if (ctx->param_count <= static_cast<uint32_t>(7 + s)) break;
                int deg = std::max(0, std::min(6, static_cast<int>(ctx->param_values[7 + s])));
                int voic = (ctx->param_count > static_cast<uint32_t>(15 + s))
                    ? std::max(0, std::min(3, static_cast<int>(ctx->param_values[15 + s]))) : 0;
                int ext = (ctx->param_count > static_cast<uint32_t>(23 + s))
                    ? std::max(0, std::min(2, static_cast<int>(ctx->param_values[23 + s]))) : 0;

                int intervals[5];
                int csz = build_chord(m, deg, ext, intervals, 5);
                apply_voicing(voic, intervals, csz);

                int base = kr + oct * 12 + kScaleIntervals[m][deg];
                float expected = static_cast<float>(base + intervals[0]);
                if (std::fabs(out_note - expected) < 0.5f) {
                    current_chord_root = (kr + kScaleIntervals[m][deg]) % 12;
                    break;
                }
            }
        }

        u.meta[0] = static_cast<float>(kr);
        u.meta[1] = static_cast<float>(current_chord_root);
        float mask_as_float;
        std::memcpy(&mask_as_float, &scale_mask, sizeof(float));
        u.meta[2] = mask_as_float;

        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "ChordProg Thumb Pass");
    }

    ~ChordProgression() override {
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
    pad: vec4f,
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

    let key_root = i32(uniforms.info.x);
    let chord_root = i32(uniforms.info.y);
    let scale_mask = bitcast<u32>(uniforms.info.z);

    // Circle of fifths mapping: chromatic -> fifths position
    let fifths = array<i32,12>(0,7,2,9,4,11,6,1,8,3,10,5);
    // Fifths position -> chromatic
    let fifths_order = array<i32,12>(0,7,2,9,4,11,6,1,8,3,10,5);

    let cx = 0.5;
    let cy = 0.5;
    let radius = 0.38;
    let pi = 3.14159265359;

    // Draw line from key root to chord root
    if (chord_root >= 0 && chord_root != key_root) {
        let kr_pos = fifths[key_root];
        let cc_pos = fifths[chord_root];
        let a0 = f32(kr_pos) * (2.0 * pi / 12.0) - pi * 0.5;
        let a1 = f32(cc_pos) * (2.0 * pi / 12.0) - pi * 0.5;
        let p0 = vec2f(cx + radius * cos(a0), cy + radius * sin(a0));
        let p1 = vec2f(cx + radius * cos(a1), cy + radius * sin(a1));

        // Point-to-line-segment distance
        let d = p1 - p0;
        let t = clamp(dot(uv - p0, d) / dot(d, d), 0.0, 1.0);
        let closest = p0 + t * d;
        let line_dist = length(uv - closest);
        if (line_dist < 0.015) {
            return vec4f(
                min(1.0, bg.r + 30.0/255.0),
                min(1.0, bg.g + 35.0/255.0),
                min(1.0, bg.b + 40.0/255.0),
                bg.a
            );
        }
    }

    // Draw 12 dots
    for (var pos = 0; pos < 12; pos++) {
        let chromatic = fifths_order[pos];
        let angle = f32(pos) * (2.0 * pi / 12.0) - pi * 0.5;
        let dx = cx + radius * cos(angle);
        let dy = cy + radius * sin(angle);

        let dist = length(uv - vec2f(dx, dy));

        var dot_r: f32;
        var col: vec4f;

        if (chromatic == key_root) {
            dot_r = 4.0 / 64.0;
            col = vec4f(255.0/255.0, 200.0/255.0, 80.0/255.0, 1.0);
        } else if (chromatic == chord_root) {
            dot_r = 3.5 / 64.0;
            col = vec4f(80.0/255.0, 220.0/255.0, 255.0/255.0, 1.0);
        } else if ((scale_mask & (1u << u32(chromatic))) != 0u) {
            dot_r = 2.5 / 64.0;
            col = vec4f(100.0/255.0, 120.0/255.0, 160.0/255.0, 200.0/255.0);
        } else {
            dot_r = 1.5 / 64.0;
            col = vec4f(50.0/255.0, 55.0/255.0, 65.0/255.0, 140.0/255.0);
        }

        if (dist < dot_r) {
            return col;
        }
    }

    return bg;
}
)";

        static constexpr uint64_t kUniformSize = sizeof(float) * 8;
        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "ChordProg Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, kUniformSize, "ChordProg Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, kUniformSize, "ChordProg Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "ChordProg Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, kUniformSize, "ChordProg Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "ChordProg Thumb Pipeline");
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

VIVID_REGISTER(ChordProgression)
VIVID_THUMBNAIL(ChordProgression)
