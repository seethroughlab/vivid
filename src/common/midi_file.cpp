#include "common/midi_file.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "MidiFile.h"

namespace vivid::midi_file {

Sequence parse_file(const std::string& path) {
    Sequence seq;

    // Detect SMPTE timing before delegating to midifile (which silently handles it).
    // Bytes 12-13 of a Standard MIDI File are the division field; bit 15 set means SMPTE.
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            seq.error = "failed to open file";
            return seq;
        }
        in.seekg(12);
        uint8_t hi = 0;
        in.read(reinterpret_cast<char*>(&hi), 1);
        if (!in) {
            seq.error = "file too small for MIDI header";
            return seq;
        }
        if (hi & 0x80u) {
            seq.error = "SMPTE MIDI timing is not supported";
            return seq;
        }
    }

    smf::MidiFile midifile;
    if (!midifile.read(path)) {
        seq.error = "failed to parse MIDI file";
        return seq;
    }

    midifile.doTimeAnalysis();
    midifile.joinTracks();

    struct SortableEvent {
        double time_seconds;
        double time_beats;
        uint8_t status;
        uint8_t data1;
        uint8_t data2;
    };
    std::vector<SortableEvent> collected;

    int event_count = midifile[0].getEventCount();
    const double ticks_per_quarter =
        std::max(1, midifile.getTicksPerQuarterNote());
    double last_event_time = 0.0;
    double last_event_beats = 0.0;
    for (int i = 0; i < event_count; ++i) {
        smf::MidiEvent& ev = midifile[0][i];
        const double ev_beats = static_cast<double>(ev.tick) / ticks_per_quarter;

        // Track the time of every event (including meta) for duration.
        if (ev.seconds > last_event_time)
            last_event_time = ev.seconds;
        if (ev_beats > last_event_beats)
            last_event_beats = ev_beats;

        if (ev.isMetaMessage() || ev.size() < 2)
            continue;

        uint8_t status = static_cast<uint8_t>(ev[0]);
        uint8_t high_nibble = status & 0xF0u;

        // Only collect channel messages (0x80-0xEF).
        if (high_nibble < 0x80u || high_nibble >= 0xF0u)
            continue;

        uint8_t data1 = static_cast<uint8_t>(ev[1]);
        uint8_t data2 = (ev.size() >= 3) ? static_cast<uint8_t>(ev[2]) : 0;

        // Normalize note-on with velocity 0 to note-off.
        if (high_nibble == 0x90u && data2 == 0)
            status = static_cast<uint8_t>(0x80u | (status & 0x0Fu));

        collected.push_back({ev.seconds, ev_beats, status, data1, data2});
    }

    // Deterministic sort: by time, then status, then data bytes.
    std::sort(collected.begin(), collected.end(),
        [](const SortableEvent& a, const SortableEvent& b) {
            if (a.time_seconds != b.time_seconds) return a.time_seconds < b.time_seconds;
            if (a.status != b.status) return a.status < b.status;
            if (a.data1 != b.data1) return a.data1 < b.data1;
            return a.data2 < b.data2;
        });

    seq.events.reserve(collected.size());
    for (const auto& e : collected)
        seq.events.push_back({e.time_seconds, e.time_beats, e.status, e.data1, e.data2});

    struct ActiveNote {
        double start_seconds = 0.0;
        double start_beats = 0.0;
        uint8_t velocity = 0;
        uint32_t order = 0;
    };
    std::vector<ActiveNote> active[16][128];
    uint32_t note_order = 0;
    for (const auto& e : seq.events) {
        const uint8_t kind = e.status & 0xF0u;
        const uint8_t ch = e.status & 0x0Fu;
        const uint8_t pitch = e.data1;
        const bool is_on = (kind == 0x90u && e.data2 > 0);
        const bool is_off = (kind == 0x80u || (kind == 0x90u && e.data2 == 0));
        if (is_on) {
            active[ch][pitch].push_back({e.time_seconds, e.time_beats, e.data2, note_order++});
        } else if (is_off && !active[ch][pitch].empty()) {
            auto a = active[ch][pitch].front();
            active[ch][pitch].erase(active[ch][pitch].begin());
            const double duration = e.time_seconds - a.start_seconds;
            const double duration_beats = e.time_beats - a.start_beats;
            if (duration > 0.0 && duration_beats > 0.0) {
                seq.note_spans.push_back({
                    a.start_seconds,
                    duration,
                    a.start_beats,
                    duration_beats,
                    ch,
                    pitch,
                    a.velocity,
                    a.order,
                });
            }
        }
    }

    for (uint8_t ch = 0; ch < 16; ++ch) {
        for (uint8_t pitch = 0; pitch < 128; ++pitch) {
            for (const auto& a : active[ch][pitch]) {
                // Always include unclosed notes — downstream clamps to a minimum.
                // duration_beats may be 0 if all events share the same tick (e.g.
                // a chord-at-tick-0 file); the consumer assigns a display default.
                seq.note_spans.push_back({
                    a.start_seconds,
                    std::max(0.0, last_event_time - a.start_seconds),
                    a.start_beats,
                    std::max(0.0, last_event_beats - a.start_beats),
                    ch,
                    pitch,
                    a.velocity,
                    a.order,
                });
            }
        }
    }
    std::sort(seq.note_spans.begin(), seq.note_spans.end(),
        [](const NoteSpan& a, const NoteSpan& b) {
            if (a.start_seconds != b.start_seconds) return a.start_seconds < b.start_seconds;
            return a.order < b.order;
        });

    // Duration spans the full file including trailing meta events, matching the
    // old parser which tracked current_time across all events.
    seq.duration_seconds = last_event_time;
    seq.duration_beats = last_event_beats;

    return seq;
}

} // namespace vivid::midi_file
