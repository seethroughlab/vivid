// Headless test for the lifted operator descriptor validator
// (operator_api/operator_descriptor_validation). Exercises a representative
// subset of the stable named issue codes + confirms a well-formed descriptor is
// clean. Adapted from vivid-classic's test_operator_descriptor_validation.cpp.
#include "operator_api/operator_descriptor_validation.h"
#include "operator_api/types.h"
#include "test_helpers.h"

#include <string>
#include <vector>

using namespace vivid;
namespace vc = vivid::validation_codes;

static bool has_code(const std::vector<DescriptorValidationIssue>& issues, const char* code) {
    for (const auto& iss : issues) {
        if (iss.code == code) {
            CHECK(!iss.message.empty());   // every issue carries an actionable message
            return true;
        }
    }
    return false;
}

// A minimal well-formed GPU descriptor: one capability, no params/ports.
static VividOperatorDescriptor base_desc() {
    VividOperatorDescriptor d{};
    d.name = "TestOp";
    d.has_process_gpu = 1;
    return d;
}

int main() {
    // Positive: a clean descriptor yields no issues.
    { VividOperatorDescriptor d = base_desc(); CHECK(validate_descriptor(&d).empty()); }

    // null_descriptor
    CHECK(has_code(validate_descriptor(nullptr), vc::kNullDescriptor));

    // missing_name
    { VividOperatorDescriptor d = base_desc(); d.name = "";
      CHECK(has_code(validate_descriptor(&d), vc::kMissingName)); }

    // missing_capability
    { VividOperatorDescriptor d = base_desc(); d.has_process_gpu = 0;
      CHECK(has_code(validate_descriptor(&d), vc::kMissingCapability)); }

    // null_params (count>0 but null pointer)
    { VividOperatorDescriptor d = base_desc(); d.param_count = 1; d.params = nullptr;
      CHECK(has_code(validate_descriptor(&d), vc::kNullParams)); }

    // param_missing_name
    { VividOperatorDescriptor d = base_desc();
      VividParamDescriptor p{}; p.name = ""; p.type = VIVID_PARAM_FLOAT;
      d.param_count = 1; d.params = &p;
      CHECK(has_code(validate_descriptor(&d), vc::kParamMissingName)); }

    // duplicate_param_name
    { VividOperatorDescriptor d = base_desc();
      VividParamDescriptor ps[2]{};
      ps[0].name = "warp"; ps[0].type = VIVID_PARAM_FLOAT;
      ps[1].name = "warp"; ps[1].type = VIVID_PARAM_FLOAT;
      d.param_count = 2; d.params = ps;
      CHECK(has_code(validate_descriptor(&d), vc::kDuplicateParamName)); }

    // port_missing_name
    { VividOperatorDescriptor d = base_desc();
      VividPortDescriptor pt{}; pt.name = ""; pt.type = VIVID_PORT_TEXTURE; pt.direction = VIVID_PORT_OUTPUT;
      d.port_count = 1; d.ports = &pt;
      CHECK(has_code(validate_descriptor(&d), vc::kPortMissingName)); }

    // duplicate_port_name — dedup is per-direction, so two *input* "tex" collide
    // (an input + output of the same name is intentionally allowed).
    { VividOperatorDescriptor d = base_desc();
      VividPortDescriptor pts[2]{};
      pts[0].name = "tex"; pts[0].type = VIVID_PORT_TEXTURE; pts[0].direction = VIVID_PORT_INPUT;
      pts[1].name = "tex"; pts[1].type = VIVID_PORT_TEXTURE; pts[1].direction = VIVID_PORT_INPUT;
      d.port_count = 2; d.ports = pts;
      CHECK(has_code(validate_descriptor(&d), vc::kDuplicatePortName)); }

    // A fully-formed Plasma-like descriptor (gpu + 1 param + 1 out port) is clean.
    { VividOperatorDescriptor d = base_desc(); d.name = "Plasma";
      VividParamDescriptor p{}; p.name = "warp"; p.type = VIVID_PARAM_FLOAT; p.default_value = 0.5f; p.min_value = 0.f; p.max_value = 1.f;
      VividPortDescriptor pt{}; pt.name = "texture"; pt.type = VIVID_PORT_TEXTURE; pt.direction = VIVID_PORT_OUTPUT;
      d.param_count = 1; d.params = &p; d.port_count = 1; d.ports = &pt;
      CHECK(validate_descriptor(&d).empty()); }

    return vivid::test::summary("test_descriptor_validation");
}
