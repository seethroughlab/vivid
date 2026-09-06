#pragma once
#include <cstddef>
#include <cstdint>

// Live MIDI byte-stream decoding, split out of the CoreMIDI callback so it can be tested against
// the byte sequences real hardware actually sends. The previous inline parser in midi_input.mm
// handled only 0x90/0x80 and advanced past everything else by a fixed byte count, which meant:
//
//   * RUNNING STATUS was dropped entirely (`if (status < 0x80) { ++j; continue; }`). Most keyboards
//     use it — the status byte appears once and subsequent notes omit it — so held chords and fast
//     passages were losing notes on real hardware.
//   * SYSEX was skipped as if it were 3 bytes, so the rest of its payload got re-read as status
//     bytes. A payload containing 0x90 fabricates note-ons out of nothing.
//   * CC / pitch-bend / aftertouch were discarded, so a mod wheel or sustain pedal did nothing.
//
// The parser is a byte-wise state machine because CoreMIDI can split a message across packets and
// can embed real-time bytes inside one. Module `midi` (layering rank 0): no CoreMIDI, no platform,
// std only.
namespace vivid::session {

enum class MidiKind : uint8_t {
    NoteOn, NoteOff, CC, PitchBend, ChannelPressure, PolyPressure, ProgramChange
};

// One decoded channel message. `data1` is the note number (notes, poly pressure), the controller
// number (CC), or the program (program change); it is 0 for channel pressure and pitch bend.
// `value` is ALWAYS normalized 0..1 — velocity, CC/pressure d/127, pitch bend 14-bit/16383 with
// 0.5 at center — so a value crosses every downstream seam without a units conversion.
struct MidiMsg {
    MidiKind kind;
    uint8_t  channel;     // 0..15
    uint8_t  data1;
    float    value;
    uint64_t host_time;   // source timestamp, carried for later sample-accurate stamping
};

class MidiByteParser {
public:
    // Decode `n` bytes, invoking `out(const MidiMsg&)` per complete message. State (running status,
    // a partial message, sysex) carries ACROSS calls, so a message split between two packets
    // decodes correctly. Allocation-free.
    template <class Sink>
    void feed(const uint8_t* d, size_t n, uint64_t host_time, Sink&& out) {
        for (size_t i = 0; i < n; ++i) {
            const uint8_t b = d[i];

            // System real-time (0xF8..0xFF): always a single byte, may appear ANYWHERE — including
            // between the data bytes of another message, or inside a sysex. It must not disturb
            // running status or a partially-received message. Clock/start/stop are not consumed yet.
            if (b >= 0xF8) continue;

            if (in_sysex_) {
                // 0xF7 ends it; any other status byte means the sysex was abandoned mid-stream
                // (malformed but seen in the wild) — fall through and reprocess it as a new status.
                if (b == 0xF7) { in_sysex_ = false; continue; }
                if (b < 0x80) continue;              // ordinary payload byte
                in_sysex_ = false;                   // abandoned; handle `b` as a status below
            }

            if (b >= 0x80) {                          // --- status byte ---
                if (b == 0xF0) { in_sysex_ = true; status_ = 0; have_ = 0; continue; }
                if (b >= 0xF1) {
                    // System common (0xF1 MTC, 0xF2 song position, 0xF3 song select, 0xF6 tune
                    // request, and the undefined 0xF4/0xF5). Per spec these CLEAR running status;
                    // clearing it also makes their data bytes fall through the `!status_` guard
                    // below and be discarded, which is what we want.
                    status_ = 0; have_ = 0; continue;
                }
                status_ = b; have_ = 0; continue;     // channel status; running status now armed
            }

            // --- data byte ---
            if (!status_) continue;                   // no running status armed: nothing to attach it to
            buf_[have_++] = b;
            const uint8_t type = status_ & 0xF0;
            const uint8_t need = (type == 0xC0 || type == 0xD0) ? 1 : 2;
            if (have_ < need) continue;
            have_ = 0;                                // status_ is DELIBERATELY kept: running status
            emit(type, uint8_t(status_ & 0x0F), buf_[0], buf_[1], host_time, out);
        }
    }

    // Drop any partial message + running status (call on a device disconnect, so a half-received
    // message from the old device can't merge with the first bytes from the next one).
    void reset() { status_ = 0; have_ = 0; in_sysex_ = false; }

private:
    template <class Sink>
    static void emit(uint8_t type, uint8_t chan, uint8_t d1, uint8_t d2, uint64_t t, Sink& out) {
        d1 &= 0x7F; d2 &= 0x7F;
        MidiMsg m{};
        m.channel = chan;
        m.host_time = t;
        switch (type) {
            case 0x80: m.kind = MidiKind::NoteOff; m.data1 = d1; m.value = d2 / 127.0f; break;
            case 0x90:
                // Note-on with velocity 0 is the canonical note-off (it is what running status
                // exists for — a whole passage under one 0x90).
                m.kind = d2 > 0 ? MidiKind::NoteOn : MidiKind::NoteOff;
                m.data1 = d1; m.value = d2 / 127.0f;
                break;
            case 0xA0: m.kind = MidiKind::PolyPressure;    m.data1 = d1; m.value = d2 / 127.0f; break;
            case 0xB0: m.kind = MidiKind::CC;              m.data1 = d1; m.value = d2 / 127.0f; break;
            case 0xC0: m.kind = MidiKind::ProgramChange;   m.data1 = d1; m.value = 0.0f;        break;
            case 0xD0: m.kind = MidiKind::ChannelPressure; m.data1 = 0;  m.value = d1 / 127.0f; break;
            case 0xE0: m.kind = MidiKind::PitchBend;       m.data1 = 0;
                       m.value = float((uint16_t(d2) << 7) | uint16_t(d1)) / 16383.0f;          break;
            default: return;
        }
        out(m);
    }

    uint8_t status_ = 0;      // running status (0 = none armed)
    uint8_t buf_[2] = {0, 0};
    uint8_t have_ = 0;
    bool    in_sysex_ = false;
};

}  // namespace vivid::session
