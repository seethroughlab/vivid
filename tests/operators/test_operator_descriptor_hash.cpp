// test_operator_descriptor_hash.cpp — stability + sensitivity of the
// descriptor fingerprint used by the project-lockfile feature.
#include "runtime/operators/operator_descriptor_hash.h"
#include "operator_api/types.h"

#include <cstdio>
#include <string>

#include "test_helpers.h"

using namespace vivid;

namespace {

// Minimal descriptor builder. Holds owned copies of strings so the test
// fixtures stay valid for the lifetime of the test.
struct DescriptorFixture {
    std::string name;
    std::vector<std::string> param_names;
    std::vector<std::string> port_names;
    std::vector<VividParamDescriptor> params;
    std::vector<VividPortDescriptor> ports;
    VividOperatorDescriptor desc{};

    void set_name(std::string n) { name = std::move(n); desc.name = name.c_str(); }

    void add_param(std::string pname, VividParamType type = 0 /*FLOAT*/,
                   float def = 0.0f) {
        param_names.push_back(std::move(pname));
        VividParamDescriptor p{};
        p.name = param_names.back().c_str();
        p.type = type;
        p.default_value = def;
        p.min_value = 0.0f;
        p.max_value = 1.0f;
        params.push_back(p);
        desc.params = params.data();
        desc.param_count = static_cast<uint32_t>(params.size());
    }

    void add_port(std::string pname, VividPortType type = 0,
                  VividPortDirection dir = VIVID_PORT_OUTPUT) {
        port_names.push_back(std::move(pname));
        VividPortDescriptor p{};
        p.name = port_names.back().c_str();
        p.type = type;
        p.direction = dir;
        ports.push_back(p);
        desc.ports = ports.data();
        desc.port_count = static_cast<uint32_t>(ports.size());
    }
};

DescriptorFixture make_baseline() {
    DescriptorFixture f;
    f.set_name("Baseline");
    f.add_param("gain", 0, 0.5f);
    f.add_param("mode", 0, 0.0f);
    f.add_port("out", 0, VIVID_PORT_OUTPUT);
    f.desc.time_dependent       = 0;
    f.desc.has_process_audio    = 0;
    f.desc.has_process_gpu      = 0;
    f.desc.has_process_frame    = 1;
    f.desc.multiplicity_behavior = VIVID_MULTIPLICITY_MAP;
    f.desc.strategy_independent = 0;
    return f;
}

void test_descriptor_hash_deterministic() {
    auto f = make_baseline();
    auto h1 = operator_descriptor_hash(&f.desc);
    auto h2 = operator_descriptor_hash(&f.desc);
    check(h1 == h2, "descriptor_hash: deterministic across calls");
    check(h1.rfind("sha256:", 0) == 0, "descriptor_hash: starts with sha256:");
    check(h1.size() == 7 + 64, "descriptor_hash: 7 + 64 chars total");
}

void test_descriptor_hash_null_returns_empty() {
    check(operator_descriptor_hash(nullptr).empty(),
          "descriptor_hash: null descriptor returns empty");
}

void test_descriptor_hash_changes_with_name() {
    auto a = make_baseline();
    auto b = make_baseline();
    b.set_name("Baseline2");
    check(operator_descriptor_hash(&a.desc) != operator_descriptor_hash(&b.desc),
          "descriptor_hash: changes with operator name");
}

void test_descriptor_hash_changes_with_param_rename() {
    auto a = make_baseline();
    auto b = make_baseline();
    b.param_names[0] = "GAIN";  // renamed
    b.params[0].name = b.param_names[0].c_str();
    check(operator_descriptor_hash(&a.desc) != operator_descriptor_hash(&b.desc),
          "descriptor_hash: changes when a param is renamed");
}

void test_descriptor_hash_changes_with_param_default() {
    auto a = make_baseline();
    auto b = make_baseline();
    b.params[0].default_value = 0.9f;
    check(operator_descriptor_hash(&a.desc) != operator_descriptor_hash(&b.desc),
          "descriptor_hash: changes when a param default changes");
}

void test_descriptor_hash_changes_with_port_type() {
    auto a = make_baseline();
    auto b = make_baseline();
    b.ports[0].type = 1;  // different port type
    check(operator_descriptor_hash(&a.desc) != operator_descriptor_hash(&b.desc),
          "descriptor_hash: changes when a port type changes");
}

void test_descriptor_hash_changes_with_flag() {
    auto a = make_baseline();
    auto b = make_baseline();
    b.desc.time_dependent = 1;  // flag flipped
    check(operator_descriptor_hash(&a.desc) != operator_descriptor_hash(&b.desc),
          "descriptor_hash: changes when a capability flag flips");
}

void test_descriptor_hash_stable_for_added_param() {
    // Adding a param must change the hash (added interface surface).
    auto a = make_baseline();
    auto b = make_baseline();
    b.add_param("extra", 0, 0.0f);
    check(operator_descriptor_hash(&a.desc) != operator_descriptor_hash(&b.desc),
          "descriptor_hash: changes when a param is added");
}

}  // namespace

int main() {
    test_descriptor_hash_deterministic();
    test_descriptor_hash_null_returns_empty();
    test_descriptor_hash_changes_with_name();
    test_descriptor_hash_changes_with_param_rename();
    test_descriptor_hash_changes_with_param_default();
    test_descriptor_hash_changes_with_port_type();
    test_descriptor_hash_changes_with_flag();
    test_descriptor_hash_stable_for_added_param();

    if (failures == 0) {
        std::fprintf(stderr, "All operator_descriptor_hash tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d operator_descriptor_hash failure(s).\n", failures);
    return 1;
}
