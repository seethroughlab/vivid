#include "common/midi_file.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace vivid::midi_file {

namespace {

struct RawEvent {
    uint64_t tick = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    bool is_tempo = false;
    uint32_t tempo_us_per_qn = 500000;
};

struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;

    bool remaining(size_t n) const { return pos + n <= size; }

    uint8_t read_u8(bool& ok) {
        if (!remaining(1)) {
            ok = false;
            return 0;
        }
        return data[pos++];
    }

    uint16_t read_be16(bool& ok) {
        if (!remaining(2)) {
            ok = false;
            return 0;
        }
        uint16_t v = (static_cast<uint16_t>(data[pos]) << 8) |
                     static_cast<uint16_t>(data[pos + 1]);
        pos += 2;
        return v;
    }

    uint32_t read_be32(bool& ok) {
        if (!remaining(4)) {
            ok = false;
            return 0;
        }
        uint32_t v = (static_cast<uint32_t>(data[pos]) << 24) |
                     (static_cast<uint32_t>(data[pos + 1]) << 16) |
                     (static_cast<uint32_t>(data[pos + 2]) << 8) |
                     static_cast<uint32_t>(data[pos + 3]);
        pos += 4;
        return v;
    }

    uint32_t read_varlen(bool& ok) {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t byte = read_u8(ok);
            if (!ok) return 0;
            value = (value << 7) | static_cast<uint32_t>(byte & 0x7Fu);
            if ((byte & 0x80u) == 0) return value;
        }
        ok = false;
        return 0;
    }
};

bool read_file_bytes(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "failed to open file";
        return false;
    }
    in.seekg(0, std::ios::end);
    auto len = in.tellg();
    if (len < 0) {
        error = "failed to determine file size";
        return false;
    }
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(len));
    if (!out.empty())
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!in && !out.empty()) {
        error = "failed to read file bytes";
        return false;
    }
    return true;
}

bool parse_track(const uint8_t* data, size_t size, std::vector<RawEvent>& out, std::string& error) {
    Reader r{data, size, 0};
    bool ok = true;
    uint64_t tick = 0;
    uint8_t running_status = 0;

    while (r.pos < r.size) {
        uint32_t delta = r.read_varlen(ok);
        if (!ok) {
            error = "malformed delta-time";
            return false;
        }
        tick += delta;

        uint8_t status = r.read_u8(ok);
        if (!ok) {
            error = "unexpected EOF while reading event status";
            return false;
        }

        if (status < 0x80u) {
            if (running_status == 0) {
                error = "running status used before status byte";
                return false;
            }
            r.pos--;
            status = running_status;
        } else if (status < 0xF0u) {
            running_status = status;
        } else {
            running_status = 0;
        }

        if (status == 0xFFu) {
            uint8_t meta_type = r.read_u8(ok);
            uint32_t len = r.read_varlen(ok);
            if (!ok || !r.remaining(len)) {
                error = "malformed meta event";
                return false;
            }
            if (meta_type == 0x51u && len == 3) {
                uint32_t tempo = (static_cast<uint32_t>(r.data[r.pos]) << 16) |
                                 (static_cast<uint32_t>(r.data[r.pos + 1]) << 8) |
                                 static_cast<uint32_t>(r.data[r.pos + 2]);
                out.push_back(RawEvent{tick, 0, 0, 0, true, tempo});
            } else if (meta_type == 0x2Fu) {
                return true;
            }
            r.pos += len;
            continue;
        }

        if (status == 0xF0u || status == 0xF7u) {
            uint32_t len = r.read_varlen(ok);
            if (!ok || !r.remaining(len)) {
                error = "malformed sysex event";
                return false;
            }
            r.pos += len;
            continue;
        }

        uint8_t event_type = status & 0xF0u;
        uint8_t data1 = r.read_u8(ok);
        if (!ok) {
            error = "malformed channel event";
            return false;
        }

        uint8_t data2 = 0;
        if (event_type != 0xC0u && event_type != 0xD0u) {
            data2 = r.read_u8(ok);
            if (!ok) {
                error = "malformed channel event data";
                return false;
            }
        }

        switch (event_type) {
            case 0x80u:
            case 0x90u:
            case 0xA0u:
            case 0xB0u:
            case 0xC0u:
            case 0xD0u:
            case 0xE0u: {
                if (event_type == 0x90u && data2 == 0) status = static_cast<uint8_t>(0x80u | (status & 0x0Fu));
                out.push_back(RawEvent{tick, status, data1, data2, false, 0});
                break;
            }
            default:
                break;
        }
    }

    return true;
}

} // namespace

Sequence parse_file(const std::string& path) {
    Sequence seq;
    std::vector<uint8_t> bytes;
    if (!read_file_bytes(path, bytes, seq.error)) return seq;

    Reader r{bytes.data(), bytes.size(), 0};
    bool ok = true;

    if (!r.remaining(14)) {
        seq.error = "file too small for MIDI header";
        return seq;
    }
    if (std::string(reinterpret_cast<const char*>(r.data), 4) != "MThd") {
        seq.error = "missing MThd header";
        return seq;
    }
    r.pos += 4;

    uint32_t header_len = r.read_be32(ok);
    uint16_t format = r.read_be16(ok);
    uint16_t track_count = r.read_be16(ok);
    uint16_t division = r.read_be16(ok);
    if (!ok) {
        seq.error = "malformed MIDI header";
        return seq;
    }
    if (header_len < 6) {
        seq.error = "unsupported MIDI header length";
        return seq;
    }
    if (header_len > 6) {
        if (!r.remaining(header_len - 6)) {
            seq.error = "truncated extended MIDI header";
            return seq;
        }
        r.pos += header_len - 6;
    }
    if (format != 0 && format != 1) {
        seq.error = "only MIDI format 0 and 1 are supported";
        return seq;
    }
    if ((division & 0x8000u) != 0) {
        seq.error = "SMPTE MIDI timing is not supported";
        return seq;
    }
    if (division == 0) {
        seq.error = "invalid MIDI division";
        return seq;
    }

    std::vector<RawEvent> raw_events;
    raw_events.reserve(256);

    for (uint16_t ti = 0; ti < track_count; ++ti) {
        if (!r.remaining(8)) {
            seq.error = "truncated track header";
            return seq;
        }
        if (std::string(reinterpret_cast<const char*>(r.data + r.pos), 4) != "MTrk") {
            seq.error = "missing MTrk header";
            return seq;
        }
        r.pos += 4;
        uint32_t track_len = r.read_be32(ok);
        if (!ok || !r.remaining(track_len)) {
            seq.error = "truncated track data";
            return seq;
        }
        std::string track_error;
        if (!parse_track(r.data + r.pos, track_len, raw_events, track_error)) {
            seq.error = std::move(track_error);
            return seq;
        }
        r.pos += track_len;
    }

    std::sort(raw_events.begin(), raw_events.end(), [](const RawEvent& a, const RawEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.is_tempo != b.is_tempo) return a.is_tempo && !b.is_tempo;
        if (a.status != b.status) return a.status < b.status;
        if (a.data1 != b.data1) return a.data1 < b.data1;
        return a.data2 < b.data2;
    });

    constexpr double kMicrosToSeconds = 1.0 / 1000000.0;
    uint32_t tempo_us_per_qn = 500000;
    uint64_t prev_tick = 0;
    double current_time = 0.0;

    for (const auto& ev : raw_events) {
        if (ev.tick > prev_tick) {
            current_time += static_cast<double>(ev.tick - prev_tick) *
                            static_cast<double>(tempo_us_per_qn) *
                            kMicrosToSeconds /
                            static_cast<double>(division);
            prev_tick = ev.tick;
        }

        if (ev.is_tempo) {
            tempo_us_per_qn = ev.tempo_us_per_qn;
            continue;
        }

        seq.events.push_back(Event{current_time, ev.status, ev.data1, ev.data2});
    }

    seq.duration_seconds = current_time;
    if (!seq.events.empty())
        seq.duration_seconds = std::max(seq.duration_seconds, seq.events.back().time_seconds);
    return seq;
}

} // namespace vivid::midi_file
