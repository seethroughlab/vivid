#pragma once
#include <vivid/types.h>
#include <webgpu/webgpu.h>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <string>

namespace vivid {

// Callback type for when readback completes
// Parameters: operatorId, pixels (RGBA), width, height
using ReadbackCallback = std::function<void(const std::string&, const std::vector<uint8_t>&, int, int)>;

// A pending readback request
struct ReadbackRequest {
    WGPUBuffer stagingBuffer;
    size_t bufferSize;
    int width;
    int height;
    std::string operatorId;
    ReadbackCallback callback;
    bool mappingComplete = false;
    bool mappingSuccess = false;
};

class AsyncReadback {
public:
    AsyncReadback();
    ~AsyncReadback();

    // Initialize with WebGPU device and queue
    void init(WGPUDevice device, WGPUQueue queue);

    // Shutdown and release resources
    void shutdown();

    // Queue a texture for async readback (non-blocking)
    // Callback will be invoked later when data is ready
    void queueReadback(const Texture& texture, const std::string& operatorId,
                       ReadbackCallback callback);

    // Process completed readbacks and invoke callbacks
    // Call this periodically (e.g., once per frame or from preview thread)
    void processCompleted();

    // Check if there are pending readbacks
    bool hasPending() const;
    size_t pendingCount() const;

private:
    // Get a staging buffer from the pool (or create new one)
    WGPUBuffer acquireStagingBuffer(size_t size);

    // Return a staging buffer to the pool
    void releaseStagingBuffer(WGPUBuffer buffer, size_t size);

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;

    // Active requests being processed
    std::vector<ReadbackRequest> activeRequests_;

    // Pool of reusable staging buffers (size -> buffer)
    std::vector<std::pair<size_t, WGPUBuffer>> bufferPool_;

    // Mutex for thread-safe access to completed queue
    mutable std::mutex mutex_;
};

} // namespace vivid
