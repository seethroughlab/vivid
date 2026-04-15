#include "video_decode_worker.h"

#include <algorithm>

VideoDecodeWorker::~VideoDecodeWorker() {
    stop();
}

void VideoDecodeWorker::start() {
    if (started_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = false;
    }
    started_ = true;
    thread_ = std::thread([this]() { worker_loop(); });
}

void VideoDecodeWorker::stop() {
    if (!started_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
        // Release any pending work to avoid leaking captured resources
        // (e.g. retained CVPixelBufferRef in the lambda).
        pending_work_.clear();
        pending_keys_.clear();
        cv_.notify_one();
    }
    if (thread_.joinable()) thread_.join();
    started_ = false;
    ready_queue_.flush();
}

bool VideoDecodeWorker::submit_work(WorkFunction&& work,
                                    uint64_t loop_generation,
                                    uint64_t request_key) {
    if (!work) return false;
    std::lock_guard<std::mutex> lock(mu_);
    if (request_key != 0 && pending_keys_.find(request_key) != pending_keys_.end()) {
        return false;
    }

    if (pending_work_.size() >= kMaxPendingWork) {
        auto drop_it = std::find_if(pending_work_.begin(), pending_work_.end(),
            [&](const WorkItem& item) {
                return item.loop_generation < loop_generation;
            });
        if (drop_it == pending_work_.end()) {
            drop_it = pending_work_.begin();
        }
        if (drop_it->request_key != 0) pending_keys_.erase(drop_it->request_key);
        pending_work_.erase(drop_it);
    }

    WorkItem item;
    item.work = std::move(work);
    item.generation = generation_.load(std::memory_order_acquire);
    item.loop_generation = loop_generation;
    item.request_key = request_key;
    if (request_key != 0) pending_keys_.insert(request_key);
    pending_work_.push_back(std::move(item));
    cv_.notify_one();
    return true;
}

bool VideoDecodeWorker::submit_decoded(DecodedFrame&& frame) {
    return ready_queue_.push(std::move(frame));
}

bool VideoDecodeWorker::pop_latest(DecodedFrame& out) {
    return ready_queue_.pop_latest(out);
}

bool VideoDecodeWorker::pop_best(double target_pts,
                                 uint64_t loop_generation,
                                 double frame_duration,
                                 DecodedFrame& out) {
    return ready_queue_.pop_best(target_pts, loop_generation, frame_duration, out);
}

bool VideoDecodeWorker::has_ready_generation(uint64_t loop_generation) const {
    return ready_queue_.has_generation(loop_generation);
}

void VideoDecodeWorker::flush() {
    generation_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(mu_);
        pending_work_.clear();
        pending_keys_.clear();
    }
    ready_queue_.flush();
}

VideoDecodeWorker::Generation VideoDecodeWorker::generation() const {
    return generation_.load(std::memory_order_acquire);
}

void VideoDecodeWorker::worker_loop() {
    for (;;) {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this]() { return stop_ || !pending_work_.empty(); });
            if (stop_ && pending_work_.empty()) break;
            item = std::move(pending_work_.front());
            pending_work_.pop_front();
            if (item.request_key != 0) pending_keys_.erase(item.request_key);
        }
        if (item.work) {
            DecodedFrame frame = item.work();
            if (!frame.empty() &&
                item.generation == generation_.load(std::memory_order_acquire)) {
                ready_queue_.push(std::move(frame));
            }
        }
    }
}
