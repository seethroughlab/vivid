#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_registry_internal.h"

#include <cstdio>
#include <filesystem>

namespace vivid {

namespace {

enum class DeferredLoadClaimState {
    MissingOrLoaded,
    Busy,
    Claimed,
};

struct ClaimedDeferredLoad {
    DeferredLoadClaimState state = DeferredLoadClaimState::MissingOrLoaded;
    std::string resolved_type;
    std::string dylib_path;
};

ClaimedDeferredLoad claim_deferred_load_locked(
        std::recursive_mutex& mutex,
        std::unordered_map<std::string, std::unique_ptr<OperatorLoader>>& loaders,
        std::unordered_map<std::string, DeferredEntry>& deferred,
        std::unordered_set<std::string>& in_flight_loads,
        const std::unordered_map<std::string, std::string>& aliases,
        const std::string& type_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const std::string resolved = operator_registry_internal::resolve_alias_once(aliases, type_name);
    if (loaders.count(resolved)) return {};
    auto dit = deferred.find(resolved);
    if (dit == deferred.end()) return {};
    if (in_flight_loads.count(resolved)) {
        return ClaimedDeferredLoad{DeferredLoadClaimState::Busy, resolved, {}};
    }
    in_flight_loads.insert(resolved);
    return ClaimedDeferredLoad{DeferredLoadClaimState::Claimed, resolved, dit->second.dylib_path};
}

} // namespace

bool OperatorRegistry::load_for_graph(const Graph& graph) {
    bool had_busy_type = false;
    std::unordered_set<std::string> seen;
    for (const auto& ndef : graph.nodes()) {
        auto claim = claim_deferred_load_locked(mutex_, loaders_, deferred_, in_flight_loads_, aliases_,
                                                ndef.type);
        if (claim.state == DeferredLoadClaimState::MissingOrLoaded) continue;
        if (!seen.insert(claim.resolved_type).second) continue;
        if (claim.state == DeferredLoadClaimState::Busy) {
            had_busy_type = true;
            continue;
        }

        auto loader = std::make_unique<OperatorLoader>();
        operator_registry_internal::maybe_delay_registry_lazy_load_for_tests();
        if (!loader->load(claim.dylib_path.c_str())) {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            in_flight_loads_.erase(claim.resolved_type);
            const std::filesystem::path p(claim.dylib_path);
            record_loader_failure(claim.dylib_path, p.filename().string(), loader->last_error());
            std::fprintf(stderr, "[vivid] Registry: failed to load %s\n", claim.resolved_type.c_str());
            return false;
        }

        std::lock_guard<std::recursive_mutex> lock(mutex_);
        in_flight_loads_.erase(claim.resolved_type);
        if (auto lit = loaders_.find(claim.resolved_type); lit != loaders_.end()) continue;
        register_target_mapping(claim.dylib_path, claim.resolved_type);
        loaders_[claim.resolved_type] = std::move(loader);
        deferred_.erase(claim.resolved_type);
        abi_mismatch_by_path_.erase(claim.dylib_path);
        loader_failure_by_path_.erase(claim.dylib_path);
        std::fprintf(stderr, "[vivid] Registry: loaded %s (on demand)\n", claim.resolved_type.c_str());
    }
    return !had_busy_type;
}

OperatorLoader* OperatorRegistry::find_loaded(const std::string& type_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string resolved = operator_registry_internal::resolve_alias_once(aliases_, type_name);
    auto it = loaders_.find(resolved);
    return (it != loaders_.end()) ? it->second.get() : nullptr;
}

const VividOperatorDescriptor* OperatorRegistry::probe_descriptor(const std::string& type_name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string resolved = operator_registry_internal::resolve_alias_once(aliases_, type_name);
    auto lit = loaders_.find(resolved);
    if (lit != loaders_.end() && lit->second) {
        return lit->second->descriptor();
    }
    auto dit = deferred_.find(resolved);
    if (dit == deferred_.end()) return nullptr;
    return &dit->second.desc;
}

std::string OperatorRegistry::probe_registration_mode(const std::string& type_name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string resolved = operator_registry_internal::resolve_alias_once(aliases_, type_name);
    auto lit = loaders_.find(resolved);
    if (lit != loaders_.end() && lit->second) {
        return lit->second->registration_mode();
    }
    auto dit = deferred_.find(resolved);
    if (dit != deferred_.end()) {
        return dit->second.registration_mode;
    }
    return "unknown";
}

const VividGeneratedUniformLayout* OperatorRegistry::probe_generated_uniform_layout(
    const std::string& type_name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string resolved = operator_registry_internal::resolve_alias_once(aliases_, type_name);
    auto lit = loaders_.find(resolved);
    if (lit != loaders_.end() && lit->second) {
        return lit->second->generated_uniform_layout();
    }
    auto dit = deferred_.find(resolved);
    if (dit != deferred_.end() && dit->second.uniform_layout.byte_size > 0) {
        return &dit->second.uniform_layout;
    }
    return nullptr;
}

OperatorLoader* OperatorRegistry::find(const std::string& type_name) {
    auto claim = claim_deferred_load_locked(mutex_, loaders_, deferred_, in_flight_loads_, aliases_,
                                            type_name);
    if (claim.state != DeferredLoadClaimState::Claimed) {
        return find_loaded(type_name);
    }

    auto loader = std::make_unique<OperatorLoader>();
    operator_registry_internal::maybe_delay_registry_lazy_load_for_tests();
    if (!loader->load(claim.dylib_path.c_str())) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        in_flight_loads_.erase(claim.resolved_type);
        const std::filesystem::path p(claim.dylib_path);
        record_loader_failure(claim.dylib_path, p.filename().string(), loader->last_error());
        return nullptr;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    in_flight_loads_.erase(claim.resolved_type);
    if (auto it = loaders_.find(claim.resolved_type); it != loaders_.end()) {
        return it->second.get();
    }

    register_target_mapping(claim.dylib_path, claim.resolved_type);
    auto* ptr = loader.get();
    loaders_[claim.resolved_type] = std::move(loader);
    deferred_.erase(claim.resolved_type);
    abi_mismatch_by_path_.erase(claim.dylib_path);
    loader_failure_by_path_.erase(claim.dylib_path);
    std::fprintf(stderr, "[vivid] Registry: loaded %s (lazy)\n", claim.resolved_type.c_str());
    return ptr;
}

std::vector<std::string> OperatorRegistry::type_names() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(loaders_.size() + deferred_.size() + aliases_.size());
    for (const auto& [name, _] : loaders_) names.push_back(name);
    for (const auto& [name, _] : deferred_) names.push_back(name);
    for (const auto& [name, _] : aliases_) names.push_back(name);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

bool OperatorRegistry::reload_operator(const std::string& type_name, const std::string& new_dylib_path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = loaders_.find(type_name);
    if (it == loaders_.end()) {
        std::fprintf(stderr, "[vivid] Registry: unknown type '%s' for reload\n", type_name.c_str());
        return false;
    }

    if (!it->second->load(new_dylib_path.c_str())) {
        const std::filesystem::path p(new_dylib_path);
        record_loader_failure(new_dylib_path, p.filename().string(), it->second->last_error());
        std::fprintf(stderr,
            "[vivid] hot-reload failed for '%s' — previous loader kept active\n",
            type_name.c_str());
        return false;
    }

    loader_failure_by_path_.erase(new_dylib_path);
    return true;
}

} // namespace vivid
