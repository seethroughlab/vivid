#include "runtime/platform/app_update_manager.h"
#include "runtime/net/http_fetch.h"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <thread>

#include <tinyxml2.h>

namespace vivid {

static constexpr const char* kDefaultAppcastURL =
    "https://vivid.seethroughlab.com/appcast.xml";
static std::atomic<uint32_t> g_active_workers{0};
static std::atomic<uint32_t> g_max_concurrent_workers{0};

static std::string trim_copy(std::string s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) b++;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) e--;
    return s.substr(b, e - b);
}

static bool parse_semver_triplet(const std::string& v, int& maj, int& min, int& pat) {
    maj = min = pat = 0;
    std::regex re("^\\s*v?(\\d+)\\.(\\d+)\\.(\\d+)\\s*$");
    std::smatch m;
    if (!std::regex_match(v, m, re) || m.size() != 4) return false;
    maj = std::stoi(m[1].str());
    min = std::stoi(m[2].str());
    pat = std::stoi(m[3].str());
    return true;
}

static int compare_semver(const std::string& a, const std::string& b) {
    int a1, a2, a3, b1, b2, b3;
    if (!parse_semver_triplet(a, a1, a2, a3)) return 0;
    if (!parse_semver_triplet(b, b1, b2, b3)) return 0;
    if (a1 != b1) return (a1 < b1) ? -1 : 1;
    if (a2 != b2) return (a2 < b2) ? -1 : 1;
    if (a3 != b3) return (a3 < b3) ? -1 : 1;
    return 0;
}

static std::string element_text(const tinyxml2::XMLElement* parent, const char* name) {
    if (!parent) return {};
    const tinyxml2::XMLElement* el = parent->FirstChildElement(name);
    if (!el) return {};
    const char* t = el->GetText();
    return t ? trim_copy(t) : std::string{};
}

AppUpdateManager::AppUpdateManager(std::string current_version)
    : current_version_(std::move(current_version)) {
    latest_.current_version = current_version_;
}

AppUpdateManager::~AppUpdateManager() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void AppUpdateManager::refresh() {
    std::thread old_worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == AppUpdateFetchState::Fetching) return;
        state_ = AppUpdateFetchState::Fetching;
        error_.clear();
        if (worker_.joinable()) {
            old_worker = std::move(worker_);
        }
    }
    if (old_worker.joinable()) {
        old_worker.join();
    }
    worker_ = std::thread(&AppUpdateManager::fetch_thread_fn, this);
}

AppUpdateFetchState AppUpdateManager::fetch_state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

std::string AppUpdateManager::fetch_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

AppUpdateInfo AppUpdateManager::latest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_;
}

bool AppUpdateManager::is_skipped(const std::string& version) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !version.empty() && version == skipped_version_;
}

void AppUpdateManager::set_skipped_version(std::string version) {
    std::lock_guard<std::mutex> lock(mutex_);
    skipped_version_ = std::move(version);
}

std::string AppUpdateManager::skipped_version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return skipped_version_;
}

std::string AppUpdateManager::appcast_url() {
    const char* v = std::getenv("VIVID_APPCAST_URL");
    if (v && v[0] != '\0') return v;
    return kDefaultAppcastURL;
}

bool AppUpdateManager::parse_appcast_for_test(const std::string& xml,
                                              const std::string& current_version,
                                              AppUpdateInfo& out,
                                              std::string& error) {
    return parse_appcast(xml, current_version, out, error);
}

void AppUpdateManager::reset_worker_metrics_for_test() {
    g_active_workers.store(0, std::memory_order_relaxed);
    g_max_concurrent_workers.store(0, std::memory_order_relaxed);
}

uint32_t AppUpdateManager::active_workers_for_test() {
    return g_active_workers.load(std::memory_order_relaxed);
}

uint32_t AppUpdateManager::max_concurrent_workers_for_test() {
    return g_max_concurrent_workers.load(std::memory_order_relaxed);
}

void AppUpdateManager::fetch_thread_fn() {
    const uint32_t active = g_active_workers.fetch_add(1, std::memory_order_relaxed) + 1;
    uint32_t observed = g_max_concurrent_workers.load(std::memory_order_relaxed);
    while (active > observed &&
           !g_max_concurrent_workers.compare_exchange_weak(
               observed, active, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
    auto worker_guard = std::unique_ptr<void, void(*)(void*)>(
        reinterpret_cast<void*>(1),
        [](void*) { g_active_workers.fetch_sub(1, std::memory_order_relaxed); });

    const char* delay_env = std::getenv("VIVID_APP_UPDATE_TEST_DELAY_MS");
    if (delay_env && delay_env[0] != '\0') {
        int delay_ms = std::atoi(delay_env);
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    auto result = http_get(appcast_url(), 10);
    if (!result.ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = AppUpdateFetchState::Error;
        error_ = "fetch failed: " + result.error;
        return;
    }

    AppUpdateInfo parsed{};
    std::string parse_error;
    if (!parse_appcast(result.body, current_version_, parsed, parse_error)) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = AppUpdateFetchState::Error;
        error_ = parse_error.empty() ? "failed to parse appcast" : parse_error;
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    latest_ = std::move(parsed);
    state_ = AppUpdateFetchState::Ready;
}

bool AppUpdateManager::parse_appcast(const std::string& xml,
                                     const std::string& current_version,
                                     AppUpdateInfo& out,
                                     std::string& error) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
        error = "failed to parse appcast XML";
        return false;
    }

    const tinyxml2::XMLElement* rss = doc.FirstChildElement("rss");
    const tinyxml2::XMLElement* channel = rss ? rss->FirstChildElement("channel") : nullptr;
    const tinyxml2::XMLElement* item = channel ? channel->FirstChildElement("item") : nullptr;
    if (!item) {
        error = "no <item> found in appcast";
        return false;
    }

    std::string best_version;
    std::string best_download;
    std::string best_release_notes;
    std::string best_pub_date;
    std::string best_title;
    std::string best_min_os;

    for (; item; item = item->NextSiblingElement("item")) {
        const tinyxml2::XMLElement* enclosure = item->FirstChildElement("enclosure");

        // Version: prefer sparkle:shortVersionString, fall back to sparkle:version.
        // These attributes live on the enclosure element.
        std::string v;
        if (enclosure) {
            const char* svs = enclosure->Attribute("sparkle:shortVersionString");
            if (svs) v = trim_copy(svs);
            if (v.empty()) {
                const char* sv = enclosure->Attribute("sparkle:version");
                if (sv) v = trim_copy(sv);
            }
        }
        if (v.empty()) continue;

        // Enclosure URL is required.
        std::string url;
        if (enclosure) {
            const char* u = enclosure->Attribute("url");
            if (u) url = trim_copy(u);
        }
        if (url.empty()) continue;

        if (best_version.empty() || compare_semver(best_version, v) < 0) {
            best_version = v;
            best_download = url;
            best_release_notes = element_text(item, "sparkle:releaseNotesLink");
            best_pub_date = element_text(item, "pubDate");
            best_title = element_text(item, "title");
            if (enclosure) {
                const char* min_os = enclosure->Attribute("sparkle:minimumSystemVersion");
                best_min_os = min_os ? trim_copy(min_os) : std::string{};
            } else {
                best_min_os.clear();
            }
        }
    }

    if (best_version.empty() || best_download.empty()) {
        error = "no valid enclosure/version in appcast";
        return false;
    }

    out = {};
    out.current_version = current_version;
    out.latest_version = best_version;
    out.download_url = best_download;
    out.release_notes_url = best_release_notes;
    out.publication_date = best_pub_date;
    out.title = best_title;
    out.minimum_system_version = best_min_os;
    out.update_available = compare_semver(current_version, best_version) < 0;
    return true;
}

}  // namespace vivid
