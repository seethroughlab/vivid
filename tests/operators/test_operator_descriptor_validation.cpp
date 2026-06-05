// Test: operator descriptor validation (audit 02-F7).
//
// Exercises validate_descriptor() across a representative subset of its ~23
// issue codes, asserting the expected code (referenced via the named
// vivid::validation_codes constants) fires with a non-empty message — and that
// a well-formed descriptor produces no issues.

#include "runtime/operators/operator_descriptor_validation.h"
#include "operator_api/types.h"
#include <cstdio>
#include <string>
#include <vector>
#include "test_helpers.h"

using namespace vivid;
namespace vc = vivid::validation_codes;

namespace {

bool has_code(const std::vector<DescriptorValidationIssue>& issues, const char* code) {
    for (const auto& iss : issues) {
        if (iss.code == code) {
            // Every issue must carry a non-empty, actionable message.
            check(!iss.message.empty(), (std::string("message non-empty for ") + code).c_str());
            return true;
        }
    }
    return false;
}

// A minimal well-formed descriptor: one frame capability, no params/ports.
VividOperatorDescriptor base_desc() {
    VividOperatorDescriptor d{};
    d.name = "TestOp";
    d.has_process_frame = 1;
    return d;
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_operator_descriptor_validation ===\n\n");

    // Positive: a clean descriptor yields no issues.
    {
        VividOperatorDescriptor d = base_desc();
        check(validate_descriptor(&d).empty(), "well-formed descriptor → no issues");
    }

    // null_descriptor
    check(has_code(validate_descriptor(nullptr), vc::kNullDescriptor),
          "null descriptor → null_descriptor");

    // missing_name
    {
        VividOperatorDescriptor d = base_desc();
        d.name = "";
        check(has_code(validate_descriptor(&d), vc::kMissingName), "empty name → missing_name");
    }

    // missing_capability
    {
        VividOperatorDescriptor d = base_desc();
        d.has_process_frame = 0;
        check(has_code(validate_descriptor(&d), vc::kMissingCapability),
              "no capability → missing_capability");
    }

    // null_params
    {
        VividOperatorDescriptor d = base_desc();
        d.param_count = 1;
        d.params = nullptr;
        check(has_code(validate_descriptor(&d), vc::kNullParams),
              "param_count>0 + null params → null_params");
    }

    // param_missing_name
    {
        VividOperatorDescriptor d = base_desc();
        VividParamDescriptor p{}; p.name = ""; p.type = VIVID_PARAM_FLOAT;
        d.param_count = 1; d.params = &p;
        check(has_code(validate_descriptor(&d), vc::kParamMissingName),
              "empty param name → param_missing_name");
    }

    // duplicate_param_name
    {
        VividOperatorDescriptor d = base_desc();
        VividParamDescriptor ps[2]{};
        ps[0].name = "amt"; ps[0].type = VIVID_PARAM_FLOAT;
        ps[1].name = "amt"; ps[1].type = VIVID_PARAM_FLOAT;
        d.param_count = 2; d.params = ps;
        check(has_code(validate_descriptor(&d), vc::kDuplicateParamName),
              "duplicate param name → duplicate_param_name");
    }

    // param_missing_choice_labels
    {
        VividOperatorDescriptor d = base_desc();
        VividParamDescriptor p{}; p.name = "mode"; p.type = VIVID_PARAM_FLOAT;
        p.choice_count = 2; p.choice_labels = nullptr;
        d.param_count = 1; d.params = &p;
        check(has_code(validate_descriptor(&d), vc::kParamMissingChoiceLabels),
              "choice_count + null labels → param_missing_choice_labels");
    }

    // param_missing_default_string (FILE/TEXT param without default_string)
    {
        VividOperatorDescriptor d = base_desc();
        VividParamDescriptor p{}; p.name = "path"; p.type = VIVID_PARAM_FILE;
        p.default_string = nullptr;
        d.param_count = 1; d.params = &p;
        check(has_code(validate_descriptor(&d), vc::kParamMissingDefaultString),
              "file param + null default_string → param_missing_default_string");
    }

    // port_missing_name
    {
        VividOperatorDescriptor d = base_desc();
        VividPortDescriptor port{}; port.name = ""; port.direction = VIVID_PORT_INPUT;
        d.port_count = 1; d.ports = &port;
        check(has_code(validate_descriptor(&d), vc::kPortMissingName),
              "empty port name → port_missing_name");
    }

    // duplicate_port_name (same direction)
    {
        VividOperatorDescriptor d = base_desc();
        VividPortDescriptor ports[2]{};
        ports[0].name = "in"; ports[0].direction = VIVID_PORT_INPUT;
        ports[1].name = "in"; ports[1].direction = VIVID_PORT_INPUT;
        d.port_count = 2; d.ports = ports;
        check(has_code(validate_descriptor(&d), vc::kDuplicatePortName),
              "duplicate input port name → duplicate_port_name");
    }

    // custom_port_missing_type_name
    {
        VividOperatorDescriptor d = base_desc();
        VividPortDescriptor port{};
        port.name = "scene"; port.direction = VIVID_PORT_OUTPUT;
        port.transport = VIVID_PORT_TRANSPORT_CUSTOM_REF; port.type_name = nullptr;
        d.port_count = 1; d.ports = &port;
        check(has_code(validate_descriptor(&d), vc::kCustomPortMissingTypeName),
              "custom port + null type_name → custom_port_missing_type_name");
    }

    // uniform_layout_not_16_byte_aligned
    {
        VividOperatorDescriptor d = base_desc();
        d.has_process_frame = 0; d.has_process_gpu = 1;
        VividGeneratedUniformLayout layout{};
        layout.struct_name = "Uniforms";
        layout.byte_size = 8;        // not a multiple of 16
        layout.member_count = 0;
        layout.members = nullptr;
        check(has_code(validate_descriptor(&d, "v2", &layout), vc::kUniformLayoutNot16ByteAligned),
              "8-byte uniform layout → uniform_layout_not_16_byte_aligned");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
