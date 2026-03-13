#include "operator_api/port_type_registry.h"
#include <unordered_map>
#include <mutex>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// File-static global registry — process-wide singleton
// ---------------------------------------------------------------------------

static std::mutex                                      s_mu;
static std::unordered_map<uint32_t, VividPortTypeInfo> s_registry;

// ---------------------------------------------------------------------------
// vivid_register_port_type
// ---------------------------------------------------------------------------

int vivid_register_port_type(const VividPortTypeInfo* info) {
    if (!info || !info->type_name || !info->stable_type_id) {
        std::fprintf(stderr, "[port_type_registry] vivid_register_port_type: NULL info, type_name, or stable_type_id\n");
        return 0;
    }
    if (!(info->type_id & 0x80000000u)) {
        std::fprintf(stderr, "[port_type_registry] vivid_register_port_type: type_id 0x%08x "
                             "does not have high bit set (not a custom type)\n", info->type_id);
        return 0;
    }
    if (info->abi_version != VIVID_PORT_TYPE_ABI_VERSION) {
        std::fprintf(stderr, "[port_type_registry] vivid_register_port_type: abi_version mismatch "
                             "for '%s' (got %u, want %u)\n",
                             info->type_name, info->abi_version, VIVID_PORT_TYPE_ABI_VERSION);
        return 0;
    }

    std::lock_guard<std::mutex> lock(s_mu);
    auto it = s_registry.find(info->type_id);
    if (it != s_registry.end()) {
        // Idempotent if all fields match; fatal on any mismatch.
        const VividPortTypeInfo& existing = it->second;
        if (existing.transport    != info->transport    ||
            existing.payload_size != info->payload_size ||
            existing.abi_version  != info->abi_version ||
            existing.audio_safe   != info->audio_safe ||
            std::strcmp(existing.stable_type_id, info->stable_type_id) != 0) {
            std::fprintf(stderr, "[port_type_registry] vivid_register_port_type: "
                                 "conflicting registration for type_id 0x%08x ('%s' [%s] vs '%s' [%s])\n",
                                 info->type_id, existing.type_name, existing.stable_type_id,
                                 info->type_name, info->stable_type_id);
            return 0;
        }
        return 1; // identical re-registration — idempotent
    }
    s_registry[info->type_id] = *info;
    return 1;
}

// ---------------------------------------------------------------------------
// vivid_lookup_port_type
// ---------------------------------------------------------------------------

int vivid_lookup_port_type(uint32_t type_id, VividPortTypeInfo* out) {
    std::lock_guard<std::mutex> lock(s_mu);
    auto it = s_registry.find(type_id);
    if (it == s_registry.end()) return 0;
    if (out) *out = it->second;
    return 1;
}

// ---------------------------------------------------------------------------
// vivid_list_port_types
// ---------------------------------------------------------------------------

void vivid_list_port_types(VividPortTypeInfo* buf, uint32_t* count) {
    if (!count) return;
    std::lock_guard<std::mutex> lock(s_mu);
    const uint32_t total = static_cast<uint32_t>(s_registry.size());
    if (!buf) {
        *count = total;
        return;
    }
    uint32_t n = 0;
    for (auto& [id, info] : s_registry) {
        if (n >= *count) break;
        buf[n++] = info;
    }
    *count = n;
}
