#include "runtime/assets/asset_library.h"
#include "runtime/assets/asset_library_internal.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace vivid {

namespace {

class WavetableAssetKindHandler final : public AssetKindHandler {
public:
    AssetKind kind() const override { return AssetKind::Wavetable; }
    const char* kind_name() const override { return "wavetable"; }
    const char* directory_name() const override { return "wavetables"; }

    std::vector<std::string> conventional_package_dirs() const override {
        return {"assets/wavetables"};
    }

    bool accepts_source_file(const std::filesystem::path& path,
                             std::string& error) const override {
        const std::string ext = asset_internal::file_extension_lower(path.filename().string());
        if (ext == "wav") return true;
        error = "Unsupported wavetable format: " + ext + " (expected wav)";
        return false;
    }

    bool probe_metadata(const std::filesystem::path& path,
                        nlohmann::json& kind_meta,
                        std::string& error) const override {
        WavetableAssetMeta meta;
        if (!asset_internal::probe_wavetable_metadata(path.string(), meta)) {
            error = "Failed to read wavetable metadata from: " + path.string();
            return false;
        }

        kind_meta = {
            {"sample_rate", meta.sample_rate},
            {"channels", meta.channels},
            {"frame_count", meta.frame_count},
            {"samples_per_frame", meta.samples_per_frame},
            {"total_samples", meta.total_samples},
            {"peak_amplitude", meta.peak_amplitude}
        };
        return true;
    }

    bool should_discover_package_file(const std::filesystem::path& path) const override {
        return asset_internal::file_extension_lower(path.filename().string()) == "wav";
    }
};

} // namespace

// --- String conversion helpers ---

const char* asset_kind_str(AssetKind kind) {
    switch (kind) {
        case AssetKind::Wavetable: return "wavetable";
    }
    return "unknown";
}

const char* asset_kind_dir(AssetKind kind) {
    switch (kind) {
        case AssetKind::Wavetable: return "wavetables";
    }
    return "unknown";
}

const char* asset_scope_str(AssetScope scope) {
    switch (scope) {
        case AssetScope::Package:   return "package";
        case AssetScope::Workspace: return "workspace";
    }
    return "unknown";
}

std::optional<AssetKind> parse_asset_kind(const std::string& s) {
    if (s == "wavetable") return AssetKind::Wavetable;
    return std::nullopt;
}

// --- AssetLibrary ---

void AssetKindRegistry::register_handler(std::unique_ptr<AssetKindHandler> handler) {
    if (!handler) return;
    handlers_.push_back(std::move(handler));
}

const AssetKindHandler* AssetKindRegistry::find(AssetKind kind) const {
    for (const auto& handler : handlers_) {
        if (handler->kind() == kind) return handler.get();
    }
    return nullptr;
}

const AssetKindHandler* AssetKindRegistry::find(std::string_view kind_name) const {
    for (const auto& handler : handlers_) {
        if (kind_name == handler->kind_name()) return handler.get();
    }
    return nullptr;
}

AssetLibrary::AssetLibrary() {
    kind_registry_.register_handler(std::make_unique<WavetableAssetKindHandler>());
}

void AssetLibrary::set_workspace_root(const std::filesystem::path& root) {
    workspace_root_ = root;
}

const AssetKindHandler* AssetLibrary::handler_for_kind(AssetKind kind) const {
    return kind_registry_.find(kind);
}

void AssetLibrary::clear_package_assets() {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [](const AssetEntry& e) { return e.scope == AssetScope::Package; }),
        entries_.end());
    package_sources_.clear();
}

std::vector<AssetEntry> AssetLibrary::list(std::optional<AssetKind> kind,
                                           std::optional<AssetScope> scope) const {
    std::vector<AssetEntry> result;
    for (const auto& e : entries_) {
        if (kind && e.kind != *kind) continue;
        if (scope && e.scope != *scope) continue;
        result.push_back(e);
    }
    return result;
}

const AssetEntry* AssetLibrary::find(const std::string& asset_id) const {
    for (const auto& e : entries_) {
        if (e.asset_id == asset_id) return &e;
    }
    return nullptr;
}

void AssetLibrary::refresh() {
    std::vector<PackageAssetSource> package_sources = package_sources_;
    entries_.clear();

    if (!workspace_root_.empty()) {
        discover_workspace_assets(workspace_root_);
    }

    for (const auto& source : package_sources) {
        discover_package_assets_from_source(source);
    }

    std::fprintf(stderr, "[asset_library] Refreshed: %zu entries\n", entries_.size());
}

} // namespace vivid
