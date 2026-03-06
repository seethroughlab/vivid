#pragma once

#include <mutex>
#include <string>

namespace vivid {

enum class AppUpdateFetchState { Idle, Fetching, Ready, Error };

struct AppUpdateInfo {
    bool update_available = false;
    std::string current_version;
    std::string latest_version;
    std::string download_url;
    std::string release_notes_url;
    std::string publication_date;
    std::string title;
    std::string minimum_system_version;
};

class AppUpdateManager {
public:
    explicit AppUpdateManager(std::string current_version);

    // Background fetch (non-blocking). No-op if already fetching.
    void refresh();

    AppUpdateFetchState fetch_state() const;
    std::string fetch_error() const;
    AppUpdateInfo latest() const;

    // Optional per-user suppression support.
    bool is_skipped(const std::string& version) const;
    void set_skipped_version(std::string version);
    std::string skipped_version() const;

    // Testing hook / troubleshooting override.
    static std::string appcast_url();

    // Test hook for parser correctness (no network dependency).
    static bool parse_appcast_for_test(const std::string& xml,
                                       const std::string& current_version,
                                       AppUpdateInfo& out,
                                       std::string& error);

private:
    void fetch_thread_fn();

    static bool parse_appcast(const std::string& xml,
                              const std::string& current_version,
                              AppUpdateInfo& out,
                              std::string& error);

    std::string current_version_;
    mutable std::mutex mutex_;
    AppUpdateFetchState state_ = AppUpdateFetchState::Idle;
    std::string error_;
    AppUpdateInfo latest_;
    std::string skipped_version_;
};

}  // namespace vivid
