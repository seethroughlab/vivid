#include "runtime/file_watcher.h"
#include <filesystem>
#include <sys/event.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <chrono>

namespace vivid {

static constexpr int kWatchTimeoutMs = 200;
static constexpr int64_t kDebounceMs = 100;
static constexpr int kReopenRetryMs = 50;

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::start(const std::string& operators_dir) {
    if (thread_.joinable()) return false;

    operators_dir_ = operators_dir;

    kq_ = kqueue();
    if (kq_ < 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: kqueue() failed\n");
        return false;
    }

    // Recursively find .cpp files under operators_dir and register them.
    // Directory structure: operators/<category>/<name>/<name>.cpp → target = <name>
    std::vector<std::string> categories;
    DIR* top = opendir(operators_dir.c_str());
    if (!top) {
        std::fprintf(stderr, "[vivid] FileWatcher: cannot open %s\n", operators_dir.c_str());
        close(kq_);
        kq_ = -1;
        return false;
    }
    struct dirent* entry;
    while ((entry = readdir(top)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type == DT_DIR)
            categories.push_back(entry->d_name);
    }
    closedir(top);

    int count = 0;
    for (const auto& category : categories) {
        std::string category_path = operators_dir + "/" + category;
        DIR* ddir = opendir(category_path.c_str());
        if (!ddir) continue;
        while ((entry = readdir(ddir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            if (entry->d_type != DT_DIR) continue;

            std::string target_name = entry->d_name;
            std::string op_dir = category_path + "/" + target_name;

            // Watch all .cpp files in this operator directory
            DIR* odir = opendir(op_dir.c_str());
            if (!odir) continue;
            struct dirent* fentry;
            while ((fentry = readdir(odir)) != nullptr) {
                size_t len = std::strlen(fentry->d_name);
                if (len < 5 || std::strcmp(fentry->d_name + len - 4, ".cpp") != 0)
                    continue;
                std::string file_path = op_dir + "/" + fentry->d_name;
                if (add_watch(file_path, target_name))
                    count++;
            }
            closedir(odir);
        }
        closedir(ddir);
    }

    if (count == 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: no .cpp files found under %s\n", operators_dir.c_str());
        close(kq_);
        kq_ = -1;
        return false;
    }

    std::fprintf(stderr, "[vivid] FileWatcher: watching %d files\n", count);
    running_ = true;
    thread_ = std::thread(&FileWatcher::watch_thread, this);
    return true;
}

void FileWatcher::stop() {
    running_ = false;
    if (thread_.joinable()) {
        // Wake up kevent by closing the kqueue fd
        if (kq_ >= 0) {
            close(kq_);
            kq_ = -1;
        }
        thread_.join();
    }

    // Close remaining fds
    for (auto& [fd, entry] : watched_fds_) {
        close(fd);
    }
    watched_fds_.clear();
}

bool FileWatcher::add_watch(const std::string& path, const std::string& target_name) {
    {
        std::lock_guard<std::mutex> lock(watch_mutex_);
        for (const auto& [fd, entry] : watched_fds_) {
            (void)fd;
            if (entry.path == path && entry.target_name == target_name) {
                return true;
            }
        }
    }

    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: cannot open %s\n", path.c_str());
        return false;
    }

    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_VNODE,
           EV_ADD | EV_CLEAR,
           NOTE_WRITE | NOTE_RENAME | NOTE_DELETE,
           0, nullptr);

    if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) < 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: kevent register failed for %s\n", path.c_str());
        close(fd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(watch_mutex_);
        watched_fds_[fd] = {path, target_name};
    }
    return true;
}

void FileWatcher::reopen_file(const std::string& path, const std::string& target_name) {
    // Called when a file is renamed/deleted (editor save pattern).
    // Try to reopen at the same path and re-register.
    // Retry a few times with small delays — the editor may not have finished writing yet.
    for (int attempt = 0; attempt < 5; ++attempt) {
        int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0) {
            struct kevent ev;
            EV_SET(&ev, fd, EVFILT_VNODE,
                   EV_ADD | EV_CLEAR,
                   NOTE_WRITE | NOTE_RENAME | NOTE_DELETE,
                   0, nullptr);
            if (kevent(kq_, &ev, 1, nullptr, 0, nullptr) >= 0) {
                std::lock_guard<std::mutex> lock(watch_mutex_);
                watched_fds_[fd] = {path, target_name};
                return;
            }
            close(fd);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kReopenRetryMs));
    }
    std::fprintf(stderr, "[vivid] FileWatcher: failed to reopen %s\n", path.c_str());
}

void FileWatcher::watch_thread() {
    while (running_) {
        struct kevent ev;
        struct timespec timeout = {0, kWatchTimeoutMs * 1000000};  // shutdown check interval
        int nev = kevent(kq_, nullptr, 0, &ev, 1, &timeout);
        if (nev < 0) break;  // kqueue fd closed or error
        if (nev == 0) continue;  // timeout

        int fd = static_cast<int>(ev.ident);

        // Copy strings under lock so we can release before reopen_file()
        std::string path, target;
        bool need_reopen = false;
        {
            std::lock_guard<std::mutex> lock(watch_mutex_);
            auto it = watched_fds_.find(fd);
            if (it == watched_fds_.end()) continue;

            path = it->second.path;
            target = it->second.target_name;

            // Debounce: ignore events within 100ms of last event for same target
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto& last = last_event_time_[target];
            if (now - last < kDebounceMs) {
                if (ev.fflags & (NOTE_RENAME | NOTE_DELETE)) {
                    close(fd);
                    watched_fds_.erase(it);
                    need_reopen = true;
                }
                if (!need_reopen) continue;
            } else {
                last = now;

                std::fprintf(stderr, "[vivid] File changed: %s (operator: %s)\n",
                    path.c_str(), target.c_str());

                {
                    std::lock_guard<std::mutex> qlock(queue_mutex_);
                    pending_.push_back({path, target});
                }

                if (ev.fflags & (NOTE_RENAME | NOTE_DELETE)) {
                    close(fd);
                    watched_fds_.erase(it);
                    need_reopen = true;
                }
            }
        }

        if (need_reopen) {
            // Note: reopen_file sleeps up to 5×50ms — blocks watch_thread during rename retries.
            reopen_file(path, target);
        }
    }
}

std::vector<FileChangeEvent> FileWatcher::poll_changes() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    std::vector<FileChangeEvent> result;
    result.swap(pending_);
    return result;
}

int FileWatcher::add_package_watches(const std::string& packages_dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(packages_dir)) return 0;

    int count = 0;
    // Walk <config_dir>/packages/*/operators/<category>/<name>/*.cpp
    std::error_code ec;
    for (auto& pkg_entry : fs::directory_iterator(packages_dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!pkg_entry.is_directory()) continue;

        std::string ops_dir = pkg_entry.path().string() + "/operators";
        if (!fs::exists(ops_dir)) continue;

        // Use package-prefixed target name: "pkg:<package_name>:<operator_name>"
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

                std::error_code ec4;
                for (auto& file_entry : fs::directory_iterator(op_entry.path(), ec4)) {
                    if (ec4) { ec4.clear(); continue; }
                    if (!file_entry.is_regular_file()) continue;
                    std::string fname = file_entry.path().filename().string();
                    size_t len = fname.size();
                    if (len < 5 || fname.substr(len - 4) != ".cpp") continue;

                    if (add_watch(file_entry.path().string(), target))
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

} // namespace vivid
