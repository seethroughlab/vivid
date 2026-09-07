// Live MIDI byte-stream decoding (midi/midi_parse.h). These are the byte sequences real hardware
// sends and the old inline parser in midi_input.mm got wrong: running status (silently dropped —
// so keyboards were losing notes), sysex (skipped as 3 bytes, so its payload was re-read as status
// and fabricated notes), embedded real-time bytes, and messages split across packets.
#include "midi/midi_parse.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace vivid::session;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__); ++g_fail; } } while (0)

namespace {

std::vector<MidiMsg> decode(std::initializer_list<uint8_t> bytes) {
    std::vector<uint8_t> b(bytes);
    std::vector<MidiMsg> out;
    MidiByteParser p;
    p.feed(b.data(), b.size(), 42, [&](const MidiMsg& m) { out.push_back(m); });
    return out;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

void test_notes() {
    auto m = decode({ 0x90, 60, 100, 0x80, 60, 0 });
    CHECK(m.size() == 2, "note on + note off");
    if (m.size() == 2) {
        CHECK(m[0].kind == MidiKind::NoteOn && m[0].data1 == 60 && near(m[0].value, 100 / 127.0f),
              "note-on decoded with normalized velocity");
        CHECK(m[1].kind == MidiKind::NoteOff, "note-off decoded");
        CHECK(m[0].host_time == 42, "host timestamp carried through");
    }
    // Note-on with velocity 0 is a note-off.
    auto z = decode({ 0x90, 60, 0 });
    CHECK(z.size() == 1 && z[0].kind == MidiKind::NoteOff, "note-on vel 0 is a note-off");
    // Channel is preserved (drums arrive on channel 9).
    auto c = decode({ 0x99, 36, 110 });
    CHECK(c.size() == 1 && c[0].channel == 9, "channel preserved");
}

void test_running_status() {
    // THE regression: one 0x90 followed by bare data-byte pairs. The old parser hit
    // `if (status < 0x80) { ++j; continue; }` and dropped every one of these.
    auto m = decode({ 0x90, 60, 100,
                            64, 100,
                            67, 100,
                            60, 0,     // still running status — a note-off
                            64, 0,
                            67, 0 });
    CHECK(m.size() == 6, "running status yields all six messages");
    if (m.size() == 6) {
        CHECK(m[0].kind == MidiKind::NoteOn && m[1].kind == MidiKind::NoteOn && m[2].kind == MidiKind::NoteOn,
              "first three are note-ons");
        CHECK(m[3].kind == MidiKind::NoteOff && m[5].data1 == 67, "last three are note-offs");
    }
    // Running status also applies to CC, which is how a controller sweep arrives.
    auto cc = decode({ 0xB0, 1, 0, 1, 64, 1, 127 });
    CHECK(cc.size() == 3, "running-status CC sweep decodes");
    if (cc.size() == 3) CHECK(near(cc[2].value, 1.0f), "CC 127 normalizes to 1.0");
}

void test_controllers() {
    auto cc = decode({ 0xB0, 64, 127 });                 // sustain pedal down
    CHECK(cc.size() == 1 && cc[0].kind == MidiKind::CC && cc[0].data1 == 64 && near(cc[0].value, 1.0f),
          "CC 64 (sustain) decoded");
    auto bend_center = decode({ 0xE0, 0x00, 0x40 });     // 8192 = center
    CHECK(bend_center.size() == 1 && bend_center[0].kind == MidiKind::PitchBend, "pitch bend decoded");
    if (!bend_center.empty()) CHECK(near(bend_center[0].value, 8192.0f / 16383.0f), "bend center ~0.5");
    auto bend_max = decode({ 0xE0, 0x7F, 0x7F });
    CHECK(!bend_max.empty() && near(bend_max[0].value, 1.0f), "bend max = 1.0");
    auto bend_min = decode({ 0xE0, 0x00, 0x00 });
    CHECK(!bend_min.empty() && near(bend_min[0].value, 0.0f), "bend min = 0.0");

    auto at = decode({ 0xD0, 64 });                      // channel pressure: ONE data byte
    CHECK(at.size() == 1 && at[0].kind == MidiKind::ChannelPressure && near(at[0].value, 64 / 127.0f),
          "channel pressure is a 1-byte message");
    auto pat = decode({ 0xA0, 60, 90 });
    CHECK(pat.size() == 1 && pat[0].kind == MidiKind::PolyPressure && pat[0].data1 == 60,
          "poly pressure decoded");
    auto pc = decode({ 0xC0, 5 });                       // program change: ONE data byte
    CHECK(pc.size() == 1 && pc[0].kind == MidiKind::ProgramChange && pc[0].data1 == 5,
          "program change is a 1-byte message");
}

void test_sysex_does_not_fabricate_notes() {
    // A sysex payload can contain any 7-bit value, including 0x3C/0x64 — and the terminator is the
    // only thing that ends it. The old fixed-3-byte skip re-read the payload as status bytes.
    auto m = decode({ 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0x40, 0x64, 0xF7,
                      0x90, 60, 100 });
    CHECK(m.size() == 1, "sysex payload produces no messages");
    if (m.size() == 1) CHECK(m[0].kind == MidiKind::NoteOn && m[0].data1 == 60,
                             "the note AFTER the sysex still decodes");

    // Sysex also clears running status: data bytes following it with no new status are dropped.
    auto after = decode({ 0x90, 60, 100, 0xF0, 0x01, 0xF7, 64, 100 });
    CHECK(after.size() == 1, "running status does not survive a sysex");
}

void test_realtime_bytes_are_transparent() {
    // A clock byte can land BETWEEN the data bytes of a note-on. It must not corrupt it, and must
    // not clear running status.
    auto m = decode({ 0x90, 60, 0xF8, 100,      // clock between the data bytes
                            0xF8, 64, 100 });   // clock before a running-status pair
    CHECK(m.size() == 2, "real-time bytes are transparent to a message in progress");
    if (m.size() == 2) {
        CHECK(m[0].data1 == 60 && near(m[0].value, 100 / 127.0f), "interrupted note-on is intact");
        CHECK(m[1].data1 == 64, "running status survives a real-time byte");
    }
    // Real-time inside a sysex must not end it.
    auto s = decode({ 0xF0, 0x01, 0xF8, 0x02, 0xF7, 0x90, 60, 100 });
    CHECK(s.size() == 1 && s[0].kind == MidiKind::NoteOn, "real-time inside sysex does not end it");
}

void test_system_common_clears_running_status() {
    // Song position (0xF2) clears running status per spec; its data bytes are then discarded.
    auto m = decode({ 0x90, 60, 100, 0xF2, 0x10, 0x20, 64, 100 });
    CHECK(m.size() == 1, "system common clears running status; its data is discarded");
}

void test_split_across_packets() {
    // CoreMIDI delivers packets, not messages. A message split across two callbacks must still
    // decode — this is why the parser holds state rather than being a pure function per packet.
    MidiByteParser p;
    std::vector<MidiMsg> out;
    auto sink = [&](const MidiMsg& m) { out.push_back(m); };
    const uint8_t a[] = { 0x90, 60 };
    const uint8_t b[] = { 100, 64, 100 };       // completes the first note, then a running-status one
    p.feed(a, sizeof a, 1, sink);
    CHECK(out.empty(), "a half-received message emits nothing yet");
    p.feed(b, sizeof b, 2, sink);
    CHECK(out.size() == 2, "the message completes on the next packet");
    if (out.size() == 2) {
        CHECK(out[0].data1 == 60 && out[1].data1 == 64, "both notes decoded across the split");
        CHECK(out[0].host_time == 2, "a split message is stamped when it completes");
    }
}

void test_stray_data_and_reset() {
    auto m = decode({ 60, 100, 64, 100 });      // data bytes with no status ever armed
    CHECK(m.empty(), "data bytes with no running status are dropped, not guessed");

    MidiByteParser p;
    std::vector<MidiMsg> out;
    const uint8_t a[] = { 0x90, 60 };
    p.feed(a, sizeof a, 1, [&](const MidiMsg& m) { out.push_back(m); });
    p.reset();                                   // e.g. the device went away mid-message
    const uint8_t b[] = { 100 };
    p.feed(b, sizeof b, 2, [&](const MidiMsg& m) { out.push_back(m); });
    CHECK(out.empty(), "reset drops a partial message so it cannot merge with the next device");
}

}  // namespace

int main() {
    std::printf("test_midi_parse\n");
    test_notes();
    test_running_status();
    test_controllers();
    test_sysex_does_not_fabricate_notes();
    test_realtime_bytes_are_transparent();
    test_system_common_clears_running_status();
    test_split_across_packets();
    test_stray_data_and_reset();
    if (g_fail == 0) std::printf("ok   test_midi_parse — running status, sysex, real-time, split packets, controllers\n");
    return g_fail;
}
