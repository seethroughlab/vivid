#pragma once

#include "runtime/package_manager.h"
#include <mutex>
#include <string>
#include <vector>

namespace vivid {

enum class CatalogFetchState { Idle, Fetching, Ready, Error };

struct CatalogEntry {
    // From remote index
    std::string name;
    std::string description;
    std::string version;
    std::string vivid_core;
    std::string author;
    std::string url;
    std::string category;
    std::vector<std::string> tags;
    // Computed from local state
    bool installed = false;
    std::string installed_version;
};

struct CatalogUpdateSummary {
    int installed_packages = 0;
    int updates_available = 0;
    int incompatible_updates = 0;
};

class PackageCatalog {
public:
    explicit PackageCatalog(PackageManager& pm);

    // Kick off a background fetch (detached thread). Safe to call repeatedly.
    // IMPORTANT: the detached thread captures `this` — the PackageCatalog
    // instance must have process lifetime (constructed once, never destroyed
    // while the application is running) to avoid use-after-free.
    void refresh();

    CatalogFetchState fetch_state() const;
    std::string fetch_error() const;

    // Thread-safe snapshot of all entries (returns by value).
    std::vector<CatalogEntry> entries() const;
    CatalogUpdateSummary summarize_updates(const std::string& core_version) const;

    // Parse package index JSON payload into catalog entries.
    static bool parse_index_json(const std::string& json_str,
                                 std::vector<CatalogEntry>& out);

    // Install by catalog name → looks up URL, delegates to PackageManager.
    InstallResult install(const std::string& name);

    // Uninstall by catalog name.
    bool uninstall(const std::string& name);

private:
    void fetch_thread_fn();
    void merge_with_installed();
    bool load_cache(std::vector<CatalogEntry>& out);
    void save_cache(const std::vector<CatalogEntry>& entries);

    PackageManager& pm_;
    mutable std::mutex mutex_;
    CatalogFetchState state_ = CatalogFetchState::Idle;
    std::string error_;
    std::vector<CatalogEntry> entries_;
};

} // namespace vivid
