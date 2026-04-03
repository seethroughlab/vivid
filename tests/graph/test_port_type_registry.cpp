#include "operator_api/port_type_registry.h"
#include "operator_api/type_id.h"
#include <cstring>
#include "test_helpers.h"

struct TestRefToken {
    uint64_t handle = 0;
    uint64_t generation = 0;
};

struct TestValuePayload {
    float values[4] = {};
};

VIVID_DECLARE_CUSTOM_REF_TYPE(TestRefToken,
                              "tests.vivid.ref_token_v1",
                              "TestRefToken",
                              true);
VIVID_DECLARE_CUSTOM_VALUE_TYPE(TestValuePayload,
                                "tests.vivid.value_payload_v1",
                                "TestValuePayload",
                                true);

int main() {
    std::fprintf(stderr, "\n=== Test: Port Type Registry ===\n");

    check(VIVID_CUSTOM_TYPE_ID("tests.vivid.ref_token_v1") ==
              VIVID_CUSTOM_TYPE_ID("tests.vivid.ref_token_v1"),
          "stable id hash is deterministic");
    check(VIVID_CUSTOM_TYPE_ID("tests.vivid.ref_token_v1") !=
              VIVID_CUSTOM_TYPE_ID("tests.vivid.value_payload_v1"),
          "different stable ids hash differently");

    const VividPortTypeInfo ref_info = vivid_custom_type_info<TestRefToken>();
    check(std::strcmp(ref_info.stable_type_id, "tests.vivid.ref_token_v1") == 0,
          "ref info exposes stable id");
    check(ref_info.transport == VIVID_PORT_TRANSPORT_CUSTOM_REF,
          "ref info transport is custom_ref");
    check(ref_info.audio_safe == 1, "ref info is audio-safe");

    check(vivid_register_port_type(&ref_info) == 1, "register ref info succeeds");
    check(vivid_register_port_type(&ref_info) == 1, "re-register identical ref info is idempotent");

    VividPortTypeInfo looked_up{};
    check(vivid_lookup_port_type(ref_info.type_id, &looked_up) == 1,
          "lookup of registered type succeeds");
    check(std::strcmp(looked_up.stable_type_id, ref_info.stable_type_id) == 0,
          "lookup preserves stable id");

    VividPortTypeInfo conflicting = ref_info;
    conflicting.audio_safe = 0;
    check(vivid_register_port_type(&conflicting) == 0,
          "conflicting registration fails without crashing");

    const VividPortTypeInfo value_info = vivid_custom_type_info<TestValuePayload>();
    check(vivid_register_port_type(&value_info) == 1, "register value info succeeds");
    check(value_info.transport == VIVID_PORT_TRANSPORT_CUSTOM_VALUE,
          "value info transport is custom_value");

    return 0;
}
