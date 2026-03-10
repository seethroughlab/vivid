#pragma once

#include <stdint.h>

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

// Convenience macro: define a VividPortDescriptor for a handle-typed port.
// Usage: VIVID_HANDLE_PORT("scene_in", VIVID_PORT_INPUT, VividSceneFragment)
#define VIVID_HANDLE_PORT(port_name, dir, CppType) \
    VividPortDescriptor { (port_name), VIVID_PORT_HANDLE, (dir), vivid_type_id<CppType>() }

#endif
