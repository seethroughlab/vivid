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
#include <queue>
#include <condition_variable>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <cmath>

// stb_image_write for PNG saving
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace vivid {

// Audio sample queue entry
struct AudioQueueEntry {
    std::vector<float> samples;
    uint32_t frameCount;
    CMTime presentationTime;
};

struct VideoExporter::Impl {
    AVAssetWriter* assetWriter = nil;
    AVAssetWriterInput* videoInput = nil;
    AVAssetWriterInputPixelBufferAdaptor* pixelBufferAdaptor = nil;
    AVAssetWriterInput* audioInput = nil;
    CMTime frameDuration;
    CMTime currentTime;
    CMTime audioTime;
    dispatch_queue_t encodingQueue = nil;
    dispatch_queue_t audioQueue = nil;
    std::mutex mutex;
    bool finalized = false;

    // Audio queue for async writing
    std::queue<AudioQueueEntry> audioEntries;
    std::mutex audioMutex;
    std::atomic<bool> audioWriterRunning{false};
};

VideoExporter::VideoExporter() {
    m_impl = new Impl();
}

VideoExporter::~VideoExporter() {
    if (m_recording) {
        stop();
    }
    for (int i = 0; i < NUM_READBACK_BUFFERS; i++) {
        if (m_readbackBuffers[i]) {
            wgpuBufferRelease(m_readbackBuffers[i]);
        }
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
    m_audioEnabled = false;
    printf("[VideoExporter] Started recording: %s (%dx%d @ %.1f fps)\n",
           path.c_str(), width, height, fps);

    return true;
}

bool VideoExporter::startWithAudio(const std::string& path, int width, int height,
                                    float fps, ExportCodec codec,
                                    uint32_t audioSampleRate, uint32_t audioChannels) {
    if (m_recording) {
        m_error = "Already recording";
        return false;
    }

    m_outputPath = path;
    m_width = width;
    m_height = height;
    m_fps = fps;
    m_frameCount = 0;
    m_audioFrameCount = 0;
    m_error.clear();
    m_impl->finalized = false;
    m_audioSampleRate = audioSampleRate;
    m_audioChannels = audioChannels;

    // Create asset writer
    NSURL* outputURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
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
            AVVideoAverageBitRateKey: @(width * height * 4),
            AVVideoExpectedSourceFrameRateKey: @(fps),
            AVVideoMaxKeyFrameIntervalKey: @(fps * 2),
        }
    };

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

    // Audio settings - use AAC with proper channel layout
    AudioChannelLayout channelLayout = {};
    channelLayout.mChannelLayoutTag = (audioChannels == 2) ?
        kAudioChannelLayoutTag_Stereo : kAudioChannelLayoutTag_Mono;

    NSDictionary* audioSettings = @{
        AVFormatIDKey: @(kAudioFormatMPEG4AAC),
        AVSampleRateKey: @(audioSampleRate),
        AVNumberOfChannelsKey: @(audioChannels),
        AVEncoderBitRateKey: @(128000),
        AVChannelLayoutKey: [NSData dataWithBytes:&channelLayout length:sizeof(channelLayout)]
    };

    m_impl->audioInput = [[AVAssetWriterInput alloc] initWithMediaType:AVMediaTypeAudio
                                                        outputSettings:audioSettings];
    m_impl->audioInput.expectsMediaDataInRealTime = YES;

    if ([m_impl->assetWriter canAddInput:m_impl->audioInput]) {
        [m_impl->assetWriter addInput:m_impl->audioInput];
    } else {
        m_error = "Cannot add audio input to asset writer";
        return false;
    }

    // Start writing
    if (![m_impl->assetWriter startWriting]) {
        m_error = std::string("Failed to start writing: ") +
                  [[m_impl->assetWriter.error localizedDescription] UTF8String];
        return false;
    }

    [m_impl->assetWriter startSessionAtSourceTime:kCMTimeZero];

    m_impl->frameDuration = CMTimeMake(1, static_cast<int32_t>(fps));
    m_impl->currentTime = kCMTimeZero;
    m_impl->audioTime = kCMTimeZero;
    m_impl->encodingQueue = dispatch_queue_create("com.vivid.videoexporter", DISPATCH_QUEUE_SERIAL);

    // Clear any old audio entries
    {
        std::lock_guard<std::mutex> lock(m_impl->audioMutex);
        while (!m_impl->audioEntries.empty()) {
            m_impl->audioEntries.pop();
        }
    }

    m_impl->audioWriterRunning = true;
    m_recording = true;
    m_audioEnabled = true;
    printf("[VideoExporter] Started recording with audio: %s (%dx%d @ %.1f fps, %dHz %dch)\n",
           path.c_str(), width, height, fps, audioSampleRate, audioChannels);

    return true;
}

// Helper to write an audio queue entry
// Note: Uses AVAssetWriterInput* directly to avoid referencing private Impl type
static void writeAudioEntryImpl(AVAssetWriterInput* audioInput, AVAssetWriter* assetWriter,
                                const AudioQueueEntry& entry,
                                uint32_t audioChannels, uint32_t audioSampleRate) {
    @autoreleasepool {
        // Create audio format description
        AudioStreamBasicDescription asbd = {};
        asbd.mSampleRate = audioSampleRate;
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mBytesPerPacket = audioChannels * sizeof(float);
        asbd.mFramesPerPacket = 1;
        asbd.mBytesPerFrame = audioChannels * sizeof(float);
        asbd.mChannelsPerFrame = audioChannels;
        asbd.mBitsPerChannel = 32;

        CMAudioFormatDescriptionRef formatDesc = nil;
        OSStatus status = CMAudioFormatDescriptionCreate(
            kCFAllocatorDefault, &asbd, 0, nil, 0, nil, nil, &formatDesc);

        if (status != noErr) {
            return;
        }

        // Create block buffer with audio data
        size_t dataSize = entry.frameCount * audioChannels * sizeof(float);
        CMBlockBufferRef blockBuffer = nil;
        status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nil, dataSize,
                                                    kCFAllocatorDefault, nil, 0, dataSize,
                                                    kCMBlockBufferAssureMemoryNowFlag, &blockBuffer);
        if (status != noErr) {
            CFRelease(formatDesc);
            return;
        }

        status = CMBlockBufferReplaceDataBytes(entry.samples.data(), blockBuffer, 0, dataSize);
        if (status != noErr) {
            CFRelease(blockBuffer);
            CFRelease(formatDesc);
            return;
        }

        // Create sample buffer with data already attached
        CMSampleBufferRef sampleBuffer = nil;

        status = CMAudioSampleBufferCreateReadyWithPacketDescriptions(
            kCFAllocatorDefault,
            blockBuffer,
            formatDesc,
            entry.frameCount,
            entry.presentationTime,
            nil,  // No packet descriptions needed for PCM
            &sampleBuffer);

        CFRelease(blockBuffer);
        CFRelease(formatDesc);

        if (status != noErr || !sampleBuffer) {
            return;
        }

        // Append to audio track
        BOOL success = [audioInput appendSampleBuffer:sampleBuffer];
        CFRelease(sampleBuffer);

        // Track success/failure counts
        static int successCount = 0;
        static int failCount = 0;
        static float lastWrittenTime = 0;

        if (success) {
            successCount++;
            lastWrittenTime = CMTimeGetSeconds(entry.presentationTime);
        } else {
            failCount++;
            if (failCount <= 5) {
                NSError* error = assetWriter.error;
                printf("[VideoExporter] Audio append failed at %.2fs: %s\n",
                       CMTimeGetSeconds(entry.presentationTime),
                       error ? [[error localizedDescription] UTF8String] : "unknown");
            }
        }

        // Log progress every 100 successful writes
        if (successCount % 100 == 0) {
            printf("[VideoExporter] Audio writes: %d success, %d fail, last=%.2fs\n",
                   successCount, failCount, lastWrittenTime);
        }
    }
}

void VideoExporter::pushAudioSamples(const float* samples, uint32_t frameCount) {
    if (!m_recording || !m_audioEnabled || !samples || frameCount == 0) return;

    // Check writer status
    if (m_impl->assetWriter.status != AVAssetWriterStatusWriting) {
        return;
    }

    // Queue the audio data
    {
        std::lock_guard<std::mutex> lock(m_impl->audioMutex);
        AudioQueueEntry entry;
        entry.samples.assign(samples, samples + (frameCount * m_audioChannels));
        entry.frameCount = frameCount;
        entry.presentationTime = m_impl->audioTime;

        // Limit queue size
        if (m_impl->audioEntries.size() < 200) {
            m_impl->audioEntries.push(std::move(entry));
            // Advance audio time for next entry
            m_impl->audioTime = CMTimeAdd(m_impl->audioTime, CMTimeMake(frameCount, m_audioSampleRate));
        }
    }

    // Track audio sample count for debugging
    bool firstPush = (m_audioFrameCount == 0);
    m_audioFrameCount += frameCount;

    if (firstPush) {
        printf("[VideoExporter] First audio push: %u frames at time %.3f\n",
               frameCount, CMTimeGetSeconds(m_impl->audioTime));
    }

    // Try to write queued audio while input is ready
    int writtenThisCall = 0;
    while ([m_impl->audioInput isReadyForMoreMediaData]) {
        AudioQueueEntry entry;
        {
            std::lock_guard<std::mutex> lock(m_impl->audioMutex);
            if (m_impl->audioEntries.empty()) {
                break;
            }
            entry = std::move(m_impl->audioEntries.front());
            m_impl->audioEntries.pop();
        }

        writeAudioEntryImpl(m_impl->audioInput, m_impl->assetWriter, entry, m_audioChannels, m_audioSampleRate);
        writtenThisCall++;
    }

    // Log queue status periodically
    static int pushCount = 0;
    pushCount++;
    if (pushCount % 60 == 0) {
        std::lock_guard<std::mutex> lock(m_impl->audioMutex);
        printf("[VideoExporter] Audio push #%d: queued=%zu, wrote=%d this call, total=%.2fs\n",
               pushCount, m_impl->audioEntries.size(), writtenThisCall,
               (float)m_audioFrameCount / m_audioSampleRate);
    }
}

// Helper to submit copy command to a specific buffer
void VideoExporter::submitCopyCommand(WGPUDevice device, WGPUQueue queue, WGPUTexture texture,
                                       int bufferIndex, uint32_t width, uint32_t height,
                                       uint32_t bytesPerRow, uint32_t bytesPerPixel) {
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPUTexelCopyTextureInfo srcCopy = {};
    srcCopy.texture = texture;
    srcCopy.mipLevel = 0;
    srcCopy.origin = {0, 0, 0};
    srcCopy.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dstCopy = {};
    dstCopy.buffer = m_readbackBuffers[bufferIndex];
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
}

// Try to encode the pending frame if the buffer is mapped
bool VideoExporter::tryEncodePendingFrame() {
    if (!m_hasPendingFrame || m_pendingBuffer < 0) {
        return false;
    }

    // Check our tracked state instead of wgpuBufferGetMapState (which is not implemented)
    if (!m_bufferMapped[m_pendingBuffer]) {
        return false;  // Buffer not ready yet
    }

    WGPUBuffer buffer = m_readbackBuffers[m_pendingBuffer];

    // Buffer is ready - encode the frame
    const void* data = wgpuBufferGetConstMappedRange(buffer, 0, m_pendingBytesPerRow * m_pendingHeight);
    if (data) {
        // We need to temporarily set the legacy buffer to the pending buffer for encodeFrame
        WGPUBuffer savedBuffer = m_readbackBuffer;
        m_readbackBuffer = buffer;

        encodeFrame(m_pendingWidth, m_pendingHeight, m_pendingBytesPerRow, m_pendingBytesPerPixel);

        m_readbackBuffer = savedBuffer;

        // Only unmap if we successfully got mapped data
        wgpuBufferUnmap(buffer);
    }
    m_bufferMapped[m_pendingBuffer] = false;  // Mark as unmapped

    m_hasPendingFrame = false;
    m_pendingBuffer = -1;
    return true;
}

void VideoExporter::captureFrame(WGPUDevice device, WGPUQueue queue, WGPUTexture texture) {
    if (!m_recording || !texture) return;

    m_device = device;  // Cache for async polling

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

    // Create or resize readback buffers if needed
    if (m_bufferSize < requiredSize) {
        for (int i = 0; i < NUM_READBACK_BUFFERS; i++) {
            if (m_readbackBuffers[i]) {
                wgpuBufferRelease(m_readbackBuffers[i]);
            }

            WGPUBufferDescriptor bufferDesc = {};
            bufferDesc.size = requiredSize;
            bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            bufferDesc.mappedAtCreation = false;

            m_readbackBuffers[i] = wgpuDeviceCreateBuffer(device, &bufferDesc);
            m_bufferMapped[i] = false;  // Reset mapped state
        }
        m_bufferSize = requiredSize;
        m_currentBuffer = 0;
        m_pendingBuffer = -1;
        m_hasPendingFrame = false;
    }

    // Poll device (non-blocking) to check if previous frame's map completed
    wgpuDevicePoll(device, false, nullptr);

    // Try to encode any pending frame
    if (m_hasPendingFrame) {
        if (!tryEncodePendingFrame()) {
            // Previous frame still not ready - we need to wait (this is the slow path)
            // This happens when GPU is slower than expected
            int pollCount = 0;
            while (!tryEncodePendingFrame() && pollCount < 100) {
                wgpuDevicePoll(device, true, nullptr);
                pollCount++;
            }
            if (pollCount >= 100) {
                printf("[VideoExporter] Warning: Timeout waiting for previous frame\n");
                m_hasPendingFrame = false;
                m_pendingBuffer = -1;
            }
        }
    }

    // Now submit the copy for the current frame
    int writeBuffer = m_currentBuffer;
    submitCopyCommand(device, queue, texture, writeBuffer, texWidth, texHeight, bytesPerRow, bytesPerPixel);

    // Start async map for this buffer
    // Pass pointer to our mapped flag via userdata
    WGPUBufferMapCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView message,
                               void* userdata1, void* userdata2) {
        if (status == WGPUMapAsyncStatus_Success && userdata1) {
            // Set the flag to indicate buffer is now mapped
            bool* mappedFlag = static_cast<bool*>(userdata1);
            *mappedFlag = true;
        }
    };
    callbackInfo.userdata1 = &m_bufferMapped[writeBuffer];
    callbackInfo.userdata2 = nullptr;

    wgpuBufferMapAsync(m_readbackBuffers[writeBuffer], WGPUMapMode_Read, 0, requiredSize, callbackInfo);

    // Mark this buffer as pending
    m_pendingBuffer = writeBuffer;
    m_hasPendingFrame = true;
    m_pendingWidth = texWidth;
    m_pendingHeight = texHeight;
    m_pendingBytesPerRow = bytesPerRow;
    m_pendingBytesPerPixel = bytesPerPixel;

    // Swap to the other buffer for next frame
    m_currentBuffer = (m_currentBuffer + 1) % NUM_READBACK_BUFFERS;
}

void VideoExporter::encodeFrame(uint32_t width, uint32_t height, uint32_t bytesPerRow, uint32_t bytesPerPixel) {
    if (!m_recording || !m_readbackBuffer) {
        return;
    }

    const void* data = wgpuBufferGetConstMappedRange(m_readbackBuffer, 0, bytesPerRow * height);
    if (!data) {
        // Don't unmap here - caller handles unmapping
        return;
    }

    std::lock_guard<std::mutex> lock(m_impl->mutex);

    // Check if input is ready
    if (![m_impl->videoInput isReadyForMoreMediaData]) {
        // Don't unmap here - caller handles unmapping
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
            // Don't unmap here - caller handles unmapping
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
    // Don't unmap here - caller handles unmapping

    // Append to video
    CMTime presentationTime = m_impl->currentTime;
    if (![m_impl->pixelBufferAdaptor appendPixelBuffer:pixelBuffer
                                  withPresentationTime:presentationTime]) {
        NSLog(@"Failed to append pixel buffer at frame %d", m_frameCount);
    }

    // Debug: log every 60th frame (once per second at 60fps)
    if (m_frameCount % 60 == 0) {
        float videoTime = CMTimeGetSeconds(m_impl->currentTime);
        float audioTime = CMTimeGetSeconds(m_impl->audioTime);
        printf("[VideoExporter] Frame %d: video=%.2fs, audio=%.2fs, diff=%.3fs\n",
               m_frameCount, videoTime, audioTime, videoTime - audioTime);
    }

    CVPixelBufferRelease(pixelBuffer);

    m_impl->currentTime = CMTimeAdd(m_impl->currentTime, m_impl->frameDuration);
    m_frameCount++;
}

void VideoExporter::stop() {
    if (!m_recording) return;

    m_recording = false;

    // Flush any pending frame from async readback
    if (m_hasPendingFrame && m_device) {
        printf("[VideoExporter] Flushing pending frame...\n");
        int pollCount = 0;
        while (!tryEncodePendingFrame() && pollCount < 200) {
            wgpuDevicePoll(m_device, true, nullptr);
            pollCount++;
        }
        if (pollCount >= 200) {
            printf("[VideoExporter] Warning: Timeout flushing pending frame\n");
        }
        m_hasPendingFrame = false;
        m_pendingBuffer = -1;
    }

    // Check if already finalized (use mutex briefly)
    {
        std::lock_guard<std::mutex> lock(m_impl->mutex);
        if (m_impl->finalized) return;
        m_impl->finalized = true;
    }

    // Calculate timing info for diagnostics
    float videoSeconds = (m_fps > 0) ? static_cast<float>(m_frameCount) / m_fps : 0;
    float audioSeconds = (m_audioSampleRate > 0) ? static_cast<float>(m_audioFrameCount) / m_audioSampleRate : 0;

    printf("[VideoExporter] Stopping recording...\n");
    printf("[VideoExporter] Video: %d frames = %.2f sec, Audio: %llu frames = %.2f sec\n",
           m_frameCount, videoSeconds, m_audioFrameCount, audioSeconds);

    // Warning: if audio was enabled but no audio was written, this could cause issues
    if (m_audioEnabled && m_audioFrameCount == 0) {
        printf("[VideoExporter] WARNING: Audio enabled but no audio samples written!\n");
    }

    // Check asset writer status before marking finished
    AVAssetWriterStatus statusBefore = m_impl->assetWriter.status;
    printf("[VideoExporter] Writer status before finish: %ld\n", (long)statusBefore);

    if (statusBefore == AVAssetWriterStatusFailed) {
        NSError* error = m_impl->assetWriter.error;
        m_error = std::string("Writer already failed: ") +
                  (error ? [[error localizedDescription] UTF8String] : "unknown error");
        printf("[VideoExporter] Error: %s\n", m_error.c_str());
        goto cleanup;
    }

    // Stop audio writer
    if (m_audioEnabled) {
        m_impl->audioWriterRunning = false;

        // Drain remaining audio queue before marking finished
        int drainedCount = 0;
        while (true) {
            // Wait for audio input to be ready (with timeout)
            int waitAttempts = 0;
            while (![m_impl->audioInput isReadyForMoreMediaData] && waitAttempts < 100) {
                usleep(10000);  // 10ms
                waitAttempts++;
            }

            if (![m_impl->audioInput isReadyForMoreMediaData]) {
                printf("[VideoExporter] Audio input not ready after drain wait, stopping\n");
                break;
            }

            AudioQueueEntry entry;
            {
                std::lock_guard<std::mutex> lock(m_impl->audioMutex);
                if (m_impl->audioEntries.empty()) {
                    break;
                }
                entry = std::move(m_impl->audioEntries.front());
                m_impl->audioEntries.pop();
            }

            writeAudioEntryImpl(m_impl->audioInput, m_impl->assetWriter, entry, m_audioChannels, m_audioSampleRate);
            drainedCount++;
        }

        size_t remainingEntries = 0;
        {
            std::lock_guard<std::mutex> lock(m_impl->audioMutex);
            remainingEntries = m_impl->audioEntries.size();
        }
        printf("[VideoExporter] Drained %d audio entries, %zu remaining\n", drainedCount, remainingEntries);
    }

    // Mark inputs as finished (outside mutex to avoid deadlock)
    if (m_impl->videoInput) {
        [m_impl->videoInput markAsFinished];
    }

    if (m_audioEnabled && m_impl->audioInput) {
        [m_impl->audioInput markAsFinished];
    }

    {
        // Finish writing with timeout
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        __block AVAssetWriterStatus completionStatus = AVAssetWriterStatusUnknown;

        [m_impl->assetWriter finishWritingWithCompletionHandler:^{
            completionStatus = m_impl->assetWriter.status;
            dispatch_semaphore_signal(semaphore);
        }];

        // Poll every 500ms for up to 10 seconds
        int attempts = 0;
        const int maxAttempts = 20;
        long result = -1;

        while (attempts < maxAttempts) {
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC);
            result = dispatch_semaphore_wait(semaphore, timeout);

            if (result == 0) {
                // Successfully completed
                break;
            }

            // Still waiting - check status
            AVAssetWriterStatus statusNow = m_impl->assetWriter.status;
            printf("[VideoExporter] Waiting... attempt %d/20, status=%ld\n", attempts + 1, (long)statusNow);

            if (statusNow == AVAssetWriterStatusFailed) {
                NSError* error = m_impl->assetWriter.error;
                printf("[VideoExporter] Writer failed: %s\n",
                       error ? [[error localizedDescription] UTF8String] : "unknown");
                break;
            }

            attempts++;
        }

        if (result != 0) {
            // Timeout or error
            AVAssetWriterStatus statusNow = m_impl->assetWriter.status;
            printf("[VideoExporter] Warning: Timed out waiting for finish (10s), status=%ld\n", (long)statusNow);
            if (statusNow == AVAssetWriterStatusFailed) {
                NSError* error = m_impl->assetWriter.error;
                m_error = std::string("Writer failed: ") +
                          (error ? [[error localizedDescription] UTF8String] : "unknown");
            } else {
                m_error = "Timed out waiting for video to finish writing";
            }
            printf("[VideoExporter] Error: %s\n", m_error.c_str());

            // Force cancel the writer
            [m_impl->assetWriter cancelWriting];
        } else {
            AVAssetWriterStatus status = completionStatus;
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
        }
    }

cleanup:
    // Cleanup
    m_impl->audioWriterRunning = false;
    m_impl->assetWriter = nil;
    m_impl->videoInput = nil;
    m_impl->audioInput = nil;
    m_impl->pixelBufferAdaptor = nil;
    if (m_impl->encodingQueue) {
        m_impl->encodingQueue = nil;
    }
    m_audioEnabled = false;
    m_audioFrameCount = 0;
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
            printf("[VideoExporter::saveSnapshot] Warning: Unknown texture format %d, assuming 4 bytes/pixel\n", format);
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
                // Simple half-float to float conversion (approximate)
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

bool VideoExporter::startWithAudio(const std::string& path, int width, int height,
                                    float fps, ExportCodec codec,
                                    uint32_t audioSampleRate, uint32_t audioChannels) {
    m_error = "Video export not implemented on this platform";
    return false;
}

void VideoExporter::captureFrame(WGPUDevice device, WGPUQueue queue, WGPUTexture texture) {}
void VideoExporter::pushAudioSamples(const float* samples, uint32_t frameCount) {}
void VideoExporter::stop() {}
float VideoExporter::duration() const { return 0; }
void VideoExporter::encodeFrame(uint32_t width, uint32_t height, uint32_t bytesPerRow, uint32_t bytesPerPixel) {}

std::string VideoExporter::generateOutputPath(const std::string& directory, ExportCodec codec) {
    return directory + "/output.mov";
}

bool VideoExporter::saveSnapshot(WGPUDevice device, WGPUQueue queue,
                                  WGPUTexture texture, const std::string& outputPath) {
    // Not implemented on this platform
    return false;
}

} // namespace vivid

#endif // __APPLE__
