// AG-0: the per-track audio signal graph (ADR-0012). Proves the pure topology + execution
// core: linear chains, multi-input stereo summing (parallel chains / racks), cycle
// rejection, an isolated-source safety case, and ZERO heap allocations in the audio-thread
// executor (program-global operator-new counter, gated so setup allocs don't count).
#include "audio/audio_graph.h"
#include "test_helpers.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <vector>

// Does the graph hold this edge? (Used by the wiring-policy cases below.)
static bool has_edge(const vivid::audio::AudioGraph& g, int from, int to) {
    for (const vivid::audio::AudioGraphEdge& e : g.edges())
        if (e.from_id == from && e.to_id == to) return true;
    return false;
}

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

using namespace vivid::audio;

namespace {
struct ConstSrc { float v; };
void const_src(void* c, const float*, const float*, float* oL, float* oR, uint32_t n) {
    const float v = static_cast<ConstSrc*>(c)->v;
    for (uint32_t i = 0; i < n; ++i) { oL[i] = v; oR[i] = v; }
}
struct Gain { float g; };
void gain_fx(void* c, const float* iL, const float* iR, float* oL, float* oR, uint32_t n) {
    const float g = static_cast<Gain*>(c)->g;
    for (uint32_t i = 0; i < n; ++i) { oL[i] = iL[i] * g; oR[i] = iR[i] * g; }
}
// Read the L (== R) value of the output buffer's first sample.
float out0(const CompiledAudioGraph& cg, const std::vector<float>& pool, uint32_t stride) {
    if (cg.output_buf < 0) return 0.f;
    return pool[static_cast<size_t>(cg.output_buf) * 2 * stride];
}
}  // namespace

int main() {
    const uint32_t frames = 256, stride = 256;

    // --- 1. Linear chain: src(0.5) -> gain(x2) -> output(passthrough). out == 1.0 ---
    {
        AudioGraph g;
        ConstSrc s{ 0.5f }; Gain g2{ 2.0f };
        const int src = g.add_node(true,  false, const_src, &s,  "src");
        const int fx  = g.add_node(false, false, gain_fx,   &g2, "gain");
        const int out = g.add_node(false, true,  nullptr,   nullptr, "out");
        g.set_output_id(out);
        CHECK(g.connect(src, fx));
        CHECK(g.connect(fx, out));
        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        std::vector<float> pool(static_cast<size_t>(cg.pool_buffers()) * 2 * stride, 0.f);
        cg.run(pool.data(), frames, stride);
        CHECK(std::fabs(out0(cg, pool, stride) - 1.0f) < 1e-6f);
    }

    // --- 2. Parallel sum (a "rack"): src(0.5) + src(0.25) -> output. out == 0.75 ---
    {
        AudioGraph g;
        ConstSrc a{ 0.5f }, b{ 0.25f };
        const int sa  = g.add_node(true, false, const_src, &a, "a");
        const int sb  = g.add_node(true, false, const_src, &b, "b");
        const int out = g.add_node(false, true, nullptr, nullptr, "out");
        g.set_output_id(out);
        CHECK(g.connect(sa, out));
        CHECK(g.connect(sb, out));
        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        std::vector<float> pool(static_cast<size_t>(cg.pool_buffers()) * 2 * stride, 0.f);
        cg.run(pool.data(), frames, stride);
        CHECK(std::fabs(out0(cg, pool, stride) - 0.75f) < 1e-6f);
    }

    // --- 3. Parallel chains then gain: (src0.5 + src0.5) -> gain(x0.5) -> out == 0.5 ---
    {
        AudioGraph g;
        ConstSrc a{ 0.5f }, b{ 0.5f }; Gain half{ 0.5f };
        const int sa  = g.add_node(true, false, const_src, &a, "a");
        const int sb  = g.add_node(true, false, const_src, &b, "b");
        const int mix = g.add_node(false, false, gain_fx, &half, "gain");
        const int out = g.add_node(false, true, nullptr, nullptr, "out");
        g.set_output_id(out);
        CHECK(g.connect(sa, mix));
        CHECK(g.connect(sb, mix));   // mix sees 0.5 + 0.5 = 1.0
        CHECK(g.connect(mix, out));
        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        std::vector<float> pool(static_cast<size_t>(cg.pool_buffers()) * 2 * stride, 0.f);
        cg.run(pool.data(), frames, stride);
        CHECK(std::fabs(out0(cg, pool, stride) - 0.5f) < 1e-6f);   // 1.0 * 0.5
    }

    // --- 4. Cycle rejection: a -> b -> a must fail to compile ---
    {
        AudioGraph g;
        const int a = g.add_node(false, false, nullptr, nullptr, "a");
        const int b = g.add_node(false, false, nullptr, nullptr, "b");
        CHECK(g.connect(a, b));
        CHECK(g.connect(b, a));
        CompiledAudioGraph cg;
        CHECK(!g.compile(cg));   // cycle -> false; caller keeps last good plan
    }

    // --- 5. dup edge + self-loop rejected at connect() ---
    {
        AudioGraph g;
        const int a = g.add_node(true, false, nullptr, nullptr, "a");
        const int b = g.add_node(false, true, nullptr, nullptr, "b");
        CHECK(g.connect(a, b));
        CHECK(!g.connect(a, b));   // dup
        CHECK(!g.connect(a, a));   // self-loop
    }

    // --- 6. Zero heap allocations in run() across many blocks ---
    {
        AudioGraph g;
        ConstSrc a{ 0.4f }, b{ 0.1f }; Gain g2{ 2.0f };
        const int sa  = g.add_node(true, false, const_src, &a, "a");
        const int sb  = g.add_node(true, false, const_src, &b, "b");
        const int fx  = g.add_node(false, false, gain_fx, &g2, "gain");
        const int out = g.add_node(false, true, nullptr, nullptr, "out");
        g.set_output_id(out);
        g.connect(sa, fx); g.connect(sb, fx); g.connect(fx, out);
        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        std::vector<float> pool(static_cast<size_t>(cg.pool_buffers()) * 2 * stride, 0.f);
        cg.run(pool.data(), frames, stride);   // warm-up (not counted)
        CHECK(std::fabs(out0(cg, pool, stride) - 1.0f) < 1e-6f);   // (0.4+0.1)*2

        g_count = true;
        for (int i = 0; i < 500; ++i) cg.run(pool.data(), frames, stride);
        g_count = false;
        CHECK(g_allocs.load() == 0);
    }

    // --- 7. Stable node ids (Stage 2 foundation): remove never recycles an id; clear() keeps the
    //        counter (authoritative editing), reset() restarts it (derived/loaded graphs). ---
    {
        AudioGraph g;
        const int a = g.add_node(true,  false, nullptr, nullptr, "a");   // 0
        const int b = g.add_node(false, false, nullptr, nullptr, "b");   // 1
        CHECK(a == 0 && b == 1);
        g.remove_node(a);                                // drop id 0
        const int c = g.add_node(false, false, nullptr, nullptr, "c");   // must be 2, NOT reuse 0
        CHECK(c == 2);
        CHECK(g.node_index(a) < 0);                      // old id truly gone
        g.clear();                                       // keeps the counter
        const int d = g.add_node(false, false, nullptr, nullptr, "d");
        CHECK(d == 3);                                   // still ascending past every prior id
        g.reset();                                       // full reset → deterministic 0-based again
        const int e = g.add_node(false, false, nullptr, nullptr, "e");
        CHECK(e == 0);
    }

    // --- 8. Editor node positions: unpositioned by default; set marks it; absent id is safe ---
    {
        AudioGraph g;
        const int a = g.add_node(true,  false, nullptr, nullptr, "a");
        const int b = g.add_node(false, true,  nullptr, nullptr, "b");
        float x = 0, y = 0;
        CHECK(!g.node_pos(a, x, y));                       // unpositioned → false
        g.set_node_pos(a, 12.5f, -3.f);
        CHECK(g.node_pos(a, x, y));
        CHECK(std::fabs(x - 12.5f) < 1e-6f && std::fabs(y + 3.f) < 1e-6f);
        CHECK(!g.node_pos(b, x, y));                       // sibling still unpositioned
        CHECK(!g.node_pos(999, x, y));                     // absent id → false, no crash
    }

    // --- 9. splice_before_output: an added EFFECT lands at the end of the signal path ---
    // Every predecessor of Output must be re-routed through the new node — not just one. With two
    // sources feeding Output (a key-split), missing that means half the signal bypasses the effect.
    {
        AudioGraph g;
        const int s1 = g.add_node(true,  false, nullptr, nullptr, "s1");
        const int s2 = g.add_node(true,  false, nullptr, nullptr, "s2");
        const int out = g.add_node(false, true, nullptr, nullptr, "out");
        g.set_output_id(out);
        g.connect(s1, out);
        g.connect(s2, out);

        const int fx = g.add_node(false, false, nullptr, nullptr, "fx");
        g.splice_before_output(fx);

        CHECK(has_edge(g, s1, fx));
        CHECK(has_edge(g, s2, fx));      // BOTH sources re-routed
        CHECK(has_edge(g, fx, out));
        CHECK(!has_edge(g, s1, out));    // ...and neither bypasses it any more
        CHECK(!has_edge(g, s2, out));
        CHECK(g.edges().size() == 3);

        // A second effect stacks after the first (it splices before Output, so fx -> fx2 -> out).
        const int fx2 = g.add_node(false, false, nullptr, nullptr, "fx2");
        g.splice_before_output(fx2);
        CHECK(has_edge(g, fx, fx2));
        CHECK(has_edge(g, fx2, out));
        CHECK(!has_edge(g, fx, out));
    }

    // --- 10. fan_in_to_output: an added SOURCE runs in parallel; a bare graph grows a sink ---
    {
        AudioGraph g;
        const int s1 = g.add_node(true, false, nullptr, nullptr, "s1");
        CHECK(g.output_id() < 0);                       // bare: no sink yet
        const int out = g.fan_in_to_output(s1, nullptr);
        CHECK(out >= 0);
        CHECK(g.output_id() == out);                    // ...materialized, or the source is silent
        CHECK(has_edge(g, s1, out));

        const int s2 = g.add_node(true, false, nullptr, nullptr, "s2");
        CHECK(g.fan_in_to_output(s2, nullptr) == out);  // reuses the existing sink
        CHECK(has_edge(g, s2, out));                    // parallel with s1 (Output sums = key-split)
        CHECK(has_edge(g, s1, out));

        // An absent node is a no-op, not a crash or a stray edge.
        CHECK(g.fan_in_to_output(999, nullptr) == -1);
        CHECK(g.edges().size() == 2);
    }

    // --- 11. remove_node_bridged on a middle node keeps the signal flowing ---
    // (A user-spawned plugin node deleted from the middle of a chain must not orphan the tail.)
    {
        AudioGraph g;
        const int src = g.add_node(true,  false, nullptr, nullptr, "src");
        const int out = g.add_node(false, true,  nullptr, nullptr, "out");
        g.set_output_id(out);
        g.connect(src, out);
        const int a = g.add_node(false, false, nullptr, nullptr, "a");
        g.splice_before_output(a);
        const int b = g.add_node(false, false, nullptr, nullptr, "b");
        g.splice_before_output(b);
        CHECK(has_edge(g, src, a) && has_edge(g, a, b) && has_edge(g, b, out));

        g.remove_node_bridged(a);                       // pull the middle node out
        CHECK(g.node_index(a) < 0);
        CHECK(has_edge(g, src, b));                     // src now feeds b directly
        CHECK(has_edge(g, b, out));
    }

    // ===== ADR-0015: notes are a second signal in the graph =====

    // --- 12. PARITY: a graph with no note edges compiles exactly as it always did ---
    // This is the migration gate. If typed edges changed anything about an all-audio graph, every
    // existing project would sound different.
    {
        AudioGraph g;
        const int src = g.add_node(true,  false, nullptr, nullptr, "src");
        const int fx  = g.add_node(false, false, nullptr, nullptr, "fx");
        const int out = g.add_node(false, true,  nullptr, nullptr, "out");
        g.set_output_id(out);
        g.connect(src, fx);
        g.connect(fx, out);

        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        CHECK(cg.note_buf_count == 0);          // no note-emitting node => no note pool at all
        CHECK(cg.buf_count == 3);
        CHECK(cg.output_buf == g.node_index(out));
        for (const CompiledStep& s : cg.steps) {
            CHECK(s.n_note_in == 0);
            CHECK(s.note_out_buf == -1);        // every step is note-free, exactly as before
        }
    }

    // --- 13. Note edges constrain execution ORDER (one DAG, both signals) ---
    // A note effect must run BEFORE the instrument it feeds — even though no AUDIO flows between
    // them. Getting this wrong means the instrument reads last block's notes.
    {
        AudioGraph g;
        const int midi = g.add_node(true,  false, nullptr, nullptr, "midi");   // MidiIn
        const int arp  = g.add_node(true,  false, nullptr, nullptr, "arp");    // note effect
        const int inst = g.add_node(true,  false, nullptr, nullptr, "inst");
        const int out  = g.add_node(false, true,  nullptr, nullptr, "out");
        g.set_output_id(out);
        g.set_note_ports(midi, false, true);
        g.set_note_ports(arp,  true,  true);
        g.set_note_ports(inst, true,  false);
        g.connect(midi, arp,  EdgeKind::Note);
        g.connect(arp,  inst, EdgeKind::Note);
        g.connect(inst, out,  EdgeKind::Audio);   // only the instrument makes sound

        CompiledAudioGraph cg;
        CHECK(g.compile(cg));

        auto step_pos = [&](int id) {   // where a node's step lands in topo order
            const int b = g.node_index(id);
            for (size_t i = 0; i < cg.steps.size(); ++i) if (cg.steps[i].out_buf == b) return static_cast<int>(i);
            return -1;
        };
        CHECK(step_pos(midi) < step_pos(arp));
        CHECK(step_pos(arp)  < step_pos(inst));   // the note chain is ordered...
        CHECK(step_pos(inst) < step_pos(out));    // ...and audio still follows it

        // Note buffers: one per EMITTING node (midi, arp) — the instrument only consumes.
        CHECK(cg.note_buf_count == 2);
        const CompiledStep& s_inst = cg.steps[static_cast<size_t>(step_pos(inst))];
        CHECK(s_inst.n_note_in == 1);
        CHECK(s_inst.note_out_buf == -1);
        const CompiledStep& s_arp = cg.steps[static_cast<size_t>(step_pos(arp))];
        CHECK(s_arp.n_note_in == 1);              // reads MidiIn
        CHECK(s_arp.note_out_buf >= 0);           // and writes its own
        CHECK(s_inst.note_in_buf[0] == s_arp.note_out_buf);   // ...which is what the instrument reads
    }

    // --- 14. Note fan-out + merge: one MidiIn feeds two instruments; two feed one ---
    {
        AudioGraph g;
        const int midi = g.add_node(true,  false, nullptr, nullptr, "midi");
        const int i1   = g.add_node(true,  false, nullptr, nullptr, "i1");
        const int i2   = g.add_node(true,  false, nullptr, nullptr, "i2");
        const int out  = g.add_node(false, true,  nullptr, nullptr, "out");
        g.set_output_id(out);
        g.set_note_ports(midi, false, true);
        g.set_note_ports(i1, true, false);
        g.set_note_ports(i2, true, false);
        CHECK(g.connect(midi, i1, EdgeKind::Note));
        CHECK(g.connect(midi, i2, EdgeKind::Note));   // fan-out: one stream, two consumers
        g.connect(i1, out); g.connect(i2, out);

        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        CHECK(cg.note_buf_count == 1);                // only MidiIn emits
        CHECK(cg.steps.size() == 4);
    }

    // --- 15. The same pair may carry BOTH signals; dedup is per kind ---
    {
        AudioGraph g;
        const int a = g.add_node(true,  false, nullptr, nullptr, "a");
        const int b = g.add_node(false, false, nullptr, nullptr, "b");
        g.set_note_ports(a, false, true);
        g.set_note_ports(b, true, false);
        CHECK(g.connect(a, b, EdgeKind::Audio));
        CHECK(g.connect(a, b, EdgeKind::Note));    // NOT a duplicate: a different signal
        CHECK(!g.connect(a, b, EdgeKind::Audio));  // ...but this one is
        CHECK(!g.connect(a, b, EdgeKind::Note));
        CHECK(g.edges().size() == 2);

        g.disconnect(a, b, EdgeKind::Note);        // removing one kind leaves the other
        CHECK(g.edges().size() == 1);
        CHECK(g.edges()[0].kind == EdgeKind::Audio);
    }

    // --- 16. A cycle through NOTE edges is rejected like any other ---
    {
        AudioGraph g;
        const int x = g.add_node(true, false, nullptr, nullptr, "x");
        const int y = g.add_node(true, false, nullptr, nullptr, "y");
        g.set_note_ports(x, true, true);
        g.set_note_ports(y, true, true);
        g.connect(x, y, EdgeKind::Note);
        g.connect(y, x, EdgeKind::Note);
        CompiledAudioGraph cg;
        CHECK(!g.compile(cg));                     // the caller keeps its last good plan
    }

    // --- 17. remove_node_bridged heals each signal SEPARATELY ---
    // Bridging across kinds would turn a note wire into an audio wire: a graph that looks right and
    // means something else.
    {
        AudioGraph g;
        const int midi = g.add_node(true,  false, nullptr, nullptr, "midi");
        const int arp  = g.add_node(true,  false, nullptr, nullptr, "arp");
        const int inst = g.add_node(true,  false, nullptr, nullptr, "inst");
        const int out  = g.add_node(false, true,  nullptr, nullptr, "out");
        g.set_output_id(out);
        g.set_note_ports(midi, false, true);
        g.set_note_ports(arp,  true,  true);
        g.set_note_ports(inst, true,  false);
        g.connect(midi, arp,  EdgeKind::Note);
        g.connect(arp,  inst, EdgeKind::Note);
        g.connect(inst, out,  EdgeKind::Audio);

        g.remove_node_bridged(arp);   // pull the arpeggiator out of the note chain
        CHECK(g.node_index(arp) < 0);
        bool midi_to_inst_note = false, any_audio_from_midi = false;
        for (const AudioGraphEdge& e : g.edges()) {
            if (e.from_id == midi && e.to_id == inst && e.kind == EdgeKind::Note) midi_to_inst_note = true;
            if (e.from_id == midi && e.kind == EdgeKind::Audio) any_audio_from_midi = true;
        }
        CHECK(midi_to_inst_note);     // the note chain healed...
        CHECK(!any_audio_from_midi);  // ...and did NOT become an audio wire
        CHECK(has_edge(g, inst, out));
    }

    // --- 18. Curated inspector: pinned param set (pure curation; add order; idempotent) ---
    {
        AudioGraph g;
        const int a = g.add_node(false, false, nullptr, nullptr, "a");
        const int b = g.add_node(false, true,  nullptr, nullptr, "b");
        CHECK(g.node_pinned(a) && g.node_pinned(a)->empty());   // nothing pinned yet
        g.pin_param(a, 5);
        g.pin_param(a, 2);
        g.pin_param(a, 5);                                       // idempotent — no duplicate
        CHECK(g.is_param_pinned(a, 5) && g.is_param_pinned(a, 2));
        CHECK(!g.is_param_pinned(a, 9));
        const std::vector<int>* v = g.node_pinned(a);
        CHECK(v && v->size() == 2);
        CHECK((*v)[0] == 5 && (*v)[1] == 2);                    // add order preserved
        g.unpin_param(a, 5);
        CHECK(!g.is_param_pinned(a, 5));
        CHECK(g.node_pinned(a)->size() == 1 && (*g.node_pinned(a))[0] == 2);
        CHECK(g.node_pinned(b)->empty());                       // sibling is independent
        CHECK(g.node_pinned(999) == nullptr);                   // absent node → nullptr, no crash
        g.pin_param(a, -1);                                     // invalid index ignored
        CHECK(g.node_pinned(a)->size() == 1);
    }

    // ===== ADR-0022 (P0): control is a THIRD signal in the graph =====

    // --- 19. PARITY: a graph with no control edges compiles exactly as it always did ---
    // The migration gate, and the mirror of case 12. If a third edge kind changed anything about a
    // graph that has no modulator, every existing project would be affected by a feature it does
    // not use.
    {
        AudioGraph g;
        const int src = g.add_node(true,  false, nullptr, nullptr, "src");
        const int fx  = g.add_node(false, false, nullptr, nullptr, "fx");
        const int out = g.add_node(false, true,  nullptr, nullptr, "out");
        g.set_output_id(out);
        g.connect(src, fx);
        g.connect(fx, out);

        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        CHECK(cg.control_buf_count == 0);       // no modulator => no control pool at all
        CHECK(cg.note_buf_count == 0);
        CHECK(cg.buf_count == 3);
        CHECK(cg.output_buf == g.node_index(out));
        for (const CompiledStep& s : cg.steps) {
            CHECK(s.n_control_in == 0);
            CHECK(s.control_out_buf == -1);     // every step is control-free, exactly as before
        }
    }

    // --- 20. Control edges constrain execution ORDER ---
    // A modulator must run BEFORE the node whose param it drives — even though no audio and no
    // notes flow between them. Getting this wrong means the target reads last block's value.
    //
    // The LFO is added LAST on purpose. Added first it would land early in the topo queue anyway
    // (both are indegree-0 roots, emitted in insertion order), so the assertion would hold even if
    // control edges constrained nothing — a test that passes by luck. Declared last, the ONLY
    // thing that can pull it ahead of the filter is the control edge itself.
    {
        AudioGraph g;
        const int src = g.add_node(true,  false, nullptr, nullptr, "src");
        const int flt = g.add_node(false, false, nullptr, nullptr, "filter");
        const int out = g.add_node(false, true,  nullptr, nullptr, "out");
        const int lfo = g.add_node(true,  false, nullptr, nullptr, "lfo");
        g.set_output_id(out);
        g.set_control_ports(lfo, false, true);
        g.set_control_ports(flt, true,  false);
        g.connect(src, flt);
        g.connect(flt, out);
        CHECK(g.connect_control(lfo, flt, /*dest_param=*/3));

        CompiledAudioGraph cg;
        CHECK(g.compile(cg));

        auto step_pos = [&](int id) {
            const int b = g.node_index(id);
            for (size_t i = 0; i < cg.steps.size(); ++i) if (cg.steps[i].out_buf == b) return static_cast<int>(i);
            return -1;
        };
        CHECK(step_pos(lfo) < step_pos(flt));   // the modulator is ordered before its target...
        CHECK(step_pos(flt) < step_pos(out));   // ...and audio still follows

        // The control edge must NOT have been summed into the filter's audio inputs: the filter
        // takes audio from `src` alone. (An LFO that leaks into the audio sum is a DC offset.)
        const CompiledStep& s_flt = cg.steps[static_cast<size_t>(step_pos(flt))];
        CHECK(s_flt.n_in == 1);
        CHECK(s_flt.in_buf[0] == g.node_index(src));
        CHECK(s_flt.n_note_in == 0);            // ...nor merged into its notes
    }

    // --- 21. Control buffers + the shaper survive compile ---
    {
        AudioGraph g;
        const int lfo = g.add_node(true,  false, nullptr, nullptr, "lfo");
        const int flt = g.add_node(false, true,  nullptr, nullptr, "filter");
        g.set_output_id(flt);
        g.set_control_ports(lfo, false, true);
        g.set_control_ports(flt, true,  false);
        ControlShape sh;
        sh.amount = 0.5f; sh.curve = -0.25f; sh.invert = true; sh.out_lo = 0.2f; sh.out_hi = 0.8f;
        CHECK(g.connect_control(lfo, flt, 7, sh));

        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        CHECK(cg.control_buf_count == 1);       // only the LFO emits

        auto step_of = [&](int id) -> const CompiledStep& {
            const int b = g.node_index(id);
            for (const CompiledStep& s : cg.steps) if (s.out_buf == b) return s;
            return cg.steps[0];
        };
        const CompiledStep& s_lfo = step_of(lfo);
        const CompiledStep& s_flt = step_of(flt);
        CHECK(s_lfo.control_out_buf >= 0);      // the emitter writes a buffer...
        CHECK(s_lfo.n_control_in == 0);
        CHECK(s_flt.control_out_buf == -1);     // ...a pure consumer emits nothing
        CHECK(s_flt.n_control_in == 1);
        CHECK(s_flt.control_in[0].src_buf == s_lfo.control_out_buf);   // ...and reads the emitter's
        CHECK(s_flt.control_in[0].param == 7);                          // the opaque selector rides through
        CHECK(std::fabs(s_flt.control_in[0].shape.amount - 0.5f) < 1e-6f);
        CHECK(std::fabs(s_flt.control_in[0].shape.curve + 0.25f) < 1e-6f);
        CHECK(s_flt.control_in[0].shape.invert);
        CHECK(std::fabs(s_flt.control_in[0].shape.out_lo - 0.2f) < 1e-6f);
        CHECK(std::fabs(s_flt.control_in[0].shape.out_hi - 0.8f) < 1e-6f);
    }

    // --- 22. Dedup is PER PARAM, and a selector-less control edge is impossible ---
    // One LFO driving two params of one node is the normal case — the (from, to, kind) rule the
    // other signals use would have wrongly called the second one a duplicate.
    {
        AudioGraph g;
        const int lfo = g.add_node(true,  false, nullptr, nullptr, "lfo");
        const int flt = g.add_node(false, true,  nullptr, nullptr, "filter");
        g.set_control_ports(lfo, false, true);
        g.set_control_ports(flt, true,  false);

        CHECK(g.connect_control(lfo, flt, 1));    // cutoff
        CHECK(g.connect_control(lfo, flt, 2));    // resonance — same pair, different param
        CHECK(!g.connect_control(lfo, flt, 1));   // ...but the same param twice is a duplicate
        CHECK(!g.connect_control(lfo, lfo, 1));   // self-loop
        CHECK(!g.connect_control(lfo, flt, -1));  // no selector
        CHECK(!g.connect_control(lfo, 999, 1));   // absent endpoint
        CHECK(g.edges().size() == 2);

        // The kind-based entry points refuse Control outright: an edge with no selector could
        // never be applied, so it must not be creatable.
        CHECK(!g.connect(lfo, flt, EdgeKind::Control));
        CHECK(g.edges().size() == 2);
        g.disconnect(lfo, flt, EdgeKind::Control);   // no-op: (from,to) doesn't identify one
        CHECK(g.edges().size() == 2);

        g.disconnect_control(lfo, flt, 1);
        CHECK(g.edges().size() == 1);
        CHECK(g.edges()[0].dest_param == 2);         // the OTHER param's edge survived

        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        CHECK(cg.control_buf_count == 1);
    }

    // --- 23. A cycle through CONTROL edges is rejected like any other ---
    // A modulator driven by something it drives is a cycle in the one DAG. P0 rejects it (a
    // last-block/deferred edge would be an explicit future decision, not an accident).
    {
        AudioGraph g;
        const int x = g.add_node(true, false, nullptr, nullptr, "x");
        const int y = g.add_node(true, false, nullptr, nullptr, "y");
        g.set_control_ports(x, true, true);
        g.set_control_ports(y, true, true);
        CHECK(g.connect_control(x, y, 0));
        CHECK(g.connect_control(y, x, 0));
        CompiledAudioGraph cg;
        CHECK(!g.compile(cg));                     // the caller keeps its last good plan
    }

    // --- 24. remove_node_bridged DROPS control edges rather than re-pointing a selector ---
    // Bridging the removed node's own modulator onto its targets would aim param selector 5 (the
    // LFO's rate) at whatever param the LFO happened to drive. Dropping is the honest outcome.
    {
        AudioGraph g;
        const int src  = g.add_node(true,  false, nullptr, nullptr, "src");
        const int lfo2 = g.add_node(true,  false, nullptr, nullptr, "lfo2");
        const int lfo  = g.add_node(true,  false, nullptr, nullptr, "lfo");
        const int flt  = g.add_node(false, false, nullptr, nullptr, "filter");
        const int out  = g.add_node(false, true,  nullptr, nullptr, "out");
        g.set_output_id(out);
        g.set_control_ports(lfo2, false, true);
        g.set_control_ports(lfo,  true,  true);    // an LFO whose rate is itself modulated
        g.set_control_ports(flt,  true,  false);
        g.connect(src, flt);
        g.connect(flt, out);
        CHECK(g.connect_control(lfo2, lfo, 5));    // lfo2 -> lfo.rate
        CHECK(g.connect_control(lfo,  flt, 3));    // lfo  -> filter.cutoff

        g.remove_node_bridged(lfo);
        CHECK(g.node_index(lfo) < 0);
        for (const AudioGraphEdge& e : g.edges())
            CHECK(e.kind != EdgeKind::Control);    // both control edges died with the node...
        CHECK(has_edge(g, src, flt));              // ...while the audio path healed as ever
        CHECK(has_edge(g, flt, out));
    }

    // --- 25. An empty graph clears every count (a stale one would outlive its nodes) ---
    {
        AudioGraph g;
        const int lfo = g.add_node(true,  false, nullptr, nullptr, "lfo");
        const int flt = g.add_node(false, true,  nullptr, nullptr, "filter");
        g.set_output_id(flt);
        g.set_control_ports(lfo, false, true);
        g.set_note_ports(lfo, false, true);
        g.set_control_ports(flt, true, false);
        CHECK(g.connect_control(lfo, flt, 0));
        CompiledAudioGraph cg;
        CHECK(g.compile(cg));
        CHECK(cg.control_buf_count == 1 && cg.note_buf_count == 1);

        g.clear();                                 // recompiling the now-empty graph into the SAME plan
        CHECK(g.compile(cg));
        CHECK(cg.steps.empty());
        CHECK(cg.buf_count == 0);
        CHECK(cg.control_buf_count == 0);          // ...must not leave the old pool sizes behind
        CHECK(cg.note_buf_count == 0);
    }

    return vivid::test::summary("test_audio_graph");
}
