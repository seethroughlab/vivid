#pragma once

#include <string>
#include <vector>

// Dependency-free filesystem watcher: polls watched files' modification times and
// reports the "target" labels whose source changed since the last poll. Driven once
// per frame on the main thread. Right-sized vs vivid-classic's efsw-based watcher —
// adequate for a handful of operator sources, with no new dependency.
namespace vivid {

class FileWatcher {
public:
    // (Re)register a file path under a target label (e.g. an operator name). Records
    // the current mtime as the baseline (so a watch added now isn't reported stale).
    void watch(const std::string& path, const std::string& target);
    void clear();

    // Targets whose watched file changed since the last call (mtime advanced).
    std::vector<std::string> poll_changes();

private:
    struct Entry { std::string path; std::string target; long long mtime = 0; };
    std::vector<Entry> entries_;
};

}  // namespace vivid
