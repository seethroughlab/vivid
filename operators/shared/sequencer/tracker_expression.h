#pragma once
#include "tracker_data.h"
#include <algorithm>
#include <cstdint>

// Per-cell expression interpolation helpers (Phase 4).
//
// Tracker patterns carry sparse anchors for pitch_bend / pressure / timbre
// in their TrackerCell. Between anchors, the played-back value is linearly
// interpolated from the previous anchor toward the next anchor, anchored at
// row boundaries. These helpers are pure logic — no audio context, no event
// emission — so they're testable headlessly and reused by both the
// playback path (TrackerCore::process_tick) and the editor display.
//
// Value conventions (matching MidiInput's MPE handling for interop):
//   pitch_bend  raw int16 \xc2\xb132767 \xe2\x86\x92 \xc2\xb148 semitones
//   pressure    raw int16  0..32767 \xe2\x86\x92 0..1 (negatives clamp to 0)
//   timbre      raw int16  0..32767 \xe2\x86\x92 0..1 (negatives clamp to 0)
//
// The "empty" sentinel kExprEmpty (= INT16_MIN) marks "no anchor at this
// row, follow interpolation from neighbouring anchors". Before the first
// anchor in a pattern, the lane is unset (no events emitted; consumer slots
// stay at their default 0).

namespace vivid::tracker_expression {

enum class Lane { PitchBend, Pressure, Timbre };

inline bool is_set(int16_t raw) { return raw != tracker::kExprEmpty; }

// Cell field accessor by lane id.
inline int16_t cell_lane_value(const tracker::TrackerCell& c, Lane lane) {
    switch (lane) {
        case Lane::PitchBend: return c.pitch_bend;
        case Lane::Pressure:  return c.pressure;
        case Lane::Timbre:    return c.timbre;
    }
    return tracker::kExprEmpty;
}

// Convert a raw int16 anchor value to its domain value. Returns 0 for an
// empty anchor (caller should not emit events when the source is empty;
// converters return 0 only as a defensive default).
inline float pitch_bend_to_semis(int16_t raw) {
    if (!is_set(raw)) return 0.0f;
    return static_cast<float>(raw) * (48.0f / 32767.0f);
}
inline float pressure_to_unit(int16_t raw) {
    if (!is_set(raw)) return 0.0f;
    float v = static_cast<float>(raw) * (1.0f / 32767.0f);
    return std::clamp(v, 0.0f, 1.0f);
}
inline float timbre_to_unit(int16_t raw) {
    return pressure_to_unit(raw);  // identical scaling
}

// Walk the channel's column backwards from current_row (inclusive) looking
// for the most recent row whose lane field is set. Returns -1 if no anchor
// at or before current_row.
inline int find_prev_anchor_row(const tracker::TrackerPattern& pat,
                                int channel, Lane lane, int current_row) {
    if (channel < 0 || channel >= tracker::MAX_CHANNELS) return -1;
    int max_r = std::min(current_row, static_cast<int>(pat.num_rows) - 1);
    for (int r = max_r; r >= 0; --r) {
        if (is_set(cell_lane_value(pat.cells[channel][r], lane))) return r;
    }
    return -1;
}

// Walk the channel's column forwards from current_row (exclusive) looking
// for the next row whose lane field is set. Returns -1 if no anchor exists
// after current_row in this pattern.
inline int find_next_anchor_row(const tracker::TrackerPattern& pat,
                                int channel, Lane lane, int current_row) {
    if (channel < 0 || channel >= tracker::MAX_CHANNELS) return -1;
    for (int r = current_row + 1; r < pat.num_rows; ++r) {
        if (is_set(cell_lane_value(pat.cells[channel][r], lane))) return r;
    }
    return -1;
}

// Compute the interpolated raw int16 value for this lane at
// (current_row, current_tick). ticks_per_row must be >= 1.
//
//   * No prev anchor      \xe2\x86\x92 returns kExprEmpty (lane unset; no event)
//   * Prev anchor only    \xe2\x86\x92 returns prev value (held)
//   * Prev + next anchors \xe2\x86\x92 linear interpolation in tick space
//
// Tick space: cur_tick_global = current_row * ticks_per_row + current_tick.
// Row anchors are positioned at row*ticks_per_row (tick 0 of that row).
inline int16_t interpolate(const tracker::TrackerPattern& pat, int channel,
                           Lane lane, int current_row, int current_tick,
                           int ticks_per_row) {
    if (ticks_per_row <= 0) ticks_per_row = 1;
    int prev_row = find_prev_anchor_row(pat, channel, lane, current_row);
    if (prev_row < 0) return tracker::kExprEmpty;
    int16_t prev_val = cell_lane_value(pat.cells[channel][prev_row], lane);

    int next_row = find_next_anchor_row(pat, channel, lane, prev_row);
    if (next_row < 0) return prev_val;  // hold value past last anchor

    int prev_tg = prev_row * ticks_per_row;
    int next_tg = next_row * ticks_per_row;
    int cur_tg  = current_row * ticks_per_row + current_tick;
    if (cur_tg <= prev_tg) return prev_val;
    if (cur_tg >= next_tg) return cell_lane_value(pat.cells[channel][next_row], lane);

    int16_t next_val = cell_lane_value(pat.cells[channel][next_row], lane);
    float alpha = static_cast<float>(cur_tg - prev_tg) /
                  static_cast<float>(next_tg - prev_tg);
    float interp = static_cast<float>(prev_val) +
                   (static_cast<float>(next_val) - static_cast<float>(prev_val)) * alpha;
    int rounded = static_cast<int>(interp + (interp >= 0.0f ? 0.5f : -0.5f));
    return static_cast<int16_t>(std::clamp(rounded, -32767, 32767));
}

inline bool lane_visible(const tracker::TrackerPattern& pat, Lane lane) {
    switch (lane) {
        case Lane::PitchBend: return (pat.expression_lane_mask & tracker::kLanePb) != 0;
        case Lane::Pressure:  return (pat.expression_lane_mask & tracker::kLanePr) != 0;
        case Lane::Timbre:    return (pat.expression_lane_mask & tracker::kLaneTb) != 0;
    }
    return false;
}

}  // namespace vivid::tracker_expression
