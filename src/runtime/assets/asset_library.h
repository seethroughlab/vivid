#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vivid {

enum class AssetKind { Wavetable };
enum class AssetScope { Package, Workspace };

struct WavetableAssetMeta {
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t frame_count = 0;
    uint32_t samples_per_frame = 2048;
    uint64_t total_samples = 0;
    float peak_amplitude = 0.0f;
};

struct AssetEntry {
    std::string asset_id;
    AssetKind kind = AssetKind::Wavetable;
    std::string display_name;
    AssetScope scope = AssetScope::Package;
    std::string package_name;       // empty for workspace assets
    std::string canonical_path;     // absolute path to source file
    std::string relative_path;      // package-relative or workspace-library-relative
    std::string source_identity;    // stable dedupe identity; source path for workspace imports
    std::string source_hash;
    std::string imported_at;        // ISO 8601
    std::string discovered_at;      // ISO 8601
    uint64_t file_size = 0;
    std::string file_format;        // "wav", etc.
    nlohmann::json kind_meta = nlohmann::json::object();
};

struct ImportResult {
    bool ok = false;
    std::string error;
    AssetEntry entry;
};

struct AssetKindHandler {
    virtual ~AssetKindHandler() = default;

    virtual AssetKind kind() const = 0;
    virtual const char* kind_name() const = 0;
    virtual const char* directory_name() const = 0;
    virtual std::vector<std::string> conventional_package_dirs() const = 0;
    virtual bool accepts_source_file(const std::filesystem::path& path,
                                     std::string& error) const = 0;
    virtual bool probe_metadata(const std::filesystem::path& path,
                                nlohmann::json& kind_meta,
                                std::string& error) const = 0;
    virtual bool should_discover_package_file(const std::filesystem::path& path) const = 0;
};

class AssetKindRegistry {
public:
    void register_handler(std::unique_ptr<AssetKindHandler> handler);
    const AssetKindHandler* find(AssetKind kind) const;
    const AssetKindHandler* find(std::string_view kind_name) const;
    const std::vector<std::unique_ptr<AssetKindHandler>>& handlers() const { return handlers_; }

private:
    std::vector<std::unique_ptr<AssetKindHandler>> handlers_;
};

class AssetLibrary {
public:
    AssetLibrary();

    void set_workspace_root(const std::filesystem::path& root);
    const std::filesystem::path& workspace_root() const { return workspace_root_; }
    const AssetKindRegistry& kind_registry() const { return kind_registry_; }

    // Discovery
    void discover_package_assets(const std::string& package_name,
                                 const std::string& package_path,
                                 AssetKind kind,
                                 const std::vector<std::string>& dirs);
    void discover_workspace_assets(const std::filesystem::path& workspace_root);

    // Clear all package-scoped entries (called before re-scan)
    void clear_package_assets();

    // Import a file into the workspace library
    ImportResult import_asset(AssetKind kind, const std::string& source_path);

    // Query
    std::vector<AssetEntry> list(std::optional<AssetKind> kind = {},
                                 std::optional<AssetScope> scope = {}) const;
    const AssetEntry* find(const std::string& asset_id) const;

    // Refresh: re-scan all known sources
    void refresh();

    // Total entry count (for diagnostics)
    size_t size() const { return entries_.size(); }

private:
    struct PackageAssetSource {
        std::string package_name;
        std::string package_path;
        AssetKind kind = AssetKind::Wavetable;
        std::vector<std::string> dirs;
    };

    void discover_package_assets_from_source(const PackageAssetSource& source);
    const AssetKindHandler* handler_for_kind(AssetKind kind) const;

    std::vector<AssetEntry> entries_;
    std::vector<PackageAssetSource> package_sources_;
    std::filesystem::path workspace_root_;
    AssetKindRegistry kind_registry_;
};

// String conversion helpers
const char* asset_kind_str(AssetKind kind);   // "wavetable" — for JSON serialization
const char* asset_kind_dir(AssetKind kind);   // "wavetables" — for directory layout
const char* asset_scope_str(AssetScope scope);
std::optional<AssetKind> parse_asset_kind(const std::string& s);

} // namespace vivid
