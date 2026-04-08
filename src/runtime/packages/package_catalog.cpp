#include "runtime/packages/package_catalog.h"
#include "runtime/net/http_fetch.h"
#include "runtime/platform/platform.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace vivid {

static constexpr const char* kCatalogPrimaryURL =
    "https://raw.githubusercontent.com/seethroughlab/vivid/master/site/packages.json";
static constexpr const char* kCatalogFallbackURL1 =
    "https://raw.githubusercontent.com/seethroughlab/vivid/main/site/packages.json";
static constexpr const char* kCatalogFallbackURL2 =
    "https://raw.githubusercontent.com/seethroughlab/package-index/main/packages.json";

static constexpr int kCacheTTLSeconds = 3600;  // 1 hour

static std::string cache_path() {
    return get_config_dir() + "/package-catalog-cache.json";
}

static bool skip_catalog_network_fetch() {
    const char* v = std::getenv("VIVID_SKIP_PACKAGE_CATALOG_NETWORK");
    return v && v[0] != '\0' && std::string(v) != "0";
}

static std::vector<std::string> catalog_urls() {
    // Optional explicit override for local testing/ops.
    const char* override_url = std::getenv("VIVID_PACKAGE_CATALOG_URL");
    if (override_url && override_url[0] != '\0')
        return {std::string(override_url)};
    return {
        kCatalogPrimaryURL,
        kCatalogFallbackURL1,
        kCatalogFallbackURL2,
    };
}

PackageCatalog::PackageCatalog(PackageManager& pm) : pm_(pm) {}

void PackageCatalog::refresh() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == CatalogFetchState::Fetching) return;
        state_ = CatalogFetchState::Fetching;
        error_.clear();
    }

    // NOTE: PackageCatalog must outlive the detached thread (safe in practice —
    // the catalog lives for the process lifetime).
    std::thread(&PackageCatalog::fetch_thread_fn, this).detach();
}

CatalogFetchState PackageCatalog::fetch_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string PackageCatalog::fetch_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

std::vector<CatalogEntry> PackageCatalog::entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

CatalogUpdateSummary PackageCatalog::summarize_updates(const std::string& core_version) const {
    std::lock_guard<std::mutex> lock(mutex_);
    CatalogUpdateSummary summary;
    for (const auto& e : entries_) {
        if (!e.installed) continue;
        summary.installed_packages++;
        PackageInfo installed;
        installed.name = e.name;
        installed.version = e.installed_version;
        auto assessment = PackageManager::assess_update(
            installed, e.version, e.vivid_core, core_version);
        if (assessment.update_available) summary.updates_available++;
        if (assessment.classification == PackageUpdateClass::IncompatibleUpdate)
            summary.incompatible_updates++;
    }
    return summary;
}

InstallResult PackageCatalog::install(const std::string& name) {
    std::string url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& e : entries_) {
            if (e.name == name) { url = e.url; break; }
        }
    }
    if (url.empty()) {
        InstallResult r;
        r.error = "Package '" + name + "' not found in catalog";
        return r;
    }
    auto result = pm_.install(url);
    if (result.success) {
        std::lock_guard<std::mutex> lock(mutex_);
        merge_with_installed();
    }
    return result;
}

bool PackageCatalog::uninstall(const std::string& name) {
    bool ok = pm_.uninstall(name);
    if (ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        merge_with_installed();
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Background fetch
// ---------------------------------------------------------------------------

void PackageCatalog::fetch_thread_fn() {
    // Try loading from cache first
    std::vector<CatalogEntry> cached;
    bool have_cache = load_cache(cached);

    if (have_cache) {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_ = cached;
        merge_with_installed();
    }

    if (skip_catalog_network_fetch()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (have_cache) {
            state_ = CatalogFetchState::Ready;
        } else {
            state_ = CatalogFetchState::Error;
            error_ = "catalog network fetch disabled and no cache available";
        }
        return;
    }

    std::vector<CatalogEntry> remote;
    std::string fetch_error_msg = "catalog fetch failed";
    bool fetched = false;

    // Try catalog URLs in deterministic order; first successful parse wins.
    for (const auto& url : catalog_urls()) {
        auto result = http_get(url, 10);
        if (!result.ok) {
            fetch_error_msg = "fetch failed: " + result.error;
            continue;
        }

        remote.clear();
        if (!parse_index_json(result.body, remote)) {
            fetch_error_msg = "Failed to parse catalog JSON";
            continue;
        }
        fetched = true;
        break;
    }

    if (!fetched) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!have_cache) {
            state_ = CatalogFetchState::Error;
            error_ = fetch_error_msg;
        } else {
            // Keep cached data, mark ready
            state_ = CatalogFetchState::Ready;
        }
        return;
    }

    // Success — update entries and cache (save_cache inside lock so it
    // completes before state transitions to Ready; callers observe a
    // consistent snapshot).
    std::lock_guard<std::mutex> lock(mutex_);
    save_cache(remote);
    entries_ = std::move(remote);
    merge_with_installed();
    state_ = CatalogFetchState::Ready;
}

// ---------------------------------------------------------------------------
// JSON parsing
// ---------------------------------------------------------------------------

bool PackageCatalog::parse_index_json(const std::string& json_str,
                                      std::vector<CatalogEntry>& out) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_str);
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }

    if (!root.is_object()) return false;

    if (!root.contains("packages") || !root["packages"].is_array())
        return false;

    for (const auto& val : root["packages"]) {
        if (!val.is_object()) continue;

        CatalogEntry e;

        if (!val.contains("name") || !val["name"].is_string()) continue;
        e.name = val["name"].get<std::string>();

        if (val.contains("description") && val["description"].is_string())
            e.description = val["description"].get<std::string>();
        if (val.contains("version") && val["version"].is_string())
            e.version = val["version"].get<std::string>();
        if (val.contains("vivid_core") && val["vivid_core"].is_string())
            e.vivid_core = val["vivid_core"].get<std::string>();
        if (val.contains("author") && val["author"].is_string())
            e.author = val["author"].get<std::string>();
        if (val.contains("url") && val["url"].is_string())
            e.url = val["url"].get<std::string>();
        if (val.contains("category") && val["category"].is_string())
            e.category = val["category"].get<std::string>();
        if (val.contains("description_short") && val["description_short"].is_string())
            e.description_short = val["description_short"].get<std::string>();
        if (val.contains("status") && val["status"].is_string())
            e.status = val["status"].get<std::string>();
        if (val.contains("status_note") && val["status_note"].is_string())
            e.status_note = val["status_note"].get<std::string>();
        if (val.contains("preview_image_url") && val["preview_image_url"].is_string())
            e.preview_image_url = val["preview_image_url"].get<std::string>();
        if (val.contains("repo_url") && val["repo_url"].is_string())
            e.repo_url = val["repo_url"].get<std::string>();
        if (val.contains("homepage_url") && val["homepage_url"].is_string())
            e.homepage_url = val["homepage_url"].get<std::string>();
        if (val.contains("install_url") && val["install_url"].is_string())
            e.install_url = val["install_url"].get<std::string>();

        out.push_back(std::move(e));
    }

    return true;
}

// ---------------------------------------------------------------------------
// Merge with installed packages
// ---------------------------------------------------------------------------

void PackageCatalog::merge_with_installed() {
    auto installed = pm_.list();
    for (auto& entry : entries_) {
        entry.installed = false;
        entry.installed_version.clear();
        for (const auto& pkg : installed) {
            if (pkg.name == entry.name) {
                entry.installed = true;
                entry.installed_version = pkg.version;
                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

bool PackageCatalog::load_cache(std::vector<CatalogEntry>& out) {
    std::string path = cache_path();
    if (!std::filesystem::exists(path)) return false;

    // Reject stale caches; the caller will fall through to a live network fetch.
    std::error_code ec;
    auto mtime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        // Can't determine mtime — treat as expired so we don't serve stale data.
        return false;
    }
    auto age = std::chrono::duration_cast<std::chrono::seconds>(
        std::filesystem::file_time_type::clock::now() - mtime);
    if (age.count() > kCacheTTLSeconds) {
        std::fprintf(stderr,
            "[vivid] PackageCatalog: cache is stale (%lld s old), fetching fresh data\n",
            static_cast<long long>(age.count()));
        return false;
    }

    std::ifstream ifs(path);
    if (!ifs) return false;

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json_str = ss.str();

    if (!parse_index_json(json_str, out)) return false;

    return true;
}

void PackageCatalog::save_cache(const std::vector<CatalogEntry>& entries) {
    // Write as the same format as the remote index
    nlohmann::json root;
    root["schema_version"] = 1;

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries) {
        nlohmann::json obj;
        obj["name"] = e.name;
        obj["description"] = e.description;
        obj["version"] = e.version;
        if (!e.vivid_core.empty()) obj["vivid_core"] = e.vivid_core;
        obj["author"] = e.author;
        obj["url"] = e.url;
        if (!e.category.empty()) obj["category"] = e.category;
        if (!e.description_short.empty()) obj["description_short"] = e.description_short;
        if (!e.status.empty()) obj["status"] = e.status;
        if (!e.status_note.empty()) obj["status_note"] = e.status_note;
        if (!e.preview_image_url.empty()) obj["preview_image_url"] = e.preview_image_url;
        if (!e.repo_url.empty()) obj["repo_url"] = e.repo_url;
        if (!e.homepage_url.empty()) obj["homepage_url"] = e.homepage_url;
        if (!e.install_url.empty()) obj["install_url"] = e.install_url;
        arr.push_back(std::move(obj));
    }
    root["packages"] = std::move(arr);

    std::string path = cache_path();
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    std::ofstream ofs(path);
    if (!ofs) {
        std::fprintf(stderr,
            "[vivid] PackageCatalog: warning: failed to write cache to %s\n",
            path.c_str());
    } else {
        ofs << root.dump(2);
    }
}

} // namespace vivid
