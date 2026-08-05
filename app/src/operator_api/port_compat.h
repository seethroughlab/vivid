#pragma once
// ADR-0047: pure, dependency-free port-compatibility rule for typed connection validation. A graph
// editor uses this to reject wiring an OUTPUT port into an INPUT port whose stream type it can't carry
// (a texture into a mesh port, a note stream into a texture input). Kept in operator_api (types.h only —
// no wgpu, no json) so both the visual graph and the headless test tier can use it.
//
// The rule mirrors the audio graph's NARROW note-edge check: only reject the genuinely-wrong case,
// never over-tighten. Plain SIGNAL (scalar / value / param lane — the common untyped port) is a
// wildcard, so every existing untyped wire stays valid.
#include "operator_api/types.h"

#include <cstring>

namespace vivid {

// The port's effective transport, applying the same type->transport fallback as
// control_json.h's port_stream_name: a few ports set only the legacy `type` and leave `transport` at
// SIGNAL. Everything else keeps its declared transport.
inline VividPortTransport port_effective_transport(const VividPortDescriptor& p) {
    if (p.transport != VIVID_PORT_TRANSPORT_SIGNAL) return p.transport;
    switch (p.type) {
        case VIVID_PORT_AUDIO_BUFFER: return VIVID_PORT_TRANSPORT_AUDIO_BUFFER;
        case VIVID_PORT_TEXTURE:      return VIVID_PORT_TRANSPORT_TEXTURE;
        case VIVID_PORT_STRING:       return VIVID_PORT_TRANSPORT_STRING;
        default:                      return VIVID_PORT_TRANSPORT_SIGNAL;
    }
}

// Can a source OUTPUT port feed a dest INPUT port?
//  - Generic SIGNAL on either side is a WILDCARD (untyped scalar/value/param lane) -> always allowed,
//    so existing untyped wires never break.
//  - Otherwise the two effective transports must be equal (texture<->texture, note<->note, ...).
//  - For CUSTOM_REF, if both name a concrete type (stable_type_id) and they differ, reject (don't wire
//    a mesh into a scene-fragment input) — the one bit of concrete-type granularity.
inline bool ports_compatible(const VividPortDescriptor& out, const VividPortDescriptor& in) {
    const VividPortTransport to = port_effective_transport(out);
    const VividPortTransport ti = port_effective_transport(in);
    if (to == VIVID_PORT_TRANSPORT_SIGNAL || ti == VIVID_PORT_TRANSPORT_SIGNAL) return true;  // wildcard
    if (to != ti) return false;
    if (to == VIVID_PORT_TRANSPORT_CUSTOM_REF &&
        out.stable_type_id && in.stable_type_id && std::strcmp(out.stable_type_id, in.stable_type_id) != 0)
        return false;   // both concrete custom types, and they differ
    return true;
}

}  // namespace vivid
