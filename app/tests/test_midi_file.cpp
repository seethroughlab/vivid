// SMF read/write (midi/midi_file.h). The parser is the import path for third-party MIDI —
// drum-plugin grooves above all — so it meets bytes nobody in this repo wrote. The cases below are
// the ones real files actually exercise and a naive parser gets wrong: running status, sysex whose
// payload contains bytes that look like note-ons, repeated same-pitch notes, and notes left open at
// end of track.
#include "midi/midi_file.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vivid::session;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__); ++g_fail; } } while (0)

namespace {

// --- a tiny SMF builder, so each test states its bytes explicitly ---
struct TrackBuilder {
    std::vector<uint8_t> ev;
    void vlq(uint32_t x) { vivid::session::detail::push_vlq(ev, x); }
    void raw(std::initializer_list<uint8_t> bs) { for (uint8_t b : bs) ev.push_back(b); }
    void note_on(uint32_t dt, uint8_t pitch, uint8_t vel, uint8_t ch = 0) {
        vlq(dt); raw({ uint8_t(0x90 | ch), pitch, vel });
    }
    void note_off(uint32_t dt, uint8_t pitch, uint8_t ch = 0) {
        vlq(dt); raw({ uint8_t(0x80 | ch), pitch, 0 });
    }
    void end() { vlq(0); raw({ 0xFF, 0x2F, 0x00 }); }
};

std::vector<uint8_t> build_smf(const std::vector<TrackBuilder>& tracks, uint16_t ppq = 96,
                               uint16_t format = 1) {
    std::vector<uint8_t> out;
    out.push_back('M'); out.push_back('T'); out.push_back('h'); out.push_back('d');
    vivid::session::detail::push_be32(out, 6);
    vivid::session::detail::push_be16(out, format);
    vivid::session::detail::push_be16(out, uint16_t(tracks.size()));
    vivid::session::detail::push_be16(out, ppq);
    for (const TrackBuilder& t : tracks) {
        out.push_back('M'); out.push_back('T'); out.push_back('r'); out.push_back('k');
        vivid::session::detail::push_be32(out, uint32_t(t.ev.size()));
        out.insert(out.end(), t.ev.begin(), t.ev.end());
    }
    return out;
}

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

// --- tests ---

void test_basic_notes() {
    TrackBuilder t;                       // ppq 96: a quarter note = 96 ticks = 1.0 beat
    t.note_on(0, 60, 100);
    t.note_off(96, 60);
    t.note_on(0, 64, 80);
    t.note_off(192, 64);
    t.end();
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(build_smf({ t }), d, &err), "basic parse succeeds");
    CHECK(d.notes.size() == 2, "two notes");
    if (d.notes.size() == 2) {
        CHECK(d.notes[0].pitch == 60 && near(d.notes[0].start, 0.0) && near(d.notes[0].dur, 1.0),
              "first note at beat 0, one beat long");
        CHECK(near(d.notes[1].start, 1.0) && near(d.notes[1].dur, 2.0), "second note at beat 1, two beats");
        CHECK(std::fabs(d.notes[0].vel - 100.0f / 127.0f) < 1e-6, "velocity scaled to 0..1");
    }
    CHECK(near(d.length_beats, 3.0), "length is the last note end");
}

void test_running_status() {
    // Real keyboards and most exporters use running status: the 0x90 appears once and subsequent
    // events omit it. A parser that requires a status byte per event silently drops these.
    TrackBuilder t;
    t.vlq(0); t.raw({ 0x90, 60, 100 });     // explicit status
    t.vlq(0); t.raw({ 64, 100 });           // running status — same 0x90
    t.vlq(0); t.raw({ 67, 100 });
    t.vlq(96); t.raw({ 60, 0 });            // note-on vel 0 == note-off, still running status
    t.vlq(0); t.raw({ 64, 0 });
    t.vlq(0); t.raw({ 67, 0 });
    t.end();
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(build_smf({ t }), d, &err), "running-status parse succeeds");
    CHECK(d.notes.size() == 3, "running status yields all three chord notes");
    for (const MidiFileNote& n : d.notes)
        CHECK(near(n.start, 0.0) && near(n.dur, 1.0), "chord notes share start and duration");
}

void test_sysex_is_skipped_by_length() {
    // A sysex payload can contain any byte — including 0x90, which a parser that advances by a
    // fixed 3 bytes will re-read as a note-on and fabricate notes from. Skip by declared length.
    TrackBuilder t;
    t.vlq(0); t.raw({ 0xF0, 0x06, 0x7E, 0x90, 0x3C, 0x64, 0x90, 0xF7 });   // 6 payload bytes
    t.note_on(0, 60, 100);
    t.note_off(96, 60);
    t.end();
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(build_smf({ t }), d, &err), "sysex parse succeeds");
    CHECK(d.notes.size() == 1, "sysex payload does not fabricate notes");
}

void test_repeated_pitch_closes_most_recent() {
    // Two overlapping note-ons on the same pitch: the first note-off must close the LATER one
    // (a first-match scan closes the wrong note and reports the wrong durations).
    TrackBuilder t;
    t.note_on(0, 60, 100);
    t.note_on(48, 60, 90);
    t.note_off(48, 60);        // tick 96
    t.note_off(96, 60);        // tick 192
    t.end();
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(build_smf({ t }), d, &err), "repeated-pitch parse succeeds");
    CHECK(d.notes.size() == 2, "both same-pitch notes survive");
    if (d.notes.size() == 2) {
        // Sorted by start: [0] began at 0 and closes last (2.0 beats); [1] began at 0.5 (0.5 beats).
        CHECK(near(d.notes[0].start, 0.0) && near(d.notes[0].dur, 2.0), "outer note spans both offs");
        CHECK(near(d.notes[1].start, 0.5) && near(d.notes[1].dur, 0.5), "inner note closes first");
    }
}

void test_unterminated_note_closes_at_track_end() {
    TrackBuilder t;
    t.note_on(0, 60, 100);
    t.vlq(192); t.raw({ 0xFF, 0x2F, 0x00 });   // end of track with the note still held
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(build_smf({ t }), d, &err), "unterminated-note parse succeeds");
    CHECK(d.notes.size() == 1, "a note left open is kept, not dropped");
    if (d.notes.size() == 1) CHECK(near(d.notes[0].dur, 2.0), "closed at end of track");
}

void test_tempo_and_multitrack() {
    TrackBuilder tempo;                                     // format-1 conductor track
    tempo.vlq(0); tempo.raw({ 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 });   // 500000 us/qn = 120 bpm
    tempo.vlq(0); tempo.raw({ 0xFF, 0x03, 0x04, 'D','r','u','m' });    // track name
    tempo.end();
    TrackBuilder part;
    part.note_on(0, 36, 100);
    part.note_off(96, 36);
    part.end();
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(build_smf({ tempo, part }), d, &err), "multitrack parse succeeds");
    CHECK(d.format == 1 && d.ntracks == 2, "format/ntracks read from the header");
    CHECK(std::fabs(d.initial_bpm - 120.0) < 1e-6, "tempo meta read as 120 bpm");
    CHECK(d.notes.size() == 1 && d.notes[0].track == 1, "note attributed to its source track");
    CHECK(d.track_names.size() == 2 && d.track_names[0] == "Drum", "track name read");
}

void test_channel_preserved() {
    // GM drums live on channel 9 (channel 10 one-indexed); importers need it to route the kit.
    TrackBuilder t;
    t.note_on(0, 36, 100, /*ch*/9);
    t.note_off(96, 36, /*ch*/9);
    t.end();
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(build_smf({ t }), d, &err), "channel parse succeeds");
    CHECK(d.notes.size() == 1 && d.notes[0].channel == 9, "channel 9 preserved for drum routing");
}

void test_malformed_is_rejected_not_guessed() {
    MidiFileData d; std::string err;
    CHECK(!parse_midi_file({ 'n','o','p','e' }, d, &err), "non-SMF bytes rejected");
    CHECK(!err.empty(), "rejection carries a reason");

    // SMPTE division: negative division bit set. Mistiming this silently would be worse than refusing.
    std::vector<uint8_t> smpte = build_smf({ TrackBuilder{} });
    smpte[12] = 0xE7; smpte[13] = 0x28;
    CHECK(!parse_midi_file(smpte, d, &err), "SMPTE division rejected rather than mistimed");

    // A chunk claiming more bytes than the file holds.
    TrackBuilder t; t.note_on(0, 60, 100); t.note_off(96, 60); t.end();
    std::vector<uint8_t> trunc = build_smf({ t });
    trunc.resize(trunc.size() - 4);
    CHECK(!parse_midi_file(trunc, d, &err), "truncated chunk rejected");
}

void test_write_read_roundtrip() {
    std::vector<ClipNote> in;
    in.push_back(ClipNote{ 60, 0.0,  1.0,  0.80f, {} });
    in.push_back(ClipNote{ 64, 1.0,  0.5,  0.60f, {} });
    in.push_back(ClipNote{ 67, 1.5,  2.25, 1.00f, {} });
    in.push_back(ClipNote{ 36, 0.25, 0.125, 0.40f, {} });
    const std::vector<uint8_t> bytes = write_midi_bytes(in.data(), int(in.size()), 120.0);

    MidiFileData d; std::string err;
    CHECK(parse_midi_file(bytes, d, &err), "written file parses back");
    CHECK(d.notes.size() == in.size(), "round-trip preserves note count");
    CHECK(std::fabs(d.initial_bpm - 120.0) < 1e-6, "round-trip preserves tempo");
    for (const ClipNote& c : in) {
        bool found = false;
        for (const MidiFileNote& n : d.notes)
            if (n.pitch == c.pitch && near(n.start, c.start, 1e-3) && near(n.dur, c.dur, 1e-3)) {
                found = true;
                CHECK(std::fabs(n.vel - c.vel) < 0.01f, "round-trip preserves velocity");
                break;
            }
        if (!found) { std::printf("  FAIL note p=%d s=%f not found after round-trip\n", c.pitch, c.start); ++g_fail; }
    }
}

void test_write_retriggers_same_pitch_cleanly() {
    // Back-to-back same-pitch notes: if the note-off sorts after the next note-on at the same tick,
    // the off kills the note that just started and the second note vanishes.
    std::vector<ClipNote> in;
    in.push_back(ClipNote{ 38, 0.0, 1.0, 0.8f, {} });
    in.push_back(ClipNote{ 38, 1.0, 1.0, 0.8f, {} });
    MidiFileData d; std::string err;
    CHECK(parse_midi_file(write_midi_bytes(in.data(), 2, 120.0), d, &err), "adjacent-note parse");
    CHECK(d.notes.size() == 2, "adjacent same-pitch notes both survive the write");
}

}  // namespace

int main() {
    std::printf("test_midi_file\n");
    test_basic_notes();
    test_running_status();
    test_sysex_is_skipped_by_length();
    test_repeated_pitch_closes_most_recent();
    test_unterminated_note_closes_at_track_end();
    test_tempo_and_multitrack();
    test_channel_preserved();
    test_malformed_is_rejected_not_guessed();
    test_write_read_roundtrip();
    test_write_retriggers_same_pitch_cleanly();
    if (g_fail == 0) std::printf("ok   test_midi_file — parse/serialize SMF (running status, sysex, retrigger, round-trip)\n");
    return g_fail;
}
