// ADR-0022 / P0.5: the LFO — the first MODULATOR. It is the proof that control is a real signal in
// the graph: no audio in, no audio out, a normalized 0..1 stream out of `control_out` (ABI v13).
//
// This is the audio peer of test_arp_op: an LFO wired to nothing is unobservable, so this is what
// makes ADR-0022's control edges audible — and what makes a regression here detectable. It also
// pins the two contracts a control SOURCE owes the model:
//   1. output is 0..1, always. Polarity/depth/offset are the EDGE's business (ControlShape), not
//      the source's — see control_resolve() in audio/audio_graph.h.
//   2. phase is continuous ACROSS blocks (free) or locked to the transport (sync). A modulator
//      that resets each block is a buzz, not an LFO.
#include "audio/audio_op_runtime.h"
#include "audio/builtin_audio_ops.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

static std::atomic<long> g_allocs{ 0 };
static bool g_count = false;

void* operator new(std::size_t n) {
    if (g_count) g_allocs.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

constexpr uint32_t kSR = 48000;
constexpr uint32_t kBlock = 256;
constexpr float    kBpm = 120.f;   // 0.5 s per beat

// One block of the LFO. `beats` is the transport position (only Sync reads it).
void run_block(vivid::AudioOp* op, std::vector<float>& ctl, double beats = 0.0) {
    std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
    vivid::audio_op_process(op, L.data(), R.data(), kBlock, kSR, kBpm, 4, beats,
                            nullptr, 0, nullptr, 0, nullptr, nullptr, 0,
                            ctl.data(), static_cast<uint32_t>(ctl.size()));
}

int param_index(vivid::AudioOp* op, const char* name) {
    for (int i = 0; i < vivid::audio_op_param_count(op); ++i) {
        const char* n = vivid::audio_op_param_name(op, i);
        if (n && std::string(n) == name) return i;
    }
    return -1;
}

}  // namespace

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);

    // --- 1. An LFO is NOT an instrument and NOT an effect ---
    // It has no audio input, so the descriptor-based classifier calls it a source — which would
    // offer it in the instrument picker, where wiring it to Output makes a DC-ish thud. The
    // registration mark is the escape hatch (the same one ADR-0015's note effects use).
    {
        CHECK(vivid::audio_op_is_mod_op("LFO"));
        CHECK(!vivid::audio_op_is_note_op("LFO"));
        CHECK(vivid::audio_mod_op_count(reg) >= 1);

        bool listed_as_instrument = false, listed_as_effect = false;
        for (int i = 0; i < vivid::audio_op_registry_count(reg, true); ++i)
            if (std::string(vivid::audio_op_registry_name(reg, true, i)) == "LFO") listed_as_instrument = true;
        for (int i = 0; i < vivid::audio_op_registry_count(reg, false); ++i)
            if (std::string(vivid::audio_op_registry_name(reg, false, i)) == "LFO") listed_as_effect = true;
        CHECK(!listed_as_instrument);
        CHECK(!listed_as_effect);

        bool in_mod_list = false;
        for (int i = 0; i < vivid::audio_mod_op_count(reg); ++i)
            if (std::string(vivid::audio_mod_op_name(reg, i)) == "LFO") in_mod_list = true;
        CHECK(in_mod_list);   // ...it appears in the modulator list instead
    }

    // --- 2. Output is normalized 0..1 across every waveform ---
    // The contract control_resolve() relies on. A source that emits -1..+1 would be clamped and
    // silently lose half its swing, so it is pinned here rather than discovered later.
    {
        vivid::AudioOp* op = vivid::audio_op_create(reg, "LFO");
        CHECK(op != nullptr);
        const int wf = param_index(op, "waveform");
        const int rt = param_index(op, "rate");
        CHECK(wf >= 0 && rt >= 0);
        vivid::audio_op_param_set(op, rt, 20.f);   // fast: sweep the whole cycle in few blocks

        std::vector<float> ctl(kBlock, -99.f);
        for (int w = 0; w <= 4; ++w) {
            vivid::audio_op_param_set(op, wf, static_cast<float>(w));
            float lo = 2.f, hi = -1.f;
            for (int b = 0; b < 200; ++b) {
                run_block(op, ctl);
                for (float v : ctl) { lo = std::min(lo, v); hi = std::max(hi, v); }
            }
            CHECK(lo >= 0.f);
            CHECK(hi <= 1.f);
            CHECK(hi > lo);        // ...and it actually MOVES (a stuck LFO passes a range check)
        }
        vivid::audio_op_destroy(op);
    }

    // --- 3. Free-running phase is continuous ACROSS blocks ---
    // The bug this catches: recomputing phase from 0 each block. Every block would start at the
    // same value, so the "LFO" becomes a buzz at the block rate. A within-block check cannot see
    // it — only comparing across a block boundary can.
    {
        vivid::AudioOp* op = vivid::audio_op_create(reg, "LFO");
        const int wf = param_index(op, "waveform");
        const int rt = param_index(op, "rate");
        vivid::audio_op_param_set(op, wf, 2.f);    // Saw: phase IS the output, so it reads directly
        vivid::audio_op_param_set(op, rt, 1.f);    // 1 Hz => 48000 samples/cycle; a block is 256

        std::vector<float> ctl(kBlock, 0.f);
        run_block(op, ctl);
        const float first_end = ctl[kBlock - 1];
        run_block(op, ctl);
        const float second_start = ctl[0];
        CHECK(second_start > first_end);                       // carried on, did not restart
        CHECK(std::fabs(second_start - first_end) < 0.01f);    // ...and by ONE sample, not a jump
        CHECK(ctl[0] < ctl[kBlock - 1]);                       // still rising within the block
        vivid::audio_op_destroy(op);
    }

    // --- 4. Sync locks to the transport, not to elapsed blocks ---
    // Phase is read off `beats_elapsed`, so the same transport position gives the same value no
    // matter how many blocks have run — that is what "locked to the bar" means, and it is why
    // sync must NOT integrate its own phase.
    {
        vivid::AudioOp* op = vivid::audio_op_create(reg, "LFO");
        const int wf = param_index(op, "waveform");
        const int sy = param_index(op, "sync");
        const int dv = param_index(op, "division");
        CHECK(sy >= 0 && dv >= 0);
        vivid::audio_op_param_set(op, wf, 2.f);    // Saw
        vivid::audio_op_param_set(op, sy, 1.f);    // Sync
        vivid::audio_op_param_set(op, dv, 2.f);    // "1/4" = 1 beat per cycle

        std::vector<float> ctl(kBlock, 0.f);
        run_block(op, ctl, 0.25);                  // a quarter of the way through the beat
        const float at_quarter = ctl[0];
        for (int i = 0; i < 37; ++i) run_block(op, ctl, 3.7);   // wander off elsewhere
        run_block(op, ctl, 0.25);                  // ...and come back to the SAME transport spot
        CHECK(std::fabs(ctl[0] - at_quarter) < 1e-4f);          // same position => same phase

        run_block(op, ctl, 0.0);
        const float at_zero = ctl[0];
        run_block(op, ctl, 1.0);                   // exactly one beat later = one full cycle
        CHECK(std::fabs(ctl[0] - at_zero) < 1e-4f);
        CHECK(std::fabs(at_quarter - 0.25f) < 1e-3f);           // 1/4 beat into a 1-beat saw
        vivid::audio_op_destroy(op);
    }

    // --- 5. An LFO wired to nothing costs nothing, and emits no AUDIO ever ---
    {
        vivid::AudioOp* op = vivid::audio_op_create(reg, "LFO");
        std::vector<float> L(kBlock, 0.5f), R(kBlock, 0.5f);
        // No control_out: the op must bail rather than compute a block for a buffer no one reads.
        vivid::audio_op_process(op, L.data(), R.data(), kBlock, kSR, kBpm, 4, 0.0, nullptr, 0);
        for (uint32_t i = 0; i < kBlock; ++i) { CHECK(L[i] == 0.f); CHECK(R[i] == 0.f); }  // silence
        vivid::audio_op_destroy(op);
    }

    // --- 6. Zero heap allocations on the audio thread ---
    {
        vivid::AudioOp* op = vivid::audio_op_create(reg, "LFO");
        const int wf = param_index(op, "waveform");
        std::vector<float> ctl(kBlock, 0.f);
        std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
        run_block(op, ctl);   // warm-up (not counted)

        g_count = true;
        for (int w = 0; w <= 4; ++w) {
            vivid::audio_op_param_set(op, wf, static_cast<float>(w));
            for (int b = 0; b < 100; ++b)
                vivid::audio_op_process(op, L.data(), R.data(), kBlock, kSR, kBpm, 4, b * 0.01,
                                        nullptr, 0, nullptr, 0, nullptr, nullptr, 0,
                                        ctl.data(), static_cast<uint32_t>(ctl.size()));
        }
        g_count = false;
        CHECK(g_allocs.load() == 0);
        vivid::audio_op_destroy(op);
    }

    // --- 7. control_out is zeroed for an op that ignores it (every op built before v13) ---
    // The ABI is additive: a v12 operator never touches control_out. If the host handed it last
    // block's signal, a non-modulator wired by mistake would emit stale garbage rather than
    // nothing.
    {
        vivid::AudioOp* op = vivid::audio_op_create(reg, "Bitcrush");   // an ordinary v12-era effect
        CHECK(op != nullptr);
        std::vector<float> ctl(kBlock, 0.77f);                          // pre-dirtied
        std::vector<float> L(kBlock, 0.1f), R(kBlock, 0.1f);
        vivid::audio_op_process(op, L.data(), R.data(), kBlock, kSR, kBpm, 4, 0.0,
                                nullptr, 0, nullptr, 0, nullptr, nullptr, 0,
                                ctl.data(), static_cast<uint32_t>(ctl.size()));
        for (float v : ctl) CHECK(v == 0.f);
        vivid::audio_op_destroy(op);
    }

    // --- 8. A param override drives the op WITHOUT destroying its base ---
    // The heart of ADR-0022's control model, at the runtime seam: the op sees the modulated value;
    // audio_op_param_get() still reports the user's knob. Verified on a real DSP op by ear-proxy —
    // the filter must actually sound different — not just by reading the value back.
    {
        vivid::AudioOp* op = vivid::audio_op_create(reg, "SVFilter");
        CHECK(op != nullptr);
        const int cut = param_index(op, "cutoff");
        CHECK(cut >= 0);
        vivid::audio_op_param_set(op, cut, 8000.f);   // the user's base: bright

        auto rms_through = [&](const vivid::AudioOpParamOverride* ov, uint32_t n) {
            std::vector<float> L(kBlock), R(kBlock);
            double acc = 0;
            for (int b = 0; b < 40; ++b) {            // let the filter settle
                for (uint32_t i = 0; i < kBlock; ++i) {   // a bright-ish square-y source
                    const float s = ((b * kBlock + i) % 48) < 24 ? 0.5f : -0.5f;
                    L[i] = s; R[i] = s;
                }
                vivid::audio_op_process(op, L.data(), R.data(), kBlock, kSR, kBpm, 4, 0.0,
                                        nullptr, 0, nullptr, 0, nullptr, ov, n);
                if (b >= 30) for (uint32_t i = 0; i < kBlock; ++i) acc += double(L[i]) * L[i];
            }
            return std::sqrt(acc / (10.0 * kBlock));
        };

        const double open = rms_through(nullptr, 0);
        vivid::AudioOpParamOverride ov{ cut, 200.f };            // modulation slams it closed
        const double closed = rms_through(&ov, 1);
        CHECK(closed < open * 0.7);                              // the op HEARD the override
        CHECK(std::fabs(vivid::audio_op_param_get(op, cut) - 8000.f) < 1e-3f);   // base survived

        const double reopened = rms_through(nullptr, 0);         // stop modulating...
        CHECK(reopened > closed * 1.3);                          // ...and the base is still there
        CHECK(std::fabs(vivid::audio_op_param_get(op, cut) - 8000.f) < 1e-3f);

        vivid::AudioOpParamOverride bad[] = { { -1, 1.f }, { 9999, 1.f } };   // out-of-range: ignored
        rms_through(bad, 2);
        CHECK(std::fabs(vivid::audio_op_param_get(op, cut) - 8000.f) < 1e-3f);
        vivid::audio_op_destroy(op);
    }

    return vivid::test::summary("test_lfo_op");
}
