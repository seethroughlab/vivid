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
    CHECK(inst != nullptr);
    CHECK(fx != nullptr);
    CHECK(audio_op_is_source(inst));      // instrument = source (no audio input)
    CHECK(!audio_op_is_source(fx));       // bitcrush = effect (has audio input)
    CHECK(audio_op_create(reg, "Plasma") == nullptr);   // a visual op is not an audio op

    const uint32_t frames = 256, sr = 48000;
    std::vector<float> L(frames), R(frames);

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
        audio_op_param_set(fx, 0, 4.f + static_cast<float>(b % 8));   // vary bits (RT-safe path)
    }
    g_count = false;
    CHECK(g_allocs.load() == 0);

    audio_op_destroy(inst);
    audio_op_destroy(fx);
    return vivid::test::summary("test_audio_op_rt");
}
