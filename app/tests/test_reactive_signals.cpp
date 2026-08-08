// ADR-0053 Phase B5: the migration lane contract. A legacy audio→visual mapping migrates to a control
// edge whose src_lane is the index of the source's suffix in the Reactive op's signal table
// (reactive_signals.h). That only stays pixel-identical if the reactive signal grammar matches the
// audio→visual BRIDGE suffix grammar (bridge_source.h) byte-for-byte, in order. This test pins that
// equivalence, so a future edit to either table that would silently mis-route a migrated mapping (e.g.
// master.mid landing on the wrong lane) fails here instead. Pure/header-only — headless.
#include "operator_api/reactive_signals.h"
#include "app/bridge_source.h"
#include "test_helpers.h"

#include <string>

int main() {
    using namespace vivid::reactive;
    namespace B = vivid::bridge;

    // ReactiveMaster lanes [0, kMasterScalarCount) are the master-bus scalars (bridge master.<suffix>).
    for (int k = 0; k < kMasterScalarCount; ++k)
        CHECK(std::string(kMasterSignals[k]) == B::kTrackKindSuffixes[k]);
    // Lanes [kMasterScalarCount, MASTER_SIGNALS) are the transport signals (bridge transport.<suffix>).
    for (int k = 0; k < B::kNumTransportKinds; ++k)
        CHECK(std::string(kMasterSignals[kMasterScalarCount + k]) == B::kTransportKindSuffixes[k]);
    CHECK(kMasterScalarCount == 5);
    CHECK(kMasterScalarCount + B::kNumTransportKinds == VIVID_REACTIVE_MASTER_SIGNALS);

    // ReactiveTrack lanes [0, TRACK_SIGNALS) are the 8 track suffixes (bridge track_<id>.<suffix>).
    CHECK(B::kNumTrackKinds == VIVID_REACTIVE_TRACK_SIGNALS);
    for (int k = 0; k < B::kNumTrackKinds; ++k)
        CHECK(std::string(kTrackSignals[k]) == B::kTrackKindSuffixes[k]);

    // Spot-check the exact lane a few well-known legacy sources migrate to (the values other code + docs
    // assume): master.low -> 2, transport.beat -> 5, transport.beat_pulse -> 8, track.gate -> 7.
    CHECK(std::string(kMasterSignals[2]) == "low");
    CHECK(std::string(kMasterSignals[5]) == "beat");
    CHECK(std::string(kMasterSignals[8]) == "beat_pulse");
    CHECK(std::string(kTrackSignals[7]) == "gate");

    return vivid::test::summary("test_reactive_signals");
}
