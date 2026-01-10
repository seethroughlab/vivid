/**
 * @file texture_converter.cpp
 * @brief GPU texture <-> OpenCV cv::Mat conversion implementation
 */

#include <vivid/opencv/texture_converter.h>
#include <webgpu/wgpu.h>  // wgpu-native extensions (wgpuDevicePoll)
#include <thread>
#include <atomic>
#include <cmath>
#include <iostream>

namespace vivid::opencv {

cv::Mat textureToMat(Context& ctx, WGPUTexture texture, int width, int height) {
    if (!texture || width <= 0 || height <= 0) {
        return cv::Mat();
    }

    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    // RGBA16Float = 8 bytes per pixel
    constexpr uint32_t bytesPerPixel = 8;

    // Calculate buffer size with 256-byte row alignment (WebGPU requirement)
    uint32_t bytesPerRow = ((width * bytesPerPixel) + 255) & ~255;
    size_t bufferSize = bytesPerRow * height;

    // Create readback buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDesc.mappedAtCreation = false;
    WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    if (!readbackBuffer) {
        return cv::Mat();
    }

    // Copy texture to buffer
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPUTexelCopyTextureInfo srcCopy = {};
    srcCopy.texture = texture;
    srcCopy.mipLevel = 0;
    srcCopy.origin = {0, 0, 0};
    srcCopy.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dstCopy = {};
    dstCopy.buffer = readbackBuffer;
    dstCopy.layout.offset = 0;
    dstCopy.layout.bytesPerRow = bytesPerRow;
    dstCopy.layout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D copySize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcCopy, &dstCopy, &copySize);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Wait for queue work to complete before mapping
    struct WorkDoneContext {
        std::atomic<bool> done{false};
    } workCtx;

    WGPUQueueWorkDoneCallbackInfo workDoneInfo = {};
    workDoneInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    workDoneInfo.callback = [](WGPUQueueWorkDoneStatus /*status*/, void* userdata1, void* /*userdata2*/) {
        auto* ctx = static_cast<WorkDoneContext*>(userdata1);
        ctx->done = true;
    };
    workDoneInfo.userdata1 = &workCtx;
    workDoneInfo.userdata2 = nullptr;

    wgpuQueueOnSubmittedWorkDone(queue, workDoneInfo);

    // Poll until work is done
    int workTimeout = 1000;
    while (!workCtx.done && workTimeout-- > 0) {
        wgpuDevicePoll(device, false, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!workCtx.done) {
        wgpuBufferRelease(readbackBuffer);
        return cv::Mat();
    }

    // Map buffer with async callback
    struct MapContext {
        std::atomic<bool> done{false};
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Unknown;
    } mapCtx;

    WGPUBufferMapCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView /*message*/,
                               void* userdata1, void* /*userdata2*/) {
        auto* ctx = static_cast<MapContext*>(userdata1);
        ctx->status = status;
        ctx->done = true;
    };
    callbackInfo.userdata1 = &mapCtx;
    callbackInfo.userdata2 = nullptr;

    wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, bufferSize, callbackInfo);

    // Poll until map completes (with timeout)
    int timeout = 1000;
    while (!mapCtx.done && timeout-- > 0) {
        wgpuDevicePoll(device, false, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!mapCtx.done || mapCtx.status != WGPUMapAsyncStatus_Success) {
        wgpuBufferRelease(readbackBuffer);
        return cv::Mat();
    }

    // Read pixel data
    const uint8_t* mappedData = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));

    if (!mappedData) {
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return cv::Mat();
    }

    // Convert RGBA16Float to CV_8UC4 (BGRA for OpenCV)
    cv::Mat result(height, width, CV_8UC4);

    // Linear to sRGB conversion (gamma correction)
    auto linearToSrgb = [](float linear) -> float {
        linear = std::max(0.0f, std::min(1.0f, linear));
        if (linear <= 0.0031308f) {
            return linear * 12.92f;
        }
        return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    };

    // Half-float to float conversion
    auto halfToFloat = [](uint16_t h) -> float {
        uint32_t sign = (h >> 15) & 0x1;
        uint32_t exp = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x3FF;
        if (exp == 0) {
            return sign ? -0.0f : 0.0f;
        } else if (exp == 31) {
            return sign ? -INFINITY : INFINITY;
        }
        float f = (1.0f + mant / 1024.0f) * std::pow(2.0f, static_cast<float>(exp) - 15.0f);
        return sign ? -f : f;
    };

    for (int y = 0; y < height; ++y) {
        const uint16_t* srcRow = reinterpret_cast<const uint16_t*>(mappedData + y * bytesPerRow);
        uint8_t* dstRow = result.ptr<uint8_t>(y);

        for (int x = 0; x < width; ++x) {
            float r = halfToFloat(srcRow[x * 4 + 0]);
            float g = halfToFloat(srcRow[x * 4 + 1]);
            float b = halfToFloat(srcRow[x * 4 + 2]);
            float a = halfToFloat(srcRow[x * 4 + 3]);

            // OpenCV uses BGRA order
            dstRow[x * 4 + 0] = static_cast<uint8_t>(linearToSrgb(b) * 255.0f + 0.5f);  // B
            dstRow[x * 4 + 1] = static_cast<uint8_t>(linearToSrgb(g) * 255.0f + 0.5f);  // G
            dstRow[x * 4 + 2] = static_cast<uint8_t>(linearToSrgb(r) * 255.0f + 0.5f);  // R
            dstRow[x * 4 + 3] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, a * 255.0f)));  // A
        }
    }

    wgpuBufferUnmap(readbackBuffer);
    wgpuBufferRelease(readbackBuffer);

    return result;
}

void matToTexture(Context& ctx, const cv::Mat& mat, WGPUTexture texture) {
    if (mat.empty() || !texture) {
        return;
    }

    if (mat.type() != CV_8UC4) {
        std::cerr << "[vivid-opencv] matToTexture: expected CV_8UC4, got type " << mat.type() << "\n";
        return;
    }

    WGPUQueue queue = ctx.queue();

    int width = mat.cols;
    int height = mat.rows;

    // Convert CV_8UC4 (BGRA) to RGBA16Float for GPU
    // Need to handle row alignment for WebGPU
    constexpr uint32_t bytesPerPixel = 8;  // RGBA16Float
    uint32_t bytesPerRow = ((width * bytesPerPixel) + 255) & ~255;
    size_t bufferSize = bytesPerRow * height;

    std::vector<uint8_t> gpuData(bufferSize);

    // sRGB to linear conversion
    auto srgbToLinear = [](float srgb) -> float {
        if (srgb <= 0.04045f) {
            return srgb / 12.92f;
        }
        return std::pow((srgb + 0.055f) / 1.055f, 2.4f);
    };

    // Float to half-float conversion
    auto floatToHalf = [](float f) -> uint16_t {
        // Clamp to valid range
        f = std::max(0.0f, std::min(65504.0f, f));

        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(f));

        uint32_t sign = (bits >> 31) & 0x1;
        int32_t exp = ((bits >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = bits & 0x7FFFFF;

        if (exp <= 0) {
            return static_cast<uint16_t>(sign << 15);  // Underflow to zero
        } else if (exp >= 31) {
            return static_cast<uint16_t>((sign << 15) | 0x7C00);  // Infinity
        }

        return static_cast<uint16_t>((sign << 15) | (exp << 10) | (mant >> 13));
    };

    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = mat.ptr<uint8_t>(y);
        uint16_t* dstRow = reinterpret_cast<uint16_t*>(gpuData.data() + y * bytesPerRow);

        for (int x = 0; x < width; ++x) {
            // OpenCV BGRA -> GPU RGBA
            float b = srgbToLinear(srcRow[x * 4 + 0] / 255.0f);
            float g = srgbToLinear(srcRow[x * 4 + 1] / 255.0f);
            float r = srgbToLinear(srcRow[x * 4 + 2] / 255.0f);
            float a = srcRow[x * 4 + 3] / 255.0f;  // Alpha stays linear

            dstRow[x * 4 + 0] = floatToHalf(r);
            dstRow[x * 4 + 1] = floatToHalf(g);
            dstRow[x * 4 + 2] = floatToHalf(b);
            dstRow[x * 4 + 3] = floatToHalf(a);
        }
    }

    // Upload to texture
    WGPUTexelCopyTextureInfo dstInfo = {};
    dstInfo.texture = texture;
    dstInfo.mipLevel = 0;
    dstInfo.origin = {0, 0, 0};
    dstInfo.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = bytesPerRow;
    layout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    wgpuQueueWriteTexture(queue, &dstInfo, gpuData.data(), bufferSize, &layout, &writeSize);
}

} // namespace vivid::opencv
