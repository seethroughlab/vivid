#pragma once

#include <string>
#include <vector>
#include <functional>

namespace vivid::ui {

struct ExampleEntry {
    std::string id;
    std::string title;
    std::string path;
    std::string summary;
    std::vector<std::string> tags;
    std::string difficulty;
    std::vector<std::string> envs;
    std::vector<std::string> requires_packages;
    int featured_rank = 1000;
    int estimated_minutes = 0;
};

struct GraphMetaEditData {
    std::string path;
    std::string id;
    std::string title;
    std::string description;
    std::string tags_csv;
    std::string difficulty;
    std::string envs_csv;
    std::string requires_packages_csv;
    std::string featured_rank;
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

} // namespace vivid::ui
