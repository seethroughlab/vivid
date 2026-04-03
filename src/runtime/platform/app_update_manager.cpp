#include "runtime/platform/app_update_manager.h"

#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <thread>
#include <vector>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

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

static std::string first_match(const std::string& s, const std::regex& re) {
    std::smatch m;
    if (std::regex_search(s, m, re) && m.size() >= 2) return trim_copy(m[1].str());
    return {};
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

    // Use posix_spawn instead of popen to avoid shell injection via the URL.
    std::string url = appcast_url();

    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = AppUpdateFetchState::Error;
        error_ = "failed to create pipe";
        return;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, pipe_fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[0]);
    posix_spawn_file_actions_addclose(&actions, pipe_fds[1]);

    const char* argv[] = {"curl", "-sS", "--max-time", "10", url.c_str(), nullptr};
    pid_t pid = 0;
    int spawn_err = posix_spawnp(&pid, "curl",
                                  &actions, nullptr,
                                  const_cast<char* const*>(argv), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(pipe_fds[1]);

    if (spawn_err != 0) {
        ::close(pipe_fds[0]);
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = AppUpdateFetchState::Error;
        error_ = "failed to execute curl";
        return;
    }

    std::string output;
    std::array<char, 4096> buf{};
    ssize_t n;
    while ((n = ::read(pipe_fds[0], buf.data(), buf.size())) > 0)
        output.append(buf.data(), static_cast<size_t>(n));
    ::close(pipe_fds[0]);

    int wstatus = 0;
    ::waitpid(pid, &wstatus, 0);
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = AppUpdateFetchState::Error;
        error_ = "curl failed: " + output.substr(0, 200);
        return;
    }

    AppUpdateInfo parsed{};
    std::string parse_error;
    if (!parse_appcast(output, current_version_, parsed, parse_error)) {
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
    static const std::regex kItemRe("<item\\b[^>]*>([\\s\\S]*?)</item>",
                                    std::regex::icase);
    std::sregex_iterator it(xml.begin(), xml.end(), kItemRe);
    std::sregex_iterator end;
    if (it == end) {
        error = "no <item> found in appcast";
        return false;
    }

    std::string best_version;
    std::string best_download;
    std::string best_release_notes;
    std::string best_pub_date;
    std::string best_title;
    std::string best_min_os;

    for (; it != end; ++it) {
        const std::string body = (*it)[1].str();
        std::string v = first_match(body, std::regex("sparkle:shortVersionString=\"([^\"]+)\"",
                                                     std::regex::icase));
        if (v.empty())
            v = first_match(body, std::regex("sparkle:version=\"([^\"]+)\"",
                                             std::regex::icase));
        if (v.empty()) continue;

        std::string url = first_match(body, std::regex("<enclosure\\b[^>]*url=\"([^\"]+)\"",
                                                       std::regex::icase));
        if (url.empty()) continue;

        if (best_version.empty() || compare_semver(best_version, v) < 0) {
            best_version = v;
            best_download = url;
            best_release_notes = first_match(body, std::regex("<sparkle:releaseNotesLink>([\\s\\S]*?)</sparkle:releaseNotesLink>",
                                                              std::regex::icase));
            best_pub_date = first_match(body, std::regex("<pubDate>([\\s\\S]*?)</pubDate>",
                                                         std::regex::icase));
            best_title = first_match(body, std::regex("<title>([\\s\\S]*?)</title>",
                                                      std::regex::icase));
            best_min_os = first_match(body, std::regex("sparkle:minimumSystemVersion=\"([^\"]+)\"",
                                                       std::regex::icase));
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
