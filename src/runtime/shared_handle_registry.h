#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "operator_api/types.h"

namespace vivid {

// Generic shared-handle registry for cross-domain opaque resources.
// This is intentionally media-agnostic and can be reused by package operators.
class SharedHandleRegistry {
public:
    struct Entry {
        std::string type;
        uint64_t generation = 0;
        void* payload = nullptr;
        uint32_t refs = 1;
        bool valid = true;
    };

    uint64_t create(const std::string& type, void* payload, uint64_t generation) {
        if (!payload) return 0;
        const uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(mu_);
        entries_[id] = Entry{type, generation, payload, 1u, true};
        return id;
    }
    bool retain(uint64_t id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = entries_.find(id);
        if (it == entries_.end() || !it->second.valid) return false;
        ++it->second.refs;
        return true;
    }
    bool release(uint64_t id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = entries_.find(id);
        if (it == entries_.end()) return false;
        if (it->second.refs > 0) --it->second.refs;
        if (it->second.refs == 0) entries_.erase(it);
        return true;
    }
    bool invalidate(uint64_t id, uint64_t generation) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = entries_.find(id);
        if (it == entries_.end()) return false;
        it->second.valid = false;
        it->second.generation = generation;
        return true;
    }
    Entry resolve(uint64_t id) const {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = entries_.find(id);
        if (it == entries_.end()) return {};
        return it->second;
    }

    static SharedHandleRegistry& instance() {
        static SharedHandleRegistry reg;
        return reg;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<uint64_t, Entry> entries_;
    std::atomic<uint64_t> next_id_{1};
};

inline uint64_t svc_create(const char* type, void* payload, uint64_t generation) {
    return SharedHandleRegistry::instance().create(type ? type : "", payload, generation);
}
inline uint8_t svc_retain(uint64_t id) {
    return SharedHandleRegistry::instance().retain(id) ? 1u : 0u;
}
inline uint8_t svc_release(uint64_t id) {
    return SharedHandleRegistry::instance().release(id) ? 1u : 0u;
}
inline uint8_t svc_invalidate(uint64_t id, uint64_t generation) {
    return SharedHandleRegistry::instance().invalidate(id, generation) ? 1u : 0u;
}
inline VividSharedHandleEntry svc_resolve(uint64_t id) {
    const auto e = SharedHandleRegistry::instance().resolve(id);
    VividSharedHandleEntry out{};
    out.type = e.type.empty() ? nullptr : e.type.c_str();
    out.generation = e.generation;
    out.payload = e.payload;
    out.valid = e.valid ? 1u : 0u;
    return out;
}
inline const VividSharedHandleService* shared_handle_service() {
    static const VividSharedHandleService svc {
        &svc_create,
        &svc_retain,
        &svc_release,
        &svc_invalidate,
        &svc_resolve
    };
    return &svc;
}

} // namespace vivid
