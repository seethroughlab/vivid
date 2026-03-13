#pragma once
#include <stdint.h>
#include "operator_api/types.h"   // VividPortType, VividPortDirection, VividPortTransport

#ifdef __cplusplus

// FNV-1a hash of __PRETTY_FUNCTION__ for a template instantiation.
// Because __PRETTY_FUNCTION__ embeds the fully-qualified type name of T,
// this produces a stable, deterministic ID for each C++ type — even across
// separate translation units and dlopen boundaries.
template<typename T>
constexpr uint32_t vivid_type_id() {
    const char* name =
#if defined(__clang__) || defined(__GNUC__)
        __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
        __FUNCSIG__;
#else
#error "vivid_type_id requires __PRETTY_FUNCTION__ or __FUNCSIG__"
#endif
    uint32_t hash = 2166136261u;
    for (const char* p = name; *p; ++p) {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(*p));
        hash *= 16777619u;
    }
    return hash;
}

// Returns the VividPortType token for a custom C++ type.
// High bit set distinguishes custom types from built-in VIVID_PORT_* values.
template<typename T>
constexpr uint32_t vivid_port_type() {
    return vivid_type_id<T>() | 0x80000000u;
}

// Convenience macro: fill all 8 fields of VividPortDescriptor for a
// custom (non-built-in) port type.
//
//   .type          = vivid_port_type<T>()     (FNV hash | 0x80000000)
//   .transport     = transport
//   .payload_size  = sizeof(T)
//   .type_name     = #T                       (stringified at call site — advisory)
//   .channels      = 0                        (auto)
//   .default_value = 0.0f
#define VIVID_CUSTOM_PORT(port_name, dir, T, transport) \
    VividPortDescriptor {                               \
        (port_name),                                    \
        vivid_port_type<T>(),                           \
        (dir),                                          \
        (transport),                                    \
        static_cast<uint32_t>(sizeof(T)),               \
        #T,                                             \
        0,                                              \
        0.0f                                            \
    }

// Convenience: fixes transport to VIVID_PORT_TRANSPORT_CUSTOM_REF.
// Use for opaque GPU handles and shared-reference types (the common case).
#define VIVID_CUSTOM_REF_PORT(port_name, dir, T) \
    VIVID_CUSTOM_PORT(port_name, dir, T, VIVID_PORT_TRANSPORT_CUSTOM_REF)

#endif /* __cplusplus */

// Predicate: is t a custom (operator-defined) port type?
// Macro so it is callable from both C and C++ translation units.
#define vivid_is_custom_port_type(t) (((t) & 0x80000000u) != 0u)
