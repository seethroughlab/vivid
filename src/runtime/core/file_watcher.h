#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <memory>

namespace efsw { class FileWatcher; }

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

    // Register a single file for watching. Thread-safe.
    bool add_watch(const std::string& path, const std::string& target_name);

    // Scan package operator directories and register them for watching.
    // Returns the number of files registered.
    int add_package_watches(const std::string& packages_dir);

    // Scan a filter directory and register .wgsl shader operators for watching.
    // Returns the number of files registered.
    int add_shader_operator_watches(const std::string& directory);

private:
    class Listener;

    void ensure_dir_watched(const std::string& dir);

    std::unique_ptr<efsw::FileWatcher> watcher_;
    std::unique_ptr<Listener> listener_;

    // path → target_name mapping for event filtering.  Protected by watch_mutex_.
    std::mutex watch_mutex_;
    std::unordered_map<std::string, std::string> path_to_target_;
    std::unordered_set<std::string> watched_dirs_;

    // Debounce: target → last event time (steady_clock ms)
    std::unordered_map<std::string, uint64_t> last_event_time_;

    std::mutex queue_mutex_;
    std::vector<FileChangeEvent> pending_;

    std::string operators_dir_;
};

} // namespace vivid
