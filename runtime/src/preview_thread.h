#pragma once
#include "shared_preview.h"
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>

namespace vivid {

// Work item for the preview thread to process
struct PreviewWorkItem {
    std::string operatorId;
    int sourceLine;
    int slotIndex;
    int srcWidth;
    int srcHeight;
    std::vector<uint8_t> rgbaPixels;  // Source RGBA pixels from GPU readback
};

// Callback when a preview slot is updated (called from preview thread)
using PreviewReadyCallback = std::function<void(int slotIndex, const std::string& operatorId)>;

class PreviewThread {
public:
    PreviewThread();
    ~PreviewThread();

    // Start the worker thread
    void start(SharedPreview* sharedPreview);

    // Stop the worker thread
    void stop();

    // Queue work for the preview thread (called from main thread)
    // This takes ownership of the pixel data via move
    void queueWork(PreviewWorkItem&& item);

    // Set callback for when previews are ready (for WebSocket notification)
    void setReadyCallback(PreviewReadyCallback callback);

    // Get list of slots that were updated since last check (thread-safe)
    std::vector<int> getUpdatedSlots();

    // Check if thread is running
    bool isRunning() const { return running_; }

    // Get pending work count
    size_t pendingCount() const;

private:
    void workerLoop();
    void processWorkItem(const PreviewWorkItem& item);

    SharedPreview* sharedPreview_ = nullptr;
    PreviewReadyCallback readyCallback_;

    std::thread workerThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shouldStop_{false};

    // Work queue
    std::queue<PreviewWorkItem> workQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;

    // Updated slots tracking
    std::vector<int> updatedSlots_;
    std::mutex updatedMutex_;
};

} // namespace vivid
