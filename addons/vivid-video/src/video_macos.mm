// Vivid Video Addon - macOS Implementation
// Uses AVFoundation for hardware-accelerated video decoding

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include "vivid/video/video.h"
#include "vivid/context.h"

// Diligent Engine includes
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "Texture.h"
#include "Shader.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"

#include <iostream>

namespace vivid::video {

using namespace Diligent;

struct VideoPlayer::Impl {
    // AVFoundation objects
    AVAsset* asset = nil;
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* videoOutput = nil;

    // Video properties
    int videoWidth = 0;
    int videoHeight = 0;
    float videoDuration = 0.0f;
    float videoFrameRate = 30.0f;

    // Playback state
    bool isPlaying_ = false;
    bool isFinished_ = false;
    bool isLooping = false;
    float currentTime_ = 0.0f;      // Current video PTS
    float playbackTime_ = 0.0f;     // Elapsed playback time (for sync)
    float nextFrameTime_ = 0.0f;    // When next frame should be displayed
    std::string filePath;
    OSType outputPixelFormat = kCVPixelFormatType_32BGRA;

    // GPU texture
    RefCntAutoPtr<ITexture> texture;
    ITextureView* srv = nullptr;

    // Staging buffer for pixel upload
    std::vector<uint8_t> pixelBuffer;

    // Diligent references
    IRenderDevice* device = nullptr;
    IDeviceContext* context = nullptr;

    ~Impl() {
        close();
    }

    void close() {
        if (reader) {
            [reader cancelReading];
            reader = nil;
        }
        videoOutput = nil;
        asset = nil;
        texture.Release();
        srv = nullptr;
        isPlaying_ = false;
        isFinished_ = true;
        currentTime_ = 0.0f;
    }

    bool open(Context& ctx, const std::string& path, bool loop) {
        close();

        device = ctx.device();
        context = ctx.immediateContext();
        filePath = path;
        isLooping = loop;

        @autoreleasepool {
            // Create asset from file
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];

            // Use AVURLAsset with options for better format support
            NSDictionary* assetOptions = @{
                AVURLAssetPreferPreciseDurationAndTimingKey: @YES
            };
            asset = [AVURLAsset URLAssetWithURL:url options:assetOptions];

            if (!asset) {
                std::cerr << "[VideoPlayer] Failed to load asset: " << path << std::endl;
                return false;
            }

            // Synchronously load required keys before accessing them
            dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
            __block NSError* loadError = nil;

            [asset loadValuesAsynchronouslyForKeys:@[@"tracks", @"duration", @"playable"]
                completionHandler:^{
                    loadError = nil;
                    dispatch_semaphore_signal(semaphore);
                }];

            // Wait for asset to load (with timeout)
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC);
            if (dispatch_semaphore_wait(semaphore, timeout) != 0) {
                std::cerr << "[VideoPlayer] Timeout loading asset: " << path << std::endl;
                return false;
            }

            // Check if asset is playable
            NSError* error = nil;
            AVKeyValueStatus status = [asset statusOfValueForKey:@"playable" error:&error];
            if (status != AVKeyValueStatusLoaded) {
                std::cerr << "[VideoPlayer] Asset not playable: " << path << std::endl;
                if (error) {
                    std::cerr << "[VideoPlayer] Error: " << [[error localizedDescription] UTF8String] << std::endl;
                }
                return false;
            }

            if (!asset.playable) {
                std::cerr << "[VideoPlayer] Warning: Asset reports not playable, trying anyway: " << path << std::endl;
                // Don't return - try to open anyway, some formats work despite this flag
            }

            // Get video track
            NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            if ([videoTracks count] == 0) {
                std::cerr << "[VideoPlayer] No video track found: " << path << std::endl;
                return false;
            }

            AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];

            // Get video properties
            CGSize naturalSize = videoTrack.naturalSize;
            videoWidth = (int)naturalSize.width;
            videoHeight = (int)naturalSize.height;
            videoDuration = (float)CMTimeGetSeconds(asset.duration);
            videoFrameRate = videoTrack.nominalFrameRate;

            if (videoFrameRate <= 0) {
                videoFrameRate = 30.0f;
            }

            // Log codec info
            CMFormatDescriptionRef formatDesc = (__bridge CMFormatDescriptionRef)[videoTrack.formatDescriptions firstObject];
            if (formatDesc) {
                FourCharCode codec = CMFormatDescriptionGetMediaSubType(formatDesc);
                char codecStr[5] = {0};
                codecStr[0] = (codec >> 24) & 0xFF;
                codecStr[1] = (codec >> 16) & 0xFF;
                codecStr[2] = (codec >> 8) & 0xFF;
                codecStr[3] = codec & 0xFF;
                std::cout << "[VideoPlayer] Codec: " << codecStr << std::endl;
            }

            std::cout << "[VideoPlayer] Opened: " << path << std::endl;
            std::cout << "[VideoPlayer] Size: " << videoWidth << "x" << videoHeight << std::endl;
            std::cout << "[VideoPlayer] Duration: " << videoDuration << "s @ " << videoFrameRate << " fps" << std::endl;

            // Create asset reader
            if (!createReader()) {
                return false;
            }

            // Create GPU texture
            if (!createTexture()) {
                return false;
            }

            // Allocate staging buffer
            pixelBuffer.resize(videoWidth * videoHeight * 4);

            isPlaying_ = true;
            isFinished_ = false;
            currentTime_ = 0.0f;
            playbackTime_ = 0.0f;
            nextFrameTime_ = 0.0f;

            return true;
        }
    }

    bool createReader() {
        @autoreleasepool {
            NSError* error = nil;
            reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];

            if (error || !reader) {
                std::cerr << "[VideoPlayer] Failed to create reader: "
                          << (error ? [[error localizedDescription] UTF8String] : "unknown") << std::endl;
                return false;
            }

            // Get video track
            NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];

            // Try different output formats for maximum compatibility
            // Some codecs work better with specific formats
            NSArray* pixelFormats = @[
                @(kCVPixelFormatType_32BGRA),      // Preferred - direct upload
                @(kCVPixelFormatType_32ARGB),      // Alternative ARGB
                @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),  // YUV for HEVC/ProRes
            ];

            for (NSNumber* format in pixelFormats) {
                NSDictionary* outputSettings = @{
                    (NSString*)kCVPixelBufferPixelFormatTypeKey: format
                };

                videoOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                               outputSettings:outputSettings];
                videoOutput.alwaysCopiesSampleData = NO;

                [reader addOutput:videoOutput];

                if ([reader startReading]) {
                    outputPixelFormat = [format unsignedIntValue];
                    std::cout << "[VideoPlayer] Using pixel format: " << outputPixelFormat << std::endl;
                    return true;
                }

                // Reset for next attempt
                [reader cancelReading];
                reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
                if (!reader) break;
            }

            // Last resort: try nil settings (native format)
            if (reader) {
                videoOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                               outputSettings:nil];
                videoOutput.alwaysCopiesSampleData = NO;
                [reader addOutput:videoOutput];

                if ([reader startReading]) {
                    outputPixelFormat = 0;  // Will detect from first frame
                    std::cout << "[VideoPlayer] Using native pixel format" << std::endl;
                    return true;
                }
            }

            std::cerr << "[VideoPlayer] Failed to start reading" << std::endl;
            if (reader.error) {
                std::cerr << "[VideoPlayer] Error: " << [[reader.error localizedDescription] UTF8String] << std::endl;
            }
            return false;
        }
    }

    bool createTexture() {
        TextureDesc desc;
        desc.Name = "VideoFrame";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = videoWidth;
        desc.Height = videoHeight;
        desc.MipLevels = 1;
        desc.Format = TEX_FORMAT_BGRA8_UNORM;  // Match AVFoundation output
        desc.BindFlags = BIND_SHADER_RESOURCE;
        desc.Usage = USAGE_DEFAULT;

        device->CreateTexture(desc, nullptr, &texture);

        if (!texture) {
            std::cerr << "[VideoPlayer] Failed to create texture" << std::endl;
            return false;
        }

        srv = texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        return true;
    }

    void update(Context& ctx) {
        if (!isPlaying_ || isFinished_ || !reader) {
            return;
        }

        // Advance playback time
        playbackTime_ += ctx.dt();

        // Check if it's time for the next frame
        if (playbackTime_ < nextFrameTime_) {
            return;  // Not time yet, keep showing current frame
        }

        @autoreleasepool {
            // Check reader status
            if (reader.status == AVAssetReaderStatusFailed) {
                std::cerr << "[VideoPlayer] Reader failed" << std::endl;
                isFinished_ = true;
                isPlaying_ = false;
                return;
            }

            if (reader.status == AVAssetReaderStatusCompleted) {
                if (isLooping) {
                    // Restart from beginning
                    seek(0.0f);
                    return;
                } else {
                    isFinished_ = true;
                    isPlaying_ = false;
                    return;
                }
            }

            // Get next sample buffer
            CMSampleBufferRef sampleBuffer = [videoOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                // No more frames
                if (isLooping) {
                    seek(0.0f);
                } else {
                    isFinished_ = true;
                    isPlaying_ = false;
                }
                return;
            }

            // Get presentation time
            CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            currentTime_ = (float)CMTimeGetSeconds(pts);

            // Schedule next frame based on video frame rate
            float frameDuration = 1.0f / videoFrameRate;
            nextFrameTime_ = playbackTime_ + frameDuration;

            // Get pixel buffer
            CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (imageBuffer) {
                uploadFrame(imageBuffer);
            }

            CFRelease(sampleBuffer);
        }
    }

    void uploadFrame(CVImageBufferRef imageBuffer) {
        CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

        size_t height = CVPixelBufferGetHeight(imageBuffer);
        size_t width = CVPixelBufferGetWidth(imageBuffer);
        OSType pixelFormat = CVPixelBufferGetPixelFormatType(imageBuffer);

        uint8_t* dst = pixelBuffer.data();
        size_t dstRowBytes = width * 4;

        if (pixelFormat == kCVPixelFormatType_32BGRA) {
            // Direct copy for BGRA
            void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
            uint8_t* src = (uint8_t*)baseAddress;

            for (size_t y = 0; y < height; y++) {
                memcpy(dst + y * dstRowBytes, src + y * bytesPerRow, dstRowBytes);
            }
        } else if (pixelFormat == kCVPixelFormatType_32ARGB) {
            // Convert ARGB to BGRA
            void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
            uint8_t* src = (uint8_t*)baseAddress;

            for (size_t y = 0; y < height; y++) {
                uint8_t* srcRow = src + y * bytesPerRow;
                uint8_t* dstRow = dst + y * dstRowBytes;
                for (size_t x = 0; x < width; x++) {
                    dstRow[x*4 + 0] = srcRow[x*4 + 3]; // B <- B (ARGB[3])
                    dstRow[x*4 + 1] = srcRow[x*4 + 2]; // G <- G (ARGB[2])
                    dstRow[x*4 + 2] = srcRow[x*4 + 1]; // R <- R (ARGB[1])
                    dstRow[x*4 + 3] = srcRow[x*4 + 0]; // A <- A (ARGB[0])
                }
            }
        } else if (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange ||
                   pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange) {
            // Convert NV12/YUV420 biplanar to BGRA
            uint8_t* yPlane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 0);
            uint8_t* uvPlane = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(imageBuffer, 1);
            size_t yBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 0);
            size_t uvBytesPerRow = CVPixelBufferGetBytesPerRowOfPlane(imageBuffer, 1);

            for (size_t y = 0; y < height; y++) {
                uint8_t* yRow = yPlane + y * yBytesPerRow;
                uint8_t* uvRow = uvPlane + (y / 2) * uvBytesPerRow;
                uint8_t* dstRow = dst + y * dstRowBytes;

                for (size_t x = 0; x < width; x++) {
                    int Y = yRow[x];
                    int U = uvRow[(x / 2) * 2] - 128;
                    int V = uvRow[(x / 2) * 2 + 1] - 128;

                    // YUV to RGB conversion (BT.601)
                    int R = Y + ((359 * V) >> 8);
                    int G = Y - ((88 * U + 183 * V) >> 8);
                    int B = Y + ((454 * U) >> 8);

                    // Clamp to 0-255
                    R = R < 0 ? 0 : (R > 255 ? 255 : R);
                    G = G < 0 ? 0 : (G > 255 ? 255 : G);
                    B = B < 0 ? 0 : (B > 255 ? 255 : B);

                    dstRow[x*4 + 0] = (uint8_t)B;
                    dstRow[x*4 + 1] = (uint8_t)G;
                    dstRow[x*4 + 2] = (uint8_t)R;
                    dstRow[x*4 + 3] = 255;
                }
            }
        } else {
            // Unknown format - try treating as BGRA
            void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
            if (baseAddress && bytesPerRow >= dstRowBytes) {
                uint8_t* src = (uint8_t*)baseAddress;
                for (size_t y = 0; y < height; y++) {
                    memcpy(dst + y * dstRowBytes, src + y * bytesPerRow, dstRowBytes);
                }
            }
        }

        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

        // Upload to GPU texture
        Box region;
        region.MinX = 0;
        region.MaxX = (Uint32)width;
        region.MinY = 0;
        region.MaxY = (Uint32)height;

        TextureSubResData subResData;
        subResData.pData = pixelBuffer.data();
        subResData.Stride = (Uint32)dstRowBytes;

        context->UpdateTexture(texture, 0, 0, region, subResData,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void seek(float seconds) {
        if (!asset) return;

        @autoreleasepool {
            // Need to recreate the reader for seeking
            if (reader) {
                [reader cancelReading];
                reader = nil;
                videoOutput = nil;
            }

            // Clamp seek time
            if (seconds < 0) seconds = 0;
            if (seconds > videoDuration) seconds = videoDuration;

            currentTime_ = seconds;
            playbackTime_ = seconds;
            nextFrameTime_ = seconds;

            // Get video track
            NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];

            CMTime startTime = CMTimeMakeWithSeconds(seconds, 600);
            CMTime duration = CMTimeSubtract(asset.duration, startTime);

            // Try formats in same order as createReader
            NSArray* pixelFormats = @[
                @(kCVPixelFormatType_32BGRA),
                @(kCVPixelFormatType_32ARGB),
                @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            ];

            NSError* error = nil;

            for (NSNumber* format in pixelFormats) {
                reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
                if (!reader) continue;

                reader.timeRange = CMTimeRangeMake(startTime, duration);

                NSDictionary* outputSettings = @{
                    (NSString*)kCVPixelBufferPixelFormatTypeKey: format
                };

                videoOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                               outputSettings:outputSettings];
                videoOutput.alwaysCopiesSampleData = NO;
                [reader addOutput:videoOutput];

                if ([reader startReading]) {
                    isFinished_ = false;
                    return;
                }

                [reader cancelReading];
            }

            // Last resort: native format
            reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
            if (reader) {
                reader.timeRange = CMTimeRangeMake(startTime, duration);

                videoOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                               outputSettings:nil];
                videoOutput.alwaysCopiesSampleData = NO;
                [reader addOutput:videoOutput];

                if ([reader startReading]) {
                    isFinished_ = false;
                    return;
                }
            }

            std::cerr << "[VideoPlayer] Failed to start reading after seek" << std::endl;
            isFinished_ = true;
        }
    }
};

// VideoPlayer implementation

VideoPlayer::VideoPlayer() : impl_(std::make_unique<Impl>()) {}

VideoPlayer::~VideoPlayer() = default;

VideoPlayer::VideoPlayer(VideoPlayer&& other) noexcept = default;
VideoPlayer& VideoPlayer::operator=(VideoPlayer&& other) noexcept = default;

bool VideoPlayer::open(Context& ctx, const std::string& path, bool loop) {
    return impl_->open(ctx, path, loop);
}

void VideoPlayer::close() {
    impl_->close();
}

void VideoPlayer::update(Context& ctx) {
    impl_->update(ctx);
}

void VideoPlayer::seek(float seconds) {
    impl_->seek(seconds);
}

void VideoPlayer::pause() {
    impl_->isPlaying_ = false;
}

void VideoPlayer::play() {
    if (!impl_->isFinished_) {
        impl_->isPlaying_ = true;
    }
}

bool VideoPlayer::isPlaying() const {
    return impl_->isPlaying_;
}

bool VideoPlayer::isFinished() const {
    return impl_->isFinished_;
}

bool VideoPlayer::isOpen() const {
    return impl_->asset != nil;
}

float VideoPlayer::currentTime() const {
    return impl_->currentTime_;
}

float VideoPlayer::duration() const {
    return impl_->videoDuration;
}

int VideoPlayer::width() const {
    return impl_->videoWidth;
}

int VideoPlayer::height() const {
    return impl_->videoHeight;
}

float VideoPlayer::frameRate() const {
    return impl_->videoFrameRate;
}

Diligent::ITexture* VideoPlayer::texture() const {
    return impl_->texture;
}

Diligent::ITextureView* VideoPlayer::textureView() const {
    return impl_->srv;
}

// VideoSource implementation

VideoSource::VideoSource() = default;
VideoSource::~VideoSource() = default;

bool VideoSource::open(Context& ctx, const std::string& path, bool loop) {
    return player_.open(ctx, path, loop);
}

void VideoSource::close() {
    player_.close();
}

void VideoSource::process(Context& ctx) {
    player_.update(ctx);
}

Diligent::ITextureView* VideoSource::getOutputSRV() const {
    return player_.textureView();
}

VideoPlayer& VideoSource::player() {
    return player_;
}

const VideoPlayer& VideoSource::player() const {
    return player_;
}

bool VideoSource::isOpen() const {
    return player_.isOpen();
}

// VideoDisplay implementation

struct VideoDisplay::Impl {
    VideoPlayer player;
    RefCntAutoPtr<IPipelineState> pso;
    IShaderResourceBinding* srb = nullptr;

    bool initialized = false;

    void createPipeline(Context& ctx) {
        // Simple passthrough shader for video display
        const char* vsSource = R"(
            struct VSInput {
                uint vertexId : SV_VertexID;
            };
            struct PSInput {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
            };
            void main(in VSInput input, out PSInput output) {
                float2 pos;
                pos.x = (input.vertexId == 1) ? 3.0 : -1.0;
                pos.y = (input.vertexId == 2) ? 3.0 : -1.0;
                output.position = float4(pos, 0.0, 1.0);
                output.uv = pos * float2(0.5, -0.5) + 0.5;
            }
        )";

        const char* psSource = R"(
            Texture2D g_Texture;
            SamplerState g_Sampler;

            struct PSInput {
                float4 position : SV_POSITION;
                float2 uv : TEXCOORD0;
            };

            float4 main(in PSInput input) : SV_TARGET {
                return g_Texture.Sample(g_Sampler, input.uv);
            }
        )";

        auto* device = ctx.device();

        // Create shaders
        ShaderCreateInfo vsCI;
        vsCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        vsCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        vsCI.Desc.Name = "VideoDisplayVS";
        vsCI.EntryPoint = "main";
        vsCI.Source = vsSource;

        RefCntAutoPtr<IShader> vs;
        device->CreateShader(vsCI, &vs);

        ShaderCreateInfo psCI;
        psCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_HLSL;
        psCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        psCI.Desc.Name = "VideoDisplayPS";
        psCI.EntryPoint = "main";
        psCI.Source = psSource;

        RefCntAutoPtr<IShader> ps;
        device->CreateShader(psCI, &ps);

        if (!vs || !ps) {
            std::cerr << "[VideoDisplay] Failed to create shaders" << std::endl;
            return;
        }

        // Create pipeline
        GraphicsPipelineStateCreateInfo psoCI;
        psoCI.PSODesc.Name = "VideoDisplayPSO";
        psoCI.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

        psoCI.pVS = vs;
        psoCI.pPS = ps;

        // Get swap chain format
        const auto& scDesc = ctx.swapChain()->GetDesc();
        psoCI.GraphicsPipeline.NumRenderTargets = 1;
        psoCI.GraphicsPipeline.RTVFormats[0] = scDesc.ColorBufferFormat;
        psoCI.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        psoCI.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
        psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

        // Resource layout - texture and sampler
        ShaderResourceVariableDesc vars[] = {
            {SHADER_TYPE_PIXEL, "g_Texture", SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}
        };
        psoCI.PSODesc.ResourceLayout.Variables = vars;
        psoCI.PSODesc.ResourceLayout.NumVariables = 1;

        SamplerDesc samplerDesc;
        samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
        samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
        samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
        samplerDesc.AddressU = TEXTURE_ADDRESS_CLAMP;
        samplerDesc.AddressV = TEXTURE_ADDRESS_CLAMP;

        ImmutableSamplerDesc immutableSamplers[] = {
            {SHADER_TYPE_PIXEL, "g_Sampler", samplerDesc}
        };
        psoCI.PSODesc.ResourceLayout.ImmutableSamplers = immutableSamplers;
        psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 1;

        device->CreateGraphicsPipelineState(psoCI, &pso);

        if (!pso) {
            std::cerr << "[VideoDisplay] Failed to create PSO" << std::endl;
            return;
        }

        pso->CreateShaderResourceBinding(&srb, true);
        initialized = true;
    }

    void render(Context& ctx) {
        if (!initialized || !player.isOpen()) return;

        auto* texView = player.textureView();
        if (!texView) return;

        auto* immediateCtx = ctx.immediateContext();

        // Set render target
        auto* rtv = ctx.currentRTV();
        immediateCtx->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        // Set viewport
        Viewport vp;
        vp.Width = static_cast<float>(ctx.width());
        vp.Height = static_cast<float>(ctx.height());
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        immediateCtx->SetViewports(1, &vp, ctx.width(), ctx.height());

        // Bind texture
        auto* texVar = srb->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture");
        if (texVar) {
            texVar->Set(texView);
        }

        // Draw
        immediateCtx->SetPipelineState(pso);
        immediateCtx->CommitShaderResources(srb, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs drawAttribs;
        drawAttribs.NumVertices = 3;
        immediateCtx->Draw(drawAttribs);
    }

    void cleanup() {
        if (srb) {
            srb->Release();
            srb = nullptr;
        }
        pso.Release();
        initialized = false;
    }
};

VideoDisplay::VideoDisplay() : impl_(std::make_unique<Impl>()) {}

VideoDisplay::~VideoDisplay() {
    close();
}

bool VideoDisplay::open(Context& ctx, const std::string& path, bool loop) {
    impl_->createPipeline(ctx);
    return impl_->player.open(ctx, path, loop);
}

void VideoDisplay::close() {
    impl_->player.close();
    impl_->cleanup();
}

void VideoDisplay::update(Context& ctx) {
    impl_->player.update(ctx);
    impl_->render(ctx);
}

void VideoDisplay::pause() {
    impl_->player.pause();
}

void VideoDisplay::play() {
    impl_->player.play();
}

void VideoDisplay::seek(float seconds) {
    impl_->player.seek(seconds);
}

bool VideoDisplay::isPlaying() const {
    return impl_->player.isPlaying();
}

bool VideoDisplay::isFinished() const {
    return impl_->player.isFinished();
}

float VideoDisplay::currentTime() const {
    return impl_->player.currentTime();
}

float VideoDisplay::duration() const {
    return impl_->player.duration();
}

VideoPlayer& VideoDisplay::player() {
    return impl_->player;
}

} // namespace vivid::video
