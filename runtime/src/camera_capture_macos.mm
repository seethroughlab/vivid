#include "camera_capture.h"
#include "renderer.h"
#include <iostream>
#include <mutex>
#include <atomic>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

// Objective-C delegate class must be at global scope
@interface VividCameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property (nonatomic, assign) CVPixelBufferRef latestFrame;
@property (nonatomic, assign) std::atomic<bool>* hasNewFrame;
@property (nonatomic, assign) std::mutex* frameMutex;
@end

@implementation VividCameraDelegate

- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
    CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (pixelBuffer) {
        std::lock_guard<std::mutex> lock(*_frameMutex);
        // Release old frame
        if (_latestFrame) {
            CVPixelBufferRelease(_latestFrame);
        }
        // Retain new frame
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
    [super dealloc];
}

@end

namespace vivid {

class CameraCaptureMacOS : public CameraCapture {
public:
    CameraCaptureMacOS() = default;
    ~CameraCaptureMacOS() override {
        close();
    }

    std::vector<CameraDeviceInfo> enumerateDevices() override {
        std::vector<CameraDeviceInfo> devices;

        @autoreleasepool {
            // Get all video devices
            AVCaptureDeviceDiscoverySession* discoverySession = [AVCaptureDeviceDiscoverySession
                discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera,
                                                  AVCaptureDeviceTypeExternal]
                                      mediaType:AVMediaTypeVideo
                                       position:AVCaptureDevicePositionUnspecified];

            AVCaptureDevice* defaultDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
            NSString* defaultId = defaultDevice ? defaultDevice.uniqueID : nil;

            for (AVCaptureDevice* device in discoverySession.devices) {
                CameraDeviceInfo info;
                info.deviceId = std::string([device.uniqueID UTF8String]);
                info.name = std::string([device.localizedName UTF8String]);
                info.isDefault = defaultId && [device.uniqueID isEqualToString:defaultId];
                devices.push_back(info);
            }
        }

        return devices;
    }

    std::vector<CameraMode> enumerateModes(const std::string& deviceId = "") override {
        std::vector<CameraMode> modes;

        @autoreleasepool {
            AVCaptureDevice* device = nil;
            if (deviceId.empty()) {
                device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
            } else {
                NSString* nsDeviceId = [NSString stringWithUTF8String:deviceId.c_str()];
                device = [AVCaptureDevice deviceWithUniqueID:nsDeviceId];
            }

            if (!device) {
                return modes;
            }

            // Iterate through all formats
            for (AVCaptureDeviceFormat* format in device.formats) {
                CMFormatDescriptionRef formatDesc = format.formatDescription;
                CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(formatDesc);

                // Get pixel format
                FourCharCode pixelFormat = CMFormatDescriptionGetMediaSubType(formatDesc);
                char fourCC[5] = {
                    static_cast<char>((pixelFormat >> 24) & 0xFF),
                    static_cast<char>((pixelFormat >> 16) & 0xFF),
                    static_cast<char>((pixelFormat >> 8) & 0xFF),
                    static_cast<char>(pixelFormat & 0xFF),
                    '\0'
                };

                // Get frame rate ranges
                for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges) {
                    CameraMode mode;
                    mode.width = dims.width;
                    mode.height = dims.height;
                    mode.minFrameRate = static_cast<float>(range.minFrameRate);
                    mode.maxFrameRate = static_cast<float>(range.maxFrameRate);
                    mode.pixelFormat = std::string(fourCC);
                    modes.push_back(mode);
                }
            }
        }

        return modes;
    }

    bool open(const CameraConfig& config) override {
        @autoreleasepool {
            AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
            if (!device) {
                std::cerr << "[CameraCapture] No default camera found\n";
                return false;
            }
            return openDevice(device, config);
        }
    }

    bool open(const std::string& deviceId, const CameraConfig& config) override {
        @autoreleasepool {
            NSString* nsDeviceId = [NSString stringWithUTF8String:deviceId.c_str()];
            AVCaptureDevice* device = [AVCaptureDevice deviceWithUniqueID:nsDeviceId];
            if (!device) {
                std::cerr << "[CameraCapture] Device not found: " << deviceId << "\n";
                return false;
            }
            return openDevice(device, config);
        }
    }

    bool openByIndex(int index, const CameraConfig& config) override {
        auto devices = enumerateDevices();
        if (index < 0 || index >= static_cast<int>(devices.size())) {
            std::cerr << "[CameraCapture] Invalid camera index: " << index << "\n";
            return false;
        }
        return open(devices[index].deviceId, config);
    }

    void close() override {
        stopCapture();

        @autoreleasepool {
            if (session_) {
                [session_ release];
                session_ = nil;
            }
            if (delegate_) {
                std::lock_guard<std::mutex> lock(frameMutex_);
                if (delegate_.latestFrame) {
                    CVPixelBufferRelease(delegate_.latestFrame);
                    delegate_.latestFrame = nil;
                }
                [delegate_ release];
                delegate_ = nil;
            }
            if (output_) {
                [output_ release];
                output_ = nil;
            }
            if (input_) {
                [input_ release];
                input_ = nil;
            }
        }

        info_ = CameraInfo{};
        hasNewFrame_.store(false);
    }

    bool isOpen() const override {
        return session_ != nil;
    }

    bool startCapture() override {
        if (!session_) return false;

        @autoreleasepool {
            [session_ startRunning];
            info_.isCapturing = session_.isRunning;
            if (info_.isCapturing) {
                std::cout << "[CameraCapture] Started capture\n";
            }
            return info_.isCapturing;
        }
    }

    void stopCapture() override {
        if (!session_) return;

        @autoreleasepool {
            if (session_.isRunning) {
                [session_ stopRunning];
                std::cout << "[CameraCapture] Stopped capture\n";
            }
            info_.isCapturing = false;
        }
    }

    bool isCapturing() const override {
        return info_.isCapturing;
    }

    const CameraInfo& info() const override {
        return info_;
    }

    bool getFrame(Texture& output, Renderer& renderer) override {
        if (!hasNewFrame_.load()) {
            return false;
        }

        CVPixelBufferRef pixelBuffer = nullptr;
        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            if (!delegate_.latestFrame) {
                return false;
            }
            pixelBuffer = delegate_.latestFrame;
            CVPixelBufferRetain(pixelBuffer);
            hasNewFrame_.store(false);
        }

        // Get frame dimensions
        size_t width = CVPixelBufferGetWidth(pixelBuffer);
        size_t height = CVPixelBufferGetHeight(pixelBuffer);

        // Ensure output texture is correct size
        if (!output.valid() || output.width != static_cast<int>(width) ||
            output.height != static_cast<int>(height)) {
            output = renderer.createTexture(static_cast<int>(width), static_cast<int>(height));
        }

        // Lock pixel buffer and copy data
        CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

        void* baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
        OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);

        // Convert to RGBA if needed
        std::vector<uint8_t> rgbaData;
        const uint8_t* srcData = nullptr;

        if (pixelFormat == kCVPixelFormatType_32BGRA) {
            // BGRA -> RGBA conversion
            rgbaData.resize(width * height * 4);
            const uint8_t* src = static_cast<const uint8_t*>(baseAddress);
            for (size_t y = 0; y < height; y++) {
                const uint8_t* row = src + y * bytesPerRow;
                uint8_t* dstRow = rgbaData.data() + y * width * 4;
                for (size_t x = 0; x < width; x++) {
                    dstRow[x * 4 + 0] = row[x * 4 + 2];  // R <- B
                    dstRow[x * 4 + 1] = row[x * 4 + 1];  // G <- G
                    dstRow[x * 4 + 2] = row[x * 4 + 0];  // B <- R
                    dstRow[x * 4 + 3] = row[x * 4 + 3];  // A <- A
                }
            }
            srcData = rgbaData.data();
        } else if (pixelFormat == kCVPixelFormatType_32RGBA) {
            // Already RGBA
            srcData = static_cast<const uint8_t*>(baseAddress);
        } else {
            std::cerr << "[CameraCapture] Unsupported pixel format: " << pixelFormat << "\n";
            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferRelease(pixelBuffer);
            return false;
        }

        // Upload to GPU
        renderer.uploadTexturePixels(output, srcData, static_cast<int>(width), static_cast<int>(height));

        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pixelBuffer);

        return true;
    }

    bool hasNewFrame() const override {
        return hasNewFrame_.load();
    }

private:
    bool openDevice(AVCaptureDevice* device, const CameraConfig& config) {
        close();

        @autoreleasepool {
            NSError* error = nil;

            // Create session
            session_ = [[AVCaptureSession alloc] init];

            // Set session preset based on requested resolution
            if (config.width >= 1920 && config.height >= 1080) {
                if ([session_ canSetSessionPreset:AVCaptureSessionPreset1920x1080]) {
                    session_.sessionPreset = AVCaptureSessionPreset1920x1080;
                }
            } else if (config.width >= 1280 && config.height >= 720) {
                if ([session_ canSetSessionPreset:AVCaptureSessionPreset1280x720]) {
                    session_.sessionPreset = AVCaptureSessionPreset1280x720;
                }
            } else if (config.width >= 640 && config.height >= 480) {
                if ([session_ canSetSessionPreset:AVCaptureSessionPreset640x480]) {
                    session_.sessionPreset = AVCaptureSessionPreset640x480;
                }
            }

            // Configure format and frame rate
            // Strategy: Find the best resolution that supports a reasonable frame rate.
            // Prioritize resolution match over exact frame rate (24-30fps is fine).
            [device lockForConfiguration:&error];
            if (!error) {
                AVCaptureDeviceFormat* bestFormat = nil;
                AVFrameRateRange* bestRange = nil;
                int bestResScore = -1;
                float bestFrameRate = 0;

                for (AVCaptureDeviceFormat* format in device.formats) {
                    CMFormatDescriptionRef formatDesc = format.formatDescription;
                    CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(formatDesc);

                    // Score resolution: prefer formats close to requested size
                    // but not too small. Penalize formats much larger than needed.
                    int resScore = dims.width * dims.height;
                    if (dims.width > config.width * 1.5 || dims.height > config.height * 1.5) {
                        resScore = resScore / 4;  // Heavy penalty for oversized
                    } else if (dims.width > config.width || dims.height > config.height) {
                        resScore = resScore / 2;  // Light penalty for slightly larger
                    }
                    // Bonus for formats that match or exceed requested size
                    if (dims.width >= config.width && dims.height >= config.height) {
                        resScore = resScore * 2;
                    }

                    // Find best frame rate range for this format
                    for (AVFrameRateRange* range in format.videoSupportedFrameRateRanges) {
                        float effectiveRate = range.maxFrameRate;
                        // Only consider if frame rate is usable (at least 15fps)
                        if (effectiveRate < 15) continue;

                        // Combined score: resolution + frame rate bonus
                        int score = resScore;
                        // Small bonus for supporting the exact requested frame rate
                        if (range.maxFrameRate >= config.frameRate) {
                            score += 10000;
                        }

                        if (score > bestResScore ||
                            (score == bestResScore && effectiveRate > bestFrameRate)) {
                            bestFormat = format;
                            bestRange = range;
                            bestResScore = score;
                            bestFrameRate = effectiveRate;
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
            input_ = [[AVCaptureDeviceInput alloc] initWithDevice:device error:&error];
            if (error || !input_) {
                std::cerr << "[CameraCapture] Failed to create input: "
                          << [[error localizedDescription] UTF8String] << "\n";
                close();
                return false;
            }

            if (![session_ canAddInput:input_]) {
                std::cerr << "[CameraCapture] Cannot add input to session\n";
                close();
                return false;
            }
            [session_ addInput:input_];

            // Create output
            output_ = [[AVCaptureVideoDataOutput alloc] init];
            output_.videoSettings = @{
                (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
            };
            output_.alwaysDiscardsLateVideoFrames = YES;

            // Create delegate
            delegate_ = [[VividCameraDelegate alloc] init];
            delegate_.hasNewFrame = &hasNewFrame_;
            delegate_.frameMutex = &frameMutex_;
            delegate_.latestFrame = nil;

            // Set up dispatch queue for frame delivery
            dispatch_queue_t queue = dispatch_queue_create("com.vivid.camera", DISPATCH_QUEUE_SERIAL);
            [output_ setSampleBufferDelegate:delegate_ queue:queue];

            if (![session_ canAddOutput:output_]) {
                std::cerr << "[CameraCapture] Cannot add output to session\n";
                close();
                return false;
            }
            [session_ addOutput:output_];

            // Get actual dimensions from output
            AVCaptureConnection* connection = [output_ connectionWithMediaType:AVMediaTypeVideo];
            if (connection) {
                // Mirror front camera
                if (device.position == AVCaptureDevicePositionFront && connection.isVideoMirroringSupported) {
                    connection.videoMirrored = YES;
                }
            }

            // Update info
            info_.deviceName = std::string([device.localizedName UTF8String]);
            // Report actual frame rate from device
            CMTime minDuration = device.activeVideoMinFrameDuration;
            if (minDuration.timescale > 0 && minDuration.value > 0) {
                info_.frameRate = static_cast<float>(minDuration.timescale) / minDuration.value;
            } else {
                info_.frameRate = config.frameRate;
            }

            // Get actual dimensions from format
            CMFormatDescriptionRef formatDesc = device.activeFormat.formatDescription;
            CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(formatDesc);
            info_.width = dims.width;
            info_.height = dims.height;

            std::cout << "[CameraCapture] Opened: " << info_.deviceName
                      << " (" << info_.width << "x" << info_.height
                      << " @ " << info_.frameRate << "fps)\n";

            return true;
        }
    }

    AVCaptureSession* session_ = nil;
    AVCaptureDeviceInput* input_ = nil;
    AVCaptureVideoDataOutput* output_ = nil;
    VividCameraDelegate* delegate_ = nil;

    CameraInfo info_;
    std::atomic<bool> hasNewFrame_{false};
    mutable std::mutex frameMutex_;
};

// Factory function
std::unique_ptr<CameraCapture> CameraCapture::create() {
    return std::make_unique<CameraCaptureMacOS>();
}

} // namespace vivid
