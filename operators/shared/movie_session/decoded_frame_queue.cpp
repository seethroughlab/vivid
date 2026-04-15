#include "decoded_frame_queue.h"

#include <algorithm>
#include <cmath>

namespace {

bool should_replace_duplicate(const DecodedFrame& existing,
                              const DecodedFrame& incoming) {
    const bool incoming_native = incoming.has_native_pixel_buffer();
    const bool existing_native = existing.has_native_pixel_buffer();

    if (incoming_native != existing_native) return incoming_native;
    if (incoming.cpu_fallback != existing.cpu_fallback) return !incoming.cpu_fallback;
    return false;
}

} // namespace

bool DecodedFrameQueue::push(DecodedFrame&& frame) {
    std::lock_guard<std::mutex> lock(mu_);
    if (frame.request_key != 0) {
        auto duplicate = std::find_if(frames_.begin(), frames_.end(),
            [&](const DecodedFrame& queued) {
                return queued.request_key == frame.request_key;
            });
        if (duplicate != frames_.end()) {
            if (should_replace_duplicate(*duplicate, frame)) {
                *duplicate = std::move(frame);
                return true;
            }
            return false;
        }
    }

    if (frames_.size() >= kMaxFrames) {
        auto drop_it = std::find_if(frames_.begin(), frames_.end(),
            [&](const DecodedFrame& queued) {
                return queued.loop_generation < frame.loop_generation;
            });
        if (drop_it == frames_.end()) {
            drop_it = frames_.begin();
        }
        frames_.erase(drop_it);
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

bool DecodedFrameQueue::pop_best(double target_pts,
                                 uint64_t loop_generation,
                                 double frame_duration,
                                 DecodedFrame& out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (frames_.empty()) return false;

    const double fd = std::max(1.0 / 240.0, frame_duration);
    const double stale_cutoff = target_pts - (2.0 * fd);
    frames_.erase(std::remove_if(frames_.begin(), frames_.end(),
        [&](const DecodedFrame& frame) {
            if (frame.loop_generation < loop_generation) return true;
            if (frame.loop_generation > loop_generation) return false;
            return frame.pts < stale_cutoff;
        }), frames_.end());

    if (frames_.empty()) return false;

    const double half_frame = 0.5 * fd;
    const double future_limit = fd;
    int best_idx = -1;
    double best_pts = -1.0;
    uint64_t best_sequence = 0;

    for (size_t i = 0; i < frames_.size(); ++i) {
        const auto& frame = frames_[i];
        if (frame.loop_generation != loop_generation) continue;
        if (frame.pts <= target_pts + half_frame) {
            if (best_idx < 0 ||
                frame.pts > best_pts ||
                (std::abs(frame.pts - best_pts) <= 1e-9 &&
                 frame.request_sequence > best_sequence)) {
                best_idx = static_cast<int>(i);
                best_pts = frame.pts;
                best_sequence = frame.request_sequence;
            }
        }
    }

    if (best_idx < 0) {
        double nearest_future = 0.0;
        for (size_t i = 0; i < frames_.size(); ++i) {
            const auto& frame = frames_[i];
            if (frame.loop_generation != loop_generation) continue;
            const double delta = frame.pts - target_pts;
            if (delta < 0.0 || delta > future_limit) continue;
            if (best_idx < 0 ||
                delta < nearest_future ||
                (std::abs(delta - nearest_future) <= 1e-9 &&
                 frame.request_sequence > best_sequence)) {
                best_idx = static_cast<int>(i);
                nearest_future = delta;
                best_sequence = frame.request_sequence;
            }
        }
    }

    if (best_idx < 0) return false;
    out = std::move(frames_[static_cast<size_t>(best_idx)]);
    frames_.erase(frames_.begin() + best_idx);
    return true;
}

bool DecodedFrameQueue::has_generation(uint64_t loop_generation) const {
    std::lock_guard<std::mutex> lock(mu_);
    return std::any_of(frames_.begin(), frames_.end(),
        [&](const DecodedFrame& frame) {
            return frame.loop_generation == loop_generation;
        });
}

bool DecodedFrameQueue::has_request_key(uint64_t request_key) const {
    if (request_key == 0) return false;
    std::lock_guard<std::mutex> lock(mu_);
    return std::any_of(frames_.begin(), frames_.end(),
        [&](const DecodedFrame& frame) {
            return frame.request_key == request_key;
        });
}

void DecodedFrameQueue::flush() {
    std::lock_guard<std::mutex> lock(mu_);
    frames_.clear();
}

size_t DecodedFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return frames_.size();
}
