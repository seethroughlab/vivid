#include "runtime/assets/asset_library.h"
#include "runtime/assets/asset_library_internal.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace vivid {

ImportResult AssetLibrary::import_asset(AssetKind kind, const std::string& source_path) {
    namespace fs = std::filesystem;
    ImportResult result;
    const AssetKindHandler* handler = handler_for_kind(kind);

    if (!handler) {
        result.error = "Unsupported asset kind";
        return result;
    }

    if (workspace_root_.empty()) {
        result.error = "Workspace root not set";
        return result;
    }

    // Validate source file
    std::error_code ec;
    if (!fs::exists(source_path, ec) || !fs::is_regular_file(source_path, ec)) {
        result.error = "Source file does not exist: " + source_path;
        return result;
    }

    std::string filename = fs::path(source_path).filename().string();
    if (!handler->accepts_source_file(fs::path(source_path), result.error)) {
        return result;
    }

    // Compute metadata before copying
    nlohmann::json kind_meta = nlohmann::json::object();
    if (!handler->probe_metadata(fs::path(source_path), kind_meta, result.error)) {
        return result;
    }

    // Same source path re-imports should be idempotent, but same-basename
    // files from different locations must remain distinct assets.
    std::string source_identity = fs::absolute(fs::path(source_path)).lexically_normal().string();

    // Generate asset ID
    std::string rel_path = filename;  // workspace assets use just the filename
    std::string asset_id = asset_internal::generate_asset_id(
        kind, AssetScope::Workspace, "", source_identity);

    // Check for existing import from the same normalized source path (idempotent)
    for (const auto& e : entries_) {
        if (e.scope == AssetScope::Workspace && e.source_identity == source_identity) {
            result.ok = true;
            result.entry = e;
            return result;
        }
    }

    // Create workspace library directory
    fs::path kind_dir = handler->directory_name();
    fs::path asset_dir = workspace_root_ / "assets" / "library" / kind_dir / asset_id;
    fs::path source_dir = asset_dir / "source";
    fs::create_directories(source_dir, ec);
    if (ec) {
        result.error = "Failed to create asset directory: " + ec.message();
        return result;
    }

    // Copy source file
    fs::path dest_file = source_dir / filename;
    fs::copy_file(source_path, dest_file, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.error = "Failed to copy file: " + ec.message();
        return result;
    }

    // Build entry
    AssetEntry entry;
    entry.asset_id = asset_id;
    entry.kind = kind;
    entry.scope = AssetScope::Workspace;
    entry.display_name = asset_internal::sanitize_display_name(filename);
    entry.canonical_path = dest_file.string();
    entry.relative_path = filename;
    entry.source_identity = source_identity;
    entry.kind_meta = std::move(kind_meta);
    std::string ext = asset_internal::file_extension_lower(filename);
    entry.file_format = ext;
    entry.file_size = static_cast<uint64_t>(fs::file_size(dest_file, ec));
    entry.source_hash = asset_internal::compute_file_hash(dest_file.string());
    entry.imported_at = asset_internal::iso_timestamp_now();
    entry.discovered_at = entry.imported_at;

    // Write sidecar
    fs::path sidecar_path = asset_dir / "asset.json";
    if (!asset_internal::write_asset_sidecar(sidecar_path.string(), entry)) {
        result.error = "Failed to write asset.json sidecar";
        return result;
    }

    // Create cache directory (empty for now, available for future derived artifacts)
    fs::path cache_dir = workspace_root_ / "assets" / "library" / ".cache" / kind_dir / asset_id;
    fs::create_directories(cache_dir, ec);

    entries_.push_back(entry);
    result.ok = true;
    result.entry = entry;

    std::fprintf(stderr, "[asset_library] Imported asset: %s → %s\n",
                 source_path.c_str(), asset_id.c_str());
    return result;
}

} // namespace vivid
