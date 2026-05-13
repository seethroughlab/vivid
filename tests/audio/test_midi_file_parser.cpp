#include "common/midi_file.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "test_helpers.h"

static void check_near(double actual, double expected, double eps, const char* msg) {
    if (std::abs(actual - expected) > eps) {
        std::fprintf(stderr, "FAIL: %s (expected %.6f, got %.6f)\n", msg, expected, actual);
        ++failures;
    } else {
        std::fprintf(stderr, "PASS: %s (%.6f)\n", msg, actual);
    }
}

static void push_be16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
}

static void push_be32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
}

static void push_varlen(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t bytes[5] = {};
    int count = 1;
    bytes[4] = static_cast<uint8_t>(value & 0x7Fu);
    while ((value >>= 7u) != 0) {
        bytes[4 - count] = static_cast<uint8_t>((value & 0x7Fu) | 0x80u);
        ++count;
    }
    for (int i = 5 - count; i < 5; ++i) out.push_back(bytes[i]);
}

static std::vector<uint8_t> make_track(const std::vector<uint8_t>& events) {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'M', 'T', 'r', 'k'});
    push_be32(out, static_cast<uint32_t>(events.size()));
    out.insert(out.end(), events.begin(), events.end());
    return out;
}

static std::vector<uint8_t> make_midi(uint16_t format,
                                      uint16_t division,
                                      const std::vector<std::vector<uint8_t>>& tracks) {
    std::vector<uint8_t> out;
    out.insert(out.end(), {'M', 'T', 'h', 'd'});
    push_be32(out, 6);
    push_be16(out, format);
    push_be16(out, static_cast<uint16_t>(tracks.size()));
    push_be16(out, division);
    for (const auto& track : tracks) {
        auto chunk = make_track(track);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return out;
}

static std::filesystem::path write_bytes(const std::filesystem::path& path,
                                         const std::vector<uint8_t>& bytes) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return path;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path sandbox = fs::path("./.test_midi_file_parser");
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);

    {
        std::vector<uint8_t> track;
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});
        push_varlen(track, 0);
        track.insert(track.end(), {0x90, 60, 100});
        push_varlen(track, 480);
        track.insert(track.end(), {0x80, 60, 0});
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x2F, 0x00});
        auto path = write_bytes(sandbox / "format0.mid", make_midi(0, 480, {track}));

        auto seq = vivid::midi_file::parse_file(path.string());
        check(seq.ok(), "format-0 MIDI parses successfully");
        if (seq.ok()) {
            check(seq.events.size() == 2, "format-0 yields note-on and note-off");
            if (seq.events.size() == 2) {
                check(seq.events[0].status == 0x90, "format-0 first event is note-on");
                check(seq.events[1].status == 0x80, "format-0 second event is note-off");
                check(seq.note_spans.size() == 1, "format-0 yields one paired note span");
                check_near(seq.events[1].time_seconds, 0.5, 1e-6,
                           "format-0 note-off time honors PPQ + tempo");
            }
        }
    }

    {
        std::vector<uint8_t> tempo_track;
        push_varlen(tempo_track, 0);
        tempo_track.insert(tempo_track.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});
        push_varlen(tempo_track, 480);
        tempo_track.insert(tempo_track.end(), {0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40});
        push_varlen(tempo_track, 0);
        tempo_track.insert(tempo_track.end(), {0xFF, 0x2F, 0x00});

        std::vector<uint8_t> note_track;
        push_varlen(note_track, 0);
        note_track.insert(note_track.end(), {0x90, 60, 64});
        push_varlen(note_track, 960);
        note_track.insert(note_track.end(), {0x80, 60, 0});
        push_varlen(note_track, 0);
        note_track.insert(note_track.end(), {0xFF, 0x2F, 0x00});
        auto path = write_bytes(sandbox / "format1.mid", make_midi(1, 480, {tempo_track, note_track}));

        auto seq = vivid::midi_file::parse_file(path.string());
        check(seq.ok(), "format-1 MIDI parses successfully");
        if (seq.ok() && seq.events.size() >= 2) {
            check_near(seq.events[1].time_seconds, 1.5, 1e-6,
                       "tempo-map conversion affects later event timing");
        }
    }

    {
        std::vector<uint8_t> track;
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});
        push_varlen(track, 0);
        track.insert(track.end(), {0x90, 60, 100});
        push_varlen(track, 240);
        track.insert(track.end(), {62, 80});
        push_varlen(track, 240);
        track.insert(track.end(), {60, 0});
        push_varlen(track, 0);
        track.insert(track.end(), {62, 0});
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x2F, 0x00});
        auto path = write_bytes(sandbox / "running_status.mid", make_midi(0, 480, {track}));

        auto seq = vivid::midi_file::parse_file(path.string());
        check(seq.ok(), "running-status MIDI parses successfully");
        if (seq.ok()) {
            check(seq.events.size() == 4, "running-status file yields four channel events");
            if (seq.events.size() == 4) {
                check(seq.events[1].status == 0x90 && seq.events[1].data1 == 62,
                      "running-status note-on inherits prior status");
                check(seq.events[2].status == 0x80,
                      "velocity-zero running-status note-on normalizes to note-off");
            }
        }
    }

    {
        std::vector<uint8_t> track;
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20});
        push_varlen(track, 0);
        track.insert(track.end(), {0xF0, 0x03, 0x01, 0x02, 0x03});
        push_varlen(track, 0);
        track.insert(track.end(), {0x90, 65, 100});
        push_varlen(track, 240);
        track.insert(track.end(), {0x80, 65, 0});
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x01, 0x05, 'h', 'e', 'l', 'l', 'o'});
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x2F, 0x00});
        auto path = write_bytes(sandbox / "skip_unsupported.mid", make_midi(0, 480, {track}));

        auto seq = vivid::midi_file::parse_file(path.string());
        check(seq.ok(), "unsupported sysex/meta events are skipped cleanly");
        if (seq.ok()) {
            check(seq.events.size() == 2, "unsupported events do not create channel events");
        }
    }

    // --- SMPTE timing rejection ---
    {
        // Division with bit 15 set indicates SMPTE timing.
        // 0xE728 = negative SMPTE frame rate in high byte, ticks-per-frame in low byte.
        std::vector<uint8_t> track;
        push_varlen(track, 0);
        track.insert(track.end(), {0x90, 60, 100});
        push_varlen(track, 0);
        track.insert(track.end(), {0xFF, 0x2F, 0x00});
        auto path = write_bytes(sandbox / "smpte.mid", make_midi(0, 0xE728, {track}));

        auto seq = vivid::midi_file::parse_file(path.string());
        check(!seq.ok(), "SMPTE timing is rejected");
        check(seq.error.find("SMPTE") != std::string::npos, "SMPTE error message mentions SMPTE");
    }

    // --- Nonexistent file ---
    {
        auto seq = vivid::midi_file::parse_file((sandbox / "does_not_exist.mid").string());
        check(!seq.ok(), "nonexistent file returns error");
    }

    // --- Fixture file parity (assets/sweelinck.mid) ---
    {
        auto fixture = fs::path(VIVID_SOURCE_DIR) / "assets" / "sweelinck.mid";
        if (fs::exists(fixture)) {
            auto seq = vivid::midi_file::parse_file(fixture.string());
            check(seq.ok(), "sweelinck.mid parses successfully");
            if (seq.ok()) {
                check(seq.events.size() > 100, "sweelinck.mid has substantial event count");
                check(seq.note_spans.size() > 100, "sweelinck.mid has substantial note span count");
                check(seq.duration_seconds > 1.0, "sweelinck.mid has nonzero duration");
                // All events should be channel messages (0x80-0xEF).
                bool all_channel = true;
                for (const auto& ev : seq.events) {
                    if (ev.status < 0x80 || ev.status >= 0xF0) {
                        all_channel = false;
                        break;
                    }
                }
                check(all_channel, "sweelinck.mid contains only channel events");
            }
        } else {
            std::fprintf(stderr, "SKIP: sweelinck.mid fixture not found at %s\n", fixture.c_str());
        }
    }

    fs::remove_all(sandbox);
    std::fprintf(stderr, "\n%d failed\n", failures);
    return failures ? 1 : 0;
}
