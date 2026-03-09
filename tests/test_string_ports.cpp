#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include "runtime/scheduler.h"

#include <cstdio>
#include <filesystem>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    std::string staging = "./.test_string_ports_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file("string_source_op.dylib", staging + "/string_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("string_sink_op.dylib", staging + "/string_sink_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("test_op_v1.dylib", staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry scan succeeds");
    check(registry.find("StringSourceOp") != nullptr, "StringSourceOp present");
    check(registry.find("StringSinkOp") != nullptr, "StringSinkOp present");

    // String scalar + spread routing.
    {
        vivid::Graph g;
        g.add_node("src", "StringSourceOp", {});
        g.add_node("sink", "StringSinkOp", {});
        g.add_connection("src", "out", "sink", "in");
        g.add_connection("src", "list", "sink", "in_list");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build string graph succeeds");
        sched.tick(0.0, 0.016, 0);

        auto* sink = sched.find_node_mut("sink");
        check(sink != nullptr, "sink found");
        if (sink) {
            auto out_it = sink->output_port_indices.find("out");
            auto list_it = sink->output_port_indices.find("out_list");
            auto valid_it = sink->output_port_indices.find("valid");
            auto count_it = sink->output_port_indices.find("count");
            check(out_it != sink->output_port_indices.end(), "sink out exists");
            check(list_it != sink->output_port_indices.end(), "sink out_list exists");
            check(valid_it != sink->output_port_indices.end(), "sink valid exists");
            check(count_it != sink->output_port_indices.end(), "sink count exists");
            if (out_it != sink->output_port_indices.end()) {
                check(sink->output_string_values[out_it->second] == "alpha", "string scalar routed");
            }
            if (list_it != sink->output_port_indices.end()) {
                check(sink->output_string_spreads[list_it->second].size() == 3, "string spread size routed");
            }
            if (valid_it != sink->output_port_indices.end()) {
                check(sink->output_values[valid_it->second] > 0.5f, "valid output true");
            }
            if (count_it != sink->output_port_indices.end()) {
                check(static_cast<int>(sink->output_values[count_it->second]) == 3, "count output is 3");
            }
        }
        sched.shutdown();
    }

    // Mixed type rejection: numeric -> string input should fail build.
    {
        vivid::Graph g;
        g.add_node("num", "TestOp", {});
        g.add_node("sink", "StringSinkOp", {});
        g.add_connection("num", "out", "sink", "in");
        vivid::Scheduler sched;
        check(!sched.build(g, registry), "mixed numeric->string wire rejected");
    }

    // String fan-in rejection.
    {
        vivid::Graph g;
        g.add_node("a", "StringSourceOp", {});
        g.add_node("b", "StringSourceOp", {});
        g.add_node("sink", "StringSinkOp", {});
        g.add_connection("a", "out", "sink", "in");
        g.add_connection("b", "out", "sink", "in");
        vivid::Scheduler sched;
        check(!sched.build(g, registry), "string scalar fan-in >1 rejected");
    }

    return failures == 0 ? 0 : 1;
}
