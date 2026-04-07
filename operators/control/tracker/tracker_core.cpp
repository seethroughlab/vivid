#include "tracker_core.h"
#include <algorithm>
#include <cmath>
#include <functional>

TrackerCore::TrackerCore() {
    vivid::description(rate, "Step rate relative to the beat clock");
    vivid::description(speed, "Number of rows advanced per beat tick, 1 to 16");
    vivid::description(base_channel, "Starting MIDI channel for track output, 1 to 16");
    vivid::description(channel_mode, "Single sends all tracks on base_channel; Multi assigns one channel per track");
    vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
    vivid::description(edit_pattern, "Index of the pattern currently shown in the editor");
    vivid::description(edit_channel, "Index of the track/channel currently focused in the editor");
    vivid::description(mute_mask, "Bitmask of muted tracks (bit 0 = track 1)");
    vivid::description(pattern_data, "Serialized tracker pattern data");
}

void TrackerCore::collect_params(std::vector<vivid::ParamBase*>& out) {
    out.push_back(&rate);
    out.push_back(&speed);
    out.push_back(&base_channel);
    out.push_back(&channel_mode);
    out.push_back(&clock_source);
    display_hint(edit_pattern, VIVID_DISPLAY_HIDDEN);
    display_hint(edit_channel, VIVID_DISPLAY_HIDDEN);
    display_hint(mute_mask, VIVID_DISPLAY_HIDDEN);
    display_hint(pattern_data, VIVID_DISPLAY_HIDDEN);
    out.push_back(&edit_pattern);
    out.push_back(&edit_channel);
    out.push_back(&mute_mask);
    out.push_back(&pattern_data);
}

void TrackerCore::collect_ports(std::vector<VividPortDescriptor>& out) {
    out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
    out.push_back({"reset", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
    out.push_back({"notes", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    out.push_back({"velocities", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    out.push_back({"gates", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    out.push_back({"row", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    out.push_back({"pattern", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    out.push_back({"order", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
}

void TrackerCore::compute(const float* input_values, const float* params,
                          VividLaneOutput* out_spreads, float* output_values,
                          void** custom_outputs, uint32_t custom_output_count) {
    float beat_phase = input_values[0];
    bool reset_signal = input_values[1] > 0.5f;

    int r = std::clamp(static_cast<int>(params[0]), 0, 8);
    int spd = std::clamp(static_cast<int>(params[1]), 1, 16);
    int base_ch = std::clamp(static_cast<int>(params[2]), 1, 16) - 1;
    int ch_mode = std::clamp(static_cast<int>(params[3]), 0, 1);
    int mute = std::clamp(static_cast<int>(params[7]), 0, 255);

    sync_pattern_data();

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

    float delta = beat_phase - prev_phase_;
    if (delta < -0.5f) beat_count_++;
    prev_phase_ = beat_phase;

    float total_beats = static_cast<float>(beat_count_) + beat_phase;
    float scaled = total_beats * kMultipliers[r];
    int global_tick = static_cast<int>(std::floor(scaled * spd));

    midi_buf_.count = 0;

    if (global_tick != prev_global_tick_) {
        int ticks_to_process = global_tick - prev_global_tick_;
        if (ticks_to_process < 0 || ticks_to_process > 256)
            ticks_to_process = 1;

        for (int t = 0; t < ticks_to_process; ++t) {
            process_tick(spd, base_ch, ch_mode, mute);
        }
    }
    prev_global_tick_ = global_tick;

    if (out_spreads) {
        auto& notes_sp = out_spreads[0];
        auto& vels_sp  = out_spreads[1];
        auto& gates_sp = out_spreads[2];

        uint32_t len = tracker::MAX_CHANNELS;
        float* notes_buf = notes_sp.resize(notes_sp.handle, len);
        float* vels_buf  = vels_sp.resize(vels_sp.handle, len);
        float* gates_buf = gates_sp.resize(gates_sp.handle, len);
        if (notes_buf && vels_buf && gates_buf) {
            for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
                bool muted = (mute >> ch) & 1;
                notes_buf[ch] = channels_[ch].current_pitch;
                vels_buf[ch] = muted ? 0.0f : static_cast<float>(channels_[ch].current_velocity) / 127.0f;
                gates_buf[ch] = (channels_[ch].gate_active && !muted) ? 1.0f : 0.0f;
            }
            notes_sp.commit(notes_sp.handle, len);
            vels_sp.commit(vels_sp.handle, len);
            gates_sp.commit(gates_sp.handle, len);
        }
    }

    int pat_idx = 0;
    if (current_order_ < song_.arrangement_length)
        pat_idx = song_.arrangement[current_order_];
    output_values[0] = static_cast<float>(current_row_);
    output_values[1] = static_cast<float>(pat_idx);
    output_values[2] = static_cast<float>(current_order_);

    if (custom_outputs && custom_output_count > 0) {
        custom_outputs[0] = &midi_buf_;
    }
}

void TrackerCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    const auto& d = ctx->draw;
    void* o = d.opaque;

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

    int cur_row = -1;
    if (ctx->output_count > 3)
        cur_row = static_cast<int>(ctx->output_values[3]);

    tracker::TrackerSong thumb_song;
    bool has_data = false;
    if (pattern_data.str_value.size() > 0)
        has_data = tracker::deserialize_song(pattern_data.str_value, thumb_song);

    int nr = 0;
    int num_ch_used = 1;
    int pat_idx = 0;
    if (has_data) {
        if (ctx->output_count > 4)
            pat_idx = std::clamp(static_cast<int>(ctx->output_values[4]), 0, thumb_song.num_patterns - 1);
        nr = thumb_song.patterns[pat_idx].num_rows;
        for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
            for (int r = 0; r < nr; ++r) {
                if (thumb_song.patterns[pat_idx].cells[ch][r].note != tracker::NOTE_EMPTY) {
                    num_ch_used = std::max(num_ch_used, ch + 1);
                    break;
                }
            }
        }
    }
    if (nr <= 0) nr = 16;

    int mute_mask_param = (ctx->param_count > 7) ? static_cast<int>(ctx->param_values[7]) : 0;

    d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

    static constexpr float kChColors[8][3] = {
        {0.39f, 0.63f, 0.86f}, {0.86f, 0.47f, 0.31f}, {0.31f, 0.78f, 0.55f},
        {0.78f, 0.71f, 0.24f}, {0.63f, 0.39f, 0.78f}, {0.24f, 0.71f, 0.78f},
        {0.78f, 0.39f, 0.63f}, {0.55f, 0.78f, 0.31f},
    };

    float margin = 2.0f;
    float col_w = (w - 2 * margin) / static_cast<float>(num_ch_used);
    float row_h = (h - 2 * margin) / static_cast<float>(std::min(nr, 32));
    int display_rows = std::min(nr, 32);
    float gap = 0.5f;
    int disp_cur_row = (nr > 32) ? (cur_row * 32 / std::max(nr, 1)) : cur_row;

    for (int ch = 0; ch < num_ch_used; ++ch) {
        bool muted = (mute_mask_param & (1 << ch)) != 0;
        float cx = margin + ch * col_w;

        for (int disp_r = 0; disp_r < display_rows; ++disp_r) {
            float ry = margin + disp_r * row_h;
            bool is_current = (disp_r == disp_cur_row);

            if (is_current && ch == 0) {
                d.draw_rect(o, margin, ry, w - 2 * margin, row_h,
                            {0.2f, 0.22f, 0.25f, 0.5f});
            }

            int pat_r = (nr > 32) ? (disp_r * nr / 32) : disp_r;
            bool has_note = has_data && pat_r < nr &&
                            thumb_song.patterns[pat_idx].cells[ch][pat_r].note != tracker::NOTE_EMPTY;

            if (has_note) {
                uint8_t vel = thumb_song.patterns[pat_idx].cells[ch][pat_r].velocity;
                float vel_f = (vel > 0) ? static_cast<float>(vel) / 127.0f : 0.8f;
                float alpha = muted ? 0.15f : (is_current ? 1.0f : 0.3f + vel_f * 0.5f);
                d.draw_rect(o, cx + gap, ry + gap, col_w - 2 * gap, row_h - 2 * gap,
                            {kChColors[ch][0], kChColors[ch][1], kChColors[ch][2], alpha});
            }
        }
    }
}

void TrackerCore::sync_pattern_data() {
    std::size_t h = std::hash<std::string>{}(pattern_data.str_value);
    if (h != prev_data_hash_) {
        prev_data_hash_ = h;
        if (!pattern_data.str_value.empty()) {
            tracker::deserialize_song(pattern_data.str_value, song_);
        }
    }
}

int TrackerCore::get_pattern_index() const {
    if (current_order_ >= song_.arrangement_length) return 0;
    int idx = song_.arrangement[current_order_];
    if (idx >= song_.num_patterns) idx = 0;
    return idx;
}

void TrackerCore::process_tick(int spd, int base_ch, int ch_mode, int mute) {
    ticks_per_row_ = spd;

    int pat_idx = get_pattern_index();
    const auto& pat = song_.patterns[pat_idx];

    if (current_tick_ == 0) {
        process_new_row(pat, base_ch, ch_mode, mute);
    } else {
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

void TrackerCore::advance_order() {
    current_row_ = 0;
    current_order_++;
    if (current_order_ >= song_.arrangement_length) {
        current_order_ = 0;
    }
}

uint8_t TrackerCore::midi_channel_for(int ch, int base_ch, int ch_mode) const {
    if (ch_mode == 0) return static_cast<uint8_t>(base_ch);
    return static_cast<uint8_t>((base_ch + ch) % 16);
}

void TrackerCore::process_new_row(const tracker::TrackerPattern& pat, int base_ch, int ch_mode, int mute) {
    for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
        auto& cs = channels_[ch];
        bool muted = (mute >> ch) & 1;
        uint8_t midi_ch = midi_channel_for(ch, base_ch, ch_mode);

        if (current_row_ >= pat.num_rows) continue;
        const auto& cell = pat.cells[ch][current_row_];

        cs.note_delay_ticks = 0;
        cs.note_cut_ticks = -1;
        cs.retrigger_period = 0;
        cs.arpeggio_x = 0;
        cs.arpeggio_y = 0;
        cs.volume_slide_delta = 0;

        uint8_t fx = cell.effect_type;
        uint8_t fp = cell.effect_param;
        uint8_t fx_hi = (fp >> 4) & 0x0F;
        uint8_t fx_lo = fp & 0x0F;

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
                    current_row_ = pat.num_rows;
                    advance_order();
                    int new_pat_idx = get_pattern_index();
                    current_row_ = std::min(target_row,
                        static_cast<int>(song_.patterns[new_pat_idx].num_rows) - 1);
                    return;
                }
                case tracker::FX_SET_SPEED:
                    if (fp > 0 && fp < 32)
                        ticks_per_row_ = fp;
                    break;
            }
        }

        if (cs.note_delay_ticks > 0) continue;

        if (cell.note == tracker::NOTE_OFF) {
            if (cs.gate_active && !muted && cs.prev_midi_note >= 0) {
                vivid_sequencers::midi_note_off(midi_buf_,
                    static_cast<uint8_t>(cs.prev_midi_note), midi_ch);
            }
            cs.gate_active = false;
        } else if (cell.note > 0 && cell.note <= 127 && fx != tracker::FX_TONE_PORTA) {
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

void TrackerCore::process_tick_effects(const tracker::TrackerPattern& pat, int base_ch, int ch_mode, int mute) {
    for (int ch = 0; ch < tracker::MAX_CHANNELS; ++ch) {
        auto& cs = channels_[ch];
        bool muted = (mute >> ch) & 1;
        uint8_t midi_ch = midi_channel_for(ch, base_ch, ch_mode);

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

        if (cs.note_cut_ticks >= 0 && current_tick_ == cs.note_cut_ticks) {
            if (cs.gate_active && !muted && cs.prev_midi_note >= 0) {
                vivid_sequencers::midi_note_off(midi_buf_,
                    static_cast<uint8_t>(cs.prev_midi_note), midi_ch);
            }
            cs.gate_active = false;
        }

        if (cs.porta_speed != 0 &&
            pat.cells[ch][current_row_].effect_type != tracker::FX_TONE_PORTA) {
            cs.current_pitch += cs.porta_speed * 0.25f;
            cs.current_pitch = std::clamp(cs.current_pitch, 0.0f, 127.0f);
        }

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

        if (cs.vibrato_speed > 0 && cs.vibrato_depth > 0) {
            cs.vibrato_phase += cs.vibrato_speed * 0.1f;
        }

        if (cs.volume_slide_delta != 0) {
            cs.volume += cs.volume_slide_delta;
            cs.volume = std::clamp(cs.volume, 0.0f, 1.0f);
        }

        if (cs.arpeggio_x > 0 || cs.arpeggio_y > 0) {
            cs.arpeggio_tick++;
        }

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
