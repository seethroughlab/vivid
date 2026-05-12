#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace midi_clip {

struct ParsedNote {
    uint8_t  pitch;           // MIDI note 0..127
    double   start_beat;      // beat position within pattern
    double   duration_beats;  // note duration in beats
    float    velocity;        // 0..1
    float    pitch_bend;      // signed semitones (±12), emitted at note-on
    float    pressure;        // per-note pressure 0..1, emitted at note-on
};

// quantize_grid enum: 0=1/32  1=1/16  2=1/8  3=1/4
inline double grid_cell_beats(int grid_division) {
    switch (grid_division) {
        case 0: return 0.125;
        case 1: return 0.25;
        case 2: return 0.5;
        case 3: return 1.0;
        default: return 0.25;
    }
}

inline double quantize_to_grid(double beat, int grid_division) {
    double cell = grid_cell_beats(grid_division);
    if (cell <= 0.0) return beat;
    return std::floor(beat / cell) * cell;
}

// JSON round-trip.
// Format: [{"p":60,"s":0.0,"d":0.5,"v":0.8,"b":0.0,"pr":0.0}, ...]
// "b" (pitch_bend) and "pr" (pressure) are omitted when zero.
std::string serialize_pattern(const std::vector<ParsedNote>& notes);
bool        parse_pattern(const std::string& json, std::vector<ParsedNote>& out);

} // namespace midi_clip
