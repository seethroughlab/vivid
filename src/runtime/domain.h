#pragma once
#include "operator_api/types.h"

// Internal domain classification — derived from descriptor flags.
// VividDomain was removed from the public operator API but remains
// as an internal concept for UI coloring, scaffolding, and backward compat.

typedef uint32_t VividDomain;
#define VIVID_DOMAIN_CONTROL  0u
#define VIVID_DOMAIN_AUDIO    1u
#define VIVID_DOMAIN_GPU      2u

// Derive domain from descriptor flags (no longer reads desc->domain)
inline VividDomain domain_from_descriptor(const VividOperatorDescriptor* desc) {
    if (desc->has_process_gpu) return VIVID_DOMAIN_GPU;
    if (desc->has_process_audio) return VIVID_DOMAIN_AUDIO;
    return VIVID_DOMAIN_CONTROL;
}
