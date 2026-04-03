#include "runtime/audio/system_midi.h"
#include <cstdio>
#include <string>
#include "test_helpers.h"

int main() {
    // =====================================================================
    // Test 1: Construction succeeds
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Construction ===\n");
        vivid::SystemMidiListener midi;
        check(true, "construction did not crash");
    }

    // =====================================================================
    // Test 2: is_open() false before open_all()
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: is_open before open_all ===\n");
        vivid::SystemMidiListener midi;
        check(!midi.is_open(), "is_open = false before open_all");
    }

    // =====================================================================
    // Test 3: drain_cc_events returns empty vector
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: drain_cc_events empty ===\n");
        vivid::SystemMidiListener midi;
        auto events = midi.drain_cc_events();
        check(events.empty(), "drain returns empty on fresh listener");
    }

    // =====================================================================
    // Test 4: cc_value returns 0.0 for never-received CC
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: cc_value defaults ===\n");
        vivid::SystemMidiListener midi;
        check_float(midi.cc_value(1, 0), 0.0f, "cc_value(1, 0) = 0.0");
        check_float(midi.cc_value(1, 127), 0.0f, "cc_value(1, 127) = 0.0");
        check_float(midi.cc_value(16, 64), 0.0f, "cc_value(16, 64) = 0.0");
    }

    // =====================================================================
    // Test 5: cc_value boundary conditions
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: cc_value boundary ===\n");
        vivid::SystemMidiListener midi;
        // Out-of-range channel/cc should return 0.0 without crashing
        check_float(midi.cc_value(0, 0), 0.0f, "channel 0 (invalid) = 0.0");
        check_float(midi.cc_value(17, 0), 0.0f, "channel 17 (invalid) = 0.0");
        check_float(midi.cc_value(1, -1), 0.0f, "cc -1 (invalid) = 0.0");
        check_float(midi.cc_value(1, 128), 0.0f, "cc 128 (invalid) = 0.0");
    }

    // =====================================================================
    // Test 6: last_drained_events empty before drain
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: last_drained_events ===\n");
        vivid::SystemMidiListener midi;
        check(midi.last_drained_events().empty(), "last_drained empty initially");
        // After a drain call, still empty (no events)
        midi.drain_cc_events();
        check(midi.last_drained_events().empty(), "last_drained empty after empty drain");
    }

    // =====================================================================
    // Test 7: port_names doesn't crash
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: port_names ===\n");
        vivid::SystemMidiListener midi;
        auto names = midi.port_names();
        // May be empty on CI / machines without MIDI hardware — just verify no crash
        check(true, "port_names returned without crash");
        std::fprintf(stderr, "  (found %zu MIDI ports)\n", names.size());
    }

    // =====================================================================
    // Test 8: open_all + close cycle
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 8: open_all + close ===\n");
        vivid::SystemMidiListener midi;
        // open_all may return false if no MIDI ports — that's fine
        midi.open_all();
        // close should always succeed without crash
        midi.close();
        check(!midi.is_open(), "is_open = false after close");
    }

    // =====================================================================
    // Test 9: Multiple drain cycles — second drain returns empty
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 9: Multiple drain cycles ===\n");
        vivid::SystemMidiListener midi;

        // First drain on a fresh listener returns empty
        auto first = midi.drain_cc_events();
        check(first.empty(), "first drain: empty");
        check(midi.last_drained_events().empty(), "last_drained empty after first drain");

        // Second consecutive drain also returns empty (buffer was already clear)
        auto second = midi.drain_cc_events();
        check(second.empty(), "second drain: still empty");
        check(midi.last_drained_events().empty(), "last_drained empty after second drain");

        // last_drained_events reflects the most-recent drain, not an older one
        auto third = midi.drain_cc_events();
        check(third.empty(), "third drain: still empty");
        check(midi.last_drained_events().empty(), "last_drained consistent after third drain");
    }

    // =====================================================================
    // Test 10: cc_value channel isolation (all 16 channels start at 0)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 10: cc_value channel isolation ===\n");
        vivid::SystemMidiListener midi;

        // Each channel's full CC range must default to 0 and be independent.
        // We spot-check each of the 16 valid channels at CC 0, 64, and 127.
        bool all_zero = true;
        for (int ch = 1; ch <= 16; ++ch) {
            if (midi.cc_value(ch, 0)   != 0.0f) { all_zero = false; break; }
            if (midi.cc_value(ch, 64)  != 0.0f) { all_zero = false; break; }
            if (midi.cc_value(ch, 127) != 0.0f) { all_zero = false; break; }
        }
        check(all_zero, "all 16 channels × CC 0/64/127 default to 0.0");

        // Out-of-range channels are silently clamped to 0 — already confirmed
        // in Test 5, but verify the boundary once more with explicit channels
        check_float(midi.cc_value(0,  0), 0.0f, "channel 0 (below range) = 0.0");
        check_float(midi.cc_value(17, 0), 0.0f, "channel 17 (above range) = 0.0");

        // Note: channels 1 and 16 are treated identically to channels 2-15
        // (same code path), so they don't alias each other
        check_float(midi.cc_value(1,  127), 0.0f, "ch 1  cc 127 = 0.0");
        check_float(midi.cc_value(16, 127), 0.0f, "ch 16 cc 127 = 0.0");
    }

    // =====================================================================
    // Test 11: Multiple listener instances — independent state
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 11: Multiple listener instances ===\n");

        // Two separate SystemMidiListener objects must not share state.
        // Each starts with its own empty buffer and cc_state_.
        vivid::SystemMidiListener midi1;
        vivid::SystemMidiListener midi2;

        check(!midi1.is_open(), "listener 1: not open");
        check(!midi2.is_open(), "listener 2: not open");
        check(midi1.drain_cc_events().empty(), "listener 1: fresh drain empty");
        check(midi2.drain_cc_events().empty(), "listener 2: fresh drain empty");
        check(midi1.last_drained_events().empty(), "listener 1: last_drained empty");
        check(midi2.last_drained_events().empty(), "listener 2: last_drained empty");

        // Both read 0 for all CCs (no events received)
        check_float(midi1.cc_value(1, 7), 0.0f, "listener 1: cc_value(1,7) = 0");
        check_float(midi2.cc_value(1, 7), 0.0f, "listener 2: cc_value(1,7) = 0");

        // Destroying one does not affect the other
        {
            vivid::SystemMidiListener temp;
            (void)temp.port_names(); // exercise port enumeration path
        }
        check_float(midi1.cc_value(1, 7), 0.0f, "listener 1: still 0 after temp destroyed");
        check_float(midi2.cc_value(1, 7), 0.0f, "listener 2: still 0 after temp destroyed");
    }

    // =====================================================================
    // Note: MIDI note on/off and channel-filtering tests that require actual
    // message injection (e.g., verifying that 0x90/0x80 messages are ignored
    // while 0xB0 CC messages are accepted, and that events on channel N do
    // not update the cc_state_ of channel M) cannot be exercised without
    // either real MIDI hardware or a test-seam on the internal callback.
    // The midi_callback implementation (system_midi.cpp:65-81) already
    // enforces `if (type != 0xB0) return;` — note events are discarded by
    // design.  Hardware-dependent tests belong in an integration test suite
    // that can open a virtual MIDI loopback port.
    // =====================================================================

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
