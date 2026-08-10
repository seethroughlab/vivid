// ADR-0053 Phase B: the reactive-signal bus round-trips. The host (frame thread) publishes master +
// per-track signals; a render-thread SOURCE op (ReactiveMaster / ReactiveTrack) pulls a lock-free
// snapshot. This proves the plumbing the ops depend on: master publish/read, per-track STABLE-ID
// addressing (a slot is found by its track id, not its position), slot freeing (track_id < 0), and the
// "not live -> 0 count" fallback that makes an edge fall back to its param base. Pure atomics, headless.
#include "operator_api/reactive_bus.h"
#include "test_helpers.h"

#include <cstring>

int main() {
    // --- Master round-trip ---------------------------------------------------------------------
    float in[VIVID_REACTIVE_MASTER_SIGNALS];
    for (int i = 0; i < VIVID_REACTIVE_MASTER_SIGNALS; ++i) in[i] = 0.1f * (i + 1);
    vivid_reactive_bus_publish_master(in, VIVID_REACTIVE_MASTER_SIGNALS);
    float out[VIVID_REACTIVE_MASTER_SIGNALS] = {0.f};
    uint32_t n = vivid_master_signals(out, VIVID_REACTIVE_MASTER_SIGNALS);
    CHECK(n == VIVID_REACTIVE_MASTER_SIGNALS);
    for (int i = 0; i < VIVID_REACTIVE_MASTER_SIGNALS; ++i) CHECK_NEAR(out[i], in[i], 1e-7);

    // Empty publish (nullptr) frees the count -> readers see 0 -> edges fall back to base.
    vivid_reactive_bus_publish_master(nullptr, 0);
    CHECK(vivid_master_signals(out, VIVID_REACTIVE_MASTER_SIGNALS) == 0);

    // --- Per-track STABLE-ID addressing --------------------------------------------------------
    float ta[VIVID_REACTIVE_TRACK_SIGNALS], tb[VIVID_REACTIVE_TRACK_SIGNALS];
    for (int i = 0; i < VIVID_REACTIVE_TRACK_SIGNALS; ++i) { ta[i] = 0.2f * i; tb[i] = 0.9f - 0.1f * i; }
    // Publish two tracks into ARBITRARY slots, tagged with non-contiguous stable ids.
    vivid_reactive_bus_publish_track(0, 7,  ta, VIVID_REACTIVE_TRACK_SIGNALS);
    vivid_reactive_bus_publish_track(1, 42, tb, VIVID_REACTIVE_TRACK_SIGNALS);

    float got[VIVID_REACTIVE_TRACK_SIGNALS] = {0.f};
    CHECK(vivid_track_signals(7, got, VIVID_REACTIVE_TRACK_SIGNALS) == VIVID_REACTIVE_TRACK_SIGNALS);
    for (int i = 0; i < VIVID_REACTIVE_TRACK_SIGNALS; ++i) CHECK_NEAR(got[i], ta[i], 1e-7);
    CHECK(vivid_track_signals(42, got, VIVID_REACTIVE_TRACK_SIGNALS) == VIVID_REACTIVE_TRACK_SIGNALS);
    for (int i = 0; i < VIVID_REACTIVE_TRACK_SIGNALS; ++i) CHECK_NEAR(got[i], tb[i], 1e-7);

    // A track id nobody published -> not live -> 0.
    CHECK(vivid_track_signals(99, got, VIVID_REACTIVE_TRACK_SIGNALS) == 0);

    // Free slot 0 (track_id < 0): id 7 is no longer live; id 42 (slot 1) is unaffected.
    vivid_reactive_bus_publish_track(0, -1, nullptr, 0);
    CHECK(vivid_track_signals(7, got, VIVID_REACTIVE_TRACK_SIGNALS) == 0);
    CHECK(vivid_track_signals(42, got, VIVID_REACTIVE_TRACK_SIGNALS) == VIVID_REACTIVE_TRACK_SIGNALS);

    // A moved track keeps its identity: re-publish id 42 into a DIFFERENT slot; still found by id.
    vivid_reactive_bus_publish_track(5, 42, ta, VIVID_REACTIVE_TRACK_SIGNALS);
    vivid_reactive_bus_publish_track(1, -1, nullptr, 0);   // vacate its old slot
    CHECK(vivid_track_signals(42, got, VIVID_REACTIVE_TRACK_SIGNALS) == VIVID_REACTIVE_TRACK_SIGNALS);
    for (int i = 0; i < VIVID_REACTIVE_TRACK_SIGNALS; ++i) CHECK_NEAR(got[i], ta[i], 1e-7);

    return 0;
}
