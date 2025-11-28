#include "async_readback.h"
#include "renderer.h"
#include <webgpu/wgpu.h>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace vivid {

AsyncReadback::AsyncReadback() = default;

AsyncReadback::~AsyncReadback() {
    shutdown();
}

void AsyncReadback::init(WGPUDevice device, WGPUQueue queue) {
    device_ = device;
    queue_ = queue;
}

void AsyncReadback::shutdown() {
    // Process any remaining requests
    processCompleted();

    // Clear active requests
    for (auto& req : activeRequests_) {
        if (req.stagingBuffer) {
            wgpuBufferRelease(req.stagingBuffer);
        }
    }
    activeRequests_.clear();

    // Release pooled buffers
    for (auto& [size, buffer] : bufferPool_) {
        if (buffer) {
            wgpuBufferRelease(buffer);
        }
    }
    bufferPool_.clear();

    device_ = nullptr;
    queue_ = nullptr;
}

void AsyncReadback::queueReadback(const Texture& texture, const std::string& operatorId,
                                   ReadbackCallback callback) {
    if (!device_ || !queue_) return;

    auto* texData = getTextureData(texture);
    if (!texData || !texData->texture) return;

    // Calculate buffer size with alignment
    size_t bytesPerRow = texture.width * 4;
    size_t alignedBytesPerRow = (bytesPerRow + 255) & ~255;  // 256-byte alignment
    size_t bufferSize = alignedBytesPerRow * texture.height;

    // Get a staging buffer
    WGPUBuffer stagingBuffer = acquireStagingBuffer(bufferSize);
    if (!stagingBuffer) return;

    // Copy texture to staging buffer
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDesc);

    WGPUTexelCopyTextureInfo source = {};
    source.texture = texData->texture;

    WGPUTexelCopyBufferInfo destination = {};
    destination.buffer = stagingBuffer;
    destination.layout.bytesPerRow = alignedBytesPerRow;
    destination.layout.rowsPerImage = texture.height;

    WGPUExtent3D copySize = {
        static_cast<uint32_t>(texture.width),
        static_cast<uint32_t>(texture.height),
        1
    };

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(queue_, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Create request
    ReadbackRequest request;
    request.stagingBuffer = stagingBuffer;
    request.bufferSize = bufferSize;
    request.width = texture.width;
    request.height = texture.height;
    request.operatorId = operatorId;
    request.callback = std::move(callback);

    // Store pointer to request for callback
    activeRequests_.push_back(std::move(request));
    ReadbackRequest* reqPtr = &activeRequests_.back();

    // Start async map
    WGPUBufferMapCallbackInfo mapCallbackInfo = {};
    mapCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    mapCallbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message,
                                   void* userdata1, void* userdata2) {
        auto* req = static_cast<ReadbackRequest*>(userdata1);
        req->mappingComplete = true;
        req->mappingSuccess = (status == WGPUMapAsyncStatus_Success);
        if (!req->mappingSuccess && message.data) {
            std::cerr << "[AsyncReadback] Map failed: " << std::string(message.data, message.length) << "\n";
        }
    };
    mapCallbackInfo.userdata1 = reqPtr;

    wgpuBufferMapAsync(stagingBuffer, WGPUMapMode_Read, 0, bufferSize, mapCallbackInfo);
}

void AsyncReadback::processCompleted() {
    if (!device_) return;

    // Poll device to process callbacks
    wgpuDevicePoll(device_, false, nullptr);

    // Process completed requests
    auto it = activeRequests_.begin();
    while (it != activeRequests_.end()) {
        if (it->mappingComplete) {
            if (it->mappingSuccess) {
                // Read the data
                const uint8_t* mappedData = static_cast<const uint8_t*>(
                    wgpuBufferGetMappedRange(it->stagingBuffer, 0, it->bufferSize)
                );

                if (mappedData && it->callback) {
                    // Copy data, removing row padding
                    size_t bytesPerRow = it->width * 4;
                    size_t alignedBytesPerRow = (bytesPerRow + 255) & ~255;

                    std::vector<uint8_t> pixels(it->width * it->height * 4);

                    if (alignedBytesPerRow == bytesPerRow) {
                        // No padding, direct copy
                        std::memcpy(pixels.data(), mappedData, pixels.size());
                    } else {
                        // Remove padding from each row
                        for (int y = 0; y < it->height; y++) {
                            std::memcpy(
                                pixels.data() + y * bytesPerRow,
                                mappedData + y * alignedBytesPerRow,
                                bytesPerRow
                            );
                        }
                    }

                    // Invoke callback with the pixel data
                    it->callback(it->operatorId, pixels, it->width, it->height);
                }
            }

            // Unmap and release buffer back to pool
            wgpuBufferUnmap(it->stagingBuffer);
            releaseStagingBuffer(it->stagingBuffer, it->bufferSize);

            it = activeRequests_.erase(it);
        } else {
            ++it;
        }
    }
}

bool AsyncReadback::hasPending() const {
    return !activeRequests_.empty();
}

size_t AsyncReadback::pendingCount() const {
    return activeRequests_.size();
}

WGPUBuffer AsyncReadback::acquireStagingBuffer(size_t size) {
    // Look for existing buffer of same size
    for (auto it = bufferPool_.begin(); it != bufferPool_.end(); ++it) {
        if (it->first == size) {
            WGPUBuffer buffer = it->second;
            bufferPool_.erase(it);
            return buffer;
        }
    }

    // Create new buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = size;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDesc.mappedAtCreation = false;

    return wgpuDeviceCreateBuffer(device_, &bufferDesc);
}

void AsyncReadback::releaseStagingBuffer(WGPUBuffer buffer, size_t size) {
    // Keep a limited pool
    constexpr size_t MAX_POOL_SIZE = 8;

    if (bufferPool_.size() < MAX_POOL_SIZE) {
        bufferPool_.push_back({size, buffer});
    } else {
        wgpuBufferRelease(buffer);
    }
}

} // namespace vivid
