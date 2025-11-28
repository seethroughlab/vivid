#pragma once
#include <efsw/efsw.hpp>
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <mutex>

namespace vivid {

// Callback type for file changes
using FileChangeCallback = std::function<void(const std::string& filePath)>;

class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();

    // Start watching a directory (recursive)
    void watch(const std::string& directory, FileChangeCallback callback);

    // Stop watching
    void stop();

    // Process pending file change events (call in main loop)
    void poll();

    // Check if watching
    bool isWatching() const { return watcher_ != nullptr; }

private:
    class Listener;

    std::unique_ptr<efsw::FileWatcher> watcher_;
    std::unique_ptr<Listener> listener_;
    FileChangeCallback callback_;
    std::string watchDirectory_;

    // Thread-safe queue for file changes
    std::mutex queueMutex_;
    std::vector<std::string> pendingChanges_;
};

} // namespace vivid
