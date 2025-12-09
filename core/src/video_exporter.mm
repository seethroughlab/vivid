#include <vivid/video_exporter.h>
#include <webgpu/wgpu.h>  // wgpu-native extensions (wgpuDevicePoll)

#ifdef __APPLE__

#import <AVFoundation/AVFoundation.h>
#import <VideoToolbox/VideoToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <mutex>

namespace vivid {

struct VideoExporter::Impl {
    AVAssetWriter* assetWriter = nil;
    AVAssetWriterInput* videoInput = nil;
    AVAssetWriterInputPixelBufferAdaptor* pixelBufferAdaptor = nil;
    CMTime frameDuration;
    CMTime currentTime;
    dispatch_queue_t encodingQueue = nil;
    std::mutex mutex;
    bool finalized = false;
};

VideoExporter::VideoExporter() {
    m_impl = new Impl();
}

VideoExporter::~VideoExporter() {
    if (m_recording) {
        stop();
    }
    if (m_readbackBuffer) {
        wgpuBufferRelease(m_readbackBuffer);
    }
    delete m_impl;
}

static CMVideoCodecType codecToVideoToolbox(ExportCodec codec) {
    switch (codec) {
        case ExportCodec::Animation:
            return kCMVideoCodecType_AppleProRes4444;
        case ExportCodec::H264:
            return kCMVideoCodecType_H264;
        case ExportCodec::H265:
            return kCMVideoCodecType_HEVC;
    }
    return kCMVideoCodecType_H264;
}

static NSString* codecToAVVideoCodecKey(ExportCodec codec) {
    switch (codec) {
        case ExportCodec::Animation:
            return AVVideoCodecTypeAppleProRes4444;
        case ExportCodec::H264:
            return AVVideoCodecTypeH264;
        case ExportCodec::H265:
            return AVVideoCodecTypeHEVC;
    }
    return AVVideoCodecTypeH264;
}

bool VideoExporter::start(const std::string& path, int width, int height,
                          float fps, ExportCodec codec) {
    if (m_recording) {
        m_error = "Already recording";
        return false;
    }

    m_outputPath = path;
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_frameCount = 0;
    m_error.clear();
    m_impl->finalized = false;

    // Create asset writer
    NSURL* outputURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];

    // Remove existing file if present
    [[NSFileManager defaultManager] removeItemAtURL:outputURL error:nil];

    NSError* error = nil;
    m_impl->assetWriter = [[AVAssetWriter alloc] initWithURL:outputURL
                                                    fileType:AVFileTypeQuickTimeMovie
                                                       error:&error];
    if (error) {
        m_error = std::string("Failed to create asset writer: ") + [[error localizedDescription] UTF8String];
        return false;
    }

    // Video settings
    NSDictionary* videoSettings = @{
        AVVideoCodecKey: codecToAVVideoCodecKey(codec),
        AVVideoWidthKey: @(width),
        AVVideoHeightKey: @(height),
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(width * height * 4),  // ~4 bits per pixel
            AVVideoExpectedSourceFrameRateKey: @(fps),
            AVVideoMaxKeyFrameIntervalKey: @(fps * 2),  // Keyframe every 2 seconds
        }
    };

    // For ProRes, use simpler settings
    if (codec == ExportCodec::Animation) {
        videoSettings = @{
            AVVideoCodecKey: codecToAVVideoCodecKey(codec),
            AVVideoWidthKey: @(width),
            AVVideoHeightKey: @(height)
        };
    }

    m_impl->videoInput = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeVideo
                                                        outputSettings:videoSettings];
    m_impl->videoInput.expectsMediaDataInRealTime = YES;

    // Pixel buffer attributes
    NSDictionary* pixelBufferAttributes = @{
        (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (NSString*)kCVPixelBufferWidthKey: @(width),
        (NSString*)kCVPixelBufferHeightKey: @(height),
        (NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
    };

    m_impl->pixelBufferAdaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
        initWithAssetWriterInput:m_impl->videoInput
        sourcePixelBufferAttributes:pixelBufferAttributes];

    if ([m_impl->assetWriter canAddInput:m_impl->videoInput]) {
        [m_impl->assetWriter addInput:m_impl->videoInput];
    } else {
        m_error = "Cannot add video input to asset writer";
        return false;
    }

    // Start writing
    if (![m_impl->assetWriter startWriting]) {
        m_error = std::string("Failed to start writing: ") +
                  [[m_impl->assetWriter.error localizedDescription] UTF8String];
        return false;
    }

    [m_impl->assetWriter startSessionAtSourceTime:kCMTimeZero];

    // Set up timing
    m_impl->frameDuration = CMTimeMake(1, static_cast<int32_t>(fps));
    m_impl->currentTime = kCMTimeZero;

    // Create encoding queue
    m_impl->encodingQueue = dispatch_queue_create("com.vivid.videoexporter", DISPATCH_QUEUE_SERIAL);

    m_recording = true;
    printf("[VideoExporter] Started recording: %s (%dx%d @ %.1f fps)\n",
           path.c_str(), width, height, fps);

    return true;
}

void VideoExporter::captureFrame(WGPUDevice device, WGPUQueue queue, WGPUTexture texture) {
    if (!m_recording || !texture) return;

    // Get texture dimensions and format
    uint32_t texWidth = wgpuTextureGetWidth(texture);
    uint32_t texHeight = wgpuTextureGetHeight(texture);
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
            printf("[VideoExporter] Warning: Unknown texture format %d, assuming 4 bytes/pixel\n", format);
            break;
    }

    // Calculate buffer size with proper row alignment (256 bytes for WebGPU)
    uint32_t bytesPerRow = ((texWidth * bytesPerPixel) + 255) & ~255;
    size_t requiredSize = bytesPerRow * texHeight;

    // Create or resize readback buffer
    if (!m_readbackBuffer || m_bufferSize < requiredSize) {
        if (m_readbackBuffer) {
            wgpuBufferRelease(m_readbackBuffer);
        }

        WGPUBufferDescriptor bufferDesc = {};
        bufferDesc.size = requiredSize;
        bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        bufferDesc.mappedAtCreation = false;

        m_readbackBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
        m_bufferSize = requiredSize;
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
    dstCopy.buffer = m_readbackBuffer;
    dstCopy.layout.offset = 0;
    dstCopy.layout.bytesPerRow = bytesPerRow;
    dstCopy.layout.rowsPerImage = texHeight;

    WGPUExtent3D copySize = {texWidth, texHeight, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcCopy, &dstCopy, &copySize);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Map buffer and read pixels synchronously
    struct MapContext {
        bool completed = false;
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Unknown;
    };

    MapContext mapCtx;

    WGPUBufferMapCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message,
                               void* userdata1, void* userdata2) {
        MapContext* ctx = static_cast<MapContext*>(userdata1);
        ctx->status = status;
        ctx->completed = true;
    };
    callbackInfo.userdata1 = &mapCtx;

    wgpuBufferMapAsync(m_readbackBuffer, WGPUMapMode_Read, 0, requiredSize, callbackInfo);

    // Poll device until mapping completes
    while (!mapCtx.completed) {
        wgpuDevicePoll(device, true, nullptr);
    }

    if (mapCtx.status == WGPUMapAsyncStatus_Success) {
        encodeFrame(texWidth, texHeight, bytesPerRow, bytesPerPixel);
    } else {
        printf("[VideoExporter] Buffer map failed: status=%d\n", (int)mapCtx.status);
    }
}

void VideoExporter::encodeFrame(uint32_t width, uint32_t height, uint32_t bytesPerRow, uint32_t bytesPerPixel) {
    if (!m_recording || !m_readbackBuffer) {
        return;
    }

    const void* data = wgpuBufferGetConstMappedRange(m_readbackBuffer, 0, bytesPerRow * height);
    if (!data) {
        wgpuBufferUnmap(m_readbackBuffer);
        return;
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);

    // Check if input is ready
    if (![m_impl->videoInput isReadyForMoreMediaData]) {
        wgpuBufferUnmap(m_readbackBuffer);
        return;
    }

    // Create pixel buffer
    CVPixelBufferRef pixelBuffer = nil;
    CVReturn cvStatus = CVPixelBufferPoolCreatePixelBuffer(
        nil, m_impl->pixelBufferAdaptor.pixelBufferPool, &pixelBuffer);

    if (cvStatus != kCVReturnSuccess || !pixelBuffer) {
        // Fallback: create pixel buffer directly
        NSDictionary* attrs = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferWidthKey: @(width),
            (NSString*)kCVPixelBufferHeightKey: @(height),
            (NSString*)kCVPixelBufferMetalCompatibilityKey: @YES
        };

        cvStatus = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                                        kCVPixelFormatType_32BGRA,
                                        (__bridge CFDictionaryRef)attrs,
                                        &pixelBuffer);
        if (cvStatus != kCVReturnSuccess) {
            wgpuBufferUnmap(m_readbackBuffer);
            return;
        }
    }

    // Copy pixels to CVPixelBuffer
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    uint8_t* dest = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));
    size_t destBytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    const uint8_t* src = static_cast<const uint8_t*>(data);

    // Debug: print buffer info on first frame
    static bool bufferDebugPrinted = false;
    if (!bufferDebugPrinted) {
        printf("[VideoExporter] CVPixelBuffer: %zux%zu, bytesPerRow=%zu, dest=%p\n",
               CVPixelBufferGetWidth(pixelBuffer), CVPixelBufferGetHeight(pixelBuffer),
               destBytesPerRow, dest);
        printf("[VideoExporter] Source: %ux%u, bytesPerRow=%u, bytesPerPixel=%u\n",
               width, height, bytesPerRow, bytesPerPixel);
        bufferDebugPrinted = true;
    }

    // Copy and convert pixels based on source format
    if (bytesPerPixel == 8) {
        // RGBA16Float -> BGRA8: convert half-float to 8-bit with channel swap
        // Debug: print first pixel of first frame
        static bool debugPrinted = false;

        // Half-float to float conversion function
        // IEEE 754 half-precision: 1 sign bit, 5 exponent bits (bias 15), 10 mantissa bits
        auto halfToFloat = [](uint16_t h) -> float {
            uint32_t sign = (h >> 15) & 0x1;
            uint32_t exp = (h >> 10) & 0x1F;
            uint32_t mant = h & 0x3FF;

            if (exp == 0) {
                if (mant == 0) return 0.0f;
                // Denormalized number
                return (sign ? -1.0f : 1.0f) * ((float)mant / 1024.0f) * powf(2.0f, -14.0f);
            } else if (exp == 31) {
                return mant ? NAN : (sign ? -INFINITY : INFINITY);
            }

            // Normalized number: (-1)^sign * 2^(exp-15) * (1 + mant/1024)
            float mantissa = 1.0f + (float)mant / 1024.0f;
            float result = mantissa * powf(2.0f, (float)exp - 15.0f);
            return sign ? -result : result;
        };

        if (!debugPrinted) {
            const uint16_t* firstPixel = reinterpret_cast<const uint16_t*>(src);
            uint16_t h = firstPixel[0];
            printf("[VideoExporter] First half-float 0x%04x: sign=%d exp=%d mant=%d\n",
                   h, (h >> 15) & 0x1, (h >> 10) & 0x1F, h & 0x3FF);
            float r = halfToFloat(firstPixel[0]);
            float g = halfToFloat(firstPixel[1]);
            float b = halfToFloat(firstPixel[2]);
            float a = halfToFloat(firstPixel[3]);
            printf("[VideoExporter] First pixel raw: %04x %04x %04x %04x -> RGBA: %.3f %.3f %.3f %.3f\n",
                   firstPixel[0], firstPixel[1], firstPixel[2], firstPixel[3], r, g, b, a);
            printf("[VideoExporter] Converted to 8-bit: %d %d %d %d\n",
                   (int)(fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f),
                   (int)(fminf(fmaxf(g, 0.0f), 1.0f) * 255.0f),
                   (int)(fminf(fmaxf(b, 0.0f), 1.0f) * 255.0f),
                   (int)(fminf(fmaxf(a, 0.0f), 1.0f) * 255.0f));
            debugPrinted = true;
        }

        for (uint32_t y = 0; y < height; y++) {
            const uint16_t* srcRow = reinterpret_cast<const uint16_t*>(src + y * bytesPerRow);
            uint8_t* destRow = dest + y * destBytesPerRow;
            for (uint32_t x = 0; x < width; x++) {
                float r = halfToFloat(srcRow[x * 4 + 0]);
                float g = halfToFloat(srcRow[x * 4 + 1]);
                float b = halfToFloat(srcRow[x * 4 + 2]);
                float a = halfToFloat(srcRow[x * 4 + 3]);

                // Clamp and convert to 8-bit BGRA
                destRow[x * 4 + 0] = static_cast<uint8_t>(fminf(fmaxf(b, 0.0f), 1.0f) * 255.0f);
                destRow[x * 4 + 1] = static_cast<uint8_t>(fminf(fmaxf(g, 0.0f), 1.0f) * 255.0f);
                destRow[x * 4 + 2] = static_cast<uint8_t>(fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f);
                destRow[x * 4 + 3] = static_cast<uint8_t>(fminf(fmaxf(a, 0.0f), 1.0f) * 255.0f);
            }
        }
    } else if (bytesPerPixel == 4) {
        // RGBA8 or BGRA8 -> BGRA8: direct copy or channel swap
        // Assume RGBA8 from WebGPU, need to swap to BGRA for CVPixelBuffer
        for (uint32_t y = 0; y < height; y++) {
            const uint8_t* srcRow = src + y * bytesPerRow;
            uint8_t* destRow = dest + y * destBytesPerRow;
            for (uint32_t x = 0; x < width; x++) {
                destRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B <- R
                destRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G <- G
                destRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R <- B
                destRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A <- A
            }
        }
    } else {
        // Unknown format, just copy
        for (uint32_t y = 0; y < height; y++) {
            memcpy(dest + y * destBytesPerRow, src + y * bytesPerRow, width * 4);
        }
    }

    // Debug: verify first pixel was written correctly
    static bool destDebugPrinted = false;
    if (!destDebugPrinted) {
        printf("[VideoExporter] First dest pixel (BGRA): %d %d %d %d\n",
               dest[0], dest[1], dest[2], dest[3]);
        destDebugPrinted = true;
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    wgpuBufferUnmap(m_readbackBuffer);

    // Append to video
    CMTime presentationTime = m_impl->currentTime;
    if (![m_impl->pixelBufferAdaptor appendPixelBuffer:pixelBuffer
                                  withPresentationTime:presentationTime]) {
        NSLog(@"Failed to append pixel buffer at frame %d", m_frameCount);
    }

    CVPixelBufferRelease(pixelBuffer);

    m_impl->currentTime = CMTimeAdd(m_impl->currentTime, m_impl->frameDuration);
    m_frameCount++;
}

void VideoExporter::stop() {
    if (!m_recording) return;

    m_recording = false;

    std::lock_guard<std::mutex> lock(m_impl->mutex);

    if (m_impl->finalized) return;
    m_impl->finalized = true;

    // Mark input as finished
    [m_impl->videoInput markAsFinished];

    // Finish writing
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    [m_impl->assetWriter finishWritingWithCompletionHandler:^{
        dispatch_semaphore_signal(semaphore);
    }];
    dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);

    AVAssetWriterStatus status = m_impl->assetWriter.status;
    if (status == AVAssetWriterStatusFailed) {
        m_error = std::string("Failed to finish writing: ") +
                  [[m_impl->assetWriter.error localizedDescription] UTF8String];
        printf("[VideoExporter] Error: %s\n", m_error.c_str());
    } else if (status == AVAssetWriterStatusCompleted) {
        printf("[VideoExporter] Finished recording: %s (%d frames, %.1f seconds)\n",
               m_outputPath.c_str(), m_frameCount, duration());
    } else {
        printf("[VideoExporter] Warning: Unexpected status %ld after finish\n", (long)status);
    }

    // Cleanup
    m_impl->assetWriter = nil;
    m_impl->videoInput = nil;
    m_impl->pixelBufferAdaptor = nil;
    if (m_impl->encodingQueue) {
        m_impl->encodingQueue = nil;
    }
}

float VideoExporter::duration() const {
    if (m_fps <= 0) return 0;
    return static_cast<float>(m_frameCount) / m_fps;
}

std::string VideoExporter::generateOutputPath(const std::string& directory, ExportCodec codec) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::ostringstream oss;
    oss << directory << "/vivid_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << ".mov";

    return oss.str();
}

} // namespace vivid

#else // !__APPLE__

// Stub implementation for non-Apple platforms
namespace vivid {

struct VideoExporter::Impl {};

VideoExporter::VideoExporter() : m_impl(nullptr) {}
VideoExporter::~VideoExporter() {}

bool VideoExporter::start(const std::string& path, int width, int height,
                          float fps, ExportCodec codec) {
    m_error = "Video export not implemented on this platform";
    return false;
}

void VideoExporter::captureFrame(WGPUDevice device, WGPUQueue queue, WGPUTexture texture) {}
void VideoExporter::stop() {}
float VideoExporter::duration() const { return 0; }

std::string VideoExporter::generateOutputPath(const std::string& directory, ExportCodec codec) {
    return directory + "/output.mov";
}

} // namespace vivid

#endif // __APPLE__
