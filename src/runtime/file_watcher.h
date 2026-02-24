#ifndef VIVID_RUNTIME_FILE_WATCHER_H
#define VIVID_RUNTIME_FILE_WATCHER_H

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

private:
    void watch_thread();
    bool add_watch(const std::string& path, const std::string& target_name);
    void reopen_file(const std::string& path, const std::string& target_name);

    int kq_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // fd → (file_path, target_name)
    struct WatchEntry {
        std::string path;
        std::string target_name;
    };
    std::unordered_map<int, WatchEntry> watched_fds_;

    // Debounce: target → last event time (steady_clock ms)
    std::unordered_map<std::string, uint64_t> last_event_time_;

    std::mutex queue_mutex_;
    std::vector<FileChangeEvent> pending_;

    std::string operators_dir_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_FILE_WATCHER_H
