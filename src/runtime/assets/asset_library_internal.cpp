#include "runtime/assets/asset_library_internal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace vivid::asset_internal {

std::string compute_file_hash(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return {};

    // FNV-1a 64-bit
    uint64_t hash = 14695981039346656037ULL;
    char buf[8192];
    while (ifs.read(buf, sizeof(buf)) || ifs.gcount() > 0) {
        auto count = ifs.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<uint8_t>(buf[i]);
            hash *= 1099511628211ULL;
        }
    }

    std::ostringstream ss;
    ss << "fnv1a:0x" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

std::string generate_asset_id(AssetKind kind, AssetScope scope,
                              const std::string& package_name,
                              const std::string& relative_path) {
    // Build a canonical identity string, then FNV-1a it.
    std::string identity = asset_kind_str(kind);
    identity += ':';
    identity += asset_scope_str(scope);
    identity += ':';
    identity += package_name;
    identity += ':';
    identity += relative_path;

    uint64_t hash = 14695981039346656037ULL;
    for (char c : identity) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }

    std::ostringstream ss;
    ss << "wt_" << std::hex << std::setfill('0') << std::setw(12)
       << (hash & 0xFFFFFFFFFFFFULL);
    return ss.str();
}

std::string sanitize_display_name(const std::string& filename) {
    namespace fs = std::filesystem;
    std::string stem = fs::path(filename).stem().string();
    // Replace underscores and hyphens with spaces
    for (char& c : stem) {
        if (c == '_' || c == '-') c = ' ';
    }
    // Title case
    bool capitalize_next = true;
    for (char& c : stem) {
        if (c == ' ') {
            capitalize_next = true;
        } else if (capitalize_next) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalize_next = false;
        }
    }
    return stem;
}

std::string iso_timestamp_now() {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &tt);
#else
    gmtime_r(&tt, &utc);
#endif
    std::ostringstream ss;
    ss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string file_extension_lower(const std::string& filename) {
    auto ext = std::filesystem::path(filename).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// --- Sidecar JSON I/O ---

bool read_asset_sidecar(const std::string& path, AssetEntry& entry) {
    std::ifstream ifs(path);
    if (!ifs) return false;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(ifs);
    } catch (...) {
        std::fprintf(stderr, "[asset_library] Failed to parse sidecar: %s\n", path.c_str());
        return false;
    }

    entry.asset_id = j.value("asset_id", "");
    std::string kind_str = j.value("kind", "");
    auto kind = parse_asset_kind(kind_str);
    if (!kind) return false;
    entry.kind = *kind;
    entry.display_name = j.value("display_name", "");
    entry.scope = AssetScope::Workspace;  // sidecars are always workspace assets
    entry.package_name = j.value("package_name", "");
    entry.relative_path = j.value("source_file", "");
    entry.source_identity = j.value("source_identity", "");
    entry.source_hash = j.value("source_hash", "");
    entry.imported_at = j.value("imported_at", "");
    entry.discovered_at = j.value("discovered_at", "");
    entry.file_size = j.value("file_size", uint64_t(0));
    entry.file_format = j.value("file_format", "");
    entry.kind_meta = nlohmann::json::object();

    if (j.contains("kind_meta") && j["kind_meta"].is_object()) {
        entry.kind_meta = j["kind_meta"];
    } else if (j.contains("wavetable") && j["wavetable"].is_object()) {
        // Legacy v1 sidecars stored the kind payload under a wavetable-specific key.
        entry.kind_meta = j["wavetable"];
    }

    // Resolve canonical_path from sidecar location + source_file
    namespace fs = std::filesystem;
    fs::path sidecar_dir = fs::path(path).parent_path();
    fs::path source_dir = sidecar_dir / "source";
    if (!entry.relative_path.empty()) {
        entry.canonical_path = (source_dir / entry.relative_path).string();
    }

    return !entry.asset_id.empty();
}

bool write_asset_sidecar(const std::string& path, const AssetEntry& entry) {
    nlohmann::json j;
    j["asset_id"] = entry.asset_id;
    j["kind"] = asset_kind_str(entry.kind);
    j["display_name"] = entry.display_name;
    j["source_file"] = entry.relative_path;
    if (!entry.source_identity.empty()) j["source_identity"] = entry.source_identity;
    j["source_hash"] = entry.source_hash;
    j["imported_at"] = entry.imported_at;
    if (!entry.discovered_at.empty()) j["discovered_at"] = entry.discovered_at;
    j["file_size"] = entry.file_size;
    j["file_format"] = entry.file_format;
    j["kind_meta"] = entry.kind_meta.is_object() ? entry.kind_meta : nlohmann::json::object();

    std::ofstream ofs(path);
    if (!ofs) {
        std::fprintf(stderr, "[asset_library] Failed to write sidecar: %s\n", path.c_str());
        return false;
    }
    ofs << j.dump(2) << '\n';
    return true;
}

} // namespace vivid::asset_internal
