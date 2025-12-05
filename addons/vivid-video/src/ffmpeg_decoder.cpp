#ifdef VIVID_HAS_FFMPEG

#include "ffmpeg_decoder.h"
#include "vivid/context.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// Diligent Engine includes
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "RefCntAutoPtr.hpp"

#include <iostream>
#include <cstring>

namespace vivid::video {

using namespace Diligent;

FFmpegDecoder::FFmpegDecoder() = default;

FFmpegDecoder::~FFmpegDecoder() {
    close();
}

bool FFmpegDecoder::needsFFmpegDecoder(const std::string& path) {
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        avformat_close_input(&ctx);
        return false;
    }

    bool needsFFmpeg = false;
    const char* codecName = nullptr;
    for (unsigned int i = 0; i < ctx->nb_streams; i++) {
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            AVCodecID codecId = ctx->streams[i]->codecpar->codec_id;

            // Codecs that need FFmpeg on macOS:
            // - HAP: DXT compressed, AVFoundation returns raw DXT data
            // - HEVC: AVFoundation returns NULL image buffers in native format
            if (codecId == AV_CODEC_ID_HAP ||
                codecId == AV_CODEC_ID_HEVC) {
                needsFFmpeg = true;
                codecName = avcodec_get_name(codecId);
                break;
            }
        }
    }

    avformat_close_input(&ctx);

    if (needsFFmpeg && codecName) {
        std::cout << "[FFmpegDecoder] Codec '" << codecName
                  << "' requires FFmpeg" << std::endl;
    }

    return needsFFmpeg;
}

bool FFmpegDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    device_ = ctx.device();
    context_ = ctx.immediateContext();
    filePath_ = path;
    isLooping_ = loop;

    // Open input file
    if (avformat_open_input(&formatCtx_, path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[FFmpegDecoder] Failed to open: " << path << std::endl;
        return false;
    }

    // Find stream info
    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        std::cerr << "[FFmpegDecoder] Failed to find stream info" << std::endl;
        close();
        return false;
    }

    // Find first video stream
    videoStreamIndex_ = -1;
    const AVCodec* codec = nullptr;
    AVCodecID codecId = AV_CODEC_ID_NONE;

    for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex_ = i;
            codecId = formatCtx_->streams[i]->codecpar->codec_id;
            codec = avcodec_find_decoder(codecId);
            break;
        }
    }

    if (videoStreamIndex_ < 0 || !codec) {
        std::cerr << "[FFmpegDecoder] No video stream found or no decoder for codec "
                  << avcodec_get_name(codecId) << std::endl;
        close();
        return false;
    }

    AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecpar = stream->codecpar;

    // Create codec context
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        std::cerr << "[FFmpegDecoder] Failed to allocate codec context" << std::endl;
        close();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecpar) < 0) {
        std::cerr << "[FFmpegDecoder] Failed to copy codec params" << std::endl;
        close();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        std::cerr << "[FFmpegDecoder] Failed to open codec" << std::endl;
        close();
        return false;
    }

    // Store video info
    width_ = codecpar->width;
    height_ = codecpar->height;
    duration_ = (formatCtx_->duration > 0) ?
                static_cast<float>(formatCtx_->duration) / AV_TIME_BASE : 0.0f;

    // Calculate frame rate
    if (stream->avg_frame_rate.den > 0) {
        frameRate_ = static_cast<float>(stream->avg_frame_rate.num) /
                     stream->avg_frame_rate.den;
    } else if (stream->r_frame_rate.den > 0) {
        frameRate_ = static_cast<float>(stream->r_frame_rate.num) /
                     stream->r_frame_rate.den;
    } else {
        frameRate_ = 30.0f;
    }

    // Calculate time base
    timeBase_ = av_q2d(stream->time_base);

    // Log codec info
    const char* codecName = avcodec_get_name(codecId);

    std::cout << "[FFmpegDecoder] Opened " << path
              << " (" << width_ << "x" << height_
              << ", " << frameRate_ << "fps, " << codecName << ")" << std::endl;

    // Allocate packet and frame
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!packet_ || !frame_) {
        std::cerr << "[FFmpegDecoder] Failed to allocate packet/frame" << std::endl;
        close();
        return false;
    }

    // Allocate pixel buffer for BGRA conversion
    pixelBuffer_.resize(width_ * height_ * 4);

    // Create GPU texture
    TextureDesc desc;
    desc.Name = "FFmpegVideoFrame";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width_;
    desc.Height = height_;
    desc.MipLevels = 1;
    desc.Format = TEX_FORMAT_BGRA8_UNORM;
    desc.BindFlags = BIND_SHADER_RESOURCE;
    desc.Usage = USAGE_DEFAULT;

    RefCntAutoPtr<ITexture> tex;
    device_->CreateTexture(desc, nullptr, &tex);

    if (!tex) {
        std::cerr << "[FFmpegDecoder] Failed to create texture" << std::endl;
        close();
        return false;
    }

    texture_ = tex.Detach();
    srv_ = texture_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    isPlaying_ = true;
    isFinished_ = false;
    currentTime_ = 0.0f;
    playbackTime_ = 0.0f;
    nextFrameTime_ = 0.0f;

    return true;
}

void FFmpegDecoder::close() {
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }
    if (frame_) {
        av_frame_free(&frame_);
        frame_ = nullptr;
    }
    if (packet_) {
        av_packet_free(&packet_);
        packet_ = nullptr;
    }
    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
        formatCtx_ = nullptr;
    }
    if (texture_) {
        texture_->Release();
        texture_ = nullptr;
    }
    srv_ = nullptr;
    videoStreamIndex_ = -1;
    isPlaying_ = false;
    isFinished_ = true;
    currentTime_ = 0.0f;
}

bool FFmpegDecoder::isOpen() const {
    return formatCtx_ != nullptr;
}

void FFmpegDecoder::update(Context& ctx) {
    if (!isPlaying_ || isFinished_ || !formatCtx_) {
        return;
    }

    // Advance playback time
    playbackTime_ += ctx.dt();

    // Check if it's time for the next frame
    if (playbackTime_ < nextFrameTime_) {
        return;
    }

    // Decode and upload frame
    if (decodeFrame()) {
        uploadFrame();

        // Schedule next frame
        float frameDuration = 1.0f / frameRate_;
        nextFrameTime_ = playbackTime_ + frameDuration;
    } else {
        // No more frames
        if (isLooping_) {
            seek(0.0f);
        } else {
            isFinished_ = true;
            isPlaying_ = false;
        }
    }
}

bool FFmpegDecoder::decodeFrame() {
    // Read packets until we get a video frame
    while (true) {
        int ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                return false;
            }
            std::cerr << "[FFmpegDecoder] Error reading frame" << std::endl;
            return false;
        }

        if (packet_->stream_index != videoStreamIndex_) {
            av_packet_unref(packet_);
            continue;
        }

        // Decode the packet
        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);

        if (ret < 0) {
            // Log error only once per second to avoid spam
            static float lastErrorTime = 0;
            if (currentTime_ - lastErrorTime > 1.0f) {
                std::cerr << "[FFmpegDecoder] Error sending packet to decoder" << std::endl;
                lastErrorTime = currentTime_;
            }
            continue;
        }

        ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == AVERROR(EAGAIN)) {
            // Need more packets
            continue;
        } else if (ret < 0) {
            std::cerr << "[FFmpegDecoder] Error receiving frame from decoder" << std::endl;
            return false;
        }

        // Got a frame!
        break;
    }

    // Update current time
    if (frame_->pts != AV_NOPTS_VALUE) {
        currentTime_ = static_cast<float>(frame_->pts * timeBase_);
    }

    return true;
}

void FFmpegDecoder::uploadFrame() {
    if (!frame_ || !texture_) return;

    int width = frame_->width;
    int height = frame_->height;

    // Use swscale to convert any format to BGRA
    if (!swsCtx_) {
        swsCtx_ = sws_getContext(
            width, height, static_cast<AVPixelFormat>(frame_->format),
            width, height, AV_PIX_FMT_BGRA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (!swsCtx_) {
            const char* fmtName = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame_->format));
            std::cerr << "[FFmpegDecoder] Cannot convert pixel format: "
                      << (fmtName ? fmtName : "unknown") << std::endl;
            av_frame_unref(frame_);
            return;
        }
    }

    uint8_t* dstData[1] = { pixelBuffer_.data() };
    int dstLinesize[1] = { width * 4 };

    sws_scale(swsCtx_, frame_->data, frame_->linesize, 0, height,
              dstData, dstLinesize);

    av_frame_unref(frame_);

    // Upload to GPU texture
    Box region;
    region.MinX = 0;
    region.MaxX = static_cast<Uint32>(width);
    region.MinY = 0;
    region.MaxY = static_cast<Uint32>(height);

    TextureSubResData subResData;
    subResData.pData = pixelBuffer_.data();
    subResData.Stride = static_cast<Uint32>(width * 4);

    context_->UpdateTexture(texture_, 0, 0, region, subResData,
                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                           RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void FFmpegDecoder::seek(float seconds) {
    if (!formatCtx_) return;

    // Clamp seek time
    if (seconds < 0) seconds = 0;
    if (seconds > duration_) seconds = duration_;

    int64_t timestamp = static_cast<int64_t>(seconds / timeBase_);
    int ret = av_seek_frame(formatCtx_, videoStreamIndex_, timestamp,
                            AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::cerr << "[FFmpegDecoder] Seek failed" << std::endl;
        return;
    }

    // Flush codec buffers after seek
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }

    // Reset sws context (in case format changes)
    if (swsCtx_) {
        sws_freeContext(swsCtx_);
        swsCtx_ = nullptr;
    }

    currentTime_ = seconds;
    playbackTime_ = seconds;
    nextFrameTime_ = seconds;
    isFinished_ = false;
}

ITexture* FFmpegDecoder::texture() const {
    return texture_;
}

ITextureView* FFmpegDecoder::textureView() const {
    return srv_;
}

} // namespace vivid::video

#endif // VIVID_HAS_FFMPEG
