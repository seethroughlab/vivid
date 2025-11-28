#include "preview_thread.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace vivid {

PreviewThread::PreviewThread() = default;

PreviewThread::~PreviewThread() {
    stop();
}

void PreviewThread::start(SharedPreview* sharedPreview) {
    if (running_) return;

    sharedPreview_ = sharedPreview;
    shouldStop_ = false;
    running_ = true;

    workerThread_ = std::thread(&PreviewThread::workerLoop, this);
}

void PreviewThread::stop() {
    if (!running_) return;

    shouldStop_ = true;
    queueCondition_.notify_all();

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    running_ = false;

    // Clear any remaining work
    std::lock_guard<std::mutex> lock(queueMutex_);
    while (!workQueue_.empty()) {
        workQueue_.pop();
    }
}

void PreviewThread::queueWork(PreviewWorkItem&& item) {
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        workQueue_.push(std::move(item));
    }
    queueCondition_.notify_one();
}

void PreviewThread::setReadyCallback(PreviewReadyCallback callback) {
    readyCallback_ = std::move(callback);
}

std::vector<int> PreviewThread::getUpdatedSlots() {
    std::lock_guard<std::mutex> lock(updatedMutex_);
    std::vector<int> result = std::move(updatedSlots_);
    updatedSlots_.clear();
    return result;
}

size_t PreviewThread::pendingCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return workQueue_.size();
}

void PreviewThread::workerLoop() {
    while (!shouldStop_) {
        PreviewWorkItem item;

        // Wait for work
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCondition_.wait(lock, [this] {
                return shouldStop_ || !workQueue_.empty();
            });

            if (shouldStop_ && workQueue_.empty()) {
                break;
            }

            if (!workQueue_.empty()) {
                item = std::move(workQueue_.front());
                workQueue_.pop();
            } else {
                continue;
            }
        }

        // Process the work item (outside the lock)
        processWorkItem(item);
    }
}

void PreviewThread::processWorkItem(const PreviewWorkItem& item) {
    if (!sharedPreview_ || !sharedPreview_->isOpen()) {
        return;
    }

    // Downsample RGBA to RGB thumbnail
    constexpr int thumbSize = PREVIEW_THUMB_WIDTH;
    int srcWidth = item.srcWidth;
    int srcHeight = item.srcHeight;

    // Calculate thumbnail dimensions maintaining aspect ratio
    int dstWidth = srcWidth;
    int dstHeight = srcHeight;

    if (srcWidth > thumbSize || srcHeight > thumbSize) {
        float scale = std::min(
            static_cast<float>(thumbSize) / srcWidth,
            static_cast<float>(thumbSize) / srcHeight
        );
        dstWidth = std::max(1, static_cast<int>(srcWidth * scale));
        dstHeight = std::max(1, static_cast<int>(srcHeight * scale));
    }

    // Downsample RGBA to RGB
    std::vector<uint8_t> rgbPixels;
    rgbPixels.reserve(dstWidth * dstHeight * 3);

    for (int y = 0; y < dstHeight; y++) {
        int srcY = y * srcHeight / dstHeight;
        for (int x = 0; x < dstWidth; x++) {
            int srcX = x * srcWidth / dstWidth;
            size_t srcIdx = (srcY * srcWidth + srcX) * 4;

            if (srcIdx + 2 < item.rgbaPixels.size()) {
                rgbPixels.push_back(item.rgbaPixels[srcIdx]);     // R
                rgbPixels.push_back(item.rgbaPixels[srcIdx + 1]); // G
                rgbPixels.push_back(item.rgbaPixels[srcIdx + 2]); // B
            } else {
                // Fallback for out-of-bounds (shouldn't happen)
                rgbPixels.push_back(0);
                rgbPixels.push_back(0);
                rgbPixels.push_back(0);
            }
        }
    }

    // Write to shared memory
    sharedPreview_->updateTextureSlot(
        item.slotIndex,
        item.operatorId,
        item.sourceLine,
        item.srcWidth,
        item.srcHeight,
        rgbPixels.data(),
        dstWidth,
        dstHeight
    );

    // Track that this slot was updated
    {
        std::lock_guard<std::mutex> lock(updatedMutex_);
        updatedSlots_.push_back(item.slotIndex);
    }

    // Invoke callback if set
    if (readyCallback_) {
        readyCallback_(item.slotIndex, item.operatorId);
    }
}

} // namespace vivid
