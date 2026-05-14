#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vivid::midi_file {

struct Event {
    double time_seconds = 0.0;
    double time_beats = 0.0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

struct NoteSpan {
    double start_seconds = 0.0;
    double duration_seconds = 0.0;
    double start_beats = 0.0;
    double duration_beats = 0.0;
    uint8_t channel = 0;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    uint32_t order = 0;
};

struct Sequence {
    std::vector<Event> events;
    std::vector<NoteSpan> note_spans;
    double duration_seconds = 0.0;
    double duration_beats = 0.0;
    std::string error;

    bool ok() const { return error.empty(); }
};

Sequence parse_file(const std::string& path);

} // namespace vivid::midi_file
