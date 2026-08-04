// The native Sampler instrument operator, rebuilt on the shared sample-playback voice engine
// (audio/sample_engine). This proves the engine-backed op: (a) loading PCM + slice regions via the
// SamplerLoadable escape hatch and playing the right slice per note, (b) real pitch — a note above
// the root reads through the sample faster (repitch with interpolation), (c) an amplitude ADSR so a
// gated note-off FADES rather than hard-cutting, (d) polyphony with oldest-voice STEALING (a note
// past the voice cap is not silently dropped), (e) the voice engine's loop wrap, and (f) that
// steady-state processing performs ZERO heap allocations (program-global operator-new counter).
#include "audio/audio_op_runtime.h"
#include "audio/sampler_op.h"            // ADR-0049: SamplerInfo / SamplerSlice (read API)
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
#include <string>
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

    // ==== ADR-0049: the Sampler read API (SamplerInspectable) the Sampler editor draws from ====
    // 5. Sliced drum-rack: 4 equal slices over 800 stereo frames, base C1 (36).
    {
        const uint32_t sr = 44100, N = 800;
        std::vector<float> L(N), R(N);
        for (uint32_t i = 0; i < N; ++i) L[i] = R[i] = std::sin(static_cast<float>(i) * 0.05f);
        const uint32_t starts[4] = { 0, 200, 400, 600 }, ends[4] = { 200, 400, 600, 800 };
        AudioOp* op = audio_op_create(reg, "Sampler");
        CHECK(op != nullptr);
        CHECK(audio_op_load_sampler(op, L.data(), R.data(), N, sr, starts, ends, 4, 36));
        audio_op_set_sampler_source(op, "/samples/amen.wav");

        SamplerInfo info{};
        CHECK(audio_op_sampler_info(op, info));
        CHECK(info.channels == 2);           // stereo (R present)
        CHECK(info.sample_rate == sr);
        CHECK(info.slice_count == 4);
        CHECK(info.base_note == 36);
        CHECK(info.frames == N);             // 4 slices of 200, concatenated = 800
        CHECK(std::string(audio_op_sampler_source(op)) == "/samples/amen.wav");

        SamplerSlice sl[8];
        const int n = audio_op_sampler_slices(op, sl, 8);
        CHECK(n == 4);
        for (int i = 0; i < 4; ++i) {
            CHECK(sl[i].start == static_cast<uint32_t>(i) * 200u);
            CHECK(sl[i].end   == static_cast<uint32_t>(i + 1) * 200u);
            CHECK(sl[i].root_note == 36 + i);                 // ascending: slice i -> base+i (one note per key)
            CHECK(sl[i].lo_note == 36 + i && sl[i].hi_note == 36 + i);
        }
        CHECK(audio_op_sampler_slices(op, sl, 2) == 4);        // a small cap still reports the true count
        audio_op_destroy(op);
    }

    // 6. Melodic direct load (no slices): one mono region spanning the whole keyboard.
    {
        const uint32_t sr = 48000, N = 500;
        std::vector<float> L(N, 0.5f);
        AudioOp* op = audio_op_create(reg, "Sampler");
        CHECK(op != nullptr);
        CHECK(audio_op_load_sampler(op, L.data(), nullptr, N, sr, nullptr, nullptr, 0, 60));
        SamplerInfo info{};
        CHECK(audio_op_sampler_info(op, info));
        CHECK(info.channels == 1);           // mono (R null)
        CHECK(info.slice_count == 1);
        CHECK(info.base_note == 60);
        CHECK(info.frames == N);
        SamplerSlice sl[2];
        CHECK(audio_op_sampler_slices(op, sl, 2) == 1);
        CHECK(sl[0].start == 0 && sl[0].end == N);
        CHECK(sl[0].root_note == 60);
        CHECK(sl[0].lo_note == 0 && sl[0].hi_note == 127);    // the melodic region spans the keyboard
        audio_op_destroy(op);
    }

    // 7. Nothing loaded → info false, no slices, empty source; a non-Sampler op cross-casts to "not a sampler".
    {
        AudioOp* empty = audio_op_create(reg, "Sampler");
        SamplerInfo info{};
        CHECK(!audio_op_sampler_info(empty, info));
        CHECK(audio_op_sampler_slices(empty, nullptr, 0) == 0);
        CHECK(std::string(audio_op_sampler_source(empty)).empty());
        audio_op_destroy(empty);

        AudioOp* tone = audio_op_create(reg, "TestTone");
        CHECK(!audio_op_sampler_info(tone, info));            // not a sampler → false, no crash
        CHECK(audio_op_sampler_source_frames(tone) == 0);     // edit cross-cast also refuses cleanly
        audio_op_destroy(tone);
    }

    // ==== ADR-0049 slice 6: the EDIT API (SamplerEditable) — re-cut trim / slices from retained PCM ====
    // 8. Trim a melodic load: play only [in,out) of the whole sample. Source is a ramp so we can confirm
    //    the trim window by its geometry; the retained source length is unchanged (edits are non-lossy).
    {
        const uint32_t sr = 44100, N = 1000;
        std::vector<float> L(N);
        for (uint32_t i = 0; i < N; ++i) L[i] = static_cast<float>(i);
        AudioOp* op = audio_op_create(reg, "Sampler");
        CHECK(audio_op_load_sampler(op, L.data(), nullptr, N, sr, nullptr, nullptr, 0, 60));
        CHECK(audio_op_sampler_source_frames(op) == N);       // source retained for editing
        SamplerInfo info{};
        CHECK(audio_op_sampler_info(op, info) && info.frames == N);   // whole sample before trim

        audio_op_sampler_set_trim(op, 200, 700);              // keep [200,700)
        CHECK(audio_op_sampler_info(op, info));
        CHECK(info.frames == 500);                            // played window is now 500 frames
        CHECK(info.slice_count == 1);                         // still melodic
        CHECK(audio_op_sampler_source_frames(op) == N);       // source is NOT consumed by the trim
        SamplerSlice sl[2];
        CHECK(audio_op_sampler_slices(op, sl, 2) == 1);
        CHECK(sl[0].start == 0 && sl[0].end == 500);          // the region carries its own [0,len) copy
        CHECK(sl[0].lo_note == 0 && sl[0].hi_note == 127);    // still spans the keyboard

        audio_op_sampler_set_trim(op, 100, 0);                // out<=in => trim to the end
        CHECK(audio_op_sampler_info(op, info) && info.frames == N - 100);
        // The editor reads the whole-source envelope + the play window in SOURCE frames (not the
        // concatenated result), so trim handles land on the right place even after a trim.
        float sp[64]; CHECK(audio_op_sampler_source_peaks(op, sp, 64) == 64);
        unsigned int bs[4], be[4];
        CHECK(audio_op_sampler_edit_boundaries(op, bs, be, 4) == 1);
        CHECK(bs[0] == 100 && be[0] == N);                    // the current window is [100,N) in source space
        audio_op_destroy(op);
    }

    // 9. Re-slice a melodic load into a drum-rack: two unequal slices mapped to ascending pitches.
    {
        const uint32_t sr = 48000, N = 1000;
        std::vector<float> L(N, 0.25f);
        AudioOp* op = audio_op_create(reg, "Sampler");
        CHECK(audio_op_load_sampler(op, L.data(), nullptr, N, sr, nullptr, nullptr, 0, 60));
        const unsigned int starts[2] = { 0, 400 }, ends[2] = { 400, 1000 };
        audio_op_sampler_reslice(op, starts, ends, 2, 48);
        SamplerInfo info{};
        CHECK(audio_op_sampler_info(op, info));
        CHECK(info.slice_count == 2);
        CHECK(info.base_note == 48);
        CHECK(info.frames == N);                              // 400 + 600 concatenated
        SamplerSlice sl[4];
        CHECK(audio_op_sampler_slices(op, sl, 4) == 2);
        CHECK(sl[0].start == 0   && sl[0].end == 400 && sl[0].root_note == 48);
        CHECK(sl[1].start == 400 && sl[1].end == 1000 && sl[1].root_note == 49);
        unsigned int bs[4], be[4];                            // SOURCE-space edges match the slice request
        CHECK(audio_op_sampler_edit_boundaries(op, bs, be, 4) == 2);
        CHECK(bs[0] == 0 && be[0] == 400 && bs[1] == 400 && be[1] == 1000);
        audio_op_destroy(op);
    }

    // 10. Edits on a never-loaded op are safe no-ops (no source to re-cut).
    {
        AudioOp* op = audio_op_create(reg, "Sampler");
        CHECK(audio_op_sampler_source_frames(op) == 0);
        const unsigned int s0 = 0, e0 = 100;
        audio_op_sampler_set_trim(op, 0, 100);               // no source → no-op, no crash
        audio_op_sampler_reslice(op, &s0, &e0, 1, 36);
        audio_op_sampler_set_slice_tune(op, 0, 5);           // no source → no-op, no crash
        CHECK(audio_op_sampler_detect_slices(op, 0.5f) == 0);
        SamplerInfo info{};
        CHECK(!audio_op_sampler_info(op, info));             // still nothing loaded
        audio_op_destroy(op);
    }

    // 11. Per-slice tune (slice 9): a slice keeps its TRIGGER note (lo/hi) but shifts pitch via root_note,
    //     so tune == lo_note - root_note. Successive tunes accumulate across slices.
    {
        const uint32_t sr = 44100, N = 1200;
        std::vector<float> L(N, 0.2f);
        AudioOp* op = audio_op_create(reg, "Sampler");
        CHECK(audio_op_load_sampler(op, L.data(), nullptr, N, sr, nullptr, nullptr, 0, 60));
        const unsigned int st[3] = { 0, 400, 800 }, en[3] = { 400, 800, 1200 };
        audio_op_sampler_reslice(op, st, en, 3, 36);         // slices trigger on 36,37,38
        audio_op_sampler_set_slice_tune(op, 0, +5);          // slice 0 up 5 st
        audio_op_sampler_set_slice_tune(op, 2, -3);          // slice 2 down 3 st (accumulates with slice 0)
        SamplerSlice sl[3];
        CHECK(audio_op_sampler_slices(op, sl, 3) == 3);
        CHECK(sl[0].lo_note == 36 && sl[0].root_note == 36 - 5);   // +5 up  => root below trigger
        CHECK(sl[1].lo_note == 37 && sl[1].root_note == 37);       // untouched
        CHECK(sl[2].lo_note == 38 && sl[2].root_note == 38 + 3);   // -3 down => root above trigger
        audio_op_destroy(op);
    }

    // 12. Transient-detect slicing (slice 9): three loud onsets on a quiet bed → several slices, all
    //     valid + covering the sample; head always starts slice 0. (Loose: detector tuning may vary.)
    {
        const uint32_t sr = 44100, N = 44100;                // 1s
        std::vector<float> L(N, 0.f);
        for (uint32_t p : { 0u, 15000u, 30000u })            // sharp onsets: a short burst at each
            for (uint32_t k = 0; k < 400 && p + k < N; ++k) L[p + k] = (k < 20 ? 0.9f : 0.5f) * ((k % 2) ? 1.f : -1.f);
        AudioOp* op = audio_op_create(reg, "Sampler");
        CHECK(audio_op_load_sampler(op, L.data(), nullptr, N, sr, nullptr, nullptr, 0, 36));
        const int n = audio_op_sampler_detect_slices(op, 0.5f);
        CHECK(n >= 2);                                        // found the onsets (at least a couple of slices)
        SamplerSlice sl[16]; const int got = audio_op_sampler_slices(op, sl, 16);
        CHECK(got == n && n <= 16);
        CHECK(sl[0].start == 0);                             // slice 0 always starts at the head
        for (int i = 0; i + 1 < got; ++i) CHECK(sl[i].end == sl[i + 1].start);   // contiguous, in order
        audio_op_destroy(op);
    }

    return vivid::test::summary("test_sampler_op");
}
