#pragma once
#include <stdint.h>
#include <type_traits>
#include "operator_api/types.h"

#ifdef __cplusplus

// FNV-1a hash of a stable namespaced id string.
constexpr uint32_t vivid_hash_type_id(const char* name) {
    uint32_t hash = 2166136261u;
    for (const char* p = name; *p; ++p) {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(*p));
        hash *= 16777619u;
    }
    return hash;
}

#define VIVID_CUSTOM_TYPE_ID(stable_id_literal) \
    (vivid_hash_type_id(stable_id_literal) | 0x80000000u)

template<typename T>
struct vivid_custom_type_traits;

template<typename T>
constexpr uint32_t vivid_port_type() {
    return vivid_custom_type_traits<T>::type_id;
}

template<typename T>
constexpr const char* vivid_stable_type_id() {
    return vivid_custom_type_traits<T>::stable_type_id;
}

template<typename T>
constexpr const char* vivid_display_type_name() {
    return vivid_custom_type_traits<T>::display_name;
}

template<typename T>
constexpr bool vivid_custom_type_audio_safe() {
    return vivid_custom_type_traits<T>::audio_safe;
}

#define VIVID_DECLARE_CUSTOM_TYPE(T, stable_id_literal, display_name_literal, transport_enum, audio_safe_value) \
    static_assert(std::is_trivially_copyable_v<T>, #T " must be trivially copyable for Vivid custom ports"); \
    template<> \
    struct vivid_custom_type_traits<T> { \
        static constexpr uint32_t type_id = VIVID_CUSTOM_TYPE_ID(stable_id_literal); \
        static constexpr const char* stable_type_id = stable_id_literal; \
        static constexpr const char* display_name = display_name_literal; \
        static constexpr VividPortTransport transport = transport_enum; \
        static constexpr bool audio_safe = audio_safe_value; \
    }

#define VIVID_DECLARE_CUSTOM_REF_TYPE(T, stable_id_literal, display_name_literal, audio_safe_value) \
    VIVID_DECLARE_CUSTOM_TYPE(T, stable_id_literal, display_name_literal, VIVID_PORT_TRANSPORT_CUSTOM_REF, audio_safe_value)

#define VIVID_DECLARE_CUSTOM_VALUE_TYPE(T, stable_id_literal, display_name_literal, audio_safe_value) \
    VIVID_DECLARE_CUSTOM_TYPE(T, stable_id_literal, display_name_literal, VIVID_PORT_TRANSPORT_CUSTOM_VALUE, audio_safe_value)

// Convenience macro: fill all fields of VividPortDescriptor for a custom type.
#define VIVID_CUSTOM_PORT(port_name, dir, T, transport_value) \
    VividPortDescriptor {                                     \
        (port_name),                                          \
        vivid_port_type<T>(),                                 \
        (dir),                                                \
        (transport_value),                                    \
        static_cast<uint32_t>(sizeof(T)),                     \
        vivid_display_type_name<T>(),                         \
        0,                                                    \
        0.0f,                                                 \
        vivid_stable_type_id<T>()                             \
    }

#define VIVID_CUSTOM_REF_PORT(port_name, dir, T) \
    VIVID_CUSTOM_PORT(port_name, dir, T, VIVID_PORT_TRANSPORT_CUSTOM_REF)

#define VIVID_CUSTOM_VALUE_PORT(port_name, dir, T) \
    VIVID_CUSTOM_PORT(port_name, dir, T, VIVID_PORT_TRANSPORT_CUSTOM_VALUE)

#endif /* __cplusplus */

#define vivid_is_custom_port_type(t) (((t) & 0x80000000u) != 0u)
