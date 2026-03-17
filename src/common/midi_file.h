#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vivid::midi_file {

struct Event {
    double time_seconds = 0.0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

struct Sequence {
    std::vector<Event> events;
    double duration_seconds = 0.0;
    std::string error;

    bool ok() const { return error.empty(); }
};

Sequence parse_file(const std::string& path);

} // namespace vivid::midi_file
