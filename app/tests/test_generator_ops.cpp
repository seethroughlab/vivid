// ADR-0022 P3.3: the note GENERATORS (Euclid / Chord / RandMelody). Algorithmic note SOURCES —
// they read no input notes and emit their own, phase-locked to the transport. This test proves:
// they are classified as generators (not instruments), a Euclidean pattern has the right hit count,
// note_flush releases held voices, RandMelody is deterministic, and none of it allocates on the
// audio thread.
#include "audio/audio_op_runtime.h"
#include "audio/builtin_audio_ops.h"
#include "gpu/op_runtime.h"
#include "midi/midi_clip.h"
#include "test_helpers.h"

#include <atomic>
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

using vivid::session::NoteEvent;

namespace {

constexpr uint32_t kSR = 48000;
constexpr uint32_t kBlock = 256;
constexpr float    kBpm = 120.f;    // 0.5 s per beat

// Run a generator for `blocks` blocks starting at beat 0 (a generator ignores input notes), advancing
// the transport each block, and collect what it emits. Optionally count audio-thread allocations.
std::vector<NoteEvent> run_gen(vivid::AudioOp* op, int blocks, long* allocs = nullptr) {
    std::vector<NoteEvent> collected;
    collected.reserve(8192);                 // reserve BEFORE arming the counter
    std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
    std::vector<NoteEvent> out(512);
    double beats = 0.0;
    if (allocs) { g_allocs.store(0); g_count = true; }
    for (int b = 0; b < blocks; ++b) {
        uint32_t produced = 0;
        vivid::audio_op_process(op, L.data(), R.data(), kBlock, kSR, kBpm, 4, beats,
                                nullptr, 0, out.data(), static_cast<uint32_t>(out.size()), &produced);
        for (uint32_t i = 0; i < produced; ++i) collected.push_back(out[i]);
        beats += static_cast<double>(kBlock) / kSR * (kBpm / 60.0);
    }
    if (allocs) { g_count = false; *allocs = g_allocs.load(); }
    return collected;
}

int count_ons(const std::vector<NoteEvent>& v) {
    int n = 0; for (const NoteEvent& e : v) if (e.on) ++n; return n;
}

// One bar = 4 beats. At 120bpm that is 2 s; run just under a bar so a full Euclidean cycle fires once.
int blocks_for_beats(double beats) {
    return static_cast<int>(beats * (60.0 / kBpm) * kSR / kBlock);
}

}  // namespace

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);

    // --- 1. Classification: exactly the three generators, and they are NOT instruments ---
    {
        CHECK(vivid::audio_gen_op_count(reg) == 3);
        CHECK(vivid::audio_op_is_gen_op(reg, "Euclid"));
        CHECK(vivid::audio_op_is_gen_op(reg, "Chord"));
        CHECK(vivid::audio_op_is_gen_op(reg, "RandMelody"));
        CHECK(!vivid::audio_op_is_gen_op(reg, "TestTone"));   // an instrument is not a generator
        // A generator has no audio input (looks like a source) but must be excluded from the
        // instrument list — its declared audio_role (ADR-0047) lists it as a generator instead.
        const int nsrc = vivid::audio_op_registry_count(reg, /*want_source*/true);
        for (int i = 0; i < nsrc; ++i)
            CHECK(!vivid::audio_op_is_gen_op(reg, vivid::audio_op_registry_name(reg, true, i)));
    }

    // --- 2. Euclid E(4,16) at 1/16 fires exactly 4 hits per bar, all on the chosen note ---
    {
        vivid::AudioOp* eu = vivid::audio_op_create(reg, "Euclid");
        CHECK(eu != nullptr);
        if (eu) {
            // defaults: steps=16 pulses=4 rate=1/16 note=36 → 4 evenly-spaced hits over 16 steps.
            const std::vector<NoteEvent> out = run_gen(eu, blocks_for_beats(3.98));
            CHECK(count_ons(out) == 4);
            for (const NoteEvent& e : out) {
                CHECK(e.pitch == 36);
                CHECK(e.sample_offset < kBlock);       // sample-accurate within its block
            }
            // Every on is eventually released (a generator that leaks note-ons hangs the synth).
            int ons = 0, offs = 0;
            for (const NoteEvent& e : out) (e.on ? ons : offs)++;
            CHECK(offs >= ons - 1);                    // the last note may still be sounding
            vivid::audio_op_destroy(eu);
        }
    }

    // --- 3. note_flush releases the voices a generator is currently sounding ---
    {
        vivid::AudioOp* ch = vivid::audio_op_create(reg, "Chord");   // default Maj = 3 voices
        CHECK(ch != nullptr);
        if (ch) {
            run_gen(ch, 1);                            // one block at beat 0: the chord fires and holds
            std::vector<NoteEvent> flushed(64);
            uint32_t n = 0;
            vivid::audio_op_note_flush(ch, flushed.data(), static_cast<uint32_t>(flushed.size()), &n);
            CHECK(n == 3);                             // an off for each held chord voice
            for (uint32_t i = 0; i < n; ++i) CHECK(!flushed[i].on);
            // A second flush emits nothing — the voices were forgotten.
            uint32_t n2 = 0;
            vivid::audio_op_note_flush(ch, flushed.data(), static_cast<uint32_t>(flushed.size()), &n2);
            CHECK(n2 == 0);
            vivid::audio_op_destroy(ch);
        }
    }

    // --- 4. RandMelody is DETERMINISTIC: same seed + same transport => identical melody ---
    {
        vivid::AudioOp* a = vivid::audio_op_create(reg, "RandMelody");
        vivid::AudioOp* b = vivid::audio_op_create(reg, "RandMelody");
        CHECK(a && b);
        if (a && b) {
            const std::vector<NoteEvent> oa = run_gen(a, blocks_for_beats(4.0));
            const std::vector<NoteEvent> ob = run_gen(b, blocks_for_beats(4.0));
            CHECK(!oa.empty());
            CHECK(oa.size() == ob.size());
            const size_t m = oa.size() < ob.size() ? oa.size() : ob.size();
            for (size_t i = 0; i < m; ++i) {
                CHECK(oa[i].pitch == ob[i].pitch);
                CHECK(oa[i].on == ob[i].on);
                CHECK(oa[i].sample_offset == ob[i].sample_offset);
            }
            vivid::audio_op_destroy(a); vivid::audio_op_destroy(b);
        }
    }

    // --- 5. ZERO allocation on the audio thread (the RT contract) ---
    {
        vivid::AudioOp* eu = vivid::audio_op_create(reg, "Euclid");
        CHECK(eu != nullptr);
        if (eu) {
            run_gen(eu, 8, nullptr);                   // warm up outside the counter
            long allocs = -1;
            const std::vector<NoteEvent> out = run_gen(eu, 400, &allocs);
            CHECK(allocs == 0);
            CHECK(count_ons(out) > 0);                 // ...and it really did generate
            vivid::audio_op_destroy(eu);
        }
    }

    return vivid::test::summary("test_generator_ops");
}
