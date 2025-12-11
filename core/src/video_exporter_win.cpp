// Video Exporter - Windows implementation
// PNG snapshot support via stb_image_write

#if defined(_WIN32)

#include <vivid/video_exporter.h>
#include <webgpu/wgpu.h>  // wgpu-native extensions (wgpuDevicePoll)
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <thread>
#include <atomic>
#include <cmath>

// stb_image_write for PNG saving
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace vivid {

struct VideoExporter::Impl {
    // Stub - no implementation yet
};

VideoExporter::VideoExporter() : m_impl(nullptr) {
}

VideoExporter::~VideoExporter() {
    stop();
}

bool VideoExporter::start(const std::string& path, int width, int height,
                          float fps, ExportCodec codec) {
    std::cerr << "[VideoExporter] Video export not yet implemented on Windows\n";
    m_error = "Video export not yet implemented on Windows";
    return false;
}

bool VideoExporter::startWithAudio(const std::string& path, int width, int height,
                                   float fps, ExportCodec codec,
                                   uint32_t audioSampleRate, uint32_t audioChannels) {
    std::cerr << "[VideoExporter] Video export not yet implemented on Windows\n";
    m_error = "Video export not yet implemented on Windows";
    return false;
}

void VideoExporter::captureFrame(WGPUDevice device, WGPUQueue queue, WGPUTexture texture) {
    // Stub - no implementation
}

void VideoExporter::pushAudioSamples(const float* samples, uint32_t frameCount) {
    // Stub - no implementation
}

void VideoExporter::stop() {
    m_recording = false;
}

float VideoExporter::duration() const {
    if (m_fps > 0) {
        return static_cast<float>(m_frameCount) / m_fps;
    }
    return 0.0f;
}

std::string VideoExporter::generateOutputPath(const std::string& directory, ExportCodec codec) {
    // Generate timestamp-based filename
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream filename;
    filename << directory << "/vivid_"
             << std::put_time(&tm, "%Y%m%d_%H%M%S");

    switch (codec) {
        case ExportCodec::Animation:
            filename << ".mov";
            break;
        case ExportCodec::H264:
            filename << ".mp4";
            break;
        case ExportCodec::H265:
            filename << ".mp4";
            break;
    }

    return filename.str();
}

bool VideoExporter::saveSnapshot(WGPUDevice device, WGPUQueue queue,
                                  WGPUTexture texture, const std::string& outputPath) {
    if (!texture) {
        return false;
    }

    // Get texture dimensions
    uint32_t width = wgpuTextureGetWidth(texture);
    uint32_t height = wgpuTextureGetHeight(texture);
    WGPUTextureFormat format = wgpuTextureGetFormat(texture);

    // Calculate bytes per pixel based on format
    uint32_t bytesPerPixel = 4;  // Default RGBA8
    switch (format) {
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            bytesPerPixel = 4;
            break;
        case WGPUTextureFormat_RGBA16Float:
            bytesPerPixel = 8;
            break;
        case WGPUTextureFormat_RGBA32Float:
            bytesPerPixel = 16;
            break;
        default:
            std::cerr << "[VideoExporter::saveSnapshot] Warning: Unknown texture format " << format << ", assuming 4 bytes/pixel\n";
            break;
    }

    // Calculate buffer size with 256-byte row alignment
    uint32_t bytesPerRow = ((width * bytesPerPixel) + 255) & ~255;
    size_t bufferSize = bytesPerRow * height;

    // Create readback buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDesc.mappedAtCreation = false;
    WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

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
    dstCopy.layout.rowsPerImage = height;

    WGPUExtent3D copySize = {width, height, 1};
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
        return false;
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
        return false;
    }

    // Read pixel data
    const uint8_t* mappedData = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));

    if (!mappedData) {
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return false;
    }

    // Copy to contiguous buffer (removing row padding) and convert to 8-bit RGBA for PNG
    std::vector<uint8_t> pixels(width * height * 4);
    bool isBGRA = (format == WGPUTextureFormat_BGRA8Unorm || format == WGPUTextureFormat_BGRA8UnormSrgb);
    bool isFloat16 = (format == WGPUTextureFormat_RGBA16Float);
    bool isFloat32 = (format == WGPUTextureFormat_RGBA32Float);

    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = mappedData + y * bytesPerRow;
        uint8_t* dstRow = pixels.data() + y * width * 4;

        if (isFloat32) {
            // Convert RGBA32Float to RGBA8
            const float* srcFloat = reinterpret_cast<const float*>(srcRow);
            for (uint32_t x = 0; x < width; ++x) {
                dstRow[x * 4 + 0] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, srcFloat[x * 4 + 0] * 255.0f)));
                dstRow[x * 4 + 1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, srcFloat[x * 4 + 1] * 255.0f)));
                dstRow[x * 4 + 2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, srcFloat[x * 4 + 2] * 255.0f)));
                dstRow[x * 4 + 3] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, srcFloat[x * 4 + 3] * 255.0f)));
            }
        } else if (isFloat16) {
            // Convert RGBA16Float to RGBA8 (half-float conversion)
            const uint16_t* srcHalf = reinterpret_cast<const uint16_t*>(srcRow);
            for (uint32_t x = 0; x < width; ++x) {
                // Simple half-float to float conversion
                auto halfToFloat = [](uint16_t h) -> float {
                    uint32_t sign = (h >> 15) & 0x1;
                    uint32_t exp = (h >> 10) & 0x1F;
                    uint32_t mant = h & 0x3FF;
                    if (exp == 0) {
                        return sign ? -0.0f : 0.0f;  // Denormals treated as zero
                    } else if (exp == 31) {
                        return sign ? -INFINITY : INFINITY;
                    }
                    float f = (1.0f + mant / 1024.0f) * std::pow(2.0f, static_cast<float>(exp) - 15.0f);
                    return sign ? -f : f;
                };
                float r = halfToFloat(srcHalf[x * 4 + 0]);
                float g = halfToFloat(srcHalf[x * 4 + 1]);
                float b = halfToFloat(srcHalf[x * 4 + 2]);
                float a = halfToFloat(srcHalf[x * 4 + 3]);
                dstRow[x * 4 + 0] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, r * 255.0f)));
                dstRow[x * 4 + 1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, g * 255.0f)));
                dstRow[x * 4 + 2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, b * 255.0f)));
                dstRow[x * 4 + 3] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, a * 255.0f)));
            }
        } else if (isBGRA) {
            // Swap B and R channels
            for (uint32_t x = 0; x < width; ++x) {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2];  // R <- B
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1];  // G
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0];  // B <- R
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3];  // A
            }
        } else {
            // RGBA8 - direct copy
            memcpy(dstRow, srcRow, width * 4);
        }
    }

    wgpuBufferUnmap(readbackBuffer);
    wgpuBufferRelease(readbackBuffer);

    // Save PNG
    int result = stbi_write_png(outputPath.c_str(), width, height, 4, pixels.data(), width * 4);
    return result != 0;
}

void VideoExporter::encodeFrame(uint32_t width, uint32_t height,
                                 uint32_t bytesPerRow, uint32_t bytesPerPixel) {
    // Stub - no implementation
}

} // namespace vivid

#endif // _WIN32
