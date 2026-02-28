#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, const char* msg) {
    if (std::fabs(actual - expected) > 1e-4f) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

// =====================================================================
// Test Euclidean algorithm by building a graph and reading spread output
// =====================================================================
static void test_euclidean(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n=== Euclidean Algorithm Tests ===\n");

    auto run_euclidean = [&](int hits, int steps, int rotation,
                             const std::vector<int>& expected, const char* label) {
        vivid::Graph g;
        g.add_node("e", "Euclidean", {
            {"hits", static_cast<float>(hits)},
            {"steps", static_cast<float>(steps)},
            {"rotation", static_cast<float>(rotation)},
            {"gate_length", 0.5f},
            {"rate", 2.0f}  // 1/4
        });

        vivid::Scheduler sched;
        if (!sched.build(g, registry)) {
            std::fprintf(stderr, "  FAIL: %s - build failed\n", label);
            failures++;
            return;
        }

        sched.tick(0.0, 0.016, 0);

        auto* node = sched.find_node_mut("e");
        check(node != nullptr, label);
        if (!node) { sched.shutdown(); return; }

        // Pattern is output port 3 (trigger=0, gate=1, step=2, pattern=3)
        auto& spread = node->output_spreads[3];
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s: spread length = %d (expected %d)",
                      label, static_cast<int>(spread.size()), steps);
        check(static_cast<int>(spread.size()) == steps, buf);

        if (static_cast<int>(spread.size()) == steps) {
            bool match = true;
            for (int i = 0; i < steps; ++i) {
                if (static_cast<int>(spread[i]) != expected[i]) {
                    match = false;
                    std::snprintf(buf, sizeof(buf), "%s: spread[%d] = %d, expected %d",
                                  label, i, static_cast<int>(spread[i]), expected[i]);
                    check(false, buf);
                }
            }
            if (match) {
                std::snprintf(buf, sizeof(buf), "%s: pattern correct", label);
                check(true, buf);
            }
        }

        sched.shutdown();
    };

    // Classic Euclidean rhythms
    run_euclidean(3, 8, 0, {1,0,0,1,0,0,1,0}, "E(3,8)");
    run_euclidean(4, 8, 0, {1,0,1,0,1,0,1,0}, "E(4,8)");
    run_euclidean(5, 8, 0, {1,0,1,1,0,1,1,0}, "E(5,8)");

    // Edge cases
    run_euclidean(0, 8, 0, {0,0,0,0,0,0,0,0}, "E(0,8) all zeros");
    run_euclidean(8, 8, 0, {1,1,1,1,1,1,1,1}, "E(8,8) all ones");

    // Rotation: E(3,8) rotated by 1 shifts pattern left
    // Original: [1,0,0,1,0,0,1,0]
    // Rotate 1: [0,0,1,0,0,1,0,1]
    run_euclidean(3, 8, 1, {0,0,1,0,0,1,0,1}, "E(3,8) rot=1");
}

// =====================================================================
// Test PatternSeq: beat-driven stepping and value output
// =====================================================================
static void test_pattern_seq(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n=== PatternSeq Tests ===\n");

    // Test basic step advancement using a source node to drive beat_phase
    {
        vivid::Graph g;
        // SpreadSourceOp outputs param_values[0] (base) as its scalar output
        g.add_node("phase_src", "SpreadSourceOp", {{"base", 0.0f}, {"count", 1.0f}});
        g.add_node("seq", "PatternSeq", {
            {"steps", 4.0f},
            {"rate", 2.0f},   // 1/4 note (multiplier=1.0)
            {"gate_length", 0.8f},
            {"probability", 1.0f},
            {"val_0", 10.0f},
            {"val_1", 20.0f},
            {"val_2", 30.0f},
            {"val_3", 40.0f}
        });
        g.add_connection("phase_src", "out", "seq", "beat_phase");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "PatternSeq build");

        auto* src = sched.find_node_mut("phase_src");
        auto* node = sched.find_node_mut("seq");
        check(node != nullptr, "PatternSeq node found");
        if (!node || !src) { sched.shutdown(); return; }

        // Tick at beat_phase=0.0 → step 0
        src->param_values[0] = 0.0f;
        sched.tick(0.0, 0.016, 0);
        check_float(node->output_values[0], 10.0f, "seq step 0: value=10");
        check_float(node->output_values[3], 0.0f, "seq step 0: step index=0");

        // Advance to near end of beat
        src->param_values[0] = 0.99f;
        sched.tick(0.016, 0.016, 1);

        // Wrap to start of beat 1
        src->param_values[0] = 0.01f;
        sched.tick(0.032, 0.016, 2);
        check_float(node->output_values[3], 1.0f, "seq after 1 beat: step=1");
        check_float(node->output_values[0], 20.0f, "seq step 1: value=20");

        // Verify spread output (output port 4 = pattern)
        auto& spread = node->output_spreads[4];
        check(spread.size() == 4, "seq spread has 4 elements");
        if (spread.size() == 4) {
            check_float(spread[0], 10.0f, "seq spread[0]=10");
            check_float(spread[1], 20.0f, "seq spread[1]=20");
            check_float(spread[2], 30.0f, "seq spread[2]=30");
            check_float(spread[3], 40.0f, "seq spread[3]=40");
        }

        sched.shutdown();
    }

    // Test probability=0.0 silences all
    {
        vivid::Graph g;
        g.add_node("seq", "PatternSeq", {
            {"steps", 4.0f},
            {"rate", 2.0f},
            {"gate_length", 0.8f},
            {"probability", 0.0f},
            {"val_0", 10.0f},
            {"val_1", 20.0f},
            {"val_2", 30.0f},
            {"val_3", 40.0f}
        });

        vivid::Scheduler sched;
        check(sched.build(g, registry), "PatternSeq prob=0 build");
        sched.tick(0.0, 0.016, 0);

        auto* node = sched.find_node_mut("seq");
        check_float(node->output_values[0], 0.0f, "seq prob=0: value=0 (silenced)");
        check_float(node->output_values[2], 0.0f, "seq prob=0: gate=0");
        sched.shutdown();
    }
}

// =====================================================================
// Test Stack: Concat and Interleave modes
// =====================================================================
static void test_stack(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n=== Stack Tests ===\n");

    // Test Concat: [1,2,3] + [2,4] = [1,2,3,2,4]
    {
        vivid::Graph g;
        g.add_node("src_a", "SpreadSourceOp", {{"base", 1.0f}, {"count", 3.0f}});
        g.add_node("src_b", "SpreadSourceOp", {{"base", 2.0f}, {"count", 2.0f}});
        g.add_node("stk", "Stack", {{"mode", 0.0f}});  // Concat
        g.add_connection("src_a", "out", "stk", "a");
        g.add_connection("src_b", "out", "stk", "b");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "Stack Concat build");
        sched.tick(0.0, 0.016, 0);

        auto* stk = sched.find_node_mut("stk");
        check(stk != nullptr, "Stack node found");
        if (!stk) { sched.shutdown(); return; }

        // SpreadSourceOp(base=1,count=3) → [1,2,3]
        // SpreadSourceOp(base=2,count=2) → [2,4]
        // Concat: [1,2,3,2,4]
        auto& out = stk->output_spreads[0];
        check(out.size() == 5, "Concat: 3+2=5 elements");
        if (out.size() == 5) {
            check_float(out[0], 1.0f, "concat[0]=1");
            check_float(out[1], 2.0f, "concat[1]=2");
            check_float(out[2], 3.0f, "concat[2]=3");
            check_float(out[3], 2.0f, "concat[3]=2");
            check_float(out[4], 4.0f, "concat[4]=4");
        }
        sched.shutdown();
    }

    // Test Interleave: [1,2,3] + [2,4,6] = [1,2,2,4,3,6]
    {
        vivid::Graph g;
        g.add_node("src_a", "SpreadSourceOp", {{"base", 1.0f}, {"count", 3.0f}});
        g.add_node("src_b", "SpreadSourceOp", {{"base", 2.0f}, {"count", 3.0f}});
        g.add_node("stk", "Stack", {{"mode", 1.0f}});  // Interleave
        g.add_connection("src_a", "out", "stk", "a");
        g.add_connection("src_b", "out", "stk", "b");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "Stack Interleave build");
        sched.tick(0.0, 0.016, 0);

        auto* stk = sched.find_node_mut("stk");
        check(stk != nullptr, "Stack interleave node found");
        if (!stk) { sched.shutdown(); return; }

        auto& out = stk->output_spreads[0];
        check(out.size() == 6, "Interleave: 3+3=6 elements");
        if (out.size() == 6) {
            check_float(out[0], 1.0f, "interleave[0]=1 (a[0])");
            check_float(out[1], 2.0f, "interleave[1]=2 (b[0])");
            check_float(out[2], 2.0f, "interleave[2]=2 (a[1])");
            check_float(out[3], 4.0f, "interleave[3]=4 (b[1])");
            check_float(out[4], 3.0f, "interleave[4]=3 (a[2])");
            check_float(out[5], 6.0f, "interleave[5]=6 (b[2])");
        }
        sched.shutdown();
    }

    // Test empty inputs skipped
    {
        vivid::Graph g;
        g.add_node("src_a", "SpreadSourceOp", {{"base", 5.0f}, {"count", 2.0f}});
        g.add_node("stk", "Stack", {{"mode", 0.0f}});
        g.add_connection("src_a", "out", "stk", "a");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "Stack empty inputs build");
        sched.tick(0.0, 0.016, 0);

        auto* stk = sched.find_node_mut("stk");
        auto& out = stk->output_spreads[0];
        check(out.size() == 2, "Stack only a connected: 2 elements");
        if (out.size() == 2) {
            check_float(out[0], 5.0f, "single input[0]=5");
            check_float(out[1], 10.0f, "single input[1]=10");
        }
        sched.shutdown();
    }
}

// =====================================================================
// Test Alternate: cycles between spread inputs
// =====================================================================
static void test_alternate(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n=== Alternate Tests ===\n");

    vivid::Graph g;
    g.add_node("phase_src", "SpreadSourceOp", {{"base", 0.0f}, {"count", 1.0f}});
    g.add_node("src_a", "SpreadSourceOp", {{"base", 1.0f}, {"count", 2.0f}});
    g.add_node("src_b", "SpreadSourceOp", {{"base", 10.0f}, {"count", 2.0f}});
    g.add_node("alt", "Alternate", {{"cycle", 0.0f}});  // Beat
    g.add_connection("phase_src", "out", "alt", "beat_phase");
    g.add_connection("src_a", "out", "alt", "a");
    g.add_connection("src_b", "out", "alt", "b");

    vivid::Scheduler sched;
    check(sched.build(g, registry), "Alternate build");

    auto* src = sched.find_node_mut("phase_src");
    auto* alt = sched.find_node_mut("alt");
    check(alt != nullptr, "Alternate node found");
    if (!alt || !src) { sched.shutdown(); return; }

    // Beat 0: phase=0.0 → active=(0/1)%2=0 → input a → [1,2]
    src->param_values[0] = 0.0f;
    sched.tick(0.0, 0.016, 0);

    auto& out = alt->output_spreads[0];
    check(out.size() == 2, "alt beat 0: 2 elements");
    if (out.size() >= 2) {
        check_float(out[0], 1.0f, "alt beat 0: [0]=1 (from a)");
        check_float(out[1], 2.0f, "alt beat 0: [1]=2 (from a)");
    }

    // Simulate beat wrap: advance to 0.99 then wrap to 0.01
    src->param_values[0] = 0.99f;
    sched.tick(0.016, 0.016, 1);
    src->param_values[0] = 0.01f;
    sched.tick(0.032, 0.016, 2);

    // Beat 1: active=(1/1)%2=1 → input b → [10,20]
    auto& out2 = alt->output_spreads[0];
    check(out2.size() == 2, "alt beat 1: 2 elements");
    if (out2.size() >= 2) {
        check_float(out2[0], 10.0f, "alt beat 1: [0]=10 (from b)");
        check_float(out2[1], 20.0f, "alt beat 1: [1]=20 (from b)");
    }

    sched.shutdown();
}

// =====================================================================
// Test PatTransform: individual and combined transforms
// =====================================================================
static void test_pat_transform(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n=== PatTransform Tests ===\n");

    auto run_transform = [&](float base, int count,
                             float reverse, float rotate, float scale, float offset,
                             float probability,
                             const std::vector<float>& expected,
                             const char* label) {
        vivid::Graph g;
        g.add_node("src", "SpreadSourceOp", {
            {"base", base}, {"count", static_cast<float>(count)}
        });
        g.add_node("pt", "PatTransform", {
            {"reverse", reverse},
            {"rotate", rotate},
            {"scale", scale},
            {"offset", offset},
            {"probability", probability}
        });
        g.add_connection("src", "out", "pt", "pattern");

        vivid::Scheduler sched;
        if (!sched.build(g, registry)) {
            std::fprintf(stderr, "  FAIL: %s - build failed\n", label);
            failures++;
            return;
        }
        sched.tick(0.0, 0.016, 0);

        auto* pt = sched.find_node_mut("pt");
        check(pt != nullptr, label);
        if (!pt) { sched.shutdown(); return; }

        auto& out = pt->output_spreads[0];
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s: length %d (expected %d)",
                      label, static_cast<int>(out.size()), static_cast<int>(expected.size()));
        check(out.size() == expected.size(), buf);

        if (out.size() == expected.size()) {
            bool match = true;
            for (size_t i = 0; i < expected.size(); ++i) {
                if (std::fabs(out[i] - expected[i]) > 0.01f) {
                    match = false;
                    std::snprintf(buf, sizeof(buf), "%s: [%d] = %f, expected %f",
                                  label, static_cast<int>(i), out[i], expected[i]);
                    check(false, buf);
                }
            }
            if (match) {
                std::snprintf(buf, sizeof(buf), "%s: values correct", label);
                check(true, buf);
            }
        }

        sched.shutdown();
    };

    // SpreadSourceOp(base=1, count=3) produces [1, 2, 3]

    // Reverse [1,2,3] = [3,2,1]
    run_transform(1.0f, 3, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f,
                  {3.0f, 2.0f, 1.0f}, "Reverse [1,2,3]");

    // Rotate [1,2,3,4] by 1 = [2,3,4,1]
    run_transform(1.0f, 4, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f,
                  {2.0f, 3.0f, 4.0f, 1.0f}, "Rotate [1,2,3,4] by 1");

    // Scale by 2.0: [1,2,3] → [2,4,6]
    run_transform(1.0f, 3, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f,
                  {2.0f, 4.0f, 6.0f}, "Scale [1,2,3] * 2");

    // Offset +10: [1,2,3] → [11,12,13]
    run_transform(1.0f, 3, 0.0f, 0.0f, 1.0f, 10.0f, 1.0f,
                  {11.0f, 12.0f, 13.0f}, "Offset [1,2,3] + 10");

    // Combined: reverse + rotate by 1 + scale by 2
    // [1,2,3] → reverse → [3,2,1] → rotate 1 → [2,1,3] → scale 2 → [4,2,6]
    run_transform(1.0f, 3, 1.0f, 1.0f, 2.0f, 0.0f, 1.0f,
                  {4.0f, 2.0f, 6.0f}, "Combined: reverse+rotate+scale");
}

// =====================================================================
// Integration test: Clock → Euclidean, Clock → PatternSeq,
//                   Euclidean/pattern → Stack/a, PatternSeq/pattern → Stack/b,
//                   Stack → PatTransform
// =====================================================================
static void test_integration(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n=== Integration Test ===\n");

    vivid::Graph g;
    g.add_node("clock", "Clock", {{"bpm", 120.0f}});
    g.add_node("euclid", "Euclidean", {
        {"hits", 3.0f}, {"steps", 8.0f}, {"rotation", 0.0f},
        {"gate_length", 0.5f}, {"rate", 2.0f}
    });
    g.add_node("seq", "PatternSeq", {
        {"steps", 4.0f}, {"rate", 2.0f}, {"gate_length", 0.8f}, {"probability", 1.0f},
        {"val_0", 1.0f}, {"val_1", 2.0f}, {"val_2", 3.0f}, {"val_3", 4.0f}
    });
    g.add_node("stk", "Stack", {{"mode", 0.0f}});
    g.add_node("xform", "PatTransform", {
        {"reverse", 0.0f}, {"rotate", 0.0f}, {"scale", 2.0f},
        {"offset", 0.0f}, {"probability", 1.0f}
    });

    g.add_connection("clock", "beat_phase", "euclid", "beat_phase");
    g.add_connection("clock", "beat_phase", "seq", "beat_phase");
    g.add_connection("euclid", "pattern", "stk", "a");
    g.add_connection("seq", "pattern", "stk", "b");
    g.add_connection("stk", "output", "xform", "pattern");

    vivid::Scheduler sched;
    check(sched.build(g, registry), "integration build");

    // Tick several frames
    for (int i = 0; i < 5; ++i)
        sched.tick(i * 0.016, 0.016, static_cast<uint64_t>(i));

    auto* stk = sched.find_node_mut("stk");
    auto* xform = sched.find_node_mut("xform");
    check(stk != nullptr, "stack node found");
    check(xform != nullptr, "transform node found");

    if (stk && xform) {
        // Stack concat: euclidean pattern (8 elements) + seq pattern (4 elements) = 12
        auto& stk_out = stk->output_spreads[0];
        check(stk_out.size() == 12, "stack output: 8+4=12 elements");

        // PatTransform: scale by 2, so each element should be doubled
        auto& xf_out = xform->output_spreads[0];
        check(xf_out.size() == 12, "transform output: 12 elements");

        if (stk_out.size() == 12 && xf_out.size() == 12) {
            bool all_doubled = true;
            for (size_t i = 0; i < 12; ++i) {
                if (std::fabs(xf_out[i] - stk_out[i] * 2.0f) > 0.01f) {
                    all_doubled = false;
                    break;
                }
            }
            check(all_doubled, "transform doubles all stack values");
        }
    }

    sched.shutdown();
}

int main() {
    std::string staging = "./.test_pattern_algebra_staging";
    std::filesystem::create_directories(staging);

    auto copy_op = [&](const char* name) {
        std::string src = std::string(name) + ".dylib";
        std::string dst = staging + "/" + src;
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };

    copy_op("euclidean");
    copy_op("pattern_seq");
    copy_op("stack");
    copy_op("alternate");
    copy_op("pat_transform");
    copy_op("spread_source_op");
    copy_op("clock");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("Euclidean") != nullptr, "Euclidean registered");
    check(registry.find("PatternSeq") != nullptr, "PatternSeq registered");
    check(registry.find("Stack") != nullptr, "Stack registered");
    check(registry.find("Alternate") != nullptr, "Alternate registered");
    check(registry.find("PatTransform") != nullptr, "PatTransform registered");
    check(registry.find("SpreadSourceOp") != nullptr, "SpreadSourceOp registered");
    check(registry.find("Clock") != nullptr, "Clock registered");

    test_euclidean(registry);
    test_pattern_seq(registry);
    test_stack(registry);
    test_alternate(registry);
    test_pat_transform(registry);
    test_integration(registry);

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
