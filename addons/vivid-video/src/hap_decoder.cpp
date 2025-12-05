#ifdef VIVID_HAS_FFMPEG

#include "hap_decoder.h"
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

HAPDecoder::HAPDecoder() = default;

HAPDecoder::~HAPDecoder() {
    close();
}

bool HAPDecoder::isHAPFile(const std::string& path) {
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.c_str(), nullptr, nullptr) < 0) {
        return false;
    }

    if (avformat_find_stream_info(ctx, nullptr) < 0) {
        avformat_close_input(&ctx);
        return false;
    }

    bool isHAP = false;
    for (unsigned int i = 0; i < ctx->nb_streams; i++) {
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_HAP) {
                isHAP = true;
                break;
            }
        }
    }

    avformat_close_input(&ctx);
    return isHAP;
}

bool HAPDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    device_ = ctx.device();
    context_ = ctx.immediateContext();
    filePath_ = path;
    isLooping_ = loop;

    // Open input file
    if (avformat_open_input(&formatCtx_, path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[HAPDecoder] Failed to open: " << path << std::endl;
        return false;
    }

    // Find stream info
    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        std::cerr << "[HAPDecoder] Failed to find stream info" << std::endl;
        close();
        return false;
    }

    // Find video stream with HAP codec
    videoStreamIndex_ = -1;
    const AVCodec* codec = nullptr;

    for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (formatCtx_->streams[i]->codecpar->codec_id == AV_CODEC_ID_HAP) {
                videoStreamIndex_ = i;
                codec = avcodec_find_decoder(AV_CODEC_ID_HAP);
                break;
            }
        }
    }

    if (videoStreamIndex_ < 0 || !codec) {
        std::cerr << "[HAPDecoder] No HAP video stream found or no decoder" << std::endl;
        close();
        return false;
    }

    AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecpar = stream->codecpar;

    // Create codec context
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        std::cerr << "[HAPDecoder] Failed to allocate codec context" << std::endl;
        close();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecpar) < 0) {
        std::cerr << "[HAPDecoder] Failed to copy codec params" << std::endl;
        close();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        std::cerr << "[HAPDecoder] Failed to open codec" << std::endl;
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
    uint32_t tag = codecpar->codec_tag;
    char tagStr[5] = {0};
    tagStr[0] = tag & 0xFF;
    tagStr[1] = (tag >> 8) & 0xFF;
    tagStr[2] = (tag >> 16) & 0xFF;
    tagStr[3] = (tag >> 24) & 0xFF;

    std::cout << "[HAPDecoder] Opened " << path
              << " (" << width_ << "x" << height_
              << ", " << frameRate_ << "fps, " << tagStr << ")" << std::endl;

    // Allocate packet and frame
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!packet_ || !frame_) {
        std::cerr << "[HAPDecoder] Failed to allocate packet/frame" << std::endl;
        close();
        return false;
    }

    // Allocate pixel buffer for BGRA conversion
    pixelBuffer_.resize(width_ * height_ * 4);

    // Create GPU texture
    TextureDesc desc;
    desc.Name = "HAPVideoFrame";
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
        std::cerr << "[HAPDecoder] Failed to create texture" << std::endl;
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

void HAPDecoder::close() {
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

bool HAPDecoder::isOpen() const {
    return formatCtx_ != nullptr;
}

void HAPDecoder::update(Context& ctx) {
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

bool HAPDecoder::decodeFrame() {
    // Read packets until we get a video frame
    while (true) {
        int ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                return false;
            }
            std::cerr << "[HAPDecoder] Error reading frame" << std::endl;
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
            std::cerr << "[HAPDecoder] Error sending packet to decoder" << std::endl;
            continue;
        }

        ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == AVERROR(EAGAIN)) {
            // Need more packets
            continue;
        } else if (ret < 0) {
            std::cerr << "[HAPDecoder] Error receiving frame from decoder" << std::endl;
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

void HAPDecoder::uploadFrame() {
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
            std::cerr << "[HAPDecoder] Cannot convert pixel format: "
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

void HAPDecoder::seek(float seconds) {
    if (!formatCtx_) return;

    // Clamp seek time
    if (seconds < 0) seconds = 0;
    if (seconds > duration_) seconds = duration_;

    int64_t timestamp = static_cast<int64_t>(seconds / timeBase_);
    int ret = av_seek_frame(formatCtx_, videoStreamIndex_, timestamp,
                            AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::cerr << "[HAPDecoder] Seek failed" << std::endl;
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

ITexture* HAPDecoder::texture() const {
    return texture_;
}

ITextureView* HAPDecoder::textureView() const {
    return srv_;
}

} // namespace vivid::video

#endif // VIVID_HAS_FFMPEG
