#pragma once

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace vivid {

struct ReloadResult {
    std::string target_name;
    std::string staged_dylib_path;  // empty on failure
    std::string error_output;       // compiler errors on failure
    bool success = false;
};

class HotReloader {
public:
    HotReloader();
    ~HotReloader();

    // build_dir: where cmake was invoked (for cmake --build and staging)
    // Returns false if already running or staging dir creation fails.
    bool start(const std::string& build_dir);
    void stop();

    // Queue a rebuild for a cmake target (called from main thread)
    void queue_rebuild(const std::string& target_name);

    // Register a package target → compile function mapping.
    // When a target matching "pkg:<name>:<op>" is queued, the callback is invoked
    // instead of cmake --build.
    using PackageCompileFn = std::function<ReloadResult(const std::string& target_name)>;
    void set_package_compiler(PackageCompileFn fn);

    // Poll for completed rebuild results (called from main thread each frame)
    std::vector<ReloadResult> poll_ready();

private:
    void compile_thread();

    std::string build_dir_;
    std::string staging_dir_;

    // Build request queue (main → compile thread)
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::string> build_queue_;
    std::unordered_set<std::string> queued_targets_;
    std::unordered_set<std::string> in_flight_targets_;
    std::unordered_set<std::string> deferred_targets_;

    // Completed results (compile thread → main)
    std::mutex result_mutex_;
    std::vector<ReloadResult> results_;

    // Per-target reload counter for unique staging paths
    std::mutex counter_mutex_;
    std::unordered_map<std::string, uint32_t> reload_counters_;

    PackageCompileFn package_compile_fn_;

    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace vivid
