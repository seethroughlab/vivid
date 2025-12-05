#ifdef VIVID_HAS_FFMPEG

#include "ffmpeg_decoder.h"
#include "audio_player.h"
#include "vivid/context.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
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

    // Find video and audio streams
    videoStreamIndex_ = -1;
    audioStreamIndex_ = -1;
    const AVCodec* videoCodec = nullptr;
    const AVCodec* audioCodec = nullptr;

    for (unsigned int i = 0; i < formatCtx_->nb_streams; i++) {
        AVCodecParameters* codecpar = formatCtx_->streams[i]->codecpar;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ < 0) {
            videoStreamIndex_ = i;
            videoCodec = avcodec_find_decoder(codecpar->codec_id);
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ < 0) {
            audioStreamIndex_ = i;
            audioCodec = avcodec_find_decoder(codecpar->codec_id);
        }
    }

    if (videoStreamIndex_ < 0 || !videoCodec) {
        std::cerr << "[FFmpegDecoder] No video stream found" << std::endl;
        close();
        return false;
    }

    // Setup video decoder
    AVStream* videoStream = formatCtx_->streams[videoStreamIndex_];
    AVCodecParameters* videoCodecpar = videoStream->codecpar;

    videoCodecCtx_ = avcodec_alloc_context3(videoCodec);
    if (!videoCodecCtx_) {
        std::cerr << "[FFmpegDecoder] Failed to allocate video codec context" << std::endl;
        close();
        return false;
    }

    if (avcodec_parameters_to_context(videoCodecCtx_, videoCodecpar) < 0) {
        std::cerr << "[FFmpegDecoder] Failed to copy video codec params" << std::endl;
        close();
        return false;
    }

    if (avcodec_open2(videoCodecCtx_, videoCodec, nullptr) < 0) {
        std::cerr << "[FFmpegDecoder] Failed to open video codec" << std::endl;
        close();
        return false;
    }

    // Store video info
    width_ = videoCodecpar->width;
    height_ = videoCodecpar->height;
    duration_ = (formatCtx_->duration > 0) ?
                static_cast<float>(formatCtx_->duration) / AV_TIME_BASE : 0.0f;

    // Calculate frame rate
    if (videoStream->avg_frame_rate.den > 0) {
        frameRate_ = static_cast<float>(videoStream->avg_frame_rate.num) /
                     videoStream->avg_frame_rate.den;
    } else if (videoStream->r_frame_rate.den > 0) {
        frameRate_ = static_cast<float>(videoStream->r_frame_rate.num) /
                     videoStream->r_frame_rate.den;
    } else {
        frameRate_ = 30.0f;
    }

    videoTimeBase_ = av_q2d(videoStream->time_base);

    // Setup audio decoder if audio stream exists
    if (audioStreamIndex_ >= 0 && audioCodec) {
        AVStream* audioStream = formatCtx_->streams[audioStreamIndex_];
        AVCodecParameters* audioCodecpar = audioStream->codecpar;

        audioCodecCtx_ = avcodec_alloc_context3(audioCodec);
        if (audioCodecCtx_) {
            if (avcodec_parameters_to_context(audioCodecCtx_, audioCodecpar) >= 0) {
                if (avcodec_open2(audioCodecCtx_, audioCodec, nullptr) >= 0) {
                    audioTimeBase_ = av_q2d(audioStream->time_base);
                    audioSampleRate_ = audioCodecCtx_->sample_rate;
                    audioChannels_ = audioCodecCtx_->ch_layout.nb_channels;

                    // Setup resampler to convert to float stereo
                    swrCtx_ = swr_alloc();
                    if (swrCtx_) {
                        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
                        av_opt_set_chlayout(swrCtx_, "in_chlayout", &audioCodecCtx_->ch_layout, 0);
                        av_opt_set_chlayout(swrCtx_, "out_chlayout", &outLayout, 0);
                        av_opt_set_int(swrCtx_, "in_sample_rate", audioSampleRate_, 0);
                        av_opt_set_int(swrCtx_, "out_sample_rate", audioSampleRate_, 0);
                        av_opt_set_sample_fmt(swrCtx_, "in_sample_fmt", audioCodecCtx_->sample_fmt, 0);
                        av_opt_set_sample_fmt(swrCtx_, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

                        if (swr_init(swrCtx_) >= 0) {
                            // Initialize audio player
                            audioPlayer_ = std::make_unique<AudioPlayer>();
                            if (audioPlayer_->init(audioSampleRate_, 2)) {
                                audioFrame_ = av_frame_alloc();
                                std::cout << "[FFmpegDecoder] Audio: " << audioSampleRate_
                                          << "Hz, " << audioChannels_ << " ch" << std::endl;
                            } else {
                                audioPlayer_.reset();
                            }
                        } else {
                            swr_free(&swrCtx_);
                            swrCtx_ = nullptr;
                        }
                    }

                    if (!audioPlayer_) {
                        avcodec_free_context(&audioCodecCtx_);
                        audioCodecCtx_ = nullptr;
                        audioStreamIndex_ = -1;
                    }
                } else {
                    avcodec_free_context(&audioCodecCtx_);
                    audioCodecCtx_ = nullptr;
                    audioStreamIndex_ = -1;
                }
            } else {
                avcodec_free_context(&audioCodecCtx_);
                audioCodecCtx_ = nullptr;
                audioStreamIndex_ = -1;
            }
        } else {
            audioStreamIndex_ = -1;
        }
    }

    // Log codec info
    const char* codecName = avcodec_get_name(videoCodecpar->codec_id);
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

    // Start audio playback
    if (audioPlayer_) {
        audioPlayer_->play();
    }

    return true;
}

void FFmpegDecoder::close() {
    // Stop and cleanup audio
    if (audioPlayer_) {
        audioPlayer_->pause();
        audioPlayer_->shutdown();
        audioPlayer_.reset();
    }
    if (swrCtx_) {
        swr_free(&swrCtx_);
        swrCtx_ = nullptr;
    }
    if (audioFrame_) {
        av_frame_free(&audioFrame_);
        audioFrame_ = nullptr;
    }
    if (audioCodecCtx_) {
        avcodec_free_context(&audioCodecCtx_);
        audioCodecCtx_ = nullptr;
    }

    // Cleanup video
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
    if (videoCodecCtx_) {
        avcodec_free_context(&videoCodecCtx_);
        videoCodecCtx_ = nullptr;
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
    audioStreamIndex_ = -1;
    isPlaying_ = false;
    isFinished_ = true;
    currentTime_ = 0.0f;
}

bool FFmpegDecoder::isOpen() const {
    return formatCtx_ != nullptr;
}

void FFmpegDecoder::pause() {
    isPlaying_ = false;
    if (audioPlayer_) {
        audioPlayer_->pause();
    }
}

void FFmpegDecoder::play() {
    if (!isFinished_) {
        isPlaying_ = true;
        if (audioPlayer_) {
            audioPlayer_->play();
        }
    }
}

void FFmpegDecoder::setVolume(float volume) {
    if (audioPlayer_) {
        audioPlayer_->setVolume(volume);
    }
}

float FFmpegDecoder::getVolume() const {
    return audioPlayer_ ? audioPlayer_->getVolume() : 1.0f;
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
            if (audioPlayer_) {
                audioPlayer_->pause();
            }
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

        // Handle audio packets
        if (packet_->stream_index == audioStreamIndex_ && audioCodecCtx_ && audioPlayer_) {
            processAudioPacket();
            av_packet_unref(packet_);
            continue;  // Keep reading for video
        }

        if (packet_->stream_index != videoStreamIndex_) {
            av_packet_unref(packet_);
            continue;
        }

        // Decode the video packet
        ret = avcodec_send_packet(videoCodecCtx_, packet_);
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

        ret = avcodec_receive_frame(videoCodecCtx_, frame_);
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
        currentTime_ = static_cast<float>(frame_->pts * videoTimeBase_);
    }

    return true;
}

void FFmpegDecoder::processAudioPacket() {
    int ret = avcodec_send_packet(audioCodecCtx_, packet_);
    if (ret < 0) {
        return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(audioCodecCtx_, audioFrame_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }

        // Convert audio to float stereo
        int outSamples = swr_get_out_samples(swrCtx_, audioFrame_->nb_samples);
        audioBuffer_.resize(outSamples * 2);  // stereo

        uint8_t* outBuffer = reinterpret_cast<uint8_t*>(audioBuffer_.data());
        int converted = swr_convert(swrCtx_,
                                    &outBuffer, outSamples,
                                    (const uint8_t**)audioFrame_->data, audioFrame_->nb_samples);

        if (converted > 0) {
            audioPlayer_->pushSamples(audioBuffer_.data(), converted);
        }

        av_frame_unref(audioFrame_);
    }
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

    int64_t timestamp = static_cast<int64_t>(seconds / videoTimeBase_);
    int ret = av_seek_frame(formatCtx_, videoStreamIndex_, timestamp,
                            AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        std::cerr << "[FFmpegDecoder] Seek failed" << std::endl;
        return;
    }

    // Flush codec buffers after seek
    if (videoCodecCtx_) {
        avcodec_flush_buffers(videoCodecCtx_);
    }
    if (audioCodecCtx_) {
        avcodec_flush_buffers(audioCodecCtx_);
    }

    // Flush audio buffer
    if (audioPlayer_) {
        audioPlayer_->flush();
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
