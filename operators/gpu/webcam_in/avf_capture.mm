#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>

#include "avf_capture.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

// =============================================================================
// Obj-C delegate — receives camera frames on a serial dispatch queue
// =============================================================================

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
    if (!pixelBuffer) return;
    std::lock_guard<std::mutex> lock(*_frameMutex);
    if (_latestFrame) CVPixelBufferRelease(_latestFrame);
    CVPixelBufferRetain(pixelBuffer);
    _latestFrame = pixelBuffer;
    _hasNewFrame->store(true);
}

- (void)dealloc {
    if (_latestFrame) {
        CVPixelBufferRelease(_latestFrame);
        _latestFrame = nil;
    }
}

@end

// =============================================================================
// AVFCapture::Impl
// =============================================================================

struct AVFCapture::Impl {
    AVCaptureSession*          session  = nil;
    AVCaptureDeviceInput*      input    = nil;
    AVCaptureVideoDataOutput*  output   = nil;
    VividWebcamDelegate*       delegate = nil;
    dispatch_queue_t           queue    = nil;

    std::atomic<bool> has_new_frame{false};
    std::mutex        frame_mutex;

    // Decoded pixel buffer (BGRA8)
    std::vector<uint8_t> pixels;
    uint32_t             frame_w = 0;
    uint32_t             frame_h = 0;
    std::string          dev_name;

    void cleanup() {
        @autoreleasepool {
            if (session) {
                if (session.isRunning) [session stopRunning];
                session = nil;
            }
            if (delegate) {
                std::lock_guard<std::mutex> lock(frame_mutex);
                if (delegate.latestFrame) {
                    CVPixelBufferRelease(delegate.latestFrame);
                    delegate.latestFrame = nil;
                }
                delegate = nil;
            }
            output  = nil;
            input   = nil;
            queue   = nil;
            has_new_frame.store(false);
        }
    }
};

// =============================================================================
// AVFCapture public API
// =============================================================================

AVFCapture::AVFCapture() : impl_(std::make_unique<Impl>()) {}

AVFCapture::~AVFCapture() { close(); }

bool AVFCapture::open(int device_index, int width, int height, float fps) {
    close();

    @autoreleasepool {
        @try {

        // Check camera authorization (inside @try — an improperly signed bundle
        // can cause macOS TCC to throw an NSException here)
        AVAuthorizationStatus auth = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
        if (auth == AVAuthorizationStatusDenied || auth == AVAuthorizationStatusRestricted) {
            std::fprintf(stderr, "[webcam_in] Camera access denied. Grant camera permission in System Settings > Privacy & Security > Camera.\n");
            return false;
        }
        if (auth == AVAuthorizationStatusNotDetermined) {
            // Request access synchronously via semaphore
            __block BOOL granted = NO;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL g) {
                granted = g;
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            if (!granted) {
                std::fprintf(stderr, "[webcam_in] Camera access not granted.\n");
                return false;
            }
        }

        // Enumerate cameras
        AVCaptureDeviceDiscoverySession* discovery = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[
                AVCaptureDeviceTypeBuiltInWideAngleCamera,
                AVCaptureDeviceTypeExternal]
            mediaType:AVMediaTypeVideo
            position:AVCaptureDevicePositionUnspecified];

        NSArray<AVCaptureDevice*>* devices = discovery.devices;
        if (device_index < 0 || device_index >= (int)devices.count) {
            std::fprintf(stderr, "[webcam_in] Invalid camera index %d (found %lu)\n",
                         device_index, (unsigned long)devices.count);
            return false;
        }

        AVCaptureDevice* device = devices[device_index];
        impl_->dev_name = std::string([device.localizedName UTF8String]);

        // Configure format — score by proximity to requested resolution
        NSError* error = nil;
        [device lockForConfiguration:&error];
        if (!error) {
            AVCaptureDeviceFormat* best_format = nil;
            int best_score = -1;

            for (AVCaptureDeviceFormat* fmt in device.formats) {
                CMVideoDimensions dims =
                    CMVideoFormatDescriptionGetDimensions(fmt.formatDescription);

                // Prefer formats that meet requested size without being too large
                int score = 0;
                if (dims.width >= width && dims.height >= height)
                    score += 100000;
                // Penalise excess resolution
                int excess = (dims.width - width) + (dims.height - height);
                score -= std::abs(excess);

                // Require a usable frame rate
                bool has_fps = false;
                for (AVFrameRateRange* range in fmt.videoSupportedFrameRateRanges) {
                    if (range.maxFrameRate >= fps) { has_fps = true; break; }
                }
                if (!has_fps) continue;

                if (score > best_score) {
                    best_format = fmt;
                    best_score  = score;
                }
            }

            if (best_format) {
                device.activeFormat = best_format;
                // Set frame rate only if within a supported range
                CMTime duration = CMTimeMake(1, (int32_t)fps);
                for (AVFrameRateRange* range in best_format.videoSupportedFrameRateRanges) {
                    if (fps >= range.minFrameRate && fps <= range.maxFrameRate) {
                        device.activeVideoMinFrameDuration = duration;
                        device.activeVideoMaxFrameDuration = duration;
                        break;
                    }
                }
            }
            [device unlockForConfiguration];
        }

        // Create session
        impl_->session = [[AVCaptureSession alloc] init];

        // Input
        impl_->input = [[AVCaptureDeviceInput alloc] initWithDevice:device error:&error];
        if (error || !impl_->input) {
            std::fprintf(stderr, "[webcam_in] Failed to create input: %s\n",
                         error ? [[error localizedDescription] UTF8String] : "unknown");
            impl_->cleanup();
            return false;
        }
        if (![impl_->session canAddInput:impl_->input]) {
            std::fprintf(stderr, "[webcam_in] Cannot add input to session\n");
            impl_->cleanup();
            return false;
        }
        [impl_->session addInput:impl_->input];

        // Output — request BGRA pixels
        impl_->output = [[AVCaptureVideoDataOutput alloc] init];
        impl_->output.videoSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)
        };
        impl_->output.alwaysDiscardsLateVideoFrames = YES;

        // Delegate + serial queue
        impl_->delegate = [[VividWebcamDelegate alloc] init];
        impl_->delegate.hasNewFrame = &impl_->has_new_frame;
        impl_->delegate.frameMutex  = &impl_->frame_mutex;
        impl_->delegate.latestFrame = nil;

        impl_->queue = dispatch_queue_create("com.vivid.webcam", DISPATCH_QUEUE_SERIAL);
        [impl_->output setSampleBufferDelegate:impl_->delegate queue:impl_->queue];

        if (![impl_->session canAddOutput:impl_->output]) {
            std::fprintf(stderr, "[webcam_in] Cannot add output to session\n");
            impl_->cleanup();
            return false;
        }
        [impl_->session addOutput:impl_->output];

        // Mirror front-facing cameras
        AVCaptureConnection* conn =
            [impl_->output connectionWithMediaType:AVMediaTypeVideo];
        if (conn && device.position == AVCaptureDevicePositionFront &&
            conn.isVideoMirroringSupported) {
            conn.videoMirrored = YES;
        }

        // Read actual dimensions from active format
        CMVideoDimensions dims =
            CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription);
        impl_->frame_w = static_cast<uint32_t>(dims.width);
        impl_->frame_h = static_cast<uint32_t>(dims.height);
        impl_->pixels.resize(impl_->frame_w * impl_->frame_h * 4);

        // Start
        [impl_->session startRunning];

        std::fprintf(stderr, "[webcam_in] Opened: %s (%ux%u)\n",
                     impl_->dev_name.c_str(), impl_->frame_w, impl_->frame_h);
        return true;

        } @catch (NSException* e) {
            std::fprintf(stderr, "[webcam_in] ObjC exception: %s — %s\n",
                         [[e name] UTF8String], [[e reason] UTF8String]);
            impl_->cleanup();
            return false;
        }
    }
}

void AVFCapture::close() {
    impl_->cleanup();
    impl_->pixels.clear();
    impl_->frame_w = 0;
    impl_->frame_h = 0;
    impl_->dev_name.clear();
}

bool AVFCapture::is_open() const {
    return impl_->session != nil;
}

void AVFCapture::stop() {
    if (impl_->session && impl_->session.isRunning)
        [impl_->session stopRunning];
}

void AVFCapture::start() {
    if (impl_->session && !impl_->session.isRunning)
        [impl_->session startRunning];
}

bool AVFCapture::update() {
    if (!impl_->has_new_frame.load()) return false;

    CVPixelBufferRef pb = nullptr;
    {
        std::lock_guard<std::mutex> lock(impl_->frame_mutex);
        if (!impl_->delegate.latestFrame) return false;
        pb = impl_->delegate.latestFrame;
        CVPixelBufferRetain(pb);
        impl_->has_new_frame.store(false);
    }

    CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);

    size_t fw = CVPixelBufferGetWidth(pb);
    size_t fh = CVPixelBufferGetHeight(pb);
    size_t stride = CVPixelBufferGetBytesPerRow(pb);
    const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pb));

    // Handle resolution changes (Continuity Camera, etc.)
    if (fw != impl_->frame_w || fh != impl_->frame_h) {
        impl_->frame_w = static_cast<uint32_t>(fw);
        impl_->frame_h = static_cast<uint32_t>(fh);
        impl_->pixels.resize(impl_->frame_w * impl_->frame_h * 4);
        std::fprintf(stderr, "[webcam_in] Resolution changed to %ux%u\n",
                     impl_->frame_w, impl_->frame_h);
    }

    if (base) {
        // Copy row-by-row (CVPixelBuffer stride may differ from width*4)
        size_t dst_stride = static_cast<size_t>(impl_->frame_w) * 4;
        uint8_t* dst = impl_->pixels.data();
        for (uint32_t y = 0; y < impl_->frame_h; ++y) {
            std::memcpy(dst + y * dst_stride, base + y * stride, dst_stride);
        }
    }

    CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferRelease(pb);
    return true;
}

const uint8_t* AVFCapture::pixel_data() const {
    return impl_->pixels.data();
}

uint32_t AVFCapture::width() const { return impl_->frame_w; }
uint32_t AVFCapture::height() const { return impl_->frame_h; }
std::string AVFCapture::device_name() const { return impl_->dev_name; }

// Factory
std::unique_ptr<CaptureSource> create_avf_capture() {
    return std::make_unique<AVFCapture>();
}

// Camera enumeration
std::vector<CameraInfo> enumerate_cameras() {
    std::vector<CameraInfo> result;
    @autoreleasepool {
        @try {
            AVCaptureDeviceDiscoverySession* discovery = [AVCaptureDeviceDiscoverySession
                discoverySessionWithDeviceTypes:@[
                    AVCaptureDeviceTypeBuiltInWideAngleCamera,
                    AVCaptureDeviceTypeExternal]
                mediaType:AVMediaTypeVideo
                position:AVCaptureDevicePositionUnspecified];

            AVCaptureDevice* defaultDev = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
            for (AVCaptureDevice* dev in discovery.devices) {
                CameraInfo info;
                info.device_id = std::string([dev.uniqueID UTF8String]);
                info.name = std::string([dev.localizedName UTF8String]);
                info.is_default = defaultDev && [dev.uniqueID isEqualToString:defaultDev.uniqueID];
                result.push_back(std::move(info));
            }
        } @catch (NSException* e) {
            std::fprintf(stderr, "[webcam_in] ObjC exception enumerating cameras: %s — %s\n",
                         [[e name] UTF8String], [[e reason] UTF8String]);
        }
    }
    return result;
}
