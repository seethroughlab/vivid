#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "midi_helpers.h"
#include "tracker_data.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <functional>
#include <string>
#include "operator_api/thumbnail.h"

// ---------------------------------------------------------------------------
// Inspector helpers (namespace-scope, used by Tracker::draw_inspector)
// ---------------------------------------------------------------------------

namespace tracker_insp {

// GLFW key constants (avoid requiring GLFW header in operator code)
static constexpr int kKeyEscape    = 256;
static constexpr int kKeyEnter     = 257;
static constexpr int kKeyBackspace = 259;
static constexpr int kKeyDelete    = 261;
static constexpr int kKeyRight     = 262;
static constexpr int kKeyLeft      = 263;
static constexpr int kKeyDown      = 264;
static constexpr int kKeyUp        = 265;

// Layout constants
static constexpr float kRowH       = 16.0f;
static constexpr float kRowNumW    = 20.0f;
static constexpr float kNoteW      = 34.0f;
static constexpr float kVelW       = 22.0f;
static constexpr float kFxW        = 30.0f;
static constexpr int   kMaxVisRows = 16;
static constexpr float kTabW       = 80.0f;
static constexpr float kChTabW     = 28.0f;
static constexpr float kChTabH     = 18.0f;
static constexpr float kLineH      = 18.0f;

static constexpr float kChColors[8][3] = {
    {0.39f, 0.63f, 0.86f}, {0.86f, 0.47f, 0.31f}, {0.31f, 0.78f, 0.55f}, {0.78f, 0.71f, 0.24f},
    {0.63f, 0.39f, 0.78f}, {0.24f, 0.71f, 0.78f}, {0.78f, 0.39f, 0.63f}, {0.55f, 0.78f, 0.31f}
};

inline void format_note(uint8_t note, char out[4]) {
    if (note == tracker::NOTE_EMPTY) { out[0]='.'; out[1]='.'; out[2]='.'; out[3]=0; return; }
    if (note == tracker::NOTE_OFF)   { out[0]='='; out[1]='='; out[2]='='; out[3]=0; return; }
    static const char* names[] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    const char* prefix = names[note % 12];
    out[0] = prefix[0];
    out[1] = prefix[1];
    int oct = (note / 12) - 1;
    out[2] = (oct >= 0 && oct <= 9) ? static_cast<char>('0' + oct) : '?';
    out[3] = 0;
}

inline void format_hex2(uint8_t val, char out[3]) {
    static const char hex[] = "0123456789ABCDEF";
    if (val == 0) { out[0]='.'; out[1]='.'; out[2]=0; return; }
    out[0] = hex[(val >> 4) & 0xF];
    out[1] = hex[val & 0xF];
    out[2] = 0;
}

inline void format_hex3(uint8_t type, uint8_t param, char out[4]) {
    static const char hex[] = "0123456789ABCDEF";
    if (type == 0 && param == 0) { out[0]='.'; out[1]='.'; out[2]='.'; out[3]=0; return; }
    out[0] = hex[type & 0xF];
    out[1] = hex[(param >> 4) & 0xF];
    out[2] = hex[param & 0xF];
    out[3] = 0;
}

inline uint8_t parse_note_str(const char* s) {
    if (!s || !s[0]) return tracker::NOTE_EMPTY;
    if (s[0] == '=') return tracker::NOTE_OFF;
    int semi = -1;
    switch (s[0]) {
        case 'C': case 'c': semi = 0; break;
        case 'D': case 'd': semi = 2; break;
        case 'E': case 'e': semi = 4; break;
        case 'F': case 'f': semi = 5; break;
        case 'G': case 'g': semi = 7; break;
        case 'A': case 'a': semi = 9; break;
        case 'B': case 'b': semi = 11; break;
        default: return tracker::NOTE_EMPTY;
    }
    int idx = 1;
    if (s[1] == '#') { semi++; idx = 2; }
    else if (s[1] == '-' || s[1] == ' ') { idx = 2; }
    if (!s[idx] || s[idx] < '0' || s[idx] > '9') return tracker::NOTE_EMPTY;
    int octave = s[idx] - '0';
    int note = (octave + 1) * 12 + semi;
    return static_cast<uint8_t>(std::clamp(note, 0, 127));
}

inline uint8_t parse_hex2_str(const char* s) {
    if (!s || !s[0] || !s[1]) return 0;
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    int hi = hex_val(s[0]), lo = hex_val(s[1]);
    if (hi < 0 || lo < 0) return 0;
    return static_cast<uint8_t>((hi << 4) | lo);
}

inline bool hit_test(float mx, float my, float rx, float ry, float rw, float rh) {
    return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
}

} // namespace tracker_insp

struct Tracker : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Tracker";
    static constexpr bool kTimeDependent = true;

    // Param indices: rate=0, speed=1, base_channel=2, channel_mode=3,
    //   edit_pattern=4, edit_channel=5, mute_mask=6, pattern_data=7
    vivid::Param<int>   rate          {"rate",          2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"}};
    vivid::Param<int>   speed         {"speed",         6, 1, 16};
    vivid::Param<int>   base_channel  {"base_channel",  1, 1, 16};
    vivid::Param<int>   channel_mode  {"channel_mode",  0, {"Single","Multi"}};
    vivid::Param<int>   edit_pattern  {"edit_pattern",  0, 0, 63};
    vivid::Param<int>   edit_channel  {"edit_channel",  0, 0, 7};
    vivid::Param<int>   mute_mask     {"mute_mask",     0, 0, 255};
    vivid::Param<vivid::TextValue> pattern_data {"pattern_data", ""};

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);           // 0
        out.push_back(&speed);          // 1
        out.push_back(&base_channel);   // 2
        out.push_back(&channel_mode);   // 3
        display_hint(edit_pattern, VIVID_DISPLAY_HIDDEN);
        display_hint(edit_channel, VIVID_DISPLAY_HIDDEN);
        display_hint(mute_mask,    VIVID_DISPLAY_HIDDEN);
        display_hint(pattern_data, VIVID_DISPLAY_HIDDEN);
        out.push_back(&edit_pattern);   // 4
        out.push_back(&edit_channel);   // 5
        out.push_back(&mute_mask);      // 6
        out.push_back(&pattern_data);   // 7
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_FLOAT,  VIVID_PORT_INPUT});   // in[0]
        out.push_back({"reset",     VIVID_PORT_FLOAT,  VIVID_PORT_INPUT});    // in[1]
        out.push_back({"notes",      VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});  // spread[0], port idx 0
        out.push_back({"velocities", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});  // spread[1], port idx 1
        out.push_back({"gates",      VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});  // spread[2], port idx 2
        out.push_back({"row",        VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});  // out[0]
        out.push_back({"pattern",    VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});  // out[1]
        out.push_back({"order",      VIVID_PORT_FLOAT,  VIVID_PORT_OUTPUT});  // out[2]
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void process_audio(const VividAudioContext* ctx) override {
        float beat_phase = ctx->input_float_values[0];
        bool reset_signal = ctx->input_float_values[1] > 0.5f;

        int r = std::clamp(static_cast<int>(ctx->param_values[0]), 0, 8);
        int spd = std::clamp(static_cast<int>(ctx->param_values[1]), 1, 16);
        int base_ch = std::clamp(static_cast<int>(ctx->param_values[2]), 1, 16) - 1;
        int ch_mode = std::clamp(static_cast<int>(ctx->param_values[3]), 0, 1);
        int mute = std::clamp(static_cast<int>(ctx->param_values[6]), 0, 255);

        // Parse pattern data if changed
        sync_pattern_data();

        // Reset handling
        if (reset_signal && !prev_reset_) {
            current_order_ = 0;
            current_row_ = 0;
            current_tick_ = 0;
            beat_count_ = 0;
            prev_global_tick_ = static_cast<int>(std::floor(
                beat_phase * kMultipliers[r] * static_cast<float>(spd)));
            prev_phase_ = beat_phase;
            for (auto& ch : channels_) {
                ch = tracker::ChannelState{};
            }
        }
        prev_reset_ = reset_signal;

        // Beat tracking
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        float total_beats = static_cast<float>(beat_count_) + beat_phase;
        float scaled = total_beats * kMultipliers[r];

        // Compute global tick
        int global_tick = static_cast<int>(std::floor(scaled * spd));

        midi_buf_.count = 0;

        // Process ticks since last frame
        if (global_tick != prev_global_tick_) {
            int ticks_to_process = global_tick - prev_global_tick_;
            if (ticks_to_process < 0 || ticks_to_process > 256)
                ticks_to_process = 1;  // safety clamp

            for (int t = 0; t < ticks_to_process; ++t) {
                process_tick(spd, base_ch, ch_mode, mute);
            }
        }
        prev_global_tick_ = global_tick;

        // Write spread outputs
        if (ctx->output_spreads) {
            // Output port indices are per-direction: notes=0, velocities=1, gates=2
            auto& notes_sp = ctx->output_spreads[0];
            auto& vels_sp  = ctx->output_spreads[1];
            auto& gates_sp = ctx->output_spreads[2];

            if (notes_sp.capacity >= tracker::MAX_CHANNELS) {
                notes_sp.length = tracker::MAX_CHANNELS;
                vels_sp.length  = tracker::MAX_CHANNELS;
                gates_sp.length = tracker::MAX_CHANNELS;
                for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
                    bool muted = (mute >> ch) & 1;
                    notes_sp.data[ch] = channels_[ch].current_pitch;
                    vels_sp.data[ch]  = muted ? 0.0f : static_cast<float>(channels_[ch].current_velocity) / 127.0f;
                    gates_sp.data[ch] = (channels_[ch].gate_active && !muted) ? 1.0f : 0.0f;
                }
            }
        }

        // Scalar outputs
        int pat_idx = 0;
        if (current_order_ < song_.arrangement_length)
            pat_idx = song_.arrangement[current_order_];
        ctx->output_float_values[0] = static_cast<float>(current_row_);
        ctx->output_float_values[1] = static_cast<float>(pat_idx);
        ctx->output_float_values[2] = static_cast<float>(current_order_);

        // MIDI output
        if (ctx->custom_outputs && ctx->custom_output_count > 0) {
            ctx->custom_outputs[0] = &midi_buf_;
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "tracker thumbnail pipeline init failed");
            return;
        }

        struct Uniforms {
            float meta[4];          // num_rows, current_row, num_channels_used, pad
            uint32_t activity_lo[4]; // ch0..ch3 row activity bitmasks (32 bits each)
            uint32_t activity_hi[4]; // ch4..ch7 row activity bitmasks
        } u{};

        int cur_row = -1;
        if (ctx->output_count > 3)
            cur_row = static_cast<int>(ctx->output_values[3]);

        // Parse song data
        tracker::TrackerSong thumb_song;
        bool has_data = false;
        if (pattern_data.str_value.size() > 0) {
            has_data = tracker::deserialize_song(pattern_data.str_value, thumb_song);
        }

        int nr = 0;
        int num_ch_used = 0;
        if (has_data) {
            int pat_idx = 0;
            if (ctx->output_count > 4)
                pat_idx = std::clamp(static_cast<int>(ctx->output_values[4]), 0, thumb_song.num_patterns - 1);

            const auto& pat = thumb_song.patterns[pat_idx];
            nr = pat.num_rows;

            // Build activity bitmasks — quantize into 32 bins
            for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
                uint32_t mask = 0;
                bool ch_has_notes = false;
                for (int r = 0; r < nr; ++r) {
                    if (pat.cells[ch][r].note != tracker::NOTE_EMPTY) {
                        int bin = (nr > 32) ? (r * 32 / nr) : r;
                        bin = std::min(bin, 31);
                        mask |= (1u << bin);
                        ch_has_notes = true;
                    }
                }
                if (ch_has_notes) num_ch_used = ch + 1;
                if (ch < 4) {
                    u.activity_lo[ch] = mask;
                } else {
                    u.activity_hi[ch - 4] = mask;
                }
            }
        }

        u.meta[0] = static_cast<float>(std::min(nr, 32));
        u.meta[1] = static_cast<float>(nr > 32 ? (cur_row * 32 / std::max(nr, 1)) : cur_row);
        u.meta[2] = static_cast<float>(num_ch_used);

        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "Tracker Thumb Pass");
    }

    ~Tracker() override {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);
    }

    void draw_inspector(VividInspectorContext* ctx) override {
        namespace ti = tracker_insp;
        auto& draw = ctx->draw;
        auto& cmds = ctx->commands;
        const auto& theme = ctx->theme;
        const auto& mouse = ctx->mouse;

        float px = ctx->content_x;
        float py = ctx->content_y;
        float panel_w = ctx->content_width;

        // Read params from context
        int edit_pat = (ctx->param_count > 4) ? std::clamp(static_cast<int>(ctx->param_values[4]), 0, 63) : 0;
        int edit_ch  = (ctx->param_count > 5) ? std::clamp(static_cast<int>(ctx->param_values[5]), 0, 7) : 0;
        int mute_mask = (ctx->param_count > 6) ? std::clamp(static_cast<int>(ctx->param_values[6]), 0, 255) : 0;

        // Parse pattern data from string params
        tracker::TrackerSong disp_song;
        bool has_data = false;
        if (ctx->string_param_count > 0 && ctx->string_param_values && ctx->string_param_values[0]) {
            const char* pd = ctx->string_param_values[0]; // pattern_data is the first (only) string param
            if (pd[0] != '\0')
                has_data = tracker::deserialize_song(std::string(pd), disp_song);
        }

        // Playback state from outputs: row=out[3], pattern=out[4], order=out[5]
        int playback_row = (ctx->output_count > 3) ? static_cast<int>(ctx->output_values[3]) : -1;
        int playback_pat = (ctx->output_count > 4) ? static_cast<int>(ctx->output_values[4]) : -1;

        // --- Process key events ---
        for (uint32_t ki = 0; ki < ctx->key_event_count; ++ki) {
            auto& ke = ctx->key_events[ki];
            if (ke.action != 1 && ke.action != 2) continue; // press or repeat only

            if (insp_editing_) {
                if (ke.key == ti::kKeyEscape) {
                    insp_editing_ = false;
                    insp_edit_buffer_.clear();
                } else if (ke.key == ti::kKeyBackspace) {
                    if (!insp_edit_buffer_.empty())
                        insp_edit_buffer_.pop_back();
                } else if (ke.key == ti::kKeyDelete) {
                    // Delete cell content
                    if (has_data) {
                        int pat_idx = std::clamp(edit_pat, 0, disp_song.num_patterns - 1);
                        auto& cell = disp_song.patterns[pat_idx].cells[edit_ch][insp_cursor_row_];
                        if (insp_cursor_col_ == 0) cell.note = tracker::NOTE_EMPTY;
                        else if (insp_cursor_col_ == 1) cell.velocity = 0;
                        else { cell.effect_type = 0; cell.effect_param = 0; }
                        cmds.set_string_param(cmds.opaque, "pattern_data",
                                              tracker::serialize_song(disp_song).c_str());
                    }
                    insp_editing_ = false;
                    insp_edit_buffer_.clear();
                } else if (ke.key == ti::kKeyUp) {
                    insp_cursor_row_ = std::max(0, insp_cursor_row_ - 1);
                    insp_edit_buffer_.clear();
                    if (insp_cursor_row_ < insp_scroll_row_)
                        insp_scroll_row_ = insp_cursor_row_;
                } else if (ke.key == ti::kKeyDown) {
                    insp_cursor_row_++;
                    insp_edit_buffer_.clear();
                    if (insp_cursor_row_ >= insp_scroll_row_ + ti::kMaxVisRows)
                        insp_scroll_row_ = insp_cursor_row_ - ti::kMaxVisRows + 1;
                } else if (ke.key == ti::kKeyLeft) {
                    insp_cursor_col_ = std::max(0, insp_cursor_col_ - 1);
                    insp_edit_buffer_.clear();
                    insp_edit_max_chars_ = (insp_cursor_col_ == 0) ? 3 : (insp_cursor_col_ == 1) ? 2 : 3;
                } else if (ke.key == ti::kKeyRight) {
                    insp_cursor_col_ = std::min(2, insp_cursor_col_ + 1);
                    insp_edit_buffer_.clear();
                    insp_edit_max_chars_ = (insp_cursor_col_ == 0) ? 3 : (insp_cursor_col_ == 1) ? 2 : 3;
                } else if (ke.key == ti::kKeyEnter) {
                    insp_editing_ = false;
                    insp_edit_buffer_.clear();
                }
            } else {
                // Not editing — arrow key navigation
                if (ke.key == ti::kKeyUp) {
                    insp_cursor_row_ = std::max(0, insp_cursor_row_ - 1);
                    if (insp_cursor_row_ < insp_scroll_row_)
                        insp_scroll_row_ = insp_cursor_row_;
                } else if (ke.key == ti::kKeyDown) {
                    insp_cursor_row_++;
                    if (insp_cursor_row_ >= insp_scroll_row_ + ti::kMaxVisRows)
                        insp_scroll_row_ = insp_cursor_row_ - ti::kMaxVisRows + 1;
                } else if (ke.key == ti::kKeyLeft) {
                    insp_cursor_col_ = std::max(0, insp_cursor_col_ - 1);
                } else if (ke.key == ti::kKeyRight) {
                    insp_cursor_col_ = std::min(2, insp_cursor_col_ + 1);
                }
            }
        }

        // --- Process char events (for cell editing) ---
        for (uint32_t ci = 0; ci < ctx->char_event_count; ++ci) {
            if (!insp_editing_) continue;
            char ch = static_cast<char>(ctx->char_events[ci]);
            if (insp_cursor_col_ == 0) {
                // Note column: accept note letters, #, -, digits, =
                if ((ch >= 'A' && ch <= 'G') || (ch >= 'a' && ch <= 'g') ||
                    ch == '#' || ch == '-' || ch == '=' || (ch >= '0' && ch <= '9')) {
                    if (ch >= 'a' && ch <= 'g') ch = ch - 'a' + 'A';
                    insp_edit_buffer_ += ch;
                }
            } else {
                // Hex column
                if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) {
                    if (ch >= 'a' && ch <= 'f') ch = ch - 'a' + 'A';
                    insp_edit_buffer_ += ch;
                }
            }

            // Commit when buffer is full
            if (static_cast<int>(insp_edit_buffer_.size()) >= insp_edit_max_chars_) {
                if (has_data) {
                    int pat_idx = std::clamp(edit_pat, 0, disp_song.num_patterns - 1);
                    auto& cell = disp_song.patterns[pat_idx].cells[edit_ch][insp_cursor_row_];
                    if (insp_cursor_col_ == 0) {
                        if (insp_edit_buffer_[0] == '=')
                            cell.note = tracker::NOTE_OFF;
                        else
                            cell.note = ti::parse_note_str(insp_edit_buffer_.c_str());
                    } else if (insp_cursor_col_ == 1) {
                        cell.velocity = ti::parse_hex2_str(insp_edit_buffer_.c_str());
                    } else {
                        if (insp_edit_buffer_.size() >= 3) {
                            char type_buf[3] = {'0', insp_edit_buffer_[0], 0};
                            cell.effect_type = ti::parse_hex2_str(type_buf);
                            char param_buf[3] = {insp_edit_buffer_[1], insp_edit_buffer_[2], 0};
                            cell.effect_param = ti::parse_hex2_str(param_buf);
                        }
                    }
                    cmds.set_string_param(cmds.opaque, "pattern_data",
                                          tracker::serialize_song(disp_song).c_str());
                }
                insp_edit_buffer_.clear();
                insp_editing_ = false;
                insp_cursor_row_++;
                if (insp_cursor_row_ >= insp_scroll_row_ + ti::kMaxVisRows)
                    insp_scroll_row_ = insp_cursor_row_ - ti::kMaxVisRows + 1;
            }
        }

        py += 4;
        float start_y = py;

        // --- Tab bar: Pattern | Song | Settings ---
        static const char* kTabLabels[] = {"Pattern", "Song", "Settings"};
        float tab_h = 18.0f;

        for (int t = 0; t < 3; ++t) {
            float tx = px + t * ti::kTabW;
            bool active = (insp_tab_ == t);
            if (active) {
                draw.draw_rect(draw.opaque, tx, py, ti::kTabW, tab_h, theme.dark_bg);
                VividColor accent_full = theme.accent; accent_full.a = 1.0f;
                draw.draw_rect(draw.opaque, tx, py + tab_h - 2, ti::kTabW, 2, accent_full);
            }
            VividColor tab_text = theme.dim_text;
            if (active) { tab_text.r *= 1.5f; tab_text.g *= 1.5f; tab_text.b *= 1.5f; }
            tab_text.a = active ? 1.0f : 0.5f;
            draw.draw_text(draw.opaque, tx + 8, py + 3, kTabLabels[t], tab_text, 1.0f);

            // Tab click detection
            if (mouse.left_clicked && ti::hit_test(mouse.x + px, mouse.y + py - (py - start_y + 4),
                                                    tx, py, ti::kTabW, tab_h)) {
                // Use absolute mouse coords for hit testing
            }
        }
        // Tab click detection (using absolute mouse coords)
        if (mouse.left_clicked) {
            float abs_mx = mouse.x + px;
            float abs_my = mouse.y + ctx->content_y;
            for (int t = 0; t < 3; ++t) {
                float tx = px + t * ti::kTabW;
                if (ti::hit_test(abs_mx, abs_my, tx, py, ti::kTabW, tab_h)) {
                    insp_tab_ = t;
                    insp_editing_ = false;
                }
            }
        }
        py += tab_h + 2;

        if (insp_tab_ == 0) {
            // ===================== PATTERN TAB =====================

            // Channel selector: 8 small tabs
            float ch_y = py;
            if (mouse.left_clicked) {
                float abs_mx = mouse.x + px;
                float abs_my = mouse.y + ctx->content_y;
                for (int c = 0; c < tracker::MAX_CHANNELS; ++c) {
                    float cx = px + c * ti::kChTabW;
                    if (ti::hit_test(abs_mx, abs_my, cx, ch_y, ti::kChTabW, ti::kChTabH)) {
                        cmds.set_param(cmds.opaque, "edit_channel", static_cast<float>(c));
                    }
                }
            }

            for (int c = 0; c < tracker::MAX_CHANNELS; ++c) {
                float cx = px + c * ti::kChTabW;
                bool active = (edit_ch == c);
                bool muted = (mute_mask >> c) & 1;

                if (active) {
                    draw.draw_rect(draw.opaque, cx, ch_y, ti::kChTabW, ti::kChTabH, theme.dark_bg);
                    VividColor ch_color = {ti::kChColors[c][0], ti::kChColors[c][1], ti::kChColors[c][2], 1.0f};
                    draw.draw_rect(draw.opaque, cx, ch_y + ti::kChTabH - 2, ti::kChTabW, 2, ch_color);
                }

                char label[4];
                std::snprintf(label, sizeof(label), "%d", c + 1);
                float alpha = muted ? 0.3f : (active ? 1.0f : 0.5f);
                VividColor ch_text = {ti::kChColors[c][0], ti::kChColors[c][1], ti::kChColors[c][2], alpha};
                draw.draw_text(draw.opaque, cx + 8, ch_y + 3, label, ch_text, 1.0f);
            }
            py += ti::kChTabH + 2;

            // Pattern index label
            {
                char pat_label[16];
                std::snprintf(pat_label, sizeof(pat_label), "P%02d", edit_pat);
                VividColor dim07 = theme.dim_text; dim07.a = 0.7f;
                draw.draw_text(draw.opaque, px, py, pat_label, dim07, 1.0f);
                py += ti::kLineH;
            }

            // --- Text grid ---
            if (!has_data) {
                VividColor dim05 = theme.dim_text; dim05.a = 0.5f;
                draw.draw_text(draw.opaque, px, py, "(no pattern data)", dim05, 1.0f);
                py += ti::kLineH;
                ctx->consumed_height = py - ctx->content_y;
                ctx->wants_keyboard = insp_editing_ ? 1 : 0;
                return;
            }

            int pat_idx = std::clamp(edit_pat, 0, disp_song.num_patterns - 1);
            const auto& pat = disp_song.patterns[pat_idx];
            int num_rows = pat.num_rows;
            if (num_rows <= 0) {
                py += 4;
                ctx->consumed_height = py - ctx->content_y;
                ctx->wants_keyboard = insp_editing_ ? 1 : 0;
                return;
            }

            // Clamp cursor
            insp_cursor_row_ = std::clamp(insp_cursor_row_, 0, num_rows - 1);

            int visible_rows = std::min(num_rows, ti::kMaxVisRows);
            insp_scroll_row_ = std::clamp(insp_scroll_row_, 0, std::max(0, num_rows - visible_rows));

            float grid_h = visible_rows * ti::kRowH;
            VividColor dark09 = theme.dark_bg; dark09.a = 0.9f;
            draw.draw_rect(draw.opaque, px, py, panel_w, grid_h + 2, dark09);

            // Column headers
            float col_x = px + ti::kRowNumW;
            VividColor dim04 = theme.dim_text; dim04.a = 0.4f;
            draw.draw_text(draw.opaque, px + 2, py, "#", dim04, 1.0f);
            draw.draw_text(draw.opaque, col_x + 2, py, "Not", dim04, 1.0f);
            draw.draw_text(draw.opaque, col_x + ti::kNoteW + 2, py, "Vl", dim04, 1.0f);
            draw.draw_text(draw.opaque, col_x + ti::kNoteW + ti::kVelW + 2, py, "Fx", dim04, 1.0f);
            py += ti::kRowH;

            bool playback_on_this_pat = (playback_pat == pat_idx);

            // Cell click detection
            if (mouse.left_clicked) {
                float abs_mx = mouse.x + px;
                float abs_my = mouse.y + ctx->content_y;
                for (int vi = 0; vi < visible_rows; ++vi) {
                    int row = insp_scroll_row_ + vi;
                    if (row >= num_rows) break;
                    float ry = py + vi * ti::kRowH;
                    float nx = px + ti::kRowNumW;
                    // Check note column
                    if (ti::hit_test(abs_mx, abs_my, nx, ry, ti::kNoteW, ti::kRowH)) {
                        insp_cursor_row_ = row; insp_cursor_col_ = 0;
                        insp_editing_ = true; insp_edit_buffer_.clear();
                        insp_edit_max_chars_ = 3;
                    }
                    // Check vel column
                    float vx = nx + ti::kNoteW;
                    if (ti::hit_test(abs_mx, abs_my, vx, ry, ti::kVelW, ti::kRowH)) {
                        insp_cursor_row_ = row; insp_cursor_col_ = 1;
                        insp_editing_ = true; insp_edit_buffer_.clear();
                        insp_edit_max_chars_ = 2;
                    }
                    // Check fx column
                    float ex = vx + ti::kVelW;
                    if (ti::hit_test(abs_mx, abs_my, ex, ry, ti::kFxW, ti::kRowH)) {
                        insp_cursor_row_ = row; insp_cursor_col_ = 2;
                        insp_editing_ = true; insp_edit_buffer_.clear();
                        insp_edit_max_chars_ = 3;
                    }
                }
            }

            // Draw rows
            for (int vi = 0; vi < visible_rows; ++vi) {
                int row = insp_scroll_row_ + vi;
                if (row >= num_rows) break;
                float ry = py + vi * ti::kRowH;
                const auto& cell = pat.cells[edit_ch][row];

                // Playback row highlight
                if (playback_on_this_pat && row == playback_row) {
                    VividColor hl = theme.accent; hl.a = 0.15f;
                    draw.draw_rect(draw.opaque, px, ry, panel_w, ti::kRowH, hl);
                }

                // Beat separator
                if (row > 0 && (row % 4) == 0) {
                    VividColor sep = theme.separator; sep.a = 0.3f;
                    draw.draw_rect(draw.opaque, px, ry, panel_w, 1, sep);
                }

                // Row number
                char row_str[4];
                std::snprintf(row_str, sizeof(row_str), "%02X", row);
                VividColor dim_row = theme.dim_text;
                dim_row.a = ((row % 4) == 0) ? 0.7f : 0.35f;
                draw.draw_text(draw.opaque, px + 2, ry + 1, row_str, dim_row, 1.0f);

                // Note column
                float nx = px + ti::kRowNumW;
                char note_str[4];
                ti::format_note(cell.note, note_str);
                VividColor note_clr = {ti::kChColors[edit_ch][0], ti::kChColors[edit_ch][1], ti::kChColors[edit_ch][2],
                                       (cell.note == tracker::NOTE_EMPTY) ? 0.2f : 0.9f};
                if (cell.note == tracker::NOTE_OFF) { note_clr.r = 0.7f; note_clr.g = 0.3f; note_clr.b = 0.3f; }

                if (insp_cursor_row_ == row && insp_cursor_col_ == 0) {
                    VividColor cursor_bg = {0.3f, 0.4f, 0.6f, 0.3f};
                    draw.draw_rect(draw.opaque, nx, ry, ti::kNoteW, ti::kRowH, cursor_bg);
                }
                draw.draw_text(draw.opaque, nx + 2, ry + 1, note_str, note_clr, 1.0f);

                // Velocity column
                float vx = nx + ti::kNoteW;
                char vel_str[3];
                ti::format_hex2(cell.velocity, vel_str);
                VividColor vel_clr = theme.bright_text;
                vel_clr.a = (cell.velocity == 0) ? 0.2f : 0.7f;
                if (insp_cursor_row_ == row && insp_cursor_col_ == 1) {
                    VividColor cursor_bg = {0.3f, 0.4f, 0.6f, 0.3f};
                    draw.draw_rect(draw.opaque, vx, ry, ti::kVelW, ti::kRowH, cursor_bg);
                }
                draw.draw_text(draw.opaque, vx + 2, ry + 1, vel_str, vel_clr, 1.0f);

                // Effect column
                float ex = vx + ti::kVelW;
                char fx_str[4];
                ti::format_hex3(cell.effect_type, cell.effect_param, fx_str);
                VividColor fx_clr = theme.bright_text;
                fx_clr.a = (cell.effect_type == 0 && cell.effect_param == 0) ? 0.2f : 0.7f;
                if (insp_cursor_row_ == row && insp_cursor_col_ == 2) {
                    VividColor cursor_bg = {0.3f, 0.4f, 0.6f, 0.3f};
                    draw.draw_rect(draw.opaque, ex, ry, ti::kFxW, ti::kRowH, cursor_bg);
                }
                draw.draw_text(draw.opaque, ex + 2, ry + 1, fx_str, fx_clr, 1.0f);
            }

            py += grid_h + 4;

            // Scroll indicator
            if (num_rows > ti::kMaxVisRows) {
                float scroll_frac = static_cast<float>(insp_scroll_row_) /
                                    static_cast<float>(num_rows - visible_rows);
                float bar_h = 4.0f;
                float bar_w = panel_w * static_cast<float>(visible_rows) / static_cast<float>(num_rows);
                float bar_x = px + scroll_frac * (panel_w - bar_w);
                VividColor accent05 = theme.accent; accent05.a = 0.5f;
                draw.draw_rect(draw.opaque, bar_x, py, bar_w, bar_h, accent05);
                py += bar_h + 2;
            }

            // Edit indicator
            if (insp_editing_) {
                char edit_label[32];
                std::snprintf(edit_label, sizeof(edit_label), "Edit: %s", insp_edit_buffer_.c_str());
                VividColor accent08 = theme.accent; accent08.a = 0.8f;
                draw.draw_text(draw.opaque, px, py, edit_label, accent08, 1.0f);
                py += ti::kLineH;
            }

        } else if (insp_tab_ == 1) {
            // ===================== SONG TAB =====================

            if (!has_data) {
                VividColor dim05 = theme.dim_text; dim05.a = 0.5f;
                draw.draw_text(draw.opaque, px, py, "(no pattern data)", dim05, 1.0f);
                py += ti::kLineH;
                ctx->consumed_height = py - ctx->content_y;
                ctx->wants_keyboard = insp_editing_ ? 1 : 0;
                return;
            }

            VividColor dim07 = theme.dim_text; dim07.a = 0.7f;
            draw.draw_text(draw.opaque, px, py, "Arrangement", dim07, 1.0f);
            py += ti::kLineH;

            int current_order = (ctx->output_count > 5) ? static_cast<int>(ctx->output_values[5]) : -1;

            float item_h = ti::kLineH;
            float list_h = std::min(static_cast<int>(disp_song.arrangement_length), 16) * item_h;
            VividColor dark09 = theme.dark_bg; dark09.a = 0.9f;
            draw.draw_rect(draw.opaque, px, py, panel_w, list_h + 2, dark09);

            // Arrangement entry click detection
            if (mouse.left_clicked) {
                float abs_mx = mouse.x + px;
                float abs_my = mouse.y + ctx->content_y;
                for (int i = 0; i < disp_song.arrangement_length && i < 16; ++i) {
                    float iy = py + i * item_h;
                    if (ti::hit_test(abs_mx, abs_my, px, iy, panel_w, item_h)) {
                        // Cycle pattern index for this entry
                        int cur = disp_song.arrangement[i];
                        int next = (cur + 1) % disp_song.num_patterns;
                        disp_song.arrangement[i] = static_cast<uint8_t>(next);
                        cmds.set_string_param(cmds.opaque, "pattern_data",
                                              tracker::serialize_song(disp_song).c_str());
                    }
                }
            }

            for (int i = 0; i < disp_song.arrangement_length && i < 16; ++i) {
                float iy = py + i * item_h;
                if (i == current_order) {
                    VividColor hl = theme.accent; hl.a = 0.15f;
                    draw.draw_rect(draw.opaque, px, iy, panel_w, item_h, hl);
                }
                char entry[16];
                std::snprintf(entry, sizeof(entry), "%02d: P%02d", i, disp_song.arrangement[i]);
                VividColor text_clr = theme.bright_text;
                text_clr.a = (i == current_order) ? 1.0f : 0.6f;
                draw.draw_text(draw.opaque, px + 4, iy + 1, entry, text_clr, 1.0f);
            }
            py += list_h + 4;

            // Add / Remove buttons
            float btn_w = 60.0f;
            float btn_h = 18.0f;
            float btn_gap = 4.0f;

            draw.draw_rect(draw.opaque, px, py, btn_w, btn_h, theme.slider_track);
            VividColor dim08 = theme.dim_text; dim08.a = 0.8f;
            draw.draw_text(draw.opaque, px + 8, py + 2, "+ Add", dim08, 1.0f);

            draw.draw_rect(draw.opaque, px + btn_w + btn_gap, py, btn_w, btn_h, theme.slider_track);
            draw.draw_text(draw.opaque, px + btn_w + btn_gap + 8, py + 2, "- Remove", dim08, 1.0f);

            // Button click detection
            if (mouse.left_clicked) {
                float abs_mx = mouse.x + px;
                float abs_my = mouse.y + ctx->content_y;
                if (ti::hit_test(abs_mx, abs_my, px, py, btn_w, btn_h)) {
                    // Add entry
                    if (disp_song.arrangement_length < tracker::MAX_ARRANGEMENT) {
                        disp_song.arrangement[disp_song.arrangement_length] = 0;
                        disp_song.arrangement_length++;
                        cmds.set_string_param(cmds.opaque, "pattern_data",
                                              tracker::serialize_song(disp_song).c_str());
                    }
                }
                if (ti::hit_test(abs_mx, abs_my, px + btn_w + btn_gap, py, btn_w, btn_h)) {
                    // Remove last entry
                    if (disp_song.arrangement_length > 1) {
                        disp_song.arrangement_length--;
                        cmds.set_string_param(cmds.opaque, "pattern_data",
                                              tracker::serialize_song(disp_song).c_str());
                    }
                }
            }

            py += btn_h + 4;

        } else {
            // ===================== SETTINGS TAB =====================
            VividColor dim04 = theme.dim_text; dim04.a = 0.4f;
            draw.draw_text(draw.opaque, px, py, "See params above", dim04, 1.0f);
            py += ti::kLineH;
        }

        ctx->consumed_height = py - ctx->content_y;
        ctx->wants_keyboard = insp_editing_ ? 1 : 0;
    }

private:
    static constexpr float kMultipliers[] = {
        0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1.5f, 3.0f, 6.0f
    };

    tracker::TrackerSong song_;
    tracker::ChannelState channels_[tracker::MAX_CHANNELS] = {};
    std::size_t prev_data_hash_ = 0;

    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
    int prev_global_tick_ = 0;
    bool prev_reset_ = false;

    int current_order_ = 0;
    int current_row_ = 0;
    int current_tick_ = 0;
    int ticks_per_row_ = 6;

    VividMidiBuffer midi_buf_ = {};

    // Inspector UI state (persisted across frames)
    int insp_tab_ = 0;             // 0=Pattern, 1=Song, 2=Settings
    int insp_cursor_row_ = 0;
    int insp_cursor_col_ = 0;      // 0=note, 1=vel, 2=fx
    int insp_scroll_row_ = 0;
    bool insp_editing_ = false;
    std::string insp_edit_buffer_;
    int insp_edit_max_chars_ = 3;

    void sync_pattern_data() {
        std::size_t h = std::hash<std::string>{}(pattern_data.str_value);
        if (h != prev_data_hash_) {
            prev_data_hash_ = h;
            if (!pattern_data.str_value.empty()) {
                tracker::deserialize_song(pattern_data.str_value, song_);
            }
        }
    }

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
    activity_lo: vec4u,
    activity_hi: vec4u,
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

fn get_activity(ch: i32, row: i32) -> bool {
    var mask: u32;
    if (ch < 4) {
        mask = uniforms.activity_lo[ch];
    } else {
        mask = uniforms.activity_hi[ch - 4];
    }
    return (mask & (1u << u32(row))) != 0u;
}

fn ch_color(ch: i32) -> vec3f {
    switch(ch) {
        case 0: { return vec3f(100.0, 160.0, 220.0); }
        case 1: { return vec3f(220.0, 120.0, 80.0); }
        case 2: { return vec3f(80.0, 200.0, 140.0); }
        case 3: { return vec3f(200.0, 180.0, 60.0); }
        case 4: { return vec3f(160.0, 100.0, 200.0); }
        case 5: { return vec3f(60.0, 180.0, 200.0); }
        case 6: { return vec3f(200.0, 100.0, 160.0); }
        case 7: { return vec3f(140.0, 200.0, 80.0); }
        default: { return vec3f(100.0, 100.0, 100.0); }
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);

    let num_rows = i32(uniforms.info.x);
    let cur_row = i32(uniforms.info.y);
    let num_ch = max(i32(uniforms.info.z), 1);

    if (num_rows <= 0) { return bg; }

    let col_f = uv.x * f32(num_ch);
    let row_f = uv.y * f32(num_rows);
    let col = i32(floor(col_f));
    let row = i32(floor(row_f));

    if (col >= num_ch || row >= num_rows) { return bg; }

    var result = bg;

    // Current row highlight
    if (row == cur_row) {
        result = vec4f(
            min(1.0, bg.r + 30.0/255.0),
            min(1.0, bg.g + 35.0/255.0),
            min(1.0, bg.b + 40.0/255.0),
            bg.a
        );
    }

    // Active cell
    let cell_x = fract(col_f);
    let cell_y = fract(row_f);
    let in_cell = cell_x > 0.05 && cell_x < 0.95 && cell_y > 0.05 && cell_y < 0.95;

    if (in_cell && get_activity(col, row)) {
        let c = ch_color(col);
        var alpha = 180.0;
        if (row == cur_row) { alpha = 255.0; }
        result = vec4f(c / 255.0, alpha / 255.0);
    }

    return result;
}
)";

        static constexpr uint64_t kUniformSize = sizeof(float) * 4 + sizeof(uint32_t) * 8;
        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Tracker Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, kUniformSize, "Tracker Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, kUniformSize, "Tracker Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "Tracker Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, kUniformSize, "Tracker Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "Tracker Thumb Pipeline");
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

    int get_pattern_index() const {
        if (current_order_ >= song_.arrangement_length) return 0;
        int idx = song_.arrangement[current_order_];
        if (idx >= song_.num_patterns) idx = 0;
        return idx;
    }

    void process_tick(int spd, int base_ch, int ch_mode, int mute) {
        ticks_per_row_ = spd;

        int pat_idx = get_pattern_index();
        const auto& pat = song_.patterns[pat_idx];

        if (current_tick_ == 0) {
            // New row: read cells and init effects
            process_new_row(pat, base_ch, ch_mode, mute);
        } else {
            // Per-tick effect processing
            process_tick_effects(pat, base_ch, ch_mode, mute);
        }

        current_tick_++;
        if (current_tick_ >= ticks_per_row_) {
            current_tick_ = 0;
            current_row_++;

            if (current_row_ >= pat.num_rows) {
                advance_order();
            }
        }
    }

    void advance_order() {
        current_row_ = 0;
        current_order_++;
        if (current_order_ >= song_.arrangement_length) {
            current_order_ = 0;
        }
    }

    uint8_t midi_channel_for(int ch, int base_ch, int ch_mode) const {
        if (ch_mode == 0) return static_cast<uint8_t>(base_ch);
        return static_cast<uint8_t>((base_ch + ch) % 16);
    }

    void process_new_row(const tracker::TrackerPattern& pat, int base_ch, int ch_mode, int mute) {
        for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
            auto& cs = channels_[ch];
            bool muted = (mute >> ch) & 1;
            uint8_t midi_ch = midi_channel_for(ch, base_ch, ch_mode);

            if (current_row_ >= pat.num_rows) continue;
            const auto& cell = pat.cells[ch][current_row_];

            // Reset per-row effect state
            cs.note_delay_ticks = 0;
            cs.note_cut_ticks = -1;
            cs.retrigger_period = 0;
            cs.arpeggio_x = 0;
            cs.arpeggio_y = 0;
            cs.volume_slide_delta = 0;

            // Process effect initialization
            uint8_t fx = cell.effect_type;
            uint8_t fp = cell.effect_param;
            uint8_t fx_hi = (fp >> 4) & 0x0F;
            uint8_t fx_lo = fp & 0x0F;

            // Handle extended effects
            if (fx == tracker::FX_EXTENDED) {
                switch (fx_hi) {
                    case tracker::FX_EXT_RETRIGGER:
                        cs.retrigger_period = fx_lo;
                        cs.retrigger_counter = 0;
                        break;
                    case tracker::FX_EXT_NOTE_CUT:
                        cs.note_cut_ticks = fx_lo;
                        break;
                    case tracker::FX_EXT_NOTE_DELAY:
                        cs.note_delay_ticks = fx_lo;
                        break;
                }
            } else {
                switch (fx) {
                    case tracker::FX_ARPEGGIO:
                        if (fp != 0) {
                            cs.arpeggio_x = fx_hi;
                            cs.arpeggio_y = fx_lo;
                            cs.arpeggio_tick = 0;
                        }
                        break;
                    case tracker::FX_PORTA_UP:
                        cs.porta_speed = static_cast<float>(fp);
                        break;
                    case tracker::FX_PORTA_DOWN:
                        cs.porta_speed = -static_cast<float>(fp);
                        break;
                    case tracker::FX_TONE_PORTA:
                        if (cell.note > 0 && cell.note < 128)
                            cs.target_pitch = static_cast<float>(cell.note);
                        cs.porta_speed = static_cast<float>(fp);
                        break;
                    case tracker::FX_VIBRATO:
                        cs.vibrato_speed = static_cast<float>(fx_hi);
                        cs.vibrato_depth = static_cast<float>(fx_lo) * 0.25f;
                        break;
                    case tracker::FX_VOL_SLIDE:
                        if (fx_hi > 0)
                            cs.volume_slide_delta = static_cast<float>(fx_hi) / 64.0f;
                        else
                            cs.volume_slide_delta = -static_cast<float>(fx_lo) / 64.0f;
                        break;
                    case tracker::FX_SET_VOLUME:
                        cs.volume = static_cast<float>(fp) / 64.0f;
                        break;
                    case tracker::FX_PATTERN_BREAK: {
                        int target_row = fx_hi * 10 + fx_lo;
                        current_row_ = pat.num_rows;  // force advance
                        advance_order();
                        int new_pat_idx = get_pattern_index();
                        current_row_ = std::min(target_row,
                            static_cast<int>(song_.patterns[new_pat_idx].num_rows) - 1);
                        return;  // skip rest of row processing
                    }
                    case tracker::FX_SET_SPEED:
                        if (fp > 0 && fp < 32)
                            ticks_per_row_ = fp;
                        break;
                }
            }

            // Handle note (skip if note delay is active)
            if (cs.note_delay_ticks > 0) continue;

            if (cell.note == tracker::NOTE_OFF) {
                // Note off
                if (cs.gate_active && !muted && cs.prev_midi_note >= 0) {
                    vivid_sequencers::midi_note_off(midi_buf_,
                        static_cast<uint8_t>(cs.prev_midi_note), midi_ch);
                }
                cs.gate_active = false;
            } else if (cell.note > 0 && cell.note <= 127 && fx != tracker::FX_TONE_PORTA) {
                // Note on (tone porta handles note differently)
                if (cs.gate_active && !muted && cs.prev_midi_note >= 0) {
                    vivid_sequencers::midi_note_off(midi_buf_,
                        static_cast<uint8_t>(cs.prev_midi_note), midi_ch);
                }
                cs.current_pitch = static_cast<float>(cell.note);
                cs.last_note = cell.note;
                if (cell.velocity > 0)
                    cs.current_velocity = cell.velocity;
                cs.gate_active = true;
                cs.prev_midi_note = cell.note;
                cs.vibrato_phase = 0;

                if (!muted) {
                    uint8_t vel = static_cast<uint8_t>(
                        std::clamp(static_cast<int>(cs.current_velocity * cs.volume), 1, 127));
                    vivid_sequencers::midi_note_on(midi_buf_, cell.note, vel, midi_ch);
                }
            }
        }
    }

    void process_tick_effects(const tracker::TrackerPattern& pat, int base_ch, int ch_mode, int mute) {
        for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
            auto& cs = channels_[ch];
            bool muted = (mute >> ch) & 1;
            uint8_t midi_ch = midi_channel_for(ch, base_ch, ch_mode);

            // Note delay: trigger note at specified tick
            if (cs.note_delay_ticks > 0 && current_tick_ == cs.note_delay_ticks) {
                if (current_row_ < pat.num_rows) {
                    const auto& cell = pat.cells[ch][current_row_];
                    if (cell.note > 0 && cell.note <= 127) {
                        if (cs.gate_active && !muted && cs.prev_midi_note >= 0) {
                            vivid_sequencers::midi_note_off(midi_buf_,
                                static_cast<uint8_t>(cs.prev_midi_note), midi_ch);
                        }
                        cs.current_pitch = static_cast<float>(cell.note);
                        cs.last_note = cell.note;
                        if (cell.velocity > 0)
                            cs.current_velocity = cell.velocity;
                        cs.gate_active = true;
                        cs.prev_midi_note = cell.note;
                        if (!muted) {
                            uint8_t vel = static_cast<uint8_t>(
                                std::clamp(static_cast<int>(cs.current_velocity * cs.volume), 1, 127));
                            vivid_sequencers::midi_note_on(midi_buf_, cell.note, vel, midi_ch);
                        }
                    }
                }
            }

            // Note cut
            if (cs.note_cut_ticks >= 0 && current_tick_ == cs.note_cut_ticks) {
                if (cs.gate_active && !muted && cs.prev_midi_note >= 0) {
                    vivid_sequencers::midi_note_off(midi_buf_,
                        static_cast<uint8_t>(cs.prev_midi_note), midi_ch);
                }
                cs.gate_active = false;
            }

            // Portamento up/down
            if (cs.porta_speed != 0 &&
                pat.cells[ch][current_row_].effect_type != tracker::FX_TONE_PORTA) {
                cs.current_pitch += cs.porta_speed * 0.25f;
                cs.current_pitch = std::clamp(cs.current_pitch, 0.0f, 127.0f);
            }

            // Tone portamento
            if (pat.cells[ch][current_row_].effect_type == tracker::FX_TONE_PORTA && cs.porta_speed > 0) {
                if (cs.current_pitch < cs.target_pitch) {
                    cs.current_pitch += cs.porta_speed * 0.25f;
                    if (cs.current_pitch > cs.target_pitch)
                        cs.current_pitch = cs.target_pitch;
                } else if (cs.current_pitch > cs.target_pitch) {
                    cs.current_pitch -= cs.porta_speed * 0.25f;
                    if (cs.current_pitch < cs.target_pitch)
                        cs.current_pitch = cs.target_pitch;
                }
            }

            // Vibrato
            if (cs.vibrato_speed > 0 && cs.vibrato_depth > 0) {
                cs.vibrato_phase += cs.vibrato_speed * 0.1f;
                // Vibrato modifies pitch output but not current_pitch
            }

            // Volume slide
            if (cs.volume_slide_delta != 0) {
                cs.volume += cs.volume_slide_delta;
                cs.volume = std::clamp(cs.volume, 0.0f, 1.0f);
            }

            // Arpeggio
            if (cs.arpeggio_x > 0 || cs.arpeggio_y > 0) {
                cs.arpeggio_tick++;
            }

            // Retrigger
            if (cs.retrigger_period > 0 && cs.gate_active) {
                cs.retrigger_counter++;
                if (cs.retrigger_counter >= cs.retrigger_period) {
                    cs.retrigger_counter = 0;
                    if (!muted && cs.prev_midi_note >= 0) {
                        vivid_sequencers::midi_note_off(midi_buf_,
                            static_cast<uint8_t>(cs.prev_midi_note), midi_ch);
                        uint8_t vel = static_cast<uint8_t>(
                            std::clamp(static_cast<int>(cs.current_velocity * cs.volume), 1, 127));
                        vivid_sequencers::midi_note_on(midi_buf_,
                            static_cast<uint8_t>(cs.prev_midi_note), vel, midi_ch);
                    }
                }
            }
        }
    }
};

VIVID_REGISTER(Tracker)
VIVID_THUMBNAIL(Tracker)
VIVID_INSPECTOR(Tracker)
