#pragma once
#include "midi/midi_clip.h"   // ClipNote (writer input)

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

// Standard MIDI File (SMF) read/write — the import path for the enormous amount of MIDI that
// already exists outside Vivid. The immediate motivation is drum plugins: EZdrummer, Superior
// Drummer, Addictive Drums et al. are built around dragging a groove out of the plugin's own
// browser, and without a .mid importer that entire library is unreachable from Vivid.
//
// Deliberately module `midi` (layering rank 0): std-only, no session/audio/platform dependency, so
// the parser is unit-testable with no app fixture. Formats 0 and 1, PPQ division only (SMPTE
// timecode division is rejected rather than silently mistimed).
namespace vivid::session {

// One note read from a file, positioned in BEATS (quarter notes) from the start of the file.
struct MidiFileNote {
    int    pitch;      // 0..127
    double start;      // beats
    double dur;        // beats
    float  vel;        // 0..1
    int    channel;    // 0..15 — drum files put the kit on channel 9 (GM channel 10)
    int    track;      // source track index within the file (format 1 keeps parts separate)
};

struct MidiFileData {
    std::vector<MidiFileNote> notes;      // sorted by (start, pitch)
    double length_beats = 0.0;            // end of the last note
    int    format  = 0;
    int    ntracks = 0;
    double initial_bpm = 0.0;             // first tempo meta event; 0 = none present (assume host tempo)
    std::vector<std::string> track_names; // 0xFF 0x03 per track ("" when absent)
};

namespace detail {

// Big-endian fixed-width reads. `p` advances; every caller has already bounds-checked.
inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline uint16_t be16(const uint8_t* p) { return uint16_t((uint32_t(p[0]) << 8) | uint32_t(p[1])); }

// SMF variable-length quantity: 7 bits per byte, high bit = "more follows". Max 4 bytes.
// Returns false on a truncated or over-long quantity rather than reading past the end.
inline bool read_vlq(const uint8_t* d, size_t n, size_t& i, uint32_t& out) {
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
        if (i >= n) return false;
        const uint8_t b = d[i++];
        v = (v << 7) | uint32_t(b & 0x7F);
        if (!(b & 0x80)) { out = v; return true; }
    }
    return false;   // 5+ continuation bytes is malformed
}

// An open note-on awaiting its note-off, keyed by (channel, pitch) within one track.
struct OpenNote { int channel; int pitch; double start; float vel; };

}  // namespace detail

// Parse SMF bytes. Returns false and sets *err on a malformed file. Tolerant where tolerance is
// safe (an unterminated note is closed at the end of its track; unknown meta/sysex events are
// skipped by their declared length), strict where guessing would silently mistime the result.
inline bool parse_midi_file(const std::vector<uint8_t>& bytes, MidiFileData& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    out = MidiFileData{};

    const uint8_t* d = bytes.data();
    const size_t   n = bytes.size();
    if (n < 14 || d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd')
        return fail("not a Standard MIDI File (no MThd header)");
    const uint32_t hdr_len = detail::be32(d + 4);
    if (hdr_len < 6 || 8 + hdr_len > n) return fail("truncated MThd header");
    out.format      = detail::be16(d + 8);
    out.ntracks     = detail::be16(d + 10);
    const uint16_t division = detail::be16(d + 12);
    if (division & 0x8000)
        return fail("SMPTE timecode division is not supported (only ticks-per-quarter-note)");
    if (division == 0) return fail("invalid division (0 ticks per quarter note)");
    if (out.format != 0 && out.format != 1)
        return fail("unsupported SMF format " + std::to_string(out.format) + " (only 0 and 1)");
    const double ppq = double(division);

    size_t i = 8 + hdr_len;
    int track_index = 0;
    while (i + 8 <= n) {
        const bool is_mtrk = (d[i] == 'M' && d[i+1] == 'T' && d[i+2] == 'r' && d[i+3] == 'k');
        const uint32_t chunk_len = detail::be32(d + i + 4);
        const size_t body = i + 8;
        if (body + chunk_len > n) return fail("truncated chunk in MIDI file");
        if (!is_mtrk) { i = body + chunk_len; continue; }   // skip unknown chunks per the spec

        const size_t end = body + chunk_len;
        size_t   p = body;
        uint64_t ticks = 0;
        uint8_t  status = 0;                    // running status
        std::vector<detail::OpenNote> open;
        std::string this_name;

        while (p < end) {
            uint32_t delta = 0;
            if (!detail::read_vlq(d, end, p, delta)) return fail("bad delta time");
            ticks += delta;
            if (p >= end) return fail("event ran past end of track");

            uint8_t b = d[p];
            if (b & 0x80) { status = b; ++p; }   // new status byte
            else if (!status) return fail("data byte with no running status");
            // else: running status — `status` carries over and `p` still points at data

            const uint8_t type = status & 0xF0;
            const int     chan = status & 0x0F;

            if (status == 0xFF) {                            // meta event
                if (p >= end) return fail("truncated meta event");
                const uint8_t meta = d[p++];
                uint32_t len = 0;
                if (!detail::read_vlq(d, end, p, len)) return fail("bad meta length");
                if (p + len > end) return fail("truncated meta payload");
                if (meta == 0x51 && len == 3 && out.initial_bpm == 0.0) {
                    const uint32_t us_per_qn = (uint32_t(d[p]) << 16) | (uint32_t(d[p+1]) << 8) | uint32_t(d[p+2]);
                    if (us_per_qn) out.initial_bpm = 60000000.0 / double(us_per_qn);
                } else if (meta == 0x03 && this_name.empty()) {
                    this_name.assign(reinterpret_cast<const char*>(d + p), len);
                }
                p += len;
                if (meta == 0x2F) break;                     // end of track
            } else if (status == 0xF0 || status == 0xF7) {    // sysex — skip by declared length
                uint32_t len = 0;
                if (!detail::read_vlq(d, end, p, len)) return fail("bad sysex length");
                if (p + len > end) return fail("truncated sysex");
                p += len;
            } else if (type == 0xC0 || type == 0xD0) {        // program change / channel pressure: 1 byte
                if (p >= end) return fail("truncated 1-byte channel message");
                ++p;
            } else if (type >= 0x80 && type <= 0xE0) {        // 2-byte channel messages
                if (p + 1 >= end) return fail("truncated 2-byte channel message");
                const uint8_t d1 = d[p] & 0x7F, d2 = d[p+1] & 0x7F;
                p += 2;
                const double beat = double(ticks) / ppq;
                const bool note_on  = (type == 0x90 && d2 > 0);
                const bool note_off = (type == 0x80) || (type == 0x90 && d2 == 0);
                if (note_on) {
                    open.push_back({ chan, int(d1), beat, float(d2) / 127.0f });
                } else if (note_off) {
                    // Close the most RECENT matching open note (correct for repeated same-pitch
                    // notes; a first-match scan would close the wrong one).
                    for (auto it = open.rbegin(); it != open.rend(); ++it) {
                        if (it->channel == chan && it->pitch == int(d1)) {
                            double dur = beat - it->start;
                            if (dur <= 0.0) dur = 1.0 / 32.0;   // zero-length note -> a tick of duration
                            out.notes.push_back({ it->pitch, it->start, dur, it->vel, chan, track_index });
                            open.erase(std::next(it).base());
                            break;
                        }
                    }
                }
                // CC / pitch-bend / poly-pressure are parsed for length but not yet imported —
                // clip-level controller lanes are a separate piece of work.
            } else {
                return fail("unhandled MIDI status 0x" + std::to_string(int(status)));
            }
        }

        // A note left hanging at end-of-track: close it there rather than dropping it. Real files
        // do this, usually by omitting the final note-off before the 0x2F.
        const double track_end = double(ticks) / ppq;
        for (const detail::OpenNote& o : open) {
            double dur = track_end - o.start;
            if (dur <= 0.0) dur = 1.0 / 32.0;
            out.notes.push_back({ o.pitch, o.start, dur, o.vel, o.channel, track_index });
        }
        out.track_names.push_back(this_name);
        ++track_index;
        i = end;
    }

    if (out.notes.empty() && track_index == 0) return fail("no MTrk chunks in MIDI file");

    std::sort(out.notes.begin(), out.notes.end(), [](const MidiFileNote& a, const MidiFileNote& b) {
        if (a.start != b.start) return a.start < b.start;
        return a.pitch < b.pitch;
    });
    for (const MidiFileNote& nt : out.notes)
        if (nt.start + nt.dur > out.length_beats) out.length_beats = nt.start + nt.dur;
    return true;
}

// Read a file from disk and parse it.
inline bool read_midi_file(const std::string& path, MidiFileData& out, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (err) *err = "could not open " + path; return false; }
    std::vector<uint8_t> bytes;
    uint8_t buf[8192];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + got);
    std::fclose(f);
    return parse_midi_file(bytes, out, err);
}

namespace detail {
inline void push_vlq(std::vector<uint8_t>& v, uint32_t x) {
    uint8_t stack[5]; int n = 0;
    stack[n++] = uint8_t(x & 0x7F);
    while ((x >>= 7) != 0) stack[n++] = uint8_t((x & 0x7F) | 0x80);
    while (n) v.push_back(stack[--n]);
}
inline void push_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));  v.push_back(uint8_t(x));
}
inline void push_be16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}
}  // namespace detail

// Serialize clip notes as a single-track (format 0) SMF at `ppq` ticks per quarter note. The
// symmetric half of import: a groove edited in Vivid can go back out to a plugin's browser or
// another DAW. Per-note expression curves are not represented (SMF has no equivalent).
inline std::vector<uint8_t> write_midi_bytes(const ClipNote* notes, int count, double bpm,
                                             int ppq = 480) {
    struct Ev { uint64_t tick; bool on; int pitch; int vel; };
    std::vector<Ev> evs;
    evs.reserve(size_t(count > 0 ? count : 0) * 2);
    for (int k = 0; k < count; ++k) {
        const ClipNote& c = notes[k];
        const uint64_t on_t  = uint64_t(c.start * ppq + 0.5);
        uint64_t       off_t = uint64_t((c.start + c.dur) * ppq + 0.5);
        if (off_t <= on_t) off_t = on_t + 1;             // never emit a zero-length note
        int v = int(c.vel * 127.0f + 0.5f);
        if (v < 1) v = 1; if (v > 127) v = 127;          // vel 0 would read back as a note-off
        int p = c.pitch; if (p < 0) p = 0; if (p > 127) p = 127;
        evs.push_back({ on_t,  true,  p, v });
        evs.push_back({ off_t, false, p, 0 });
    }
    // Note-OFFs sort before note-ONs at the same tick, so a repeated pitch retriggers cleanly
    // instead of the off killing the note that just started.
    std::sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return (a.on ? 1 : 0) < (b.on ? 1 : 0);
    });

    std::vector<uint8_t> trk;
    if (bpm > 0.0) {   // tempo meta so the file opens at the right speed elsewhere
        const uint32_t us_per_qn = uint32_t(60000000.0 / bpm + 0.5);
        detail::push_vlq(trk, 0);
        trk.push_back(0xFF); trk.push_back(0x51); trk.push_back(0x03);
        trk.push_back(uint8_t(us_per_qn >> 16)); trk.push_back(uint8_t(us_per_qn >> 8));
        trk.push_back(uint8_t(us_per_qn));
    }
    uint64_t prev = 0;
    for (const Ev& e : evs) {
        detail::push_vlq(trk, uint32_t(e.tick - prev));
        prev = e.tick;
        trk.push_back(uint8_t(e.on ? 0x90 : 0x80));      // channel 0; no running status (simpler, still valid)
        trk.push_back(uint8_t(e.pitch));
        trk.push_back(uint8_t(e.vel));
    }
    detail::push_vlq(trk, 0);
    trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0x00);   // end of track

    std::vector<uint8_t> out;
    out.push_back('M'); out.push_back('T'); out.push_back('h'); out.push_back('d');
    detail::push_be32(out, 6);
    detail::push_be16(out, 0);                       // format 0
    detail::push_be16(out, 1);                       // one track
    detail::push_be16(out, uint16_t(ppq));
    out.push_back('M'); out.push_back('T'); out.push_back('r'); out.push_back('k');
    detail::push_be32(out, uint32_t(trk.size()));
    out.insert(out.end(), trk.begin(), trk.end());
    return out;
}

inline bool write_midi_file(const std::string& path, const ClipNote* notes, int count, double bpm,
                            std::string* err, int ppq = 480) {
    const std::vector<uint8_t> bytes = write_midi_bytes(notes, count, bpm, ppq);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { if (err) *err = "could not open " + path + " for writing"; return false; }
    const size_t wrote = std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    if (wrote != bytes.size()) { if (err) *err = "short write to " + path; return false; }
    return true;
}

}  // namespace vivid::session
