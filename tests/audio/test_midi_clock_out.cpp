// MidiClockOut accumulator math and transport edge-detection tests.
//
// Compiles midi_clock_out.cpp directly to access the test_capture_mode_ seam,
// which redirects sendMessage() calls into test_captured_ without requiring
// a real MIDI port.

#include "operator_api/operator.h"
#include "RtMidi.h"
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "test_helpers.h"

#include "../../operators/audio/midi_clock_out/midi_clock_out.cpp"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static VividAudioContext make_ctx(float bpm, uint32_t buffer_size,
                                   double beats_elapsed = 0.0) {
    VividAudioContext ctx{};
    ctx.sample_rate              = 48000;
    ctx.buffer_size              = buffer_size;
    ctx.metronome_bpm            = bpm;
    ctx.metronome_beats_elapsed  = beats_elapsed;
    return ctx;
}

static int count_clocks(const std::vector<std::vector<unsigned char>>& captured) {
    int n = 0;
    for (const auto& m : captured)
        if (m.size() == 1 && m[0] == 0xF8) ++n;
    return n;
}

static bool has_byte(const std::vector<std::vector<unsigned char>>& captured,
                     unsigned char b) {
    for (const auto& m : captured)
        if (m.size() == 1 && m[0] == b) return true;
    return false;
}

static bool has_msg(const std::vector<std::vector<unsigned char>>& captured,
                    std::initializer_list<unsigned char> bytes) {
    std::vector<unsigned char> target(bytes);
    for (const auto& m : captured)
        if (m == target) return true;
    return false;
}

// ---------------------------------------------------------------------------
// 1. 120 BPM, 48000 Hz, 1000-sample buffer → exactly 1 clock per call
//    interval = 48000 * 60 / (120 * 24) = 1000 samples exactly
// ---------------------------------------------------------------------------
static void test_exact_interval() {
    std::fprintf(stderr, "\n--- 120 BPM / 48kHz / 1000-sample buffer → 1 clock ---\n");
    MidiClockOut op;
    op.test_capture_mode_ = true;
    op.send_transport.value = 0.0f;
    op.enabled.value        = 1.0f;

    VividAudioContext ctx = make_ctx(120.0f, 1000);

    op.process_audio(&ctx);
    check(count_clocks(op.test_captured_) == 1, "first buffer: 1 clock");

    op.test_captured_.clear();
    op.process_audio(&ctx);
    check(count_clocks(op.test_captured_) == 1, "second buffer: 1 clock");
}

// ---------------------------------------------------------------------------
// 2. 120 BPM, 48000 Hz, 500-sample buffer → 1 clock every 2 calls
// ---------------------------------------------------------------------------
static void test_half_interval() {
    std::fprintf(stderr, "\n--- 120 BPM / 48kHz / 500-sample buffer → 1 clock per 2 calls ---\n");
    MidiClockOut op;
    op.test_capture_mode_ = true;
    op.send_transport.value = 0.0f;
    op.enabled.value        = 1.0f;

    VividAudioContext ctx = make_ctx(120.0f, 500);

    op.process_audio(&ctx);
    check(count_clocks(op.test_captured_) == 0, "first 500-sample buffer: no clock");

    op.process_audio(&ctx);
    check(count_clocks(op.test_captured_) == 1, "second 500-sample buffer: 1 clock total");
}

// ---------------------------------------------------------------------------
// 3. 240 BPM, 48000 Hz, 1000-sample buffer → 2 clocks per call
//    interval = 48000 * 60 / (240 * 24) = 500 samples
// ---------------------------------------------------------------------------
static void test_double_interval() {
    std::fprintf(stderr, "\n--- 240 BPM / 48kHz / 1000-sample buffer → 2 clocks ---\n");
    MidiClockOut op;
    op.test_capture_mode_ = true;
    op.send_transport.value = 0.0f;
    op.enabled.value        = 1.0f;

    VividAudioContext ctx = make_ctx(240.0f, 1000);

    op.process_audio(&ctx);
    check(count_clocks(op.test_captured_) == 2, "1000-sample buffer at 240 BPM: 2 clocks");
}

// ---------------------------------------------------------------------------
// 4. enabled = false → 0 messages
// ---------------------------------------------------------------------------
static void test_disabled_no_output() {
    std::fprintf(stderr, "\n--- enabled = false → no messages ---\n");
    MidiClockOut op;
    op.test_capture_mode_ = true;
    op.enabled.value        = 0.0f;

    VividAudioContext ctx = make_ctx(120.0f, 1000);

    op.process_audio(&ctx);
    check(op.test_captured_.empty(), "disabled: no messages");
}

// ---------------------------------------------------------------------------
// 5. enabled false→true at beats_elapsed = 0 → Start (0xFA) + SPP [0xF2,0,0]
// ---------------------------------------------------------------------------
static void test_transport_start() {
    std::fprintf(stderr, "\n--- enabled false→true at beat 0 → Start + SPP ---\n");
    MidiClockOut op;
    op.test_capture_mode_    = true;
    op.send_transport.value  = 1.0f;
    op.song_position.value   = 1.0f;
    op.enabled.value         = 0.0f;

    VividAudioContext ctx = make_ctx(120.0f, 1000, 0.0);

    // Call with enabled=false to settle prev_enabled_ = false
    op.process_audio(&ctx);
    op.test_captured_.clear();

    // Now enable → transition fires Start
    op.enabled.value = 1.0f;
    op.process_audio(&ctx);

    check(has_msg(op.test_captured_, {0xF2, 0, 0}), "SPP sent: [0xF2, 0, 0]");
    check(has_byte(op.test_captured_, 0xFA), "Start (0xFA) sent");
    check(count_clocks(op.test_captured_) >= 1, "clock(s) sent in same buffer");
}

// ---------------------------------------------------------------------------
// 6. enabled false→true at beats_elapsed = 2.0 → SPP + Continue (0xFB)
//    SPP = 2.0 * 4 = 8 MIDI beats → [0xF2, 8, 0]
// ---------------------------------------------------------------------------
static void test_transport_continue() {
    std::fprintf(stderr, "\n--- enabled false→true at beat 2 → Continue + SPP ---\n");
    MidiClockOut op;
    op.test_capture_mode_    = true;
    op.send_transport.value  = 1.0f;
    op.song_position.value   = 1.0f;
    op.enabled.value         = 0.0f;

    VividAudioContext ctx = make_ctx(120.0f, 1000, 0.0);

    op.process_audio(&ctx);
    op.test_captured_.clear();

    // Enable at beat 2 → spp = 2*4 = 8 → Continue
    op.enabled.value = 1.0f;
    ctx.metronome_beats_elapsed = 2.0;
    op.process_audio(&ctx);

    check(has_msg(op.test_captured_, {0xF2, 8, 0}), "SPP sent: [0xF2, 8, 0]");
    check(has_byte(op.test_captured_, 0xFB), "Continue (0xFB) sent");
}

// ---------------------------------------------------------------------------
// 7. enabled true→false → Stop (0xFC) + All Sound Off [0xB0, 120, 0]
// ---------------------------------------------------------------------------
static void test_transport_stop() {
    std::fprintf(stderr, "\n--- enabled true→false → Stop + All Sound Off ---\n");
    MidiClockOut op;
    op.test_capture_mode_    = true;
    op.send_transport.value  = 1.0f;
    op.song_position.value   = 1.0f;
    op.enabled.value         = 0.0f;

    VividAudioContext ctx = make_ctx(120.0f, 1000, 0.0);

    // Settle into disabled state
    op.process_audio(&ctx);
    op.test_captured_.clear();

    // Enable → running
    op.enabled.value = 1.0f;
    op.process_audio(&ctx);
    op.test_captured_.clear();

    // Disable → Stop + All Sound Off
    op.enabled.value = 0.0f;
    op.process_audio(&ctx);

    check(has_byte(op.test_captured_, 0xFC), "Stop (0xFC) sent");
    check(has_msg(op.test_captured_, {0xB0, 120, 0}), "All Sound Off [0xB0, 120, 0] sent");
    check(count_clocks(op.test_captured_) == 0, "no clock ticks on disable");
}

// ---------------------------------------------------------------------------
// 8. send_transport = false: transitions send no transport bytes
// ---------------------------------------------------------------------------
static void test_no_transport_messages() {
    std::fprintf(stderr, "\n--- send_transport = false: no 0xFA/0xFB/0xFC on transitions ---\n");
    MidiClockOut op;
    op.test_capture_mode_    = true;
    op.send_transport.value  = 0.0f;
    op.enabled.value         = 0.0f;

    VividAudioContext ctx = make_ctx(120.0f, 1000, 0.0);

    op.process_audio(&ctx);
    op.test_captured_.clear();

    op.enabled.value = 1.0f;
    op.process_audio(&ctx);

    check(!has_byte(op.test_captured_, 0xFA), "no Start with send_transport=false");
    check(!has_byte(op.test_captured_, 0xFB), "no Continue with send_transport=false");
    check(count_clocks(op.test_captured_) == 1, "clock still sent");

    op.test_captured_.clear();
    op.enabled.value = 0.0f;
    op.process_audio(&ctx);

    check(!has_byte(op.test_captured_, 0xFC), "no Stop with send_transport=false");
}

// ---------------------------------------------------------------------------
// 9. song_position = false: Start/Continue sent without preceding SPP
// ---------------------------------------------------------------------------
static void test_no_song_position() {
    std::fprintf(stderr, "\n--- song_position = false: no SPP before Start ---\n");
    MidiClockOut op;
    op.test_capture_mode_    = true;
    op.send_transport.value  = 1.0f;
    op.song_position.value   = 0.0f;
    op.enabled.value         = 0.0f;

    VividAudioContext ctx = make_ctx(120.0f, 1000, 0.0);

    op.process_audio(&ctx);
    op.test_captured_.clear();

    op.enabled.value = 1.0f;
    op.process_audio(&ctx);

    bool has_spp = false;
    for (const auto& m : op.test_captured_)
        if (m.size() == 3 && m[0] == 0xF2) has_spp = true;

    check(!has_spp, "no SPP when song_position = false");
    check(has_byte(op.test_captured_, 0xFA), "Start still sent without SPP");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::fprintf(stderr, "=== MidiClockOut accumulator math and transport edge detection ===\n");

    test_exact_interval();
    test_half_interval();
    test_double_interval();
    test_disabled_no_output();
    test_transport_start();
    test_transport_continue();
    test_transport_stop();
    test_no_transport_messages();
    test_no_song_position();

    std::fprintf(stderr, "\n=== Results: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
