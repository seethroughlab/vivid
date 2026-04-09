#include "video_decode_worker.h"

VideoDecodeWorker::~VideoDecodeWorker() {
    stop();
}

void VideoDecodeWorker::start() {
    if (started_) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = false;
        has_pending_ = false;
        pending_work_ = nullptr;
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
        pending_work_ = nullptr;
        has_pending_ = false;
        cv_.notify_one();
    }
    if (thread_.joinable()) thread_.join();
    started_ = false;
    ready_queue_.flush();
}

void VideoDecodeWorker::submit_work(WorkFunction&& work) {
    std::lock_guard<std::mutex> lock(mu_);
    // Replace any unprocessed work item (newest wins).
    pending_work_ = std::move(work);
    has_pending_ = true;
    cv_.notify_one();
}

void VideoDecodeWorker::submit_decoded(DecodedFrame&& frame) {
    ready_queue_.push(std::move(frame));
}

bool VideoDecodeWorker::pop_latest(DecodedFrame& out) {
    return ready_queue_.pop_latest(out);
}

void VideoDecodeWorker::flush() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        pending_work_ = nullptr;
        has_pending_ = false;
    }
    ready_queue_.flush();
}

void VideoDecodeWorker::worker_loop() {
    for (;;) {
        WorkFunction work;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this]() { return stop_ || has_pending_; });
            if (stop_ && !has_pending_) break;
            work = std::move(pending_work_);
            pending_work_ = nullptr;
            has_pending_ = false;
        }
        if (work) {
            DecodedFrame frame = work();
            if (!frame.empty()) {
                ready_queue_.push(std::move(frame));
            }
        }
    }
}
