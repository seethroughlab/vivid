#include "runtime/file_watcher.h"
#include <sys/event.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <chrono>

namespace vivid {

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::start(const std::string& operators_dir) {
    operators_dir_ = operators_dir;

    kq_ = kqueue();
    if (kq_ < 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: kqueue() failed\n");
        return false;
    }

    // Recursively find .cpp files under operators_dir and register them.
    // Directory structure: operators/<domain>/<name>/<name>.cpp → target = <name>
    std::vector<std::string> domains;
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
            domains.push_back(entry->d_name);
    }
    closedir(top);

    int count = 0;
    for (const auto& domain : domains) {
        std::string domain_path = operators_dir + "/" + domain;
        DIR* ddir = opendir(domain_path.c_str());
        if (!ddir) continue;
        while ((entry = readdir(ddir)) != nullptr) {
            if (entry->d_name[0] == '.') continue;
            if (entry->d_type != DT_DIR) continue;

            std::string target_name = entry->d_name;
            std::string op_dir = domain_path + "/" + target_name;

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

    watched_fds_[fd] = {path, target_name};
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
                watched_fds_[fd] = {path, target_name};
                return;
            }
            close(fd);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::fprintf(stderr, "[vivid] FileWatcher: failed to reopen %s\n", path.c_str());
}

void FileWatcher::watch_thread() {
    while (running_) {
        struct kevent ev;
        struct timespec timeout = {0, 200000000};  // 200ms timeout for shutdown check
        int nev = kevent(kq_, nullptr, 0, &ev, 1, &timeout);
        if (nev < 0) break;  // kqueue fd closed or error
        if (nev == 0) continue;  // timeout

        int fd = static_cast<int>(ev.ident);
        auto it = watched_fds_.find(fd);
        if (it == watched_fds_.end()) continue;

        const auto& entry = it->second;

        // Debounce: ignore events within 100ms of last event for same target
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto& last = last_event_time_[entry.target_name];
        if (now - last < 100) {
            // Handle rename/delete even when debounced, to keep watch alive
            if (ev.fflags & (NOTE_RENAME | NOTE_DELETE)) {
                close(fd);
                watched_fds_.erase(it);
                reopen_file(entry.path, entry.target_name);
            }
            continue;
        }
        last = now;

        std::fprintf(stderr, "[vivid] File changed: %s (operator: %s)\n",
            entry.path.c_str(), entry.target_name.c_str());

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending_.push_back({entry.path, entry.target_name});
        }

        // Handle editor rename-on-save: file was renamed/deleted, reopen at same path
        if (ev.fflags & (NOTE_RENAME | NOTE_DELETE)) {
            std::string path = entry.path;
            std::string target = entry.target_name;
            close(fd);
            watched_fds_.erase(it);
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

} // namespace vivid
