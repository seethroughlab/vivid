#include "runtime/core/file_watcher.h"
#include <efsw/efsw.hpp>
#include <filesystem>
#include <cstdio>
#include <chrono>

namespace vivid {

static constexpr int64_t kDebounceMs = 100;

// --- Listener (efsw callback → pending queue) ---

class FileWatcher::Listener : public efsw::FileWatchListener {
public:
    explicit Listener(FileWatcher& owner) : owner_(owner) {}

    void handleFileAction(efsw::WatchID, const std::string& dir,
                          const std::string& filename, efsw::Action action,
                          std::string) override {
        if (action != efsw::Actions::Modified &&
            action != efsw::Actions::Add &&
            action != efsw::Actions::Moved)
            return;

        std::string path = dir;
        if (!path.empty() && path.back() != '/')
            path += '/';
        path += filename;

        // Normalize to canonical if possible
        std::error_code ec;
        auto canonical = std::filesystem::canonical(path, ec);
        if (!ec) path = canonical.string();

        std::string target;
        {
            std::lock_guard<std::mutex> lock(owner_.watch_mutex_);
            auto it = owner_.path_to_target_.find(path);
            if (it == owner_.path_to_target_.end()) return;
            target = it->second;

            // Debounce: skip if within 100ms of last event for same target
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto& last = owner_.last_event_time_[target];
            if (now - last < kDebounceMs) return;
            last = now;
        }

        std::fprintf(stderr, "[vivid] File changed: %s (operator: %s)\n",
                     path.c_str(), target.c_str());

        {
            std::lock_guard<std::mutex> lock(owner_.queue_mutex_);
            owner_.pending_.push_back({path, target});
        }
    }

private:
    FileWatcher& owner_;
};

// --- FileWatcher ---

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::ensure_dir_watched(const std::string& dir) {
    if (!watcher_ || watched_dirs_.count(dir)) return;
    watched_dirs_.insert(dir);
    watcher_->addWatch(dir, listener_.get(), false);
}

bool FileWatcher::start(const std::string& operators_dir) {
    if (watcher_) return false;

    operators_dir_ = operators_dir;
    listener_ = std::make_unique<Listener>(*this);
    watcher_ = std::make_unique<efsw::FileWatcher>();
    watcher_->followSymlinks(true);

    namespace fs = std::filesystem;
    if (!fs::exists(operators_dir)) {
        std::fprintf(stderr, "[vivid] FileWatcher: cannot open %s\n", operators_dir.c_str());
        watcher_.reset();
        listener_.reset();
        return false;
    }

    int count = 0;
    std::error_code ec;
    for (auto& domain_entry : fs::directory_iterator(operators_dir, ec)) {
        if (ec || !domain_entry.is_directory()) { ec.clear(); continue; }

        for (auto& op_entry : fs::directory_iterator(domain_entry.path(), ec)) {
            if (ec || !op_entry.is_directory()) { ec.clear(); continue; }

            std::string target_name = op_entry.path().filename().string();
            std::string op_dir = op_entry.path().string();

            for (auto& file_entry : fs::directory_iterator(op_entry.path(), ec)) {
                if (ec) { ec.clear(); continue; }
                if (!file_entry.is_regular_file()) continue;
                if (file_entry.path().extension() != ".cpp") continue;

                std::error_code canon_ec;
                std::string abs = fs::canonical(file_entry.path(), canon_ec).string();
                if (canon_ec) abs = file_entry.path().string();

                std::lock_guard<std::mutex> lock(watch_mutex_);
                path_to_target_[abs] = target_name;
                ensure_dir_watched(op_dir);
                count++;
            }
        }
    }

    if (count == 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: no .cpp files found under %s\n",
                     operators_dir.c_str());
        watcher_.reset();
        listener_.reset();
        return false;
    }

    watcher_->watch();
    std::fprintf(stderr, "[vivid] FileWatcher: watching %d files\n", count);
    return true;
}

void FileWatcher::stop() {
    watcher_.reset();
    listener_.reset();
    std::lock_guard<std::mutex> lock(watch_mutex_);
    path_to_target_.clear();
    watched_dirs_.clear();
    last_event_time_.clear();
}

std::vector<FileChangeEvent> FileWatcher::poll_changes() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::vector<FileChangeEvent> result;
    result.swap(pending_);
    return result;
}

bool FileWatcher::add_watch(const std::string& path, const std::string& target_name) {
    namespace fs = std::filesystem;
    if (!fs::exists(path)) return false;

    std::error_code ec;
    std::string abs = fs::canonical(path, ec).string();
    if (ec) abs = path;

    std::string dir = fs::path(abs).parent_path().string();

    std::lock_guard<std::mutex> lock(watch_mutex_);
    path_to_target_[abs] = target_name;
    if (watcher_) ensure_dir_watched(dir);
    return true;
}

int FileWatcher::add_package_watches(const std::string& packages_dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(packages_dir)) return 0;

    int count = 0;
    std::error_code ec;
    for (auto& pkg_entry : fs::directory_iterator(packages_dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!pkg_entry.is_directory()) continue;

        std::string ops_dir = pkg_entry.path().string() + "/operators";
        if (!fs::exists(ops_dir)) continue;

        std::string pkg_name = pkg_entry.path().filename().string();

        std::error_code ec2;
        for (auto& category_entry : fs::directory_iterator(ops_dir, ec2)) {
            if (ec2) { ec2.clear(); continue; }
            if (!category_entry.is_directory()) continue;

            std::error_code ec3;
            for (auto& op_entry : fs::directory_iterator(category_entry.path(), ec3)) {
                if (ec3) { ec3.clear(); continue; }
                if (!op_entry.is_directory()) continue;

                std::string op_name = op_entry.path().filename().string();
                std::string target = "pkg:" + pkg_name + ":" + op_name;
                std::string op_dir_str = op_entry.path().string();

                std::error_code ec4;
                for (auto& file_entry : fs::directory_iterator(op_entry.path(), ec4)) {
                    if (ec4) { ec4.clear(); continue; }
                    if (!file_entry.is_regular_file()) continue;
                    std::string fname = file_entry.path().filename().string();
                    if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".cpp") continue;

                    std::error_code canon_ec;
                    std::string abs = fs::canonical(file_entry.path(), canon_ec).string();
                    if (canon_ec) abs = file_entry.path().string();

                    std::lock_guard<std::mutex> lock(watch_mutex_);
                    path_to_target_[abs] = target;
                    ensure_dir_watched(op_dir_str);
                    count++;
                }
            }
        }
    }

    if (count > 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: watching %d package files\n", count);
    }
    return count;
}

int FileWatcher::add_shader_operator_watches(const std::string& directory) {
    namespace fs = std::filesystem;
    if (directory.empty() || !fs::exists(directory)) return 0;

    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".wgsl") continue;

        std::error_code canon_ec;
        std::string abs = fs::canonical(entry.path(), canon_ec).string();
        if (canon_ec) abs = entry.path().string();

        std::string target = "shader:" + abs;

        std::lock_guard<std::mutex> lock(watch_mutex_);
        path_to_target_[abs] = target;
        ensure_dir_watched(directory);
        count++;
    }

    if (count > 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: watching %d shader files in %s\n",
                     count, directory.c_str());
    }
    return count;
}

} // namespace vivid
