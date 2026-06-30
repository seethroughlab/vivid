#include "packages/hot_reload.h"

#include <utility>

namespace vivid {

HotReloader::~HotReloader() { stop(); }

void HotReloader::start(CompileFn compile) {
    if (running_) return;
    compile_ = std::move(compile);
    running_ = true;
    thread_ = std::thread([this] { thread_main(); });
}

void HotReloader::stop() {
    if (!running_) return;
    running_ = false;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void HotReloader::queue_rebuild(const std::string& target) {
    {
        std::lock_guard<std::mutex> lk(qmtx_);
        if (!queued_.insert(target).second) return;   // already pending — coalesce
        queue_.push_back(target);
    }
    cv_.notify_one();
}

std::vector<ReloadResult> HotReloader::poll_ready() {
    std::vector<ReloadResult> out;
    std::lock_guard<std::mutex> lk(rmtx_);
    out.swap(results_);
    return out;
}

void HotReloader::thread_main() {
    for (;;) {
        std::string target;
        {
            std::unique_lock<std::mutex> lk(qmtx_);
            cv_.wait(lk, [this] { return !queue_.empty() || !running_; });
            if (!running_ && queue_.empty()) return;
            target = std::move(queue_.front());
            queue_.pop_front();
            queued_.erase(target);   // a new edit during compile can re-queue it
        }
        ReloadResult r = compile_ ? compile_(target)
                                  : ReloadResult{ target, "", "no compiler set", false };
        std::lock_guard<std::mutex> lk(rmtx_);
        results_.push_back(std::move(r));
    }
}

}  // namespace vivid
