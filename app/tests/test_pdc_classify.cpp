// Headless test for the ADR-0032 Phase E1.1 PDC alignment math (audio/pdc.h pdc_compute_delays): given
// per-track latency + a compensable flag, each compensable track is delayed by (L_max - latency) and
// L_max is taken over the COMPENSABLE SET ONLY. Pure — no session. This locks the load-bearing invariant:
// an unknown/live track (compensable=0) must neither be delayed NOR raise L_max (which would over-delay
// the rest and drift the post-master movie mix).
#include "audio/pdc.h"
#include "test_helpers.h"

#include <vector>

using vivid::audio::pdc_compute_delays;

int main() {
    // Two compensable tracks of differing latency → both align to the max; the max-latency track gets 0.
    {
        const int lat[] = { 100, 300 };
        const unsigned char comp[] = { 1, 1 };
        int d[2] = {};
        const int lmax = pdc_compute_delays(lat, comp, 2, d);
        CHECK(lmax == 300);
        CHECK(d[0] == 200);   // pulled 200 late to meet the 300-latency track
        CHECK(d[1] == 0);     // the reference (already the latest)
    }

    // An UNKNOWN/LIVE track (compensable=0) with the HIGHEST latency must NOT raise L_max and must not be
    // delayed — the other tracks align only to the compensable max.
    {
        const int lat[] = { 100, 300, 5000 };
        const unsigned char comp[] = { 1, 1, 0 };   // track 2 is unknown/live
        int d[3] = {};
        const int lmax = pdc_compute_delays(lat, comp, 3, d);
        CHECK(lmax == 300);   // NOT 5000 — the live track is excluded from L_max
        CHECK(d[0] == 200);
        CHECK(d[1] == 0);
        CHECK(d[2] == 0);     // the live track stays undelayed
    }

    // All-native / zero-latency (or nothing compensable) → all delays 0, L_max 0 (PDC is a no-op).
    {
        const int lat[] = { 0, 0, 0 };
        const unsigned char comp[] = { 1, 1, 1 };
        int d[3] = {};
        CHECK(pdc_compute_delays(lat, comp, 3, d) == 0);
        CHECK(d[0] == 0 && d[1] == 0 && d[2] == 0);
    }
    {
        const int lat[] = { 100, 300 };
        const unsigned char comp[] = { 0, 0 };   // none compensable (all unknown/live)
        int d[2] = {};
        CHECK(pdc_compute_delays(lat, comp, 2, d) == 0);
        CHECK(d[0] == 0 && d[1] == 0);
    }

    // A live track with lower latency than the compensable max is simply left live; compensable set drives.
    {
        const int lat[] = { 512, 64, 128 };
        const unsigned char comp[] = { 1, 0, 1 };   // track 1 live
        int d[3] = {};
        const int lmax = pdc_compute_delays(lat, comp, 3, d);
        CHECK(lmax == 512);
        CHECK(d[0] == 0);     // reference
        CHECK(d[1] == 0);     // live
        CHECK(d[2] == 384);   // 512 - 128
    }

    // Empty session → L_max 0, no writes.
    { int lmax = pdc_compute_delays(nullptr, nullptr, 0, nullptr); CHECK(lmax == 0); }

    return vivid::test::summary("test_pdc_classify");
}
