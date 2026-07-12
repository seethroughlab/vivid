// AO-1: the native audio-operator runtime must be RT-safe. This proves (a) an instrument
// operator renders audio from note events and (b) processing a native instrument + effect
// across many blocks performs ZERO heap allocations (a program-global operator-new counter,
// gated so create/warm-up allocations don't count).
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

    AudioOp* inst = audio_op_create(reg, "TestTone");
    AudioOp* fx   = audio_op_create(reg, "Bitcrush");
    AudioOp* filt = audio_op_create(reg, "SVFilter");
    CHECK(inst != nullptr);
    CHECK(fx != nullptr);
    CHECK(filt != nullptr);
    CHECK(audio_op_is_source(inst));      // instrument = source (no audio input)
    CHECK(!audio_op_is_source(fx));       // bitcrush = effect (has audio input)
    CHECK(!audio_op_is_source(filt));     // filter = effect (has audio input)
    CHECK(audio_op_create(reg, "Plasma") == nullptr);   // a visual op is not an audio op

    const uint32_t frames = 256, sr = 48000;
    std::vector<float> L(frames), R(frames);

    // --- SVFilter frequency response: LP passes lows/blocks highs; HP does the reverse. ---
    // Run a continuous sine through the filter and measure steady-state output RMS relative
    // to the unit-sine input RMS (sqrt(0.5)). Deterministic — no randomness.
    auto band_ratio = [&](float freq, int type, float cutoff) -> float {
        audio_op_param_set(filt, 0, static_cast<float>(type));   // type: 0 LP / 1 HP / 2 BP
        audio_op_param_set(filt, 1, cutoff);                     // cutoff Hz
        audio_op_param_set(filt, 2, 0.1f);                       // resonance
        const double w = 2.0 * 3.14159265358979323846 * freq / sr;
        double ph = 0.0, out_sumsq = 0.0; long cnt = 0;
        for (int b = 0; b < 40; ++b) {                           // 30-block warm-up settles the state
            for (uint32_t i = 0; i < frames; ++i) {
                const float s = static_cast<float>(std::sin(ph));
                L[i] = s; R[i] = s; ph += w; if (ph > 2 * 3.14159265358979323846) ph -= 2 * 3.14159265358979323846;
            }
            audio_op_process(filt, L.data(), R.data(), frames, sr, 120.f, 4, 0.0, nullptr, 0);
            if (b >= 30) { for (uint32_t i = 0; i < frames; ++i) out_sumsq += static_cast<double>(L[i]) * L[i]; cnt += frames; }
        }
        return static_cast<float>(std::sqrt(out_sumsq / cnt) / std::sqrt(0.5));
    };
    CHECK(band_ratio(100.f,  0, 1000.f) > 0.7f);    // LP @1k: 100 Hz passes
    CHECK(band_ratio(8000.f, 0, 1000.f) < 0.2f);    // LP @1k: 8 kHz blocked
    CHECK(band_ratio(8000.f, 1, 1000.f) > 0.7f);    // HP @1k: 8 kHz passes
    CHECK(band_ratio(100.f,  1, 1000.f) < 0.2f);    // HP @1k: 100 Hz blocked

    // Denormal / stability guard: feed an impulse then long silence — output stays finite.
    for (uint32_t i = 0; i < frames; ++i) { L[i] = (i == 0) ? 1.f : 0.f; R[i] = L[i]; }
    audio_op_process(filt, L.data(), R.data(), frames, sr, 120.f, 4, 0.0, nullptr, 0);
    for (int b = 0; b < 400; ++b) {
        for (uint32_t i = 0; i < frames; ++i) { L[i] = 0.f; R[i] = 0.f; }
        audio_op_process(filt, L.data(), R.data(), frames, sr, 120.f, 4, 0.0, nullptr, 0);
    }
    CHECK(std::isfinite(L[0]) && std::isfinite(L[frames - 1]));

    // Warm up (not counted): hand the instrument a note-on and run one block each.
    session::NoteEvent on{ 0u, true, 60, 1.0f, 1, 0.f };
    audio_op_process(inst, L.data(), R.data(), frames, sr, 120.f, 4, 0.0, &on, 1);
    audio_op_process(fx,   L.data(), R.data(), frames, sr, 120.f, 4, 0.0, nullptr, 0);

    // Note delivery: the held note must produce audible output.
    float peak = 0.f;
    for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(L[i]));
    CHECK(peak > 0.01f);

    // Steady state: zero heap allocations across many blocks (instrument + effect + param sets).
    g_count = true;
    for (int b = 0; b < 200; ++b) {
        audio_op_process(inst, L.data(), R.data(), frames, sr, 120.f, 4, b * 0.5, nullptr, 0);
        audio_op_process(fx,   L.data(), R.data(), frames, sr, 120.f, 4, b * 0.5, nullptr, 0);
        audio_op_process(filt, L.data(), R.data(), frames, sr, 120.f, 4, b * 0.5, nullptr, 0);
        audio_op_param_set(fx,   0, 4.f + static_cast<float>(b % 8));   // vary bits (RT-safe path)
        audio_op_param_set(filt, 1, 500.f + static_cast<float>(b % 8) * 400.f);   // sweep cutoff
    }
    g_count = false;
    CHECK(g_allocs.load() == 0);

    audio_op_destroy(inst);
    audio_op_destroy(fx);
    audio_op_destroy(filt);
    return vivid::test::summary("test_audio_op_rt");
}
