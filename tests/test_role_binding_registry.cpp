#include "runtime/operator_registry.h"
#include "runtime/platform.h"
#include "operator_api/operator.h"
#include "operator_api/bound_control_instance.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

static VividProcessContext make_ctx(double time, double dt, uint64_t frame) {
    VividProcessContext ctx{};
    ctx.time       = time;
    ctx.delta_time = dt;
    ctx.frame      = frame;
    return ctx;
}

// Find the operator build directory containing .vivid plugins
static std::string find_operators_dir(const std::string& build_dir) {
    namespace fs = std::filesystem;
    // Check build_dir itself for plugin files
    for (auto& entry : fs::directory_iterator(build_dir)) {
        if (entry.path().extension() == vivid::kPluginSuffix)
            return build_dir;
    }
    return build_dir;
}

int main() {
    std::string build_dir = ".";

    // =====================================================================
    // Test 1: Registry probes bindable flag
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 1: Registry probes bindable flag ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        check(reg.is_bindable("Envelope"), "Envelope is bindable");
        check(reg.is_bindable("LFO"), "LFO is bindable");
        check(!reg.is_bindable("NonexistentOp"), "nonexistent op is not bindable");

        // Find a non-bindable type from the deferred set
        auto types = reg.type_names();
        bool found_non_bindable = false;
        for (const auto& t : types) {
            if (t != "Envelope" && t != "LFO" && !reg.is_bindable(t)) {
                std::fprintf(stderr, "    (non-bindable type found: %s)\n", t.c_str());
                found_non_bindable = true;
                break;
            }
        }
        check(found_non_bindable || types.size() <= 2,
              "found at least one non-bindable type (or only envelope/lfo present)");
    }

    // =====================================================================
    // Test 2: create_bindable returns valid instance
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 2: create_bindable returns valid instance ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        auto env = reg.create_bindable("Envelope");
        check(env != nullptr, "create_bindable(Envelope) returns non-null");

        if (env) {
            std::vector<vivid::ParamBase*> params;
            env->collect_params(params);
            check(params.size() == 7, "Envelope has 7 params");

            std::vector<VividPortDescriptor> ports;
            env->collect_ports(ports);
            check(!ports.empty(), "Envelope has ports");

            // Verify param names
            bool has_attack = false;
            for (auto* p : params) {
                if (std::strcmp(p->name, "attack") == 0) has_attack = true;
            }
            check(has_attack, "Envelope has 'attack' param");
        }
    }

    // =====================================================================
    // Test 3: Types without VIVID_EMBEDDABLE return nullptr
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 3: Non-bindable types return nullptr ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        auto result = reg.create_bindable("NonexistentType");
        check(result == nullptr, "create_bindable(NonexistentType) returns nullptr");

        // Find a real non-bindable type
        auto types = reg.type_names();
        for (const auto& t : types) {
            if (!reg.is_bindable(t)) {
                auto result2 = reg.create_bindable(t);
                check(result2 == nullptr,
                      (std::string("create_bindable(") + t + ") returns nullptr").c_str());
                break;
            }
        }
    }

    // =====================================================================
    // Test 4: Multiple instances are independent
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 4: Multiple instances are independent ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        auto env1 = reg.create_bindable("Envelope");
        auto env2 = reg.create_bindable("Envelope");
        check(env1 != nullptr && env2 != nullptr, "two Envelope instances created");

        if (env1 && env2) {
            // Wrap in BoundControlInstance — extract deleter for paired destruction
            auto d1 = env1.get_deleter();
            auto d2 = env2.get_deleter();
            vivid::BoundControlInstance slot1(env1.release(),
                [d1](vivid::OperatorBase* p) { d1(p); });
            vivid::BoundControlInstance slot2(env2.release(),
                [d2](vivid::OperatorBase* p) { d2(p); });

            // Different attack values
            slot1.set_param("attack", 0.01f);
            slot1.set_param("amplitude", 1.0f);
            slot2.set_param("attack", 1.0f);
            slot2.set_param("amplitude", 1.0f);

            // Gate on for both
            slot1.set_input("gate", 1.0f);
            slot2.set_input("gate", 1.0f);

            // Process a few frames
            double t = 0.0;
            double dt = 0.01;
            for (uint64_t i = 0; i < 5; ++i) {
                auto ctx = make_ctx(t, dt, i);
                slot1.process(&ctx);
                slot2.process(&ctx);
                t += dt;
            }

            float v1 = slot1.output("value");
            float v2 = slot2.output("value");
            // Fast attack should be further along than slow attack
            check(v1 > v2, "fast-attack envelope > slow-attack envelope");
            std::fprintf(stderr, "    (fast=%.4f, slow=%.4f)\n", v1, v2);
        }
    }

    // =====================================================================
    // Test 5: BoundControlInstance wrapping registry-created LFO
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 5: BoundControlInstance + registry LFO ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        auto lfo = reg.create_bindable("LFO");
        check(lfo != nullptr, "create_bindable(LFO) returns non-null");

        if (lfo) {
            auto d = lfo.get_deleter();
            vivid::BoundControlInstance slot(lfo.release(),
                [d](vivid::OperatorBase* p) { d(p); });

            slot.set_param("frequency", 1.0f);
            slot.set_param("amplitude", 1.0f);
            slot.set_param("offset", 0.0f);
            slot.set_param("waveform", 0.0f);  // sine

            // Process several frames
            auto ctx0 = make_ctx(0.0, 0.01, 0);
            slot.process(&ctx0);
            float v0 = slot.output("value");

            auto ctx1 = make_ctx(0.01, 0.01, 1);
            slot.process(&ctx1);
            float v1 = slot.output("value");

            check(v1 > v0, "LFO output increasing from t=0");

            // Process to near quarter cycle
            double t = 0.02;
            for (uint64_t i = 2; i < 25; ++i) {
                auto ctx = make_ctx(t, 0.01, i);
                slot.process(&ctx);
                t += 0.01;
            }
            float v_peak = slot.output("value");
            check_float(v_peak, 1.0f, 0.15f, "LFO near peak at ~quarter cycle");
        }
    }

    // =====================================================================
    // Test 6: Factory function via registry
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 6: Factory function via registry ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        // Factory returns raw pointer; the destroy function handles paired destruction.
        auto destroy_fn = [&reg](vivid::OperatorBase* p) {
            auto* loader = reg.find("Envelope");
            if (loader) loader->destroy_bindable_instance(p);
            else delete p;
        };
        auto factory_fn = [&reg]() -> vivid::OperatorBase* {
            auto handle = reg.create_bindable("Envelope");
            return handle.release();
        };

        auto initial = reg.create_bindable("Envelope");
        check(initial != nullptr, "factory produces initial instance");

        if (initial) {
            vivid::BoundControlInstance slot(initial.release(), destroy_fn, factory_fn);

            // Process with gate on
            slot.set_param("attack", 0.05f);
            slot.set_param("amplitude", 1.0f);
            slot.set_input("gate", 1.0f);

            double t = 0.0;
            double dt = 0.01;
            for (uint64_t i = 0; i < 10; ++i) {
                auto ctx = make_ctx(t, dt, i);
                slot.process(&ctx);
                t += dt;
            }
            float before_reset = slot.output("value");
            check(before_reset > 0.5f, "output > 0.5 before reset");

            // Reset — factory creates fresh instance
            slot.reset();

            auto ctx = make_ctx(0.0, dt, 0);
            slot.process(&ctx);
            float after_reset = slot.output("value");
            check_float(after_reset, 0.0f, 1e-6f, "output = 0 after reset (idle)");

            // Params should be back to defaults
            check_float(slot.param("attack"), 0.001f, 1e-6f, "attack back to default after reset");
        }
    }

    // =====================================================================
    // Test 7: Operator with no roles has role_binding_count == 0
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 7: No-role operator descriptor ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        const auto* desc = reg.probe_descriptor("Envelope");
        check(desc != nullptr, "Envelope descriptor probed");
        if (desc) {
            check(desc->role_binding_count == 0, "Envelope role_binding_count == 0");
            check(desc->role_bindings == nullptr, "Envelope role_bindings == nullptr");
        }
    }

    // =====================================================================
    // Test 8: Probed descriptor deep-copies role binding metadata
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 8: Role binding descriptor deep-copy (via direct descriptor) ===\n");
    {
        // Since no operators declare role bindings yet, we verify the zero case
        // round-trips correctly through deep_copy_descriptor by probing
        // an operator that has no role bindings (Envelope) and checking the fields.
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        const auto* desc = reg.probe_descriptor("LFO");
        check(desc != nullptr, "LFO descriptor probed");
        if (desc) {
            check(desc->role_binding_count == 0, "LFO role_binding_count == 0");
            check(desc->role_bindings == nullptr, "LFO role_bindings == nullptr");
        }

        // Also verify the fields exist on any probed descriptor
        auto types = reg.type_names();
        for (const auto& t : types) {
            const auto* d = reg.probe_descriptor(t);
            if (d) {
                // role_binding_count should be a sane value (0 for all current operators)
                check(d->role_binding_count <= 32,
                      (t + " has sane role_binding_count").c_str());
                if (d->role_binding_count == 0) {
                    check(d->role_bindings == nullptr,
                          (t + " null role_bindings when count==0").c_str());
                }
                break;  // just test one
            }
        }
    }

    // =====================================================================
    // Test 9: validate_role_binding — unknown role → kRoleNotFound
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 9: validate — unknown role_id ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        VividRoleBindingDescriptor roles[1]{};
        roles[0].role_id = "env";
        roles[0].accepted_domain = VIVID_DOMAIN_CONTROL;

        VividOperatorDescriptor host_desc{};
        host_desc.role_binding_count = 1;
        host_desc.role_bindings = roles;

        auto result = vivid::validate_role_binding(
            &host_desc, "nonexistent", "Envelope", "value", reg);
        check(result == vivid::RoleBindingValidation::kRoleNotFound,
              "unknown role → kRoleNotFound");
    }

    // =====================================================================
    // Test 10: validate_role_binding — non-bindable → kNotBindable
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 10: validate — non-bindable type ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        VividRoleBindingDescriptor roles[1]{};
        roles[0].role_id = "env";
        roles[0].accepted_domain = VIVID_DOMAIN_CONTROL;

        VividOperatorDescriptor host_desc{};
        host_desc.role_binding_count = 1;
        host_desc.role_bindings = roles;

        auto result = vivid::validate_role_binding(
            &host_desc, "env", "NonExistentType", "value", reg);
        check(result == vivid::RoleBindingValidation::kNotBindable,
              "nonexistent type → kNotBindable");
    }

    // =====================================================================
    // Test 11: validate_role_binding — type not in allowlist
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 11: validate — type not in allowlist ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        const char* allowed[] = {"LFO"};
        VividRoleBindingDescriptor roles[1]{};
        roles[0].role_id = "mod";
        roles[0].accepted_domain = VIVID_DOMAIN_CONTROL;
        roles[0].allowed_operator_types = allowed;
        roles[0].allowed_operator_type_count = 1;

        VividOperatorDescriptor host_desc{};
        host_desc.role_binding_count = 1;
        host_desc.role_bindings = roles;

        auto result = vivid::validate_role_binding(
            &host_desc, "mod", "Envelope", "value", reg);
        check(result == vivid::RoleBindingValidation::kTypeNotAllowed,
              "Envelope not in [LFO] → kTypeNotAllowed");
    }

    // =====================================================================
    // Test 12: validate_role_binding — domain mismatch
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 12: validate — domain mismatch ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        VividRoleBindingDescriptor roles[1]{};
        roles[0].role_id = "filter";
        roles[0].accepted_domain = VIVID_DOMAIN_GPU;  // Envelope is CONTROL domain

        VividOperatorDescriptor host_desc{};
        host_desc.role_binding_count = 1;
        host_desc.role_bindings = roles;

        auto result = vivid::validate_role_binding(
            &host_desc, "filter", "Envelope", "value", reg);
        check(result == vivid::RoleBindingValidation::kDomainMismatch,
              "CONTROL in GPU slot → kDomainMismatch");
    }

    // =====================================================================
    // Test 13: validate_role_binding — valid assignment → kOk
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 13: validate — valid assignment ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        const char* allowed[] = {"Envelope", "LFO"};
        VividRoleBindingDescriptor roles[1]{};
        roles[0].role_id = "env";
        roles[0].accepted_domain = VIVID_DOMAIN_CONTROL;
        roles[0].allowed_operator_types = allowed;
        roles[0].allowed_operator_type_count = 2;

        VividOperatorDescriptor host_desc{};
        host_desc.role_binding_count = 1;
        host_desc.role_bindings = roles;

        auto result = vivid::validate_role_binding(
            &host_desc, "env", "Envelope", "value", reg);
        check(result == vivid::RoleBindingValidation::kOk,
              "valid assignment → kOk");
    }

    // =====================================================================
    // Test 14: bindable_candidates — unfiltered returns Envelope and LFO
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 14: bindable_candidates (unfiltered) ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        auto candidates = reg.bindable_candidates();
        bool has_env = false, has_lfo = false;
        for (const auto& c : candidates) {
            if (c == "Envelope") has_env = true;
            if (c == "LFO") has_lfo = true;
        }
        check(has_env, "bindable_candidates includes Envelope");
        check(has_lfo, "bindable_candidates includes LFO");
        std::fprintf(stderr, "    (%zu candidates total)\n", candidates.size());
    }

    // =====================================================================
    // Test 15: bindable_candidates — filtered by allowlist returns subset
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 15: bindable_candidates (filtered) ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        const char* allowed[] = {"LFO"};
        VividRoleBindingDescriptor role{};
        role.role_id = "mod";
        role.accepted_domain = VIVID_DOMAIN_CONTROL;
        role.allowed_operator_types = allowed;
        role.allowed_operator_type_count = 1;

        auto candidates = reg.bindable_candidates(&role);
        check(candidates.size() == 1, "filtered candidates has 1 entry");
        check(!candidates.empty() && candidates[0] == "LFO",
              "filtered candidates is [LFO]");
    }

    // =====================================================================
    // Test 16: validate_role_binding — invalid output name → kOutputNotFound
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 16: validate — invalid output name ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        VividRoleBindingDescriptor roles[1]{};
        roles[0].role_id = "env";
        roles[0].accepted_domain = VIVID_DOMAIN_CONTROL;

        VividOperatorDescriptor host_desc{};
        host_desc.role_binding_count = 1;
        host_desc.role_bindings = roles;

        auto result = vivid::validate_role_binding(
            &host_desc, "env", "Envelope", "nonexistent_output", reg);
        check(result == vivid::RoleBindingValidation::kOutputNotFound,
              "nonexistent output → kOutputNotFound");
    }

    // =====================================================================
    // Test 17: audio-for-control exception — audio-domain control ops accepted
    // =====================================================================
    std::fprintf(stderr, "\n=== Test 17: audio-for-control exception ===\n");
    {
        vivid::OperatorRegistry reg;
        reg.scan_deferred(build_dir.c_str());

        // Envelope/LFO use process_audio for sample accuracy but are logically
        // control operators. They should be accepted for control-domain roles.
        VividRoleBindingDescriptor roles[1]{};
        roles[0].role_id = "env";
        roles[0].accepted_domain = VIVID_DOMAIN_CONTROL;

        VividOperatorDescriptor host_desc{};
        host_desc.role_binding_count = 1;
        host_desc.role_bindings = roles;

        auto result = vivid::validate_role_binding(
            &host_desc, "env", "Envelope", "value", reg);
        check(result == vivid::RoleBindingValidation::kOk,
              "audio-domain Envelope accepted for control-domain role");

        // GPU-domain should still be rejected
        VividRoleBindingDescriptor gpu_roles[1]{};
        gpu_roles[0].role_id = "fx";
        gpu_roles[0].accepted_domain = VIVID_DOMAIN_GPU;

        VividOperatorDescriptor gpu_host{};
        gpu_host.role_binding_count = 1;
        gpu_host.role_bindings = gpu_roles;

        auto r2 = vivid::validate_role_binding(
            &gpu_host, "fx", "Envelope", "value", reg);
        check(r2 == vivid::RoleBindingValidation::kDomainMismatch,
              "Envelope rejected for GPU-domain role");
    }

    std::fprintf(stderr, "\n%s (%d failure%s)\n",
                 failures ? "FAILED" : "ALL TESTS PASSED",
                 failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
