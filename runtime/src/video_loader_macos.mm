#include "video_loader.h"
#include "renderer.h"
#include "audio_player.h"
#include <iostream>

#if defined(__APPLE__)

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <AudioToolbox/AudioToolbox.h>

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
            // Use alloc/init to get an owned object (not autoreleased)
            NSError* error = nil;
            assetReader_ = [[AVAssetReader alloc] initWithAsset:asset_ error:&error];
            if (error || !assetReader_) {
                std::cerr << "[VideoLoaderMacOS] Failed to create reader: "
                          << (error ? [[error localizedDescription] UTF8String] : "unknown") << "\n";
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

            // Create new reader with time range (use alloc/init for owned object)
            NSError* error = nil;
            assetReader_ = [[AVAssetReader alloc] initWithAsset:asset_ error:&error];
            if (error || !assetReader_) {
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
            if (!assetReader_) return false;
            AVAssetReaderStatus status = assetReader_.status;
            if (status != AVAssetReaderStatusReading) {
                if (status == AVAssetReaderStatusCompleted) {
                    // End of video - don't log every frame
                    return false;
                }
                std::cerr << "[VideoLoaderMacOS] Reader not in reading state: " << static_cast<int>(status);
                if (status == AVAssetReaderStatusFailed) {
                    std::cerr << " (failed: " << [[assetReader_.error localizedDescription] UTF8String] << ")";
                }
                std::cerr << "\n";
                return false;
            }

            // Get next sample buffer
            if (!readerOutput_) return false;
            CMSampleBufferRef sampleBuffer = [readerOutput_ copyNextSampleBuffer];
            if (!sampleBuffer) return false;

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
                if (!output.valid()) {
                    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
                    CFRelease(sampleBuffer);
                    return false;
                }
            }

            // Upload to GPU texture
            // CVPixelBuffer is BGRA, convert to RGBA for WebGPU
            if (output.valid() && baseAddress) {
                std::vector<uint8_t> pixels(width * height * 4);
                for (int y = 0; y < height; y++) {
                    uint8_t* src = baseAddress + y * bytesPerRow;
                    uint8_t* dst = pixels.data() + y * width * 4;
                    for (int x = 0; x < width; x++) {
                        // BGRA -> RGBA
                        dst[0] = src[2];  // R <- B
                        dst[1] = src[1];  // G <- G
                        dst[2] = src[0];  // B <- R
                        dst[3] = src[3];  // A <- A
                        src += 4;
                        dst += 4;
                    }
                }
                renderer.uploadTexturePixels(output, pixels.data(), width, height);
            }

            CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
            CFRelease(sampleBuffer);

            return true;
        }
    }

    double currentTime() const override { return currentTime_; }
    int64_t currentFrame() const override { return currentFrame_; }

    // === Audio Support ===

    void setAudioEnabled(bool enabled) override {
        audioEnabled_ = enabled;
        if (audioPlayer_) {
            if (enabled) {
                audioPlayer_->play();
            } else {
                audioPlayer_->pause();
            }
        }
    }

    bool isAudioEnabled() const override { return audioEnabled_; }

    void setAudioVolume(float volume) override {
        audioVolume_ = volume;
        if (audioPlayer_) {
            audioPlayer_->setVolume(volume);
        }
    }

    float getAudioVolume() const override { return audioVolume_; }

    AudioPlayer* getAudioPlayer() override { return audioPlayer_.get(); }

private:
    bool setupAudioReader() {
        @autoreleasepool {
            NSArray* audioTracks = [asset_ tracksWithMediaType:AVMediaTypeAudio];
            if ([audioTracks count] == 0) {
                return false;
            }

            AVAssetTrack* audioTrack = [audioTracks objectAtIndex:0];

            // Get audio format info
            NSArray* formatDescriptions = audioTrack.formatDescriptions;
            if ([formatDescriptions count] > 0) {
                CMFormatDescriptionRef desc = (__bridge CMFormatDescriptionRef)[formatDescriptions objectAtIndex:0];
                const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(desc);
                if (asbd) {
                    audioSampleRate_ = static_cast<uint32_t>(asbd->mSampleRate);
                    audioChannels_ = asbd->mChannelsPerFrame;
                }
            }

            // Default to common values if not detected
            if (audioSampleRate_ == 0) audioSampleRate_ = 48000;
            if (audioChannels_ == 0) audioChannels_ = 2;

            // Configure output for float PCM
            NSDictionary* audioSettings = @{
                AVFormatIDKey: @(kAudioFormatLinearPCM),
                AVLinearPCMBitDepthKey: @32,
                AVLinearPCMIsFloatKey: @YES,
                AVLinearPCMIsNonInterleaved: @NO,
                AVSampleRateKey: @(audioSampleRate_),
                AVNumberOfChannelsKey: @(audioChannels_)
            };

            audioReaderOutput_ = [AVAssetReaderAudioMixOutput assetReaderAudioMixOutputWithAudioTracks:@[audioTrack]
                                                                                        audioSettings:audioSettings];
            audioReaderOutput_.alwaysCopiesSampleData = NO;

            if (![assetReader_ canAddOutput:audioReaderOutput_]) {
                std::cerr << "[VideoLoaderMacOS] Cannot add audio reader output\n";
                audioReaderOutput_ = nil;
                return false;
            }
            [assetReader_ addOutput:audioReaderOutput_];

            // Create audio player
            audioPlayer_ = std::make_unique<AudioPlayer>();
            if (!audioPlayer_->init(audioSampleRate_, audioChannels_)) {
                std::cerr << "[VideoLoaderMacOS] Failed to initialize audio player\n";
                audioPlayer_.reset();
                return false;
            }

            audioPlayer_->setVolume(audioVolume_);
            std::cout << "[VideoLoaderMacOS] Audio: " << audioSampleRate_ << "Hz, "
                      << audioChannels_ << " channel(s)\n";

            return true;
        }
    }

    void decodeAudioSamples() {
        if (!audioReaderOutput_ || !audioPlayer_ || !audioEnabled_) return;

        @autoreleasepool {
            // Decode audio samples to stay ahead of video
            // Try to buffer ~0.5 seconds of audio
            uint32_t targetBufferFrames = audioSampleRate_ / 2;

            while (audioPlayer_->getBufferedFrames() < targetBufferFrames) {
                CMSampleBufferRef sampleBuffer = [audioReaderOutput_ copyNextSampleBuffer];
                if (!sampleBuffer) break;

                // Get audio buffer list
                CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
                if (!blockBuffer) {
                    CFRelease(sampleBuffer);
                    continue;
                }

                size_t totalLength = 0;
                char* dataPointer = nullptr;
                OSStatus status = CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &totalLength, &dataPointer);

                if (status == noErr && dataPointer) {
                    // Data is interleaved float samples
                    const float* samples = reinterpret_cast<const float*>(dataPointer);
                    uint32_t frameCount = static_cast<uint32_t>(totalLength / (sizeof(float) * audioChannels_));
                    audioPlayer_->pushSamples(samples, frameCount);
                }

                CFRelease(sampleBuffer);
            }
        }
    }

    // Use __strong to ensure ARC properly retains ObjC objects in C++ class
    __strong AVAsset* asset_ = nil;
    __strong AVAssetReader* assetReader_ = nil;
    __strong AVAssetReaderTrackOutput* readerOutput_ = nil;
    __strong AVAssetReaderAudioMixOutput* audioReaderOutput_ = nil;

    VideoInfo info_;
    std::string path_;
    bool isOpen_ = false;
    double currentTime_ = 0;
    int64_t currentFrame_ = 0;

    // Audio
    std::unique_ptr<AudioPlayer> audioPlayer_;
    uint32_t audioSampleRate_ = 0;
    uint32_t audioChannels_ = 0;
    bool audioEnabled_ = true;
    float audioVolume_ = 1.0f;
};

std::unique_ptr<VideoLoader> createVideoLoaderMacOS() {
    return std::make_unique<VideoLoaderMacOS>();
}

} // namespace vivid

#endif // __APPLE__
