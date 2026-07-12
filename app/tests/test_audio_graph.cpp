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

    return vivid::test::summary("test_audio_graph");
}
