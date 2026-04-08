#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>

namespace vivid {

struct FileChangeEvent {
    std::string file_path;
    std::string target_name;  // cmake target derived from directory structure
};

class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();

    // Scan operators directory and start watching .cpp files
    bool start(const std::string& operators_dir);
    void stop();

    // Called from main thread each frame; drains pending events
    std::vector<FileChangeEvent> poll_changes();

    // Register a single file for watching. Thread-safe (locks watch_mutex_).
    bool add_watch(const std::string& path, const std::string& target_name);

    // Scan package operator directories and register them for watching.
    // Returns the number of files registered.
    int add_package_watches(const std::string& packages_dir);

    // Scan a filter directory and register .wgsl shader operators for watching.
    // Returns the number of files registered.
    int add_shader_operator_watches(const std::string& directory);

private:
    void watch_thread();
    void reopen_file(const std::string& path, const std::string& target_name);

    int kq_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // fd → (file_path, target_name) and last_event_time_. Protected by watch_mutex_.
    struct WatchEntry {
        std::string path;
        std::string target_name;
    };
    std::mutex watch_mutex_;
    std::unordered_map<int, WatchEntry> watched_fds_;
    std::unordered_map<std::string, int> path_to_fd_;  // reverse lookup: path → fd

    // Debounce: target → last event time (steady_clock ms)
    std::unordered_map<std::string, uint64_t> last_event_time_;

    std::mutex queue_mutex_;
    std::vector<FileChangeEvent> pending_;

    std::string operators_dir_;
};

} // namespace vivid
