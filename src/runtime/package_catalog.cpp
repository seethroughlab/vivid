#include "runtime/package_catalog.h"
#include "runtime/platform.h"
#include "yyjson.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace vivid {

// Shell-quote a string (single-quote style, with embedded ' escaped).
static std::string quote(const std::string& s) {
    std::string escaped;
    for (char c : s) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    return "'" + escaped + "'";
}

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
    std::string fetch_error_msg = "Failed to execute curl";
    bool fetched = false;

    // Try catalog URLs in deterministic order; first successful parse wins.
    for (const auto& url : catalog_urls()) {
        std::string cmd = "curl -sS --max-time 10 " + quote(url) + " 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            fetch_error_msg = "Failed to execute curl";
            continue;
        }

        std::string output;
        std::array<char, 4096> buf;
        bool output_truncated = false;
        while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
            if (output.size() < 1024 * 1024)
                output += buf.data();
            else if (!output_truncated) {
                output += "\n... (catalog fetch output truncated at 1MB) ...\n";
                output_truncated = true;
            }
        }
        int status = pclose(pipe);

        if (status != 0) {
            fetch_error_msg = "curl failed: " + output.substr(0, 200);
            continue;
        }

        remote.clear();
        if (!parse_index_json(output, remote)) {
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
    yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
    if (!doc) return false;

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_val* pkgs = yyjson_obj_get(root, "packages");
    if (!pkgs || !yyjson_is_arr(pkgs)) {
        yyjson_doc_free(doc);
        return false;
    }

    size_t idx, max;
    yyjson_val* val;
    yyjson_arr_foreach(pkgs, idx, max, val) {
        if (!yyjson_is_obj(val)) continue;

        CatalogEntry e;
        yyjson_val* v;

        v = yyjson_obj_get(val, "name");
        if (!v || !yyjson_is_str(v)) continue;
        e.name = yyjson_get_str(v);

        v = yyjson_obj_get(val, "description");
        if (v && yyjson_is_str(v)) e.description = yyjson_get_str(v);

        v = yyjson_obj_get(val, "version");
        if (v && yyjson_is_str(v)) e.version = yyjson_get_str(v);

        v = yyjson_obj_get(val, "vivid_core");
        if (v && yyjson_is_str(v)) e.vivid_core = yyjson_get_str(v);

        v = yyjson_obj_get(val, "author");
        if (v && yyjson_is_str(v)) e.author = yyjson_get_str(v);

        v = yyjson_obj_get(val, "url");
        if (v && yyjson_is_str(v)) e.url = yyjson_get_str(v);

        v = yyjson_obj_get(val, "category");
        if (v && yyjson_is_str(v)) e.category = yyjson_get_str(v);

        v = yyjson_obj_get(val, "description_short");
        if (v && yyjson_is_str(v)) e.description_short = yyjson_get_str(v);

        v = yyjson_obj_get(val, "status");
        if (v && yyjson_is_str(v)) e.status = yyjson_get_str(v);

        v = yyjson_obj_get(val, "status_note");
        if (v && yyjson_is_str(v)) e.status_note = yyjson_get_str(v);

        v = yyjson_obj_get(val, "preview_image_url");
        if (v && yyjson_is_str(v)) e.preview_image_url = yyjson_get_str(v);

        v = yyjson_obj_get(val, "repo_url");
        if (v && yyjson_is_str(v)) e.repo_url = yyjson_get_str(v);

        v = yyjson_obj_get(val, "homepage_url");
        if (v && yyjson_is_str(v)) e.homepage_url = yyjson_get_str(v);

        v = yyjson_obj_get(val, "install_url");
        if (v && yyjson_is_str(v)) e.install_url = yyjson_get_str(v);

        out.push_back(std::move(e));
    }

    yyjson_doc_free(doc);
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
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    yyjson_mut_obj_add_int(doc, root, "schema_version", 1);

    yyjson_mut_val* arr = yyjson_mut_arr(doc);
    for (const auto& e : entries) {
        yyjson_mut_val* obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, obj, "name", e.name.c_str());
        yyjson_mut_obj_add_strcpy(doc, obj, "description", e.description.c_str());
        yyjson_mut_obj_add_strcpy(doc, obj, "version", e.version.c_str());
        if (!e.vivid_core.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "vivid_core", e.vivid_core.c_str());
        yyjson_mut_obj_add_strcpy(doc, obj, "author", e.author.c_str());
        yyjson_mut_obj_add_strcpy(doc, obj, "url", e.url.c_str());
        if (!e.category.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "category", e.category.c_str());
        if (!e.description_short.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "description_short", e.description_short.c_str());
        if (!e.status.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "status", e.status.c_str());
        if (!e.status_note.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "status_note", e.status_note.c_str());
        if (!e.preview_image_url.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "preview_image_url", e.preview_image_url.c_str());
        if (!e.repo_url.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "repo_url", e.repo_url.c_str());
        if (!e.homepage_url.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "homepage_url", e.homepage_url.c_str());
        if (!e.install_url.empty())
            yyjson_mut_obj_add_strcpy(doc, obj, "install_url", e.install_url.c_str());

        yyjson_mut_arr_add_val(arr, obj);
    }
    yyjson_mut_obj_add_val(doc, root, "packages", arr);

    char* json_str = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY, nullptr);
    if (json_str) {
        std::string path = cache_path();
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());
        std::ofstream ofs(path);
        if (!ofs) {
            std::fprintf(stderr,
                "[vivid] PackageCatalog: warning: failed to write cache to %s\n",
                path.c_str());
        } else {
            ofs << json_str;
        }
        free(json_str);
    }

    yyjson_mut_doc_free(doc);
}

} // namespace vivid
