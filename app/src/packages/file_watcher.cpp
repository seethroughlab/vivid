#include "packages/file_watcher.h"

#include <filesystem>

namespace vivid {

namespace {
long long mtime_of(const std::string& path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return static_cast<long long>(t.time_since_epoch().count());
}
}  // namespace

void FileWatcher::watch(const std::string& path, const std::string& target) {
    for (auto& e : entries_)
        if (e.path == path) { e.target = target; e.mtime = mtime_of(path); return; }
    entries_.push_back({ path, target, mtime_of(path) });
}

void FileWatcher::clear() { entries_.clear(); }

std::vector<std::string> FileWatcher::poll_changes() {
    std::vector<std::string> changed;
    for (auto& e : entries_) {
        const long long m = mtime_of(e.path);
        if (m != 0 && m != e.mtime) { e.mtime = m; changed.push_back(e.target); }
    }
    return changed;
}

}  // namespace vivid
