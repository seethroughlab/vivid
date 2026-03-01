#pragma once

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace vivid {

struct ReloadResult {
    std::string target_name;
    std::string staged_dylib_path;  // empty on failure
    std::string error_output;       // compiler errors on failure
    bool success;
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

    // Completed results (compile thread → main)
    std::mutex result_mutex_;
    std::vector<ReloadResult> results_;

    // Per-target reload counter for unique staging paths
    std::mutex counter_mutex_;
    std::unordered_map<std::string, uint32_t> reload_counters_;

    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace vivid
