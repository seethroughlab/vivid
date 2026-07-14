// ADR-0015 / M4: the Arp — the first NOTE EFFECT. It is the proof that notes are a real signal in
// the graph: notes in, notes out, no audio. One held key must become a stream of retriggered notes,
// in time, without allocating on the audio thread.
//
// This is the test the whole note-graph rests on: M1's note edges are unobservable by ear (MidiIn
// emits exactly what the old broadcast did), so the arp is what makes them audible — and what makes
// a regression here detectable.
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
constexpr float    kBpm = 120.f;    // 0.5 s per beat; a 1/8 step = 0.25 s = 12000 samples

// Run the arp for `blocks` blocks, feeding `in` on the first block only, and collect what it emits.
std::vector<NoteEvent> run_arp(vivid::AudioOp* op, const std::vector<NoteEvent>& in, int blocks,
                               long* allocs = nullptr) {
    std::vector<NoteEvent> collected;
    collected.reserve(8192);   // reserve BEFORE the counter arms: the harness must not allocate
                               // inside the measured region, or it would blame the operator
    std::vector<float> L(kBlock, 0.f), R(kBlock, 0.f);
    std::vector<NoteEvent> out(512);
    double beats = 0.0;
    if (allocs) { g_allocs.store(0); g_count = true; }
    for (int b = 0; b < blocks; ++b) {
        const NoteEvent* nin = (b == 0 && !in.empty()) ? in.data() : nullptr;
        const uint32_t nn = (b == 0) ? static_cast<uint32_t>(in.size()) : 0;
        uint32_t produced = 0;
        vivid::audio_op_process(op, L.data(), R.data(), kBlock, kSR, kBpm, 4, beats,
                                nin, nn, out.data(), static_cast<uint32_t>(out.size()), &produced);
        for (uint32_t i = 0; i < produced; ++i) collected.push_back(out[i]);
        beats += static_cast<double>(kBlock) / kSR * (kBpm / 60.0);
    }
    if (allocs) { g_count = false; *allocs = g_allocs.load(); }
    return collected;
}

int count_ons(const std::vector<NoteEvent>& v) {
    int n = 0;
    for (const NoteEvent& e : v) if (e.on) ++n;
    return n;
}

}  // namespace

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);

    // The arp must actually be in the registry (it's what the chooser offers).
    vivid::AudioOp* arp = vivid::audio_op_create(reg, "Arp");
    CHECK(arp != nullptr);
    if (!arp) return vivid::test::summary("test_arp_op");

    // --- 1. Nothing held => nothing emitted. (An arp must not invent notes.) ---
    {
        const std::vector<NoteEvent> none;
        const std::vector<NoteEvent> out = run_arp(arp, none, 40);
        CHECK(out.empty());
    }

    // --- 2. ONE held key becomes a STREAM of retriggered notes ---
    // This is the money case: hold one note, hear an arpeggio. 2 seconds at 120bpm with the default
    // 1/8T rate is many steps; assert it retriggers repeatedly rather than passing one note through.
    {
        vivid::audio_op_destroy(arp);
        arp = vivid::audio_op_create(reg, "Arp");
        std::vector<NoteEvent> in;
        in.push_back(NoteEvent{ 0u, true, 60, 0.9f, 1, 0.f });   // hold C4, never released

        const int blocks = static_cast<int>(2.0 * kSR / kBlock);   // ~2 seconds
        const std::vector<NoteEvent> out = run_arp(arp, in, blocks);
        const int ons = count_ons(out);
        CHECK(ons >= 4);                       // a rhythmic stream, not a single pass-through
        for (const NoteEvent& e : out) {
            CHECK(e.pitch == 60);              // one held key, one octave => that key, retriggered
            CHECK(e.sample_offset < kBlock);   // sample-accurate WITHIN its block
        }
        // Every note it starts, it also ends: an arp that leaks note-ons hangs the synth.
        int offs = 0;
        for (const NoteEvent& e : out) if (!e.on) ++offs;
        CHECK(offs >= ons - 1);                // (the last note may still be sounding)
    }

    // --- 3. Its note ids are its OWN — they must not collide with clip/live note ids ---
    // Colliding ids would make a clip's note-off kill an arp voice (and vice versa).
    {
        vivid::audio_op_destroy(arp);
        arp = vivid::audio_op_create(reg, "Arp");
        std::vector<NoteEvent> in;
        in.push_back(NoteEvent{ 0u, true, 64, 0.8f, 7, 0.f });   // incoming id 7
        const std::vector<NoteEvent> out = run_arp(arp, in, 200);
        CHECK(!out.empty());
        for (const NoteEvent& e : out) CHECK(e.note_id != 7);
    }

    // --- 4. ZERO allocation on the audio thread (the RT contract) ---
    {
        vivid::audio_op_destroy(arp);
        arp = vivid::audio_op_create(reg, "Arp");
        std::vector<NoteEvent> in;
        in.push_back(NoteEvent{ 0u, true, 60, 0.9f, 1, 0.f });
        in.push_back(NoteEvent{ 0u, true, 64, 0.9f, 2, 0.f });
        in.push_back(NoteEvent{ 0u, true, 67, 0.9f, 3, 0.f });   // a held chord
        run_arp(arp, in, 4, nullptr);                            // warm up outside the counter
        long allocs = -1;
        const std::vector<NoteEvent> out = run_arp(arp, in, 400, &allocs);
        CHECK(allocs == 0);
        CHECK(count_ons(out) > 0);                               // ...and it really did work
    }

    vivid::audio_op_destroy(arp);
    return vivid::test::summary("test_arp_op");
}
