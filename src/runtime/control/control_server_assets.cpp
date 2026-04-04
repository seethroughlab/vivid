#include "runtime/control/control_server_internal.h"

namespace vivid {

static nlohmann::json asset_entry_to_json(const AssetEntry& e) {
    nlohmann::json j;
    j["asset_id"] = e.asset_id;
    j["kind"] = asset_kind_str(e.kind);
    j["display_name"] = e.display_name;
    j["scope"] = asset_scope_str(e.scope);
    if (!e.package_name.empty()) j["package_name"] = e.package_name;
    j["canonical_path"] = e.canonical_path;
    j["relative_path"] = e.relative_path;
    j["source_hash"] = e.source_hash;
    if (!e.imported_at.empty()) j["imported_at"] = e.imported_at;
    if (!e.discovered_at.empty()) j["discovered_at"] = e.discovered_at;
    j["file_size"] = e.file_size;
    j["file_format"] = e.file_format;
    j["kind_meta"] = e.kind_meta.is_object() ? e.kind_meta : nlohmann::json::object();
    return j;
}

std::string handle_list_assets(AssetLibrary& lib, const nlohmann::json& root) {
    std::optional<AssetKind> kind;
    std::optional<AssetScope> scope;

    if (root.contains("kind") && root["kind"].is_string()) {
        std::string kind_str = root["kind"].get<std::string>();
        auto parsed_kind = parse_asset_kind(kind_str);
        if (!parsed_kind) return json_err("unsupported asset kind: " + kind_str);
        kind = *parsed_kind;
    }
    if (root.contains("scope") && root["scope"].is_string()) {
        std::string s = root["scope"].get<std::string>();
        if (s == "package") scope = AssetScope::Package;
        else if (s == "workspace") scope = AssetScope::Workspace;
    }

    auto entries = lib.list(kind, scope);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries) {
        arr.push_back(asset_entry_to_json(e));
    }
    return json_ok(std::move(arr));
}

std::string handle_inspect_asset(AssetLibrary& lib, const nlohmann::json& root) {
    if (!root.contains("asset_id") || !root["asset_id"].is_string())
        return json_err("missing required field 'asset_id'");

    std::string asset_id = root["asset_id"].get<std::string>();
    const AssetEntry* entry = lib.find(asset_id);
    if (!entry) return json_err("asset not found: " + asset_id);

    return json_ok(asset_entry_to_json(*entry));
}

std::string handle_import_asset(AssetLibrary& lib, const nlohmann::json& root) {
    if (!root.contains("source_path") || !root["source_path"].is_string())
        return json_err("missing required field 'source_path'");

    AssetKind kind = AssetKind::Wavetable;
    if (root.contains("kind") && root["kind"].is_string()) {
        std::string kind_str = root["kind"].get<std::string>();
        auto parsed_kind = parse_asset_kind(kind_str);
        if (!parsed_kind) return json_err("unsupported asset kind: " + kind_str);
        kind = *parsed_kind;
    }

    std::string source_path = root["source_path"].get<std::string>();
    auto result = lib.import_asset(kind, source_path);
    if (!result.ok) return json_err(result.error);

    return json_ok(asset_entry_to_json(result.entry));
}

std::string handle_refresh_assets(AssetLibrary& lib) {
    lib.refresh();
    nlohmann::json result;
    result["count"] = lib.size();
    return json_ok(std::move(result));
}

} // namespace vivid
