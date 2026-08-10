// Headless test for the ADR-0032 Phase E1 PDC delay-ring primitive (audio/pdc.h): a fixed
// read-behind-write stereo delay. Pure — no audio engine — so it links in the deps-free suite. Proves the
// modulo/wrap arithmetic delays a signal by exactly D samples for any legal delay and any (varying) block
// size, including across a ring wrap. This is the whole RT-critical math of E1, isolated.
#include "audio/pdc.h"
#include "test_helpers.h"

#include <cstdint>
#include <vector>

using vivid::audio::kPdcRingCap;
using vivid::audio::kPdcMaxComp;
using vivid::audio::pdc_delay_accumulate;

// Run the whole input timeline through the ring at delay D with a varying block schedule and a mix scale,
// then assert every output sample equals scale * input[n-D] (0 before the signal reaches the tap). The
// input is a distinct ramp per channel so any off-by-one or channel swap is caught.
static bool run_case(uint32_t D, float scale) {
    const uint32_t T = kPdcMaxComp + 8192;   // long enough to exceed the max delay AND wrap the ring
    std::vector<float> inL(T), inR(T), outL(T, 0.f), outR(T, 0.f);
    for (uint32_t n = 0; n < T; ++n) { inL[n] = static_cast<float>(n + 1); inR[n] = -static_cast<float>(n + 1); }

    std::vector<float> ringL(kPdcRingCap, 0.f), ringR(kPdcRingCap, 0.f);
    uint32_t w = 0;
    // A deliberately irregular block schedule that exercises frames < D, == D, > D, and the wrap.
    const uint32_t sched[] = { 512, 4096, 63, 1000, 4096, 1, 337, 4096, 2048, 4095 };
    size_t si = 0;
    std::vector<float> blk(2 * 4096);   // scratch interleaved out for one block
    for (uint32_t pos = 0; pos < T; ) {
        uint32_t frames = sched[si % (sizeof(sched) / sizeof(sched[0]))]; si++;
        if (pos + frames > T) frames = T - pos;
        if (frames == 0) break;
        for (uint32_t i = 0; i < 2 * frames; ++i) blk[i] = 0.f;   // helper accumulates (+=)
        w = pdc_delay_accumulate(ringL.data(), ringR.data(), w, D,
                                 inL.data() + pos, inR.data() + pos, blk.data(), scale, frames);
        for (uint32_t i = 0; i < frames; ++i) { outL[pos + i] = blk[2 * i]; outR[pos + i] = blk[2 * i + 1]; }
        pos += frames;
    }

    bool ok = true;
    for (uint32_t n = 0; n < T; ++n) {
        const float expL = (n >= D) ? scale * inL[n - D] : 0.f;
        const float expR = (n >= D) ? scale * inR[n - D] : 0.f;
        if (outL[n] != expL || outR[n] != expR) { ok = false; break; }
    }
    return ok;
}

int main() {
    // Delay sweep: 0 (passthrough through the ring), small, around a representative block size, and the cap.
    const uint32_t delays[] = { 0, 1, 2, 511, 512, 513, 100, 4095, 4096, 4097, 60000, kPdcMaxComp };
    for (uint32_t d : delays) {
        CHECK(run_case(d, 1.0f));
        CHECK(run_case(d, 0.5f));   // scale is applied to the delayed sample
    }

    // Constants sanity: the reserve covers one max audio block (the no-alias guarantee).
    CHECK(kPdcMaxComp == kPdcRingCap - 4096);
    CHECK((kPdcRingCap & (kPdcRingCap - 1)) == 0u);   // power of two → mask modulo valid

    return vivid::test::summary("test_pdc_ring");
}
