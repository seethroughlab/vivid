#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstdint>

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

    enum class WatchRootKind { SeedOperators, PackageOperators, ShaderDirectory };
    struct WatchRoot {
        WatchRootKind kind;
        std::string root;
        std::string package_name;
    };

    // State touched by the efsw callback thread, held via shared_ptr by both the
    // FileWatcher and its Listener. efsw's macOS FSEvents backend routes
    // callbacks through a serial dispatch queue that it does NOT drain on
    // teardown, so one in-flight callback can fire AFTER stop(); keeping this
    // alive (with active == false) makes that late callback a safe no-op instead
    // of a use-after-free. See stop().
    struct SharedState {
        std::mutex watch_mutex;
        // path → target_name mapping for event filtering. Protected by watch_mutex.
        std::unordered_map<std::string, std::string> path_to_target;
        std::vector<WatchRoot> watch_roots;
        // Debounce: target → last event time (steady_clock ms). watch_mutex.
        std::unordered_map<std::string, uint64_t> last_event_time;

        std::mutex queue_mutex;
        std::vector<FileChangeEvent> pending;

        // Cleared by stop() once efsw is torn down; a late callback checks this
        // (no lock) and returns before touching any shared map.
        std::atomic<bool> active{true};

        std::string resolve_target_locked(const std::string& path) const;
    };

    void ensure_dir_watched(const std::string& dir, bool recursive);

    std::unique_ptr<efsw::FileWatcher> watcher_;
    std::shared_ptr<Listener> listener_;   // efsw holds listener_.get()
    std::shared_ptr<SharedState> state_;

    // Directories already registered with efsw (main-thread only).
    std::unordered_map<std::string, bool> watched_dirs_;
    std::string operators_dir_;
};

} // namespace vivid
