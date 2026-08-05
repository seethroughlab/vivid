// ADR-0047: the pure typed-port compatibility rule used to validate visual-graph connections
// (operator_api/port_compat.h). Proves the wildcard-on-SIGNAL rule (untyped wires never break), the
// equal-transport requirement for typed streams, the type<->transport fallback, and concrete custom-ref
// type matching.
#include "operator_api/port_compat.h"
#include "test_helpers.h"

using namespace vivid;

// A minimal descriptor: transport (0 = SIGNAL), legacy type, and an optional custom type id.
static VividPortDescriptor port(VividPortTransport transport, VividPortType type = VIVID_PORT_SCALAR,
                                const char* type_id = nullptr) {
    VividPortDescriptor p{};
    p.transport = transport;
    p.type = type;
    p.stable_type_id = type_id;
    return p;
}

int main() {
    const auto TEX  = port(VIVID_PORT_TRANSPORT_TEXTURE);
    const auto NOTE = port(VIVID_PORT_TRANSPORT_NOTE_STREAM);
    const auto CTRL = port(VIVID_PORT_TRANSPORT_CONTROL_SIGNAL);
    const auto AUD  = port(VIVID_PORT_TRANSPORT_AUDIO_BUFFER);
    const auto SIG  = port(VIVID_PORT_TRANSPORT_SIGNAL);   // plain scalar/value/param lane

    // --- same typed stream connects; different typed streams don't ---
    CHECK(ports_compatible(TEX, TEX));
    CHECK(ports_compatible(NOTE, NOTE));
    CHECK(ports_compatible(CTRL, CTRL));
    CHECK(!ports_compatible(TEX, NOTE));
    CHECK(!ports_compatible(NOTE, TEX));
    CHECK(!ports_compatible(AUD, TEX));
    CHECK(!ports_compatible(CTRL, NOTE));

    // --- generic SIGNAL is a wildcard on either side (untyped wires stay valid) ---
    CHECK(ports_compatible(SIG, TEX));
    CHECK(ports_compatible(TEX, SIG));
    CHECK(ports_compatible(SIG, NOTE));
    CHECK(ports_compatible(SIG, SIG));

    // --- type -> transport fallback: a port that set only the legacy `type` reads as that stream ---
    const auto TEX_BY_TYPE = port(VIVID_PORT_TRANSPORT_SIGNAL, VIVID_PORT_TEXTURE);
    CHECK(port_effective_transport(TEX_BY_TYPE) == VIVID_PORT_TRANSPORT_TEXTURE);
    CHECK(ports_compatible(TEX_BY_TYPE, TEX));    // texture(by type) <-> texture(by transport)
    CHECK(!ports_compatible(TEX_BY_TYPE, NOTE));  // still a texture, so it can't feed a note input
    const auto AUD_BY_TYPE = port(VIVID_PORT_TRANSPORT_SIGNAL, VIVID_PORT_AUDIO_BUFFER);
    CHECK(port_effective_transport(AUD_BY_TYPE) == VIVID_PORT_TRANSPORT_AUDIO_BUFFER);

    // --- custom_ref: same concrete type connects, different types don't; an unknown side is permissive ---
    const auto MESH  = port(VIVID_PORT_TRANSPORT_CUSTOM_REF, VIVID_PORT_SCALAR, "vivid.mesh");
    const auto MESH2 = port(VIVID_PORT_TRANSPORT_CUSTOM_REF, VIVID_PORT_SCALAR, "vivid.mesh");
    const auto SCENE = port(VIVID_PORT_TRANSPORT_CUSTOM_REF, VIVID_PORT_SCALAR, "vivid.scene_fragment");
    const auto REF_ANON = port(VIVID_PORT_TRANSPORT_CUSTOM_REF);   // no stable_type_id
    CHECK(ports_compatible(MESH, MESH2));       // same concrete type
    CHECK(!ports_compatible(MESH, SCENE));      // mesh -> scene input rejected
    CHECK(ports_compatible(MESH, REF_ANON));    // one side untyped custom_ref -> permissive
    CHECK(ports_compatible(REF_ANON, SCENE));

    return vivid::test::summary("test_port_compat");
}
