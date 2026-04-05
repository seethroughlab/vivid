#pragma once

#include <string>
#include <vector>
#include <functional>

namespace vivid {

struct PreviewControl {
    std::string node;
    std::string param;
    std::string label;
};

struct PreviewSnapshotRow {
    std::string label;
    std::string value;
};

struct PreviewControlOption {
    std::string node;
    std::vector<std::string> params;
};

struct ExampleEntry {
    std::string id;
    std::string title;
    std::string path;
    std::string summary;
    std::vector<std::string> tags;
    std::string difficulty;
    std::vector<std::string> domains;
    std::vector<std::string> requires_packages;
    int featured_rank = 1000;
    int estimated_minutes = 0;
    std::string content_kind;
    std::string category;
    std::string family;
    std::string role;
    std::string playability;
    std::vector<PreviewControl> preview_controls;
    std::vector<PreviewSnapshotRow> preview_rows;
    std::string package_name;
};

struct GraphMetaEditData {
    std::string path;
    std::string id;
    std::string title;
    std::string description;
    std::string tags_csv;
    std::string difficulty;
    std::string domains_csv;
    std::string requires_packages_csv;
    std::string featured_rank;
    std::string content_kind;
    std::string category;
    std::string family;
    std::string role;
    std::string playability;
    std::vector<PreviewControl> preview_controls;
    std::vector<PreviewControlOption> preview_options;
};

struct AssetBrowserEntry {
    std::string asset_id;
    std::string kind;
    std::string display_name;
    std::string scope;
    std::string package_name;
    std::string canonical_path;
    std::string relative_path;
    std::string file_format;
};

struct AssetBrowserCallbacks {
    std::function<void()> refresh;
    std::function<std::vector<AssetBrowserEntry>(const std::string&)> list_entries;
    std::function<bool(const std::string&, const std::string&, AssetBrowserEntry&, std::string&)> import_asset;
};

enum class PackageBrowserFetchState {
    Idle,
    Fetching,
    Ready,
    Error
};

struct PackageBrowserEntry {
    std::string name;
    std::string description;
    std::string version;
    std::string author;
    std::string category;
    std::vector<std::string> tags;
    bool installed = false;
    bool linked = false;
    bool needs_rebuild = false;      // ABI mismatch or load failure detected
    std::string health_detail;       // e.g. "ABI mismatch — try rebuild"
};

struct PackageBrowserUpdateSummary {
    int installed_packages = 0;
    int updates_available = 0;
    int incompatible_updates = 0;
};

struct PackageBrowserCallbacks {
    std::function<void()> refresh;
    std::function<std::vector<PackageBrowserEntry>()> list_entries;
    std::function<PackageBrowserFetchState()> fetch_state;
    std::function<std::string()> fetch_error;
    std::function<PackageBrowserUpdateSummary()> update_summary;
    std::function<bool(const std::string&, std::string&)> install;
    std::function<bool(const std::string&, std::string&)> uninstall;
    std::function<bool(const std::string&, std::string&)> unlink;
    std::function<bool(const std::string&, std::string&)> link;
    std::function<bool(const std::string&, std::string&)> rebuild;
    std::function<void()> open_build_console;
};

enum class SaveConfirmAction { kNewGraph, kNewProject };

} // namespace vivid

namespace vivid::ui {

using ::vivid::PreviewControl;
using ::vivid::PreviewControlOption;
using ::vivid::PreviewSnapshotRow;
using ::vivid::ExampleEntry;
using ::vivid::GraphMetaEditData;
using ::vivid::AssetBrowserEntry;
using ::vivid::AssetBrowserCallbacks;
using ::vivid::PackageBrowserFetchState;
using ::vivid::PackageBrowserEntry;
using ::vivid::PackageBrowserUpdateSummary;
using ::vivid::PackageBrowserCallbacks;
using ::vivid::SaveConfirmAction;

} // namespace vivid::ui
