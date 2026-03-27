/* port_type_registry.h
 * ---------------------------------------------------------------------------
 * Runtime registry for custom (operator-defined) port types.
 *
 * Operator dylibs export vivid_describe_custom_types() which returns a static
 * VividPortTypeInfo array. The Vivid runtime calls that export after dlopen
 * and registers each type via vivid_register_port_type(). This pull model
 * avoids operators needing to call runtime-owned functions directly, which
 * would require undefined-symbol resolution across the dlopen boundary.
 *
 * The runtime calls vivid_lookup_port_type() at wire-validation time.
 *
 * C89-compatible; all functions have C linkage.
 * ---------------------------------------------------------------------------
 */
#pragma once
#include <stdint.h>
#include "operator_api/types.h"   // VividPortTransport

#ifdef __cplusplus
extern "C" {
#endif

// ABI version embedded in every record. Increment when VividPortTypeInfo
// gains new fields.
#define VIVID_PORT_TYPE_ABI_VERSION 1u

// Metadata record for one registered custom port type.
typedef struct VividPortTypeInfo {
    uint32_t           type_id;       // VIVID_CUSTOM_TYPE_ID("...") — high bit set
    VividPortTransport transport;     // how the payload is conveyed
    uint32_t           payload_size;  // sizeof(T); must match on both sides of a wire
    const char*        type_name;     // human-readable label; never NULL
    const char*        stable_type_id;// stable namespaced id; never NULL
    uint8_t            audio_safe;    // 1 if valid for audio snapshot crossing
    uint8_t            reserved0;
    uint8_t            reserved1;
    uint8_t            reserved2;
    uint32_t           abi_version;   // set to VIVID_PORT_TYPE_ABI_VERSION
    const char*        package_name;  // optional; owning package (e.g. "vivid_media"); may be NULL
    const char*        description;   // optional; human-readable description; may be NULL
} VividPortTypeInfo;

// Register a custom port type with the runtime.
// info must remain valid for the lifetime of the dylib (use static storage).
// Re-registering the same type_id with identical fields is idempotent.
// Re-registering with mismatched fields fails and returns 0.
int vivid_register_port_type(const VividPortTypeInfo* info);

// Look up a registered type by its type_id token.
// Returns 1 and writes *out on success; returns 0 if not found.
// out may be NULL for a pure existence check.
int vivid_lookup_port_type(uint32_t type_id, VividPortTypeInfo* out);

// Enumerate all registered types.
// Pass buf=NULL, *count=0 to query the needed capacity.
// On return *count is the number written (or total when buf==NULL).
void vivid_list_port_types(VividPortTypeInfo* buf, uint32_t* count);

#ifdef __cplusplus
}
#endif

// ---------------------------------------------------------------------------
// Operator export contract (optional)
//
// An operator dylib may export this symbol to declare custom port types.
// The Vivid runtime calls it after dlopen and registers each returned type.
// Using a pull model (runtime calls the export) avoids operators needing to
// call vivid_register_port_type() directly across the dlopen boundary.
//
// The returned pointer must remain valid for the lifetime of the dylib;
// use static storage. *count must be set to the number of entries.
//
//   extern "C" const VividPortTypeInfo* vivid_describe_custom_types(uint32_t* count);
//
// ---------------------------------------------------------------------------
typedef const VividPortTypeInfo* (*VividDescribeCustomTypesFn)(uint32_t* count);

#ifdef __cplusplus
#include "operator_api/type_id.h"

template<typename T>
constexpr VividPortTypeInfo vivid_custom_type_info(const char* package_name = nullptr,
                                                   const char* description = nullptr) {
    return VividPortTypeInfo {
        vivid_port_type<T>(),
        vivid_custom_type_traits<T>::transport,
        static_cast<uint32_t>(sizeof(T)),
        vivid_display_type_name<T>(),
        vivid_stable_type_id<T>(),
        vivid_custom_type_audio_safe<T>() ? 1u : 0u,
        0u, 0u, 0u,
        VIVID_PORT_TYPE_ABI_VERSION,
        package_name,
        description
    };
}

// Emit vivid_describe_custom_types for a single CUSTOM_REF type.
// Usage (at file scope): VIVID_DESCRIBE_REF_TYPE(MyType)
#define VIVID_DESCRIBE_REF_TYPE(T) \
    extern "C" const VividPortTypeInfo* vivid_describe_custom_types(uint32_t* count) { \
        static const VividPortTypeInfo kInfo = vivid_custom_type_info<T>(); \
        *count = 1; return &kInfo; \
    }

// Emit vivid_describe_custom_types for exactly two CUSTOM_REF types.
// Usage (at file scope): VIVID_DESCRIBE_REF_TYPES2(TypeA, TypeB)
#define VIVID_DESCRIBE_REF_TYPES2(T1, T2) \
    extern "C" const VividPortTypeInfo* vivid_describe_custom_types(uint32_t* count) { \
        static const VividPortTypeInfo kInfos[2] = { \
            vivid_custom_type_info<T1>(), \
            vivid_custom_type_info<T2>() \
        }; \
        *count = 2; return kInfos; \
    }
#endif /* __cplusplus */
