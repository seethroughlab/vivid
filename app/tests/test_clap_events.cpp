// P4 Phase C: the CLAP event bytes. Two things here are easy to get backwards and impossible to
// notice by ear — the status nibble for each channel message, and the LSB-first order of a 14-bit
// pitch bend — so they are pinned rather than trusted. Also covers the note-expression path, which
// until this phase was never built at all: render_clap_instrument took notes and silently dropped
// every per-note bend/pressure/timbre curve on CLAP instruments.
#include "audio/clap_host.h"
#include "test_helpers.h"

#include <cmath>

using vivid::session::ClapEventScratch;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__); ++g_fail; } } while (0)

int main() {
    std::printf("test_clap_events\n");
    ClapEventScratch s;

    // --- header invariant: every union member must expose its header at offset 0, because
    // ev_get() hands back &evts[i].note.header whatever type was actually stored. ---
    s.clear();
    s.add_midi(0xB0, 1, 64, 7);
    const clap_event_header_t* h = ClapEventScratch::ev_get(&s.in, 0);
    CHECK(h->type == CLAP_EVENT_MIDI, "a MIDI event reads back as CLAP_EVENT_MIDI through ev_get");
    CHECK(h->time == 7, "sample offset survives (CLAP carries it natively, unlike the VST3 param path)");
    CHECK(h->size == sizeof(clap_event_midi_t), "header size matches the stored type");
    CHECK(h->space_id == CLAP_CORE_EVENT_SPACE_ID, "core event space");

    // --- control change ---
    s.clear();
    s.add_midi(uint8_t(0xB0 | 3), 74, 100, 0);
    const auto* cc = reinterpret_cast<const clap_event_midi_t*>(ClapEventScratch::ev_get(&s.in, 0));
    CHECK(cc->data[0] == 0xB3, "CC status carries the channel in the low nibble");
    CHECK(cc->data[1] == 74 && cc->data[2] == 100, "controller number and value");

    // --- channel pressure is a TWO-byte message: the third byte must stay 0 ---
    s.clear();
    s.add_midi(0xD0, 90, 0, 0);
    const auto* cp = reinterpret_cast<const clap_event_midi_t*>(ClapEventScratch::ev_get(&s.in, 0));
    CHECK(cp->data[0] == 0xD0 && cp->data[1] == 90 && cp->data[2] == 0, "channel pressure is 2 bytes");

    // --- pitch bend is 14-bit, LSB FIRST. Center (8192) must be 0x00,0x40. ---
    s.clear();
    const int center = 8192;
    s.add_midi(0xE0, uint8_t(center & 0x7F), uint8_t((center >> 7) & 0x7F), 0);
    const auto* pb = reinterpret_cast<const clap_event_midi_t*>(ClapEventScratch::ev_get(&s.in, 0));
    CHECK(pb->data[0] == 0xE0, "bend status");
    CHECK(pb->data[1] == 0x00 && pb->data[2] == 0x40, "bend center is LSB 0x00, MSB 0x40 (not swapped)");
    const int rebuilt = (int(pb->data[2]) << 7) | int(pb->data[1]);
    CHECK(rebuilt == center, "the 14-bit value reassembles");

    // --- note expression: CLAP TUNING is in SEMITONES, so a bend value passes through
    //     unconverted (the VST3 path needs semis/240 + 0.5 instead). ---
    s.clear();
    s.add_note_expression(CLAP_NOTE_EXPRESSION_TUNING, 42, 60, -3.5, 11);
    const auto* nx = reinterpret_cast<const clap_event_note_expression_t*>(ClapEventScratch::ev_get(&s.in, 0));
    CHECK(nx->header.type == CLAP_EVENT_NOTE_EXPRESSION, "note expression type");
    CHECK(nx->expression_id == CLAP_NOTE_EXPRESSION_TUNING, "tuning axis");
    CHECK(nx->note_id == 42 && nx->key == 60, "note id + key are carried (per-NOTE, not per-channel)");
    CHECK(std::fabs(nx->value - (-3.5)) < 1e-9, "semitones pass through unconverted");
    CHECK(nx->header.time == 11, "expression keeps its sample offset");

    // --- mixed stream: notes, expression and MIDI coexist in one list ---
    s.clear();
    s.add_note(true, 60, 0.8, 1, 0);
    s.add_note_expression(CLAP_NOTE_EXPRESSION_PRESSURE, 1, 60, 0.5, 0);
    s.add_midi(0xB0, 1, 127, 0);
    CHECK(ClapEventScratch::ev_size(&s.in) == 3, "three events queued");
    CHECK(ClapEventScratch::ev_get(&s.in, 0)->type == CLAP_EVENT_NOTE_ON, "0 = note on");
    CHECK(ClapEventScratch::ev_get(&s.in, 1)->type == CLAP_EVENT_NOTE_EXPRESSION, "1 = expression");
    CHECK(ClapEventScratch::ev_get(&s.in, 2)->type == CLAP_EVENT_MIDI, "2 = midi");

    if (g_fail == 0) std::printf("ok   test_clap_events — MIDI status/14-bit bend byte order, note expression, mixed stream\n");
    return g_fail;
}
