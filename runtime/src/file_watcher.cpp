#include "file_watcher.h"
#include <iostream>
#include <algorithm>

namespace vivid {

// Listener implementation that inherits from efsw::FileWatchListener
class FileWatcher::Listener : public efsw::FileWatchListener {
public:
    Listener(FileWatcher& owner) : owner_(owner) {}

    void handleFileAction(efsw::WatchID watchid, const std::string& dir,
                          const std::string& filename, efsw::Action action,
                          std::string oldFilename) override {
        // Only handle modifications and additions (not deletions for now)
        if (action != efsw::Actions::Modified && action != efsw::Actions::Add) {
            return;
        }

        // Filter for relevant file types
        std::string ext = filename.substr(filename.find_last_of('.') + 1);
        if (ext != "cpp" && ext != "h" && ext != "hpp" && ext != "wgsl") {
            return;
        }

        // Build full path
        std::string fullPath = dir;
        if (!fullPath.empty() && fullPath.back() != '/') {
            fullPath += '/';
        }
        fullPath += filename;

        // Queue the change (thread-safe)
        {
            std::lock_guard<std::mutex> lock(owner_.queueMutex_);
            // Avoid duplicates
            if (std::find(owner_.pendingChanges_.begin(), owner_.pendingChanges_.end(), fullPath)
                == owner_.pendingChanges_.end()) {
                owner_.pendingChanges_.push_back(fullPath);
            }
        }
    }

private:
    FileWatcher& owner_;
};

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::watch(const std::string& directory, FileChangeCallback callback) {
    stop();

    callback_ = callback;
    watchDirectory_ = directory;

    // Create efsw watcher
    watcher_ = std::make_unique<efsw::FileWatcher>();
    listener_ = std::make_unique<Listener>(*this);

    // Add watch (recursive)
    efsw::WatchID watchId = watcher_->addWatch(directory, listener_.get(), true);
    if (watchId < 0) {
        std::cerr << "[FileWatcher] Failed to watch " << directory
                  << ": " << efsw::Errors::Log::getLastErrorLog() << "\n";
        watcher_.reset();
        listener_.reset();
        return;
    }

    // Start watching in background thread
    watcher_->watch();

    std::cout << "[FileWatcher] Watching " << directory << " (recursive)\n";
}

void FileWatcher::stop() {
    if (watcher_) {
        watcher_->removeWatch(watchDirectory_);
        watcher_.reset();
        listener_.reset();
        std::cout << "[FileWatcher] Stopped watching\n";
    }

    std::lock_guard<std::mutex> lock(queueMutex_);
    pendingChanges_.clear();
}

void FileWatcher::poll() {
    std::vector<std::string> changes;

    // Grab pending changes (thread-safe)
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        changes.swap(pendingChanges_);
    }

    // Fire callbacks
    for (const auto& path : changes) {
        if (callback_) {
            callback_(path);
        }
    }
}

} // namespace vivid
