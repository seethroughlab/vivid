#include "decoded_frame_queue.h"

bool DecodedFrameQueue::push(DecodedFrame&& frame) {
    std::lock_guard<std::mutex> lock(mu_);
    if (frames_.size() >= kMaxFrames) {
        // Drop the oldest frame to make room.
        frames_.erase(frames_.begin());
    }
    frames_.push_back(std::move(frame));
    return true;
}

bool DecodedFrameQueue::pop_latest(DecodedFrame& out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (frames_.empty()) return false;
    out = std::move(frames_.back());
    frames_.clear();
    return true;
}

void DecodedFrameQueue::flush() {
    std::lock_guard<std::mutex> lock(mu_);
    frames_.clear();
}

size_t DecodedFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return frames_.size();
}
