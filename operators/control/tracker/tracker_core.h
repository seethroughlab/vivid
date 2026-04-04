#pragma once
#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/thumbnail.h"
#include "operator_api/type_id.h"
#include "midi_helpers.h"
#include "tracker_data.h"
#include <string>

/**
 * @brief Multi-channel tracker-style pattern sequencer with keyboard editing.
 *
 * Classic tracker interface with per-channel note, velocity, and effect
 * columns. Supports multiple tracks with independent patterns, edited via
 * a custom keyboard-driven inspector. Outputs per-track MIDI.
 *
 * @see Sequencer, DrumSequencer, NotePattern
 */
struct TrackerCore : vivid::OperatorBase {
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

    TrackerCore();

    void collect_params(std::vector<vivid::ParamBase*>& out) override;
    void collect_ports(std::vector<VividPortDescriptor>& out) override;
    void compute(const float* input_values, const float* params,
                 VividLanePort* out_spreads, float* output_values,
                 void** custom_outputs, uint32_t custom_output_count);
    void draw_thumbnail(const VividThumbnailContext* ctx) override;
    void draw_inspector(VividInspectorContext* ctx) override;

protected:
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

    void sync_pattern_data();
    int get_pattern_index() const;
    void process_tick(int spd, int base_ch, int ch_mode, int mute);
    void advance_order();
    uint8_t midi_channel_for(int ch, int base_ch, int ch_mode) const;
    void process_new_row(const tracker::TrackerPattern& pat, int base_ch, int ch_mode, int mute);
    void process_tick_effects(const tracker::TrackerPattern& pat, int base_ch, int ch_mode, int mute);
};
