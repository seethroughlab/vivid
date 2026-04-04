#pragma once

#include "runtime/assets/asset_library.h"
#include <string>

namespace vivid::asset_internal {

// FNV-1a hash of file contents, returned as "fnv1a:0x<hex>" string.
// Returns empty string on read failure.
std::string compute_file_hash(const std::string& path);

// Deterministic asset ID from identity components.
std::string generate_asset_id(AssetKind kind, AssetScope scope,
                              const std::string& package_name,
                              const std::string& relative_path);

// Prettify a filename into a display name: stem, underscores to spaces, title case.
std::string sanitize_display_name(const std::string& filename);

// Current time as ISO 8601 UTC string.
std::string iso_timestamp_now();

// Extract lowercase file extension without dot (e.g. "wav").
std::string file_extension_lower(const std::string& filename);

// Probe a WAV file and fill metadata. Returns false on failure.
bool probe_wavetable_metadata(const std::string& path, WavetableAssetMeta& meta);

// Read an asset.json sidecar into an AssetEntry. Returns false on failure.
bool read_asset_sidecar(const std::string& path, AssetEntry& entry);

// Write an asset.json sidecar from an AssetEntry. Returns false on failure.
bool write_asset_sidecar(const std::string& path, const AssetEntry& entry);

} // namespace vivid::asset_internal
