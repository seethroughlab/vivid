#include "video_loader.h"
#include "renderer.h"
#include <iostream>

#if defined(__APPLE__)

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

namespace vivid {

/**
 * @brief macOS video loader using AVFoundation.
 *
 * Provides hardware-accelerated decode via VideoToolbox
 * and zero-copy GPU upload via IOSurface when possible.
 */
class VideoLoaderMacOS : public VideoLoader {
public:
    VideoLoaderMacOS() = default;
    ~VideoLoaderMacOS() override { close(); }

    bool open(const std::string& path) override {
        close();

        @autoreleasepool {
            NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
            NSURL* url = [NSURL fileURLWithPath:nsPath];

            // Create asset
            asset_ = [AVAsset assetWithURL:url];
            if (!asset_) {
                std::cerr << "[VideoLoaderMacOS] Failed to create asset: " << path << "\n";
                return false;
            }

            // Get video track
            NSArray* videoTracks = [asset_ tracksWithMediaType:AVMediaTypeVideo];
            if ([videoTracks count] == 0) {
                std::cerr << "[VideoLoaderMacOS] No video tracks found: " << path << "\n";
                return false;
            }

            AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];

            // Extract video info
            info_.width = static_cast<int>(videoTrack.naturalSize.width);
            info_.height = static_cast<int>(videoTrack.naturalSize.height);
            info_.duration = CMTimeGetSeconds(asset_.duration);
            info_.frameRate = videoTrack.nominalFrameRate;
            info_.frameCount = static_cast<int64_t>(info_.duration * info_.frameRate);
            info_.hasAudio = [[asset_ tracksWithMediaType:AVMediaTypeAudio] count] > 0;
            info_.codecType = VideoCodecType::Standard;

            // Get codec name from format descriptions
            NSArray* formatDescriptions = videoTrack.formatDescriptions;
            if ([formatDescriptions count] > 0) {
                CMFormatDescriptionRef desc = (__bridge CMFormatDescriptionRef)[formatDescriptions objectAtIndex:0];
                FourCharCode codec = CMFormatDescriptionGetMediaSubType(desc);

                // Check for HAP codecs
                if (codec == 'Hap1') {
                    info_.codecType = VideoCodecType::HAP;
                    info_.codecName = "HAP";
                } else if (codec == 'Hap5') {
                    info_.codecType = VideoCodecType::HAPAlpha;
                    info_.codecName = "HAP Alpha";
                } else if (codec == 'HapY') {
                    info_.codecType = VideoCodecType::HAPQ;
                    info_.codecName = "HAP Q";
                } else if (codec == 'HapM') {
                    info_.codecType = VideoCodecType::HAPQAlpha;
                    info_.codecName = "HAP Q Alpha";
                } else {
                    // Standard codec - get name
                    char codecStr[5] = {
                        static_cast<char>((codec >> 24) & 0xFF),
                        static_cast<char>((codec >> 16) & 0xFF),
                        static_cast<char>((codec >> 8) & 0xFF),
                        static_cast<char>(codec & 0xFF),
                        '\0'
                    };
                    info_.codecName = codecStr;
                }
            }

            // If HAP, we need to use a different decode path (phase 12.2c)
            if (isHAP()) {
                std::cerr << "[VideoLoaderMacOS] HAP codec detected - HAP support not yet implemented\n";
                close();
                return false;
            }

            // Create asset reader for standard codecs
            NSError* error = nil;
            assetReader_ = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
            if (error) {
                std::cerr << "[VideoLoaderMacOS] Failed to create reader: "
                          << [[error localizedDescription] UTF8String] << "\n";
                close();
                return false;
            }

            // Configure output for BGRA pixels (matches WebGPU surface format)
            NSDictionary* outputSettings = @{
                (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
            };

            readerOutput_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack
                                                                       outputSettings:outputSettings];
            readerOutput_.alwaysCopiesSampleData = NO;  // Zero-copy when possible

            if (![assetReader_ canAddOutput:readerOutput_]) {
                std::cerr << "[VideoLoaderMacOS] Cannot add reader output\n";
                close();
                return false;
            }
            [assetReader_ addOutput:readerOutput_];

            // Start reading
            if (![assetReader_ startReading]) {
                std::cerr << "[VideoLoaderMacOS] Failed to start reading: "
                          << [[assetReader_.error localizedDescription] UTF8String] << "\n";
                close();
                return false;
            }

            path_ = path;
            isOpen_ = true;

            std::cout << "[VideoLoaderMacOS] Opened " << path
                      << " (" << info_.width << "x" << info_.height
                      << ", " << info_.frameRate << "fps"
                      << ", " << info_.codecName << ")\n";

            return true;
        }
    }

    void close() override {
        @autoreleasepool {
            if (assetReader_) {
                [assetReader_ cancelReading];
                assetReader_ = nil;
            }
            readerOutput_ = nil;
            asset_ = nil;
            isOpen_ = false;
            currentTime_ = 0;
            currentFrame_ = 0;
            info_ = VideoInfo{};
            path_.clear();
        }
    }

    bool isOpen() const override { return isOpen_; }

    const VideoInfo& info() const override { return info_; }

    bool seek(double timeSeconds) override {
        if (!isOpen_) return false;

        // AVAssetReader doesn't support seeking once started
        // We need to recreate the reader with a time range
        // This is expensive but necessary for frame-accurate seeking

        @autoreleasepool {
            // Cancel current reader
            [assetReader_ cancelReading];

            // Create new reader with time range
            NSError* error = nil;
            assetReader_ = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
            if (error) {
                std::cerr << "[VideoLoaderMacOS] Seek failed - reader creation error\n";
                return false;
            }

            AVAssetTrack* videoTrack = [[asset_ tracksWithMediaType:AVMediaTypeVideo] objectAtIndex:0];

            NSDictionary* outputSettings = @{
                (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
            };

            readerOutput_ = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:videoTrack
                                                                       outputSettings:outputSettings];
            readerOutput_.alwaysCopiesSampleData = NO;

            // Set time range starting from seek position
            CMTime startTime = CMTimeMakeWithSeconds(timeSeconds, 600);
            CMTime duration = CMTimeSubtract(asset_.duration, startTime);
            assetReader_.timeRange = CMTimeRangeMake(startTime, duration);

            [assetReader_ addOutput:readerOutput_];

            if (![assetReader_ startReading]) {
                std::cerr << "[VideoLoaderMacOS] Seek failed - cannot start reading\n";
                return false;
            }

            currentTime_ = timeSeconds;
            currentFrame_ = static_cast<int64_t>(timeSeconds * info_.frameRate);

            return true;
        }
    }

    bool seekToFrame(int64_t frameNumber) override {
        if (info_.frameRate <= 0) return false;
        double time = static_cast<double>(frameNumber) / info_.frameRate;
        return seek(time);
    }

    bool getFrame(Texture& output, Renderer& renderer) override {
        if (!isOpen_) return false;

        @autoreleasepool {
            // Check reader status
            if (assetReader_.status != AVAssetReaderStatusReading) {
                if (assetReader_.status == AVAssetReaderStatusCompleted) {
                    // End of video
                    return false;
                }
                std::cerr << "[VideoLoaderMacOS] Reader not in reading state: "
                          << static_cast<int>(assetReader_.status) << "\n";
                return false;
            }

            // Get next sample buffer
            CMSampleBufferRef sampleBuffer = [readerOutput_ copyNextSampleBuffer];
            if (!sampleBuffer) {
                return false;
            }

            // Get pixel buffer
            CVPixelBufferRef pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (!pixelBuffer) {
                CFRelease(sampleBuffer);
                return false;
            }

            // Get presentation time
            CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            currentTime_ = CMTimeGetSeconds(pts);
            currentFrame_ = static_cast<int64_t>(currentTime_ * info_.frameRate);

            // Lock pixel buffer for reading
            CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

            int width = static_cast<int>(CVPixelBufferGetWidth(pixelBuffer));
            int height = static_cast<int>(CVPixelBufferGetHeight(pixelBuffer));
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
            uint8_t* baseAddress = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixelBuffer));

            // Ensure output texture has correct dimensions
            if (!output.valid() || output.width != width || output.height != height) {
                if (output.valid()) {
                    renderer.destroyTexture(output);
                }
                output = renderer.createTexture(width, height);
            }

            // Upload to GPU texture
            // Note: CVPixelBuffer is BGRA, we need to convert or handle in shader
            // For now, upload as-is and swap in the blit shader
            if (output.valid() && baseAddress) {
                // Handle potential row padding
                if (bytesPerRow == static_cast<size_t>(width * 4)) {
                    // No padding, direct upload
                    renderer.uploadTexturePixels(output, baseAddress, width, height);
                } else {
                    // Has padding, need to copy row by row
                    std::vector<uint8_t> pixels(width * height * 4);
                    for (int y = 0; y < height; y++) {
                        memcpy(pixels.data() + y * width * 4,
                               baseAddress + y * bytesPerRow,
                               width * 4);
                    }
                    renderer.uploadTexturePixels(output, pixels.data(), width, height);
                }
            }

            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CFRelease(sampleBuffer);

            return true;
        }
    }

    double currentTime() const override { return currentTime_; }
    int64_t currentFrame() const override { return currentFrame_; }

private:
    AVAsset* asset_ = nil;
    AVAssetReader* assetReader_ = nil;
    AVAssetReaderTrackOutput* readerOutput_ = nil;

    VideoInfo info_;
    std::string path_;
    bool isOpen_ = false;
    double currentTime_ = 0;
    int64_t currentFrame_ = 0;
};

std::unique_ptr<VideoLoader> createVideoLoaderMacOS() {
    return std::make_unique<VideoLoaderMacOS>();
}

} // namespace vivid

#endif // __APPLE__
