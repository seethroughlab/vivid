// Test operator with one role binding ("mod", accepts control-domain ops).
// Used by test_role_binding_commands to verify RuntimeAPI role binding commands.
#include "operator_api/operator.h"

struct TestOpWithRoles : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "TestOpWithRoles";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void collect_role_bindings(std::vector<VividRoleBindingDescriptor>& out) override {
        VividRoleBindingDescriptor role{};
        role.role_id         = "mod";
        role.label           = "Modulator";
        role.accepted_domain = VIVID_DOMAIN_CONTROL;
        role.runtime_scope   = VIVID_ROLE_SHARED;
        role.allowed_operator_types      = nullptr;
        role.allowed_operator_type_count = 0;
        role.default_operator_type       = "Envelope";
        role.preferred_output_name       = nullptr;
        role.preferred_output_semantic_tags      = nullptr;
        role.preferred_output_semantic_tag_count = 0;
        out.push_back(role);
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 2.0f;
    }
};

VIVID_REGISTER(TestOpWithRoles)
