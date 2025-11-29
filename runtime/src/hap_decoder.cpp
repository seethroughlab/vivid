#ifdef VIVID_HAS_FFMPEG

#include "hap_decoder.h"
#include "renderer.h"
#include <iostream>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace vivid {

HAPDecoder::HAPDecoder() = default;

HAPDecoder::~HAPDecoder() {
    close();
}

bool HAPDecoder::open(const std::string& path) {
    close();

    // Open input file
    if (avformat_open_input(&formatCtx_, path.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[HAPDecoder] Failed to open: " << path << "\n";
        return false;
    }

    // Find stream info
    if (avformat_find_stream_info(formatCtx_, nullptr) < 0) {
        std::cerr << "[HAPDecoder] Failed to find stream info\n";
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
        std::cerr << "[HAPDecoder] No HAP video stream found or no decoder\n";
        close();
        return false;
    }

    AVStream* stream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* codecpar = stream->codecpar;

    // Create codec context
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        std::cerr << "[HAPDecoder] Failed to allocate codec context\n";
        close();
        return false;
    }

    if (avcodec_parameters_to_context(codecCtx_, codecpar) < 0) {
        std::cerr << "[HAPDecoder] Failed to copy codec params\n";
        close();
        return false;
    }

    if (avcodec_open2(codecCtx_, codec, nullptr) < 0) {
        std::cerr << "[HAPDecoder] Failed to open codec\n";
        close();
        return false;
    }

    // Store video info
    info_.width = codecpar->width;
    info_.height = codecpar->height;
    info_.duration = (formatCtx_->duration > 0) ?
                     static_cast<double>(formatCtx_->duration) / AV_TIME_BASE : 0.0;

    // Calculate frame rate
    if (stream->avg_frame_rate.den > 0) {
        info_.frameRate = static_cast<double>(stream->avg_frame_rate.num) /
                          stream->avg_frame_rate.den;
    } else if (stream->r_frame_rate.den > 0) {
        info_.frameRate = static_cast<double>(stream->r_frame_rate.num) /
                          stream->r_frame_rate.den;
    } else {
        info_.frameRate = 30.0;
    }

    info_.frameCount = stream->nb_frames;
    if (info_.frameCount == 0 && info_.duration > 0 && info_.frameRate > 0) {
        info_.frameCount = static_cast<int64_t>(info_.duration * info_.frameRate);
    }

    // Determine HAP type from codec tag
    info_.codecType = VideoCodecType::HAP;
    uint32_t tag = codecpar->codec_tag;
    char tagStr[5] = {0};
    tagStr[0] = tag & 0xFF;
    tagStr[1] = (tag >> 8) & 0xFF;
    tagStr[2] = (tag >> 16) & 0xFF;
    tagStr[3] = (tag >> 24) & 0xFF;

    if (strcmp(tagStr, "Hap1") == 0) {
        info_.codecType = VideoCodecType::HAP;
    } else if (strcmp(tagStr, "Hap5") == 0) {
        info_.codecType = VideoCodecType::HAPAlpha;
    } else if (strcmp(tagStr, "HapY") == 0) {
        info_.codecType = VideoCodecType::HAPQ;
    } else if (strcmp(tagStr, "HapM") == 0) {
        info_.codecType = VideoCodecType::HAPQAlpha;
    }

    // Calculate time base
    timeBase_ = av_q2d(stream->time_base);

    // Allocate packet and frame
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!packet_ || !frame_) {
        std::cerr << "[HAPDecoder] Failed to allocate packet/frame\n";
        close();
        return false;
    }

    std::cout << "[HAPDecoder] Opened " << path
              << " (" << info_.width << "x" << info_.height
              << ", " << info_.frameRate << "fps, " << tagStr << ")\n";

    return true;
}

void HAPDecoder::close() {
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
    videoStreamIndex_ = -1;
    currentTime_ = 0.0;
}

bool HAPDecoder::seek(double timeSeconds) {
    if (!formatCtx_) return false;

    int64_t timestamp = static_cast<int64_t>(timeSeconds / timeBase_);
    int ret = av_seek_frame(formatCtx_, videoStreamIndex_, timestamp,
                            AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::cerr << "[HAPDecoder] Seek failed\n";
        return false;
    }

    // Flush codec buffers after seek
    if (codecCtx_) {
        avcodec_flush_buffers(codecCtx_);
    }

    currentTime_ = timeSeconds;
    return true;
}

bool HAPDecoder::getFrame(Texture& output, Renderer& renderer) {
    if (!formatCtx_ || !codecCtx_ || videoStreamIndex_ < 0) return false;

    // Read packets until we get a video frame
    while (true) {
        int ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                return false;
            }
            std::cerr << "[HAPDecoder] Error reading frame\n";
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
            std::cerr << "[HAPDecoder] Error sending packet to decoder\n";
            continue;
        }

        ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == AVERROR(EAGAIN)) {
            // Need more packets
            continue;
        } else if (ret < 0) {
            std::cerr << "[HAPDecoder] Error receiving frame from decoder\n";
            return false;
        }

        // Got a frame!
        break;
    }

    // Update current time
    if (frame_->pts != AV_NOPTS_VALUE) {
        currentTime_ = frame_->pts * timeBase_;
    }

    // Create/resize texture if needed
    if (!output.valid() || output.width != info_.width || output.height != info_.height) {
        output = renderer.createTexture(info_.width, info_.height);
    }

    // Convert frame to RGBA using swscale
    int width = frame_->width;
    int height = frame_->height;

    std::vector<uint8_t> rgba(width * height * 4);

    // Use swscale to convert any format to RGBA
    SwsContext* swsCtx = sws_getContext(
        width, height, static_cast<AVPixelFormat>(frame_->format),
        width, height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!swsCtx) {
        const char* fmtName = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame_->format));
        std::cerr << "[HAPDecoder] Cannot convert pixel format: "
                  << (fmtName ? fmtName : "unknown") << " (" << frame_->format << ")\n";
        av_frame_unref(frame_);
        return false;
    }

    uint8_t* dstData[1] = { rgba.data() };
    int dstLinesize[1] = { width * 4 };

    sws_scale(swsCtx, frame_->data, frame_->linesize, 0, height,
              dstData, dstLinesize);

    sws_freeContext(swsCtx);
    av_frame_unref(frame_);

    // Upload to texture
    renderer.uploadTexturePixels(output, rgba.data(), width, height);

    return true;
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

} // namespace vivid

#endif // VIVID_HAS_FFMPEG
