// AVFoundation Webcam Capture - macOS camera capture via AVCaptureSession
// Captures frames to RGBA pixels and uploads to GPU texture

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <vivid/video/avf_webcam.h>
#include <vivid/context.h>

#include <iostream>
#include <mutex>
#include <atomic>

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Objective-C delegate for receiving camera frames
@interface VividWebcamDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) CVPixelBufferRef latestFrame;
@property (nonatomic, assign) std::atomic<bool>* hasNewFrame;
@property (nonatomic, assign) std::mutex* frameMutex;
@end

@implementation VividWebcamDelegate

- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (pixelBuffer) {
        std::lock_guard<std::mutex> lock(*_frameMutex);
        if (_latestFrame) {
            CVPixelBufferRelease(_latestFrame);
        }
        CVPixelBufferRetain(pixelBuffer);
        _latestFrame = pixelBuffer;
        _hasNewFrame->store(true);
    }
}

- (void)dealloc {
    if (_latestFrame) {
        CVPixelBufferRelease(_latestFrame);
        _latestFrame = nil;
    }
}

@end

namespace vivid::video {

struct AVFWebcam::Impl {
    AVCaptureSession* session = nil;
    AVCaptureDeviceInput* input = nil;
    AVCaptureVideoDataOutput* output = nil;
    VividWebcamDelegate* delegate = nil;
    dispatch_queue_t queue = nil;

    std::atomic<bool> hasNewFrame{false};
    std::mutex frameMutex;

    void cleanup() {
        if (session) {
            if (session.isRunning) {
                [session stopRunning];
            }
            [session release];
            session = nil;
        }
        if (delegate) {
            std::lock_guard<std::mutex> lock(frameMutex);
            if (delegate.latestFrame) {
                CVPixelBufferRelease(delegate.latestFrame);
                delegate.latestFrame = nil;
            }
            [delegate release];
            delegate = nil;
        }
        if (output) {
            [output release];
            output = nil;
        }
        if (input) {
            [input release];
            input = nil;
        }
        hasNewFrame.store(false);
    }
};

AVFWebcam::AVFWebcam() : impl_(std::make_unique<Impl>()) {}

AVFWebcam::~AVFWebcam() {
    close();
}

std::vector<CameraDevice> AVFWebcam::enumerateDevices() {
    std::vector<CameraDevice> devices;

    @autoreleasepool {
        AVCaptureDeviceDiscoverySession* discoverySession = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera,
                                              AVCaptureDeviceTypeExternal]
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionUnspecified];

        AVCaptureDevice* defaultDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        NSString* defaultId = defaultDevice ? defaultDevice.uniqueID : nil;

        for (AVCaptureDevice* device in discoverySession.devices) {
            CameraDevice info;
            info.deviceId = std::string([device.uniqueID UTF8String]);
            info.name = std::string([device.localizedName UTF8String]);
            info.isDefault = defaultId && [device.uniqueID isEqualToString:defaultId];
            devices.push_back(info);
        }
    }

    return devices;
}

void AVFWebcam::createTexture() {
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }
    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }

    WGPUTextureDescriptor desc = {};
    desc.label = toStringView("WebcamFrame");
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &desc);
    if (!texture_) {
        std::cerr << "[AVFWebcam] Failed to create texture" << std::endl;
        return;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = toStringView("WebcamFrameView");
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
}

bool AVFWebcam::open(Context& ctx, int width, int height, float fps) {
    @autoreleasepool {
        AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        if (!device) {
            std::cerr << "[AVFWebcam] No default camera found" << std::endl;
            return false;
        }

        close();
        device_ = ctx.device();
        queue_ = ctx.queue();

        return setupCapture(device, width, height, fps);
    }
}

bool AVFWebcam::open(Context& ctx, const std::string& deviceId, int width, int height, float fps) {
    @autoreleasepool {
        NSString* nsDeviceId = [NSString stringWithUTF8String:deviceId.c_str()];
        AVCaptureDevice* device = [AVCaptureDevice deviceWithUniqueID:nsDeviceId];
        if (!device) {
            std::cerr << "[AVFWebcam] Device not found: " << deviceId << std::endl;
            return false;
        }

        close();
        device_ = ctx.device();
        queue_ = ctx.queue();

        return setupCapture(device, width, height, fps);
    }
}

bool AVFWebcam::openByIndex(Context& ctx, int index, int width, int height, float fps) {
    auto devices = enumerateDevices();
    if (index < 0 || index >= static_cast<int>(devices.size())) {
        std::cerr << "[AVFWebcam] Invalid camera index: " << index << std::endl;
        return false;
    }
    return open(ctx, devices[index].deviceId, width, height, fps);
}

bool AVFWebcam::setupCapture(void* devicePtr, int requestedWidth, int requestedHeight, float requestedFps) {
    AVCaptureDevice* device = (__bridge AVCaptureDevice*)devicePtr;

    @autoreleasepool {
        NSError* error = nil;

        // Create session
        impl_->session = [[AVCaptureSession alloc] init];

        // Configure format and frame rate
        [device lockForConfiguration:&error];
        if (!error) {
            AVCaptureDeviceFormat* bestFormat = nil;
            AVFrameRateRange* bestRange = nil;
            int bestScore = -1;

            for (AVCaptureDeviceFormat* format in device.formats) {
                CMFormatDescriptionRef formatDesc = format.formatDescription;
                CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(formatDesc);

                // Score: prefer formats close to requested size
                int resScore = dims.width * dims.height;
                if (dims.width >= requestedWidth && dims.height >= requestedHeight) {
                    resScore *= 2;  // Bonus for meeting requirements
                }
                if (dims.width > requestedWidth * 1.5 || dims.height > requestedHeight * 1.5) {
                    resScore /= 4;  // Penalty for oversized
                }

                for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges) {
                    if (range.maxFrameRate < 15) continue;

                    int score = resScore;
                    if (range.maxFrameRate >= requestedFps) {
                        score += 10000;
                    }

                    if (score > bestScore) {
                        bestFormat = format;
                        bestRange = range;
                        bestScore = score;
                    }
                }
            }

            if (bestFormat && bestRange) {
                device.activeFormat = bestFormat;
                device.activeVideoMinFrameDuration = bestRange.minFrameDuration;
                device.activeVideoMaxFrameDuration = bestRange.maxFrameDuration;
            }
            [device unlockForConfiguration];
        }

        // Create input
        impl_->input = [[AVCaptureDeviceInput alloc] initWithDevice:device error:&error];
        if (error || !impl_->input) {
            std::cerr << "[AVFWebcam] Failed to create input: "
                      << [[error localizedDescription] UTF8String] << std::endl;
            impl_->cleanup();
            return false;
        }

        if (![impl_->session canAddInput:impl_->input]) {
            std::cerr << "[AVFWebcam] Cannot add input to session" << std::endl;
            impl_->cleanup();
            return false;
        }
        [impl_->session addInput:impl_->input];

        // Create output - request BGRA (we'll convert to RGBA)
        impl_->output = [[AVCaptureVideoDataOutput alloc] init];
        impl_->output.videoSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };
        impl_->output.alwaysDiscardsLateVideoFrames = YES;

        // Create delegate
        impl_->delegate = [[VividWebcamDelegate alloc] init];
        impl_->delegate.hasNewFrame = &impl_->hasNewFrame;
        impl_->delegate.frameMutex = &impl_->frameMutex;
        impl_->delegate.latestFrame = nil;

        // Set up dispatch queue
        dispatch_queue_t captureQueue = dispatch_queue_create("com.vivid.webcam", DISPATCH_QUEUE_SERIAL);
        [impl_->output setSampleBufferDelegate:impl_->delegate queue:captureQueue];

        if (![impl_->session canAddOutput:impl_->output]) {
            std::cerr << "[AVFWebcam] Cannot add output to session" << std::endl;
            impl_->cleanup();
            return false;
        }
        [impl_->session addOutput:impl_->output];

        // Mirror front camera
        AVCaptureConnection* connection = [impl_->output connectionWithMediaType:AVMediaTypeVideo];
        if (connection && device.position == AVCaptureDevicePositionFront &&
            connection.isVideoMirroringSupported) {
            connection.videoMirrored = YES;
        }

        // Get actual dimensions
        CMFormatDescriptionRef formatDesc = device.activeFormat.formatDescription;
        CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(formatDesc);
        width_ = dims.width;
        height_ = dims.height;

        // Get actual frame rate
        CMTime minDuration = device.activeVideoMinFrameDuration;
        if (minDuration.timescale > 0 && minDuration.value > 0) {
            frameRate_ = static_cast<float>(minDuration.timescale) / minDuration.value;
        } else {
            frameRate_ = requestedFps;
        }

        deviceName_ = std::string([device.localizedName UTF8String]);

        // Allocate pixel buffer
        pixelBuffer_.resize(width_ * height_ * 4);

        // Create GPU texture
        createTexture();
        if (!texture_) {
            close();
            return false;
        }

        // Start capture automatically
        startCapture();

        std::cout << "[AVFWebcam] Opened: " << deviceName_
                  << " (" << width_ << "x" << height_
                  << " @ " << frameRate_ << "fps)" << std::endl;

        return true;
    }
}

void AVFWebcam::close() {
    stopCapture();
    impl_->cleanup();

    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }

    width_ = 0;
    height_ = 0;
    frameRate_ = 30.0f;
    deviceName_.clear();
    isCapturing_ = false;
}

bool AVFWebcam::isOpen() const {
    return impl_->session != nil;
}

bool AVFWebcam::startCapture() {
    if (!impl_->session) return false;

    @autoreleasepool {
        [impl_->session startRunning];
        isCapturing_ = impl_->session.isRunning;
        if (isCapturing_) {
            std::cout << "[AVFWebcam] Started capture" << std::endl;
        }
        return isCapturing_;
    }
}

void AVFWebcam::stopCapture() {
    if (!impl_->session) return;

    @autoreleasepool {
        if (impl_->session.isRunning) {
            [impl_->session stopRunning];
            std::cout << "[AVFWebcam] Stopped capture" << std::endl;
        }
        isCapturing_ = false;
    }
}

bool AVFWebcam::update(Context& ctx) {
    if (!impl_->hasNewFrame.load()) {
        return false;
    }

    CVPixelBufferRef pixelBuffer = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->frameMutex);
        if (!impl_->delegate.latestFrame) {
            return false;
        }
        pixelBuffer = impl_->delegate.latestFrame;
        CVPixelBufferRetain(pixelBuffer);
        impl_->hasNewFrame.store(false);
    }

    // Lock and copy pixels
    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);

    if (baseAddress) {
        // Convert BGRA -> RGBA
        uint8_t* src = static_cast<uint8_t*>(baseAddress);
        uint8_t* dst = pixelBuffer_.data();
        size_t expectedStride = width * 4;

        for (size_t y = 0; y < height; y++) {
            for (size_t x = 0; x < width; x++) {
                size_t srcIdx = x * 4;
                size_t dstIdx = x * 4;
                dst[dstIdx + 0] = src[srcIdx + 2];  // R <- B
                dst[dstIdx + 1] = src[srcIdx + 1];  // G <- G
                dst[dstIdx + 2] = src[srcIdx + 0];  // B <- R
                dst[dstIdx + 3] = src[srcIdx + 3];  // A <- A
            }
            src += bytesPerRow;
            dst += expectedStride;
        }

        // Upload to GPU
        WGPUTexelCopyTextureInfo destination = {};
        destination.texture = texture_;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout dataLayout = {};
        dataLayout.offset = 0;
        dataLayout.bytesPerRow = static_cast<uint32_t>(width * 4);
        dataLayout.rowsPerImage = static_cast<uint32_t>(height);

        WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

        wgpuQueueWriteTexture(queue_, &destination, pixelBuffer_.data(), pixelBuffer_.size(),
                              &dataLayout, &writeSize);
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferRelease(pixelBuffer);

    return true;
}

} // namespace vivid::video
