#include "runtime/assets/asset_library.h"
#include "runtime/assets/asset_library_internal.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace vivid {

void AssetLibrary::discover_package_assets_from_source(const PackageAssetSource& source) {
    namespace fs = std::filesystem;
    const AssetKindHandler* handler = handler_for_kind(source.kind);
    if (!handler) return;

    for (const auto& rel_dir : source.dirs) {
        fs::path abs_dir = fs::path(source.package_path) / rel_dir;
        std::error_code ec;
        if (!fs::is_directory(abs_dir, ec)) continue;

        for (const auto& dir_entry : fs::directory_iterator(abs_dir, ec)) {
            if (ec) break;
            if (!dir_entry.is_regular_file()) continue;

            std::string filename = dir_entry.path().filename().string();
            std::string ext = asset_internal::file_extension_lower(filename);
            if (!handler->should_discover_package_file(dir_entry.path())) continue;
            std::string rel_path = rel_dir + "/" + filename;
            std::string abs_path = dir_entry.path().string();

            AssetEntry entry;
            entry.kind = source.kind;
            entry.scope = AssetScope::Package;
            entry.package_name = source.package_name;
            entry.relative_path = rel_path;
            entry.source_identity = rel_path;
            entry.canonical_path = abs_path;
            entry.display_name = asset_internal::sanitize_display_name(filename);
            entry.file_format = ext;
            entry.discovered_at = asset_internal::iso_timestamp_now();
            entry.asset_id = asset_internal::generate_asset_id(
                entry.kind, entry.scope, source.package_name, rel_path);
            entry.file_size = static_cast<uint64_t>(fs::file_size(dir_entry.path(), ec));
            entry.source_hash = asset_internal::compute_file_hash(abs_path);

            std::string error;
            if (!handler->probe_metadata(dir_entry.path(), entry.kind_meta, error))
                continue;

            entries_.push_back(std::move(entry));
        }
    }
}

void AssetLibrary::discover_package_assets(const std::string& package_name,
                                           const std::string& package_path,
                                           AssetKind kind,
                                           const std::vector<std::string>& dirs) {
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [&](const AssetEntry& entry) {
                           return entry.scope == AssetScope::Package &&
                                  entry.kind == kind &&
                                  entry.package_name == package_name;
                       }),
        entries_.end());

    for (auto& existing : package_sources_) {
        if (existing.package_name == package_name &&
            existing.package_path == package_path &&
            existing.kind == kind) {
            existing.dirs = dirs;
            discover_package_assets_from_source(existing);
            return;
        }
    }

    package_sources_.push_back(PackageAssetSource{package_name, package_path, kind, dirs});
    discover_package_assets_from_source(package_sources_.back());
}

void AssetLibrary::discover_workspace_assets(const std::filesystem::path& workspace_root) {
    namespace fs = std::filesystem;

    for (const auto& handler_ptr : kind_registry_.handlers()) {
        const AssetKindHandler& handler = *handler_ptr;
        fs::path lib_root = workspace_root / "assets" / "library" / handler.directory_name();
        std::error_code ec;
        if (!fs::is_directory(lib_root, ec)) continue;

        for (const auto& dir_entry : fs::directory_iterator(lib_root, ec)) {
            if (ec) break;
            if (!dir_entry.is_directory()) continue;

            fs::path sidecar_path = dir_entry.path() / "asset.json";
            if (!fs::exists(sidecar_path, ec)) continue;

            AssetEntry entry;
            if (asset_internal::read_asset_sidecar(sidecar_path.string(), entry)) {
                entry.scope = AssetScope::Workspace;
                entries_.push_back(std::move(entry));
                std::fprintf(stderr, "[asset_library] Loaded workspace asset: %s\n",
                             entry.asset_id.c_str());
            }
        }
    }
}

} // namespace vivid
