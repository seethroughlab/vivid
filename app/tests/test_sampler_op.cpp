// AO-4: the native Sampler instrument operator plays one PCM slice per note (drum-rack /
// slicer). This proves (a) loading PCM + slice regions via the SamplerLoadable escape hatch,
// (b) note `base_note+k` triggers slice k (ascending pitches → ascending slices), and (c)
// steady-state processing performs ZERO heap allocations (program-global operator-new counter).
#include "audio/audio_op_runtime.h"
#include "audio/builtin_audio_ops.h"
#include "gpu/op_runtime.h"
#include "midi/midi_clip.h"
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
void* operator new[](std::size_t n) { return operator new(n); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace vivid;

int main() {
    OpRegistry reg;
    register_builtin_audio_ops(reg);

    AudioOp* smp = audio_op_create(reg, "Sampler");
    CHECK(smp != nullptr);
    CHECK(audio_op_is_source(smp));        // instrument = source (no audio input)

    // Build PCM with three constant-valued slices so a slice's identity shows in the output:
    // slice 0 = +0.5, slice 1 = -0.25, slice 2 = +1.0. 100 samples each.
    const uint32_t SLICE = 100, sr = 48000;
    std::vector<float> L, R;
    const float vals[3] = { 0.5f, -0.25f, 1.0f };
    for (float v : vals) for (uint32_t i = 0; i < SLICE; ++i) { L.push_back(v); R.push_back(v); }
    const uint32_t starts[3] = { 0, SLICE, 2 * SLICE };
    const uint32_t ends[3]   = { SLICE, 2 * SLICE, 3 * SLICE };
    const int base = 36;
    CHECK(audio_op_load_sampler(smp, L.data(), R.data(), L.size(), sr, starts, ends, 3, base));
    AudioOp* tone = audio_op_create(reg, "TestTone");
    CHECK(!audio_op_load_sampler(tone, L.data(), R.data(), L.size(), sr, starts, ends, 3, base));  // not a sampler
    audio_op_destroy(tone);

    const uint32_t frames = 64;
    std::vector<float> outL(frames), outR(frames);

    // Trigger slice k via note base+k; the first output sample must equal that slice's value
    // (gain 1, velocity 1). Reload before each to clear any still-ringing one-shot voice so the
    // slices are isolated (reloading also resets voices — part of the load contract).
    for (int k = 0; k < 3; ++k) {
        audio_op_load_sampler(smp, L.data(), R.data(), L.size(), sr, starts, ends, 3, base);
        session::NoteEvent on{ 0u, true, static_cast<int16_t>(base + k), 1.0f, 100 + k, 0.f };
        audio_op_process(smp, outL.data(), outR.data(), frames, sr, 120.f, 4, 0.0, &on, 1);
        CHECK(std::fabs(outL[0] - vals[k]) < 1e-4f);
        CHECK(std::fabs(outR[0] - vals[k]) < 1e-4f);
    }

    // A note outside the slice range is silent (no voice). Reload first to clear prior voices.
    {
        audio_op_load_sampler(smp, L.data(), R.data(), L.size(), sr, starts, ends, 3, base);
        session::NoteEvent on{ 0u, true, static_cast<int16_t>(base + 9), 1.0f, 200, 0.f };
        audio_op_process(smp, outL.data(), outR.data(), frames, sr, 120.f, 4, 0.0, &on, 1);
        float peak = 0.f; for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(outL[i]));
        CHECK(peak < 1e-6f);
    }

    // Steady state: zero heap allocations across many blocks (note churn + param sets).
    g_count = true;
    for (int b = 0; b < 200; ++b) {
        session::NoteEvent on{ 0u, true, static_cast<int16_t>(base + (b % 3)), 1.0f, 300 + b, 0.f };
        audio_op_process(smp, outL.data(), outR.data(), frames, sr, 120.f, 4, b * 0.25, &on, 1);
        audio_op_param_set(smp, 1, 0.5f + 0.5f * static_cast<float>(b % 2));   // vary gain (RT-safe)
    }
    g_count = false;
    CHECK(g_allocs.load() == 0);

    audio_op_destroy(smp);
    return vivid::test::summary("test_sampler_op");
}
