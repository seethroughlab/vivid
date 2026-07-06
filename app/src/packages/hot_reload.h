#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

// Recompiles operators off the main thread so an edit never stalls rendering.
// queue_rebuild(target) (main thread) → the compile thread runs the registered
// CompileFn (the package compiler, producing a staged .dylib) → poll_ready() (main
// thread, each frame) returns finished results for the main loop to hot-swap.
// Repeated edits while a target is pending coalesce into one rebuild.
namespace vivid {

struct ReloadResult {
    std::string target;
    std::string dylib_path;   // staged dylib on success
    std::string error;        // compiler output on failure
    bool        success = false;
};

class HotReloader {
public:
    using CompileFn = std::function<ReloadResult(const std::string& target)>;

    ~HotReloader();

    void start(CompileFn compile);             // spawn the compile thread
    void stop();                               // join
    void queue_rebuild(const std::string& target);
    std::vector<ReloadResult> poll_ready();    // drain finished results (main thread)

private:
    void thread_main();

    CompileFn               compile_;
    std::mutex              qmtx_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::unordered_set<std::string> queued_;   // coalesce duplicates while pending
    std::mutex              rmtx_;
    std::vector<ReloadResult> results_;
    std::thread             thread_;
    std::atomic<bool>       running_{false};
};

}  // namespace vivid
