// Unit tests for Phase 2 safe-mode disabled-node handling in GraphCompiler.
//
// No GPU / audio / window / fixture operators.  Uses the same pattern as
// tests/graph/test_graph_compiler.cpp: unknown operator types + empty
// registry land in the missing_operator placeholder branch, which is where
// the disabled-node machinery also lives.

#include "runtime/core/safe_mode.h"
#include "runtime/core/crash_recovery.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/graph_compiler.h"
#include "runtime/operators/operator_registry.h"

#include "test_helpers.h"

#include <cstdio>

namespace {

vivid::Graph make_three_unknown_nodes() {
    vivid::Graph g;
    g.add_node("a", "UnknownType");
    g.add_node("b", "UnknownType");
    g.add_node("c", "OtherType");
    g.add_connection("a", "out", "b", "in");
    g.add_connection("b", "out", "c", "in");
    return g;
}

// ---------------------------------------------------------------------------
// Compiler-level tests
// ---------------------------------------------------------------------------

void test_disable_by_id() {
    std::fprintf(stderr, "\n--- safe-mode: disable by ID ---\n");
    vivid::Graph g = make_three_unknown_nodes();
    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;
    opts.disabled_node_ids = {"b"};

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compile succeeded");
    if (!cg) return;
    check(cg->nodes.size() == 3, "three nodes in compiled graph");

    const vivid::CompiledNode* a = cg->find_node("a");
    const vivid::CompiledNode* b = cg->find_node("b");
    const vivid::CompiledNode* c = cg->find_node("c");
    check(a && b && c, "all three nodes present");
    if (!a || !b || !c) return;

    // a and c are unknown types with an empty registry → "not_found"
    check(a->missing_operator && a->missing_operator_reason == "not_found",
          "a: not_found (not disabled)");
    check(c->missing_operator && c->missing_operator_reason == "not_found",
          "c: not_found (not disabled)");

    check(b->missing_operator, "b: missing_operator = true");
    check(b->missing_operator_reason == "disabled", "b: reason = \"disabled\"");
    check(b->missing_operator_detail == "Disabled by safe mode (crash recovery)",
          "b: exact detail string");
    check(b->instance == nullptr, "b: no operator instance");
}

void test_disable_by_type() {
    std::fprintf(stderr, "\n--- safe-mode: disable by type ---\n");
    vivid::Graph g = make_three_unknown_nodes();
    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;
    opts.disabled_types = {"UnknownType"};

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compile succeeded");
    if (!cg) return;
    const auto* a = cg->find_node("a");
    const auto* b = cg->find_node("b");
    const auto* c = cg->find_node("c");
    if (!a || !b || !c) { check(false, "all three nodes present"); return; }

    check(a->missing_operator_reason == "disabled", "a: disabled by type");
    check(b->missing_operator_reason == "disabled", "b: disabled by type");
    check(c->missing_operator_reason == "not_found", "c: different type, not disabled");
}

void test_connections_to_disabled_node_are_inert() {
    std::fprintf(stderr, "\n--- safe-mode: connections touching disabled node are inert ---\n");
    // Pass 2's edge validation (graph_compiler.cpp:369) skips type checking for
    // missing_operator nodes, and FrameExecutor:297 skips missing_operator
    // nodes at tick time.  Net effect: the disabled node cannot execute, and
    // compile does not fail due to spurious type mismatches on its edges.
    vivid::Graph g = make_three_unknown_nodes();   // a->b, b->c
    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;
    opts.disabled_node_ids = {"b"};

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compile succeeded with disabled middle node");
    if (!cg) return;

    const auto* b = cg->find_node("b");
    check(b && b->missing_operator,        "b flagged missing_operator");
    check(b && b->instance == nullptr,     "b has no operator instance");
    check(b && b->loader   == nullptr,     "b has no loader — cannot be invoked");
}

void test_disabled_wins_over_not_found() {
    std::fprintf(stderr, "\n--- safe-mode: disabled classification wins over not_found ---\n");
    vivid::Graph g;
    g.add_node("x", "SomeUnknownType");
    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;
    opts.disabled_types = {"SomeUnknownType"};

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compile succeeded");
    if (!cg) return;
    const auto* x = cg->find_node("x");
    check(x && x->missing_operator_reason == "disabled",
          "reason is 'disabled', not 'not_found'");
}

void test_empty_disabled_sets_unchanged() {
    std::fprintf(stderr, "\n--- safe-mode: empty disabled sets → normal compile ---\n");
    vivid::Graph g = make_three_unknown_nodes();
    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;
    // Leave disabled_node_ids and disabled_types empty.

    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compile succeeded");
    if (!cg) return;
    for (const auto& cn : cg->nodes) {
        check(cn.missing_operator_reason != "disabled",
              "no node marked disabled");
    }
}

// ---------------------------------------------------------------------------
// compute_safe_mode_config() tests
// ---------------------------------------------------------------------------

void test_compute_config_null_record() {
    std::fprintf(stderr, "\n--- safe-mode config: null CrashRecord ---\n");
    vivid::SafeModeConfig c = vivid::compute_safe_mode_config(nullptr);
    check(c.active, "active=true even without crash record");
    check(c.disabled_node_ids.empty(), "no disabled IDs");
    check(c.disabled_types.empty(),    "no disabled types");
    check(c.crash_operator.empty(),    "no crash operator");
}

void test_compute_config_full_record() {
    std::fprintf(stderr, "\n--- safe-mode config: full CrashRecord ---\n");
    vivid::CrashRecord rec;
    rec.operator_name = "blur";
    rec.node_type     = "blur";
    rec.node_id       = "blur1";
    rec.signal_name   = "SIGSEGV";

    vivid::SafeModeConfig c = vivid::compute_safe_mode_config(&rec);
    check(c.active,                                     "active=true");
    check(c.disabled_types.count("blur") == 1,          "blur type disabled");
    check(c.disabled_node_ids.count("blur1") == 1,      "blur1 id disabled");
    check(c.crash_operator == "blur",                   "crash_operator copied");
    check(c.crash_node_id  == "blur1",                  "crash_node_id copied");
    check(c.crash_reason   == "SIGSEGV",                "crash_reason copied");
}

void test_compute_config_operator_name_fallback() {
    std::fprintf(stderr, "\n--- safe-mode config: operator_name fallback ---\n");
    vivid::CrashRecord rec;
    rec.operator_name = "lfo";   // pre-Phase-1 marker expansion may leave node_type empty
    rec.node_type     = "";
    rec.node_id       = "";
    rec.signal_name   = "SIGABRT";

    vivid::SafeModeConfig c = vivid::compute_safe_mode_config(&rec);
    check(c.active,                              "active=true");
    check(c.disabled_types.count("lfo") == 1,    "falls back to operator_name");
    check(c.disabled_node_ids.empty(),           "no node id → no id-set entry");
}

} // namespace

int main(int, char**) {
    test_disable_by_id();
    test_disable_by_type();
    test_connections_to_disabled_node_are_inert();
    test_disabled_wins_over_not_found();
    test_empty_disabled_sets_unchanged();
    test_compute_config_null_record();
    test_compute_config_full_record();
    test_compute_config_operator_name_fallback();

    std::fprintf(stderr, "\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
