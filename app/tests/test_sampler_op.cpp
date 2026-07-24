// The native Sampler instrument operator, rebuilt on the shared sample-playback voice engine
// (audio/sample_engine). This proves the engine-backed op: (a) loading PCM + slice regions via the
// SamplerLoadable escape hatch and playing the right slice per note, (b) real pitch — a note above
// the root reads through the sample faster (repitch with interpolation), (c) an amplitude ADSR so a
// gated note-off FADES rather than hard-cutting, (d) polyphony with oldest-voice STEALING (a note
// past the voice cap is not silently dropped), (e) the voice engine's loop wrap, and (f) that
// steady-state processing performs ZERO heap allocations (program-global operator-new counter).
#include "audio/audio_op_runtime.h"
#include "audio/builtin_audio_ops.h"
#include "audio/sample_engine/voice.h"   // engine-level loop-wrap check
#include "gpu/op_runtime.h"
#include "midi/midi_clip.h"
#include "test_helpers.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
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

// Sampler param indices (collect_params order): 0 base_note, 1 gain, 2 gate, 3 attack, 4 decay,
// 5 sustain, 6 release, 7 voices, 8 transpose, 9 tune.
enum { P_BASE = 0, P_GAIN, P_GATE, P_ATK, P_DEC, P_SUS, P_REL, P_VOICES, P_TRANSPOSE, P_TUNE };

int main() {
    OpRegistry reg;
    register_builtin_audio_ops(reg);

    AudioOp* smp = audio_op_create(reg, "Sampler");
    CHECK(smp != nullptr);
    CHECK(audio_op_is_source(smp));        // instrument = source (no audio input)

    const uint32_t sr = 48000;
    const uint32_t frames = 256;
    std::vector<float> outL(4096), outR(4096);

    // ---- (a) slice identity, now through the ADSR ------------------------------------------
    // Three constant-valued slices (0.5, -0.25, 1.0), 200 samples each; note base+k triggers slice k.
    // The first output sample is mid-attack, so check a POST-ATTACK sample where the envelope has
    // reached sustain (=1) — the value there is the slice's own value at gain 1 / velocity 1.
    {
        const uint32_t SLICE = 200;
        std::vector<float> L, R;
        const float vals[3] = { 0.5f, -0.25f, 1.0f };
        for (float v : vals) for (uint32_t i = 0; i < SLICE; ++i) { L.push_back(v); R.push_back(v); }
        const uint32_t starts[3] = { 0, SLICE, 2 * SLICE };
        const uint32_t ends[3]   = { SLICE, 2 * SLICE, 3 * SLICE };
        const int base = 36;
        for (int k = 0; k < 3; ++k) {
            audio_op_load_sampler(smp, L.data(), R.data(), L.size(), sr, starts, ends, 3, base);  // reload clears voices
            session::NoteEvent on{ 0u, true, static_cast<int16_t>(base + k), 1.0f, 100 + k, 0.f };
            audio_op_process(smp, outL.data(), outR.data(), frames, sr, 120.f, 4, 0.0, &on, 1);
            CHECK(std::fabs(outL[120] - vals[k]) < 2e-3f);   // sample 120: past the 48-sample attack
            CHECK(std::fabs(outR[120] - vals[k]) < 2e-3f);
        }
        // A note outside the slice map is silent (no nearest-region fallback for a drum-rack).
        audio_op_load_sampler(smp, L.data(), R.data(), L.size(), sr, starts, ends, 3, base);
        session::NoteEvent off_map{ 0u, true, static_cast<int16_t>(base + 9), 1.0f, 200, 0.f };
        audio_op_process(smp, outL.data(), outR.data(), frames, sr, 120.f, 4, 0.0, &off_map, 1);
        float peak = 0.f; for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(outL[i]));
        CHECK(peak < 1e-6f);
    }

    // ---- (b) real pitch: an octave up reads through the sample ~2x faster --------------------
    // One keyboard-spanning region (nslices == 0) of a constant value over M frames; one-shot (gate 0),
    // so a voice ends exactly when playback reaches the sample end. The note at the root lasts ~M
    // output frames; the note an octave up (rate 2) lasts ~M/2.
    auto voice_duration = [&](int note, int root) -> uint32_t {
        std::vector<float> S(1500, 0.5f);
        audio_op_load_sampler(smp, S.data(), nullptr, S.size(), sr, nullptr, nullptr, 0, root);
        session::NoteEvent on{ 0u, true, static_cast<int16_t>(note), 1.0f, 500, 0.f };
        audio_op_process(smp, outL.data(), outR.data(), 3000, sr, 120.f, 4, 0.0, &on, 1);
        uint32_t last = 0;
        for (uint32_t i = 0; i < 3000; ++i) if (std::fabs(outL[i]) > 1e-3f) last = i;
        return last;
    };
    {
        const uint32_t dur_root = voice_duration(60, 60);        // rate 1.0
        const uint32_t dur_oct  = voice_duration(72, 60);        // rate 2.0 (one octave up)
        CHECK(dur_root > 1400 && dur_root < 1560);               // ~1500-sample sample
        CHECK(dur_oct  > 680  && dur_oct  < 820);                // ~half as long
        CHECK(dur_root > dur_oct * 17 / 10);                     // ~2x, allow slack
    }

    // ---- (c) gated note-off FADES (ADSR release), not a hard cut ----------------------------
    {
        std::vector<float> S(4000, 0.5f);
        audio_op_load_sampler(smp, S.data(), nullptr, S.size(), sr, nullptr, nullptr, 0, 60);
        audio_op_param_set(smp, P_GATE, 1.f);        // gate mode: note-off releases
        audio_op_param_set(smp, P_REL, 0.02f);       // 20 ms release = 960 frames
        session::NoteEvent on{ 0u, true, 60, 1.0f, 600, 0.f };
        audio_op_process(smp, outL.data(), outR.data(), 256, sr, 120.f, 4, 0.0, &on, 1);   // sustain
        CHECK(std::fabs(outL[200] - 0.5f) < 5e-3f);              // holding at sustain
        session::NoteEvent off{ 0u, false, 60, 0.f, 600, 0.f };
        audio_op_process(smp, outL.data(), outR.data(), 1200, sr, 120.f, 4, 0.0, &off, 1);  // release tail
        CHECK(outL[2]   > 0.40f);                                // right after note-off: still high (no hard cut)
        CHECK(outL[480] > 0.18f && outL[480] < 0.32f);           // mid-release, decaying (~0.5 * 0.5)
        CHECK(std::fabs(outL[1000]) < 1e-2f);                    // release finished ~960 frames in
        audio_op_param_set(smp, P_GATE, 0.f);
        audio_op_param_set(smp, P_REL, 0.05f);
    }

    // ---- (d) polyphony with oldest-voice stealing -------------------------------------------
    // voices = 2, three distinct notes in one block: the 3rd steals the oldest (base+0), so it SOUNDS
    // rather than being dropped. Slice values 0.5 / -0.25 / 1.0; the surviving pair is (base+1, base+2)
    // = -0.25 + 1.0 = 0.75, distinct from the no-steal pair (base+0, base+1) = 0.25.
    {
        const uint32_t SLICE = 400;
        std::vector<float> L; const float vals[3] = { 0.5f, -0.25f, 1.0f };
        for (float v : vals) for (uint32_t i = 0; i < SLICE; ++i) L.push_back(v);
        const uint32_t starts[3] = { 0, SLICE, 2 * SLICE };
        const uint32_t ends[3]   = { SLICE, 2 * SLICE, 3 * SLICE };
        const int base = 36;
        audio_op_load_sampler(smp, L.data(), nullptr, L.size(), sr, starts, ends, 3, base);
        audio_op_param_set(smp, P_VOICES, 2.f);
        session::NoteEvent on3[3] = {
            { 0u, true, static_cast<int16_t>(base + 0), 1.0f, 700, 0.f },
            { 0u, true, static_cast<int16_t>(base + 1), 1.0f, 701, 0.f },
            { 0u, true, static_cast<int16_t>(base + 2), 1.0f, 702, 0.f },
        };
        audio_op_process(smp, outL.data(), outR.data(), 256, sr, 120.f, 4, 0.0, on3, 3);
        CHECK(outL[120] > 0.6f);                                 // 0.75 (stole) not 0.25 (dropped)
        audio_op_param_set(smp, P_VOICES, 16.f);
    }

    // ---- (e) engine-level loop wrap ---------------------------------------------------------
    // A region with a sustain loop [2,8): after rendering far past the sample length, an un-released
    // voice keeps sounding and its cursor stays inside the loop instead of running off the end.
    {
        auto data = std::make_shared<sample_engine::SampleData>();
        data->sample_rate = sr;
        data->samples_L.resize(10);
        for (int i = 0; i < 10; ++i) data->samples_L[i] = static_cast<float>(i);
        sample_engine::SampleRegion rgn;
        rgn.root_note = 60; rgn.lo_note = 0; rgn.hi_note = 127;
        rgn.loop_enabled = true; rgn.loop_start = 2; rgn.loop_end = 8;
        rgn.data = data;
        sample_engine::Voice v;
        sample_engine::voice_note_on(v, 60, 1.0f, &rgn, 1.0, 0, /*one_shot*/false);
        const float dt = 1.f / static_cast<float>(sr);
        for (int i = 0; i < 200; ++i) { float l = 0, r = 0; sample_engine::voice_render_frame(v, l, r, dt, 0.001f, 0.f, 1.f, 0.05f); }
        CHECK(v.active);                                         // still sounding (didn't run off the end)
        CHECK(v.playback_pos >= 2.0 && v.playback_pos < 8.0);    // cursor stayed inside the loop
    }

    // ---- not-a-sampler guard ----------------------------------------------------------------
    {
        AudioOp* tone = audio_op_create(reg, "TestTone");
        std::vector<float> one(4, 0.f);
        const uint32_t s0[1] = { 0 }, e0[1] = { 4 };
        CHECK(!audio_op_load_sampler(tone, one.data(), one.data(), one.size(), sr, s0, e0, 1, 36));
        audio_op_destroy(tone);
    }

    // ---- (f) steady state: zero heap allocations across many blocks --------------------------
    {
        const uint32_t SLICE = 200;
        std::vector<float> L; const float vals[3] = { 0.5f, -0.25f, 1.0f };
        for (float v : vals) for (uint32_t i = 0; i < SLICE; ++i) L.push_back(v);
        const uint32_t starts[3] = { 0, SLICE, 2 * SLICE };
        const uint32_t ends[3]   = { SLICE, 2 * SLICE, 3 * SLICE };
        audio_op_load_sampler(smp, L.data(), nullptr, L.size(), sr, starts, ends, 3, 36);
        g_count = true;
        for (int b = 0; b < 200; ++b) {
            session::NoteEvent on{ 0u, true, static_cast<int16_t>(36 + (b % 3)), 1.0f, 900 + b, 0.f };
            audio_op_process(smp, outL.data(), outR.data(), frames, sr, 120.f, 4, b * 0.25, &on, 1);
            audio_op_param_set(smp, P_GAIN, 0.5f + 0.5f * static_cast<float>(b % 2));   // vary gain (RT-safe)
        }
        g_count = false;
        CHECK(g_allocs.load() == 0);
    }

    audio_op_destroy(smp);
    return vivid::test::summary("test_sampler_op");
}
