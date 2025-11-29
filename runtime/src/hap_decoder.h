#pragma once

#ifdef VIVID_HAS_FFMPEG

#include <vivid/types.h>
#include <string>
#include <vector>
#include <cstdint>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;

namespace vivid {

class Renderer;

/**
 * @brief HAP video decoder using FFmpeg for demuxing and Snappy for decompression.
 *
 * HAP is a GPU-accelerated video codec that stores DXT (S3TC) compressed texture data.
 * The decode pipeline:
 * 1. FFmpeg demuxes the container (MOV/AVI) to extract raw HAP frame data
 * 2. Snappy decompresses the frame (HAP uses Snappy compression)
 * 3. The DXT data is uploaded directly to GPU as a compressed texture
 *
 * This bypasses CPU-side pixel decoding entirely - the GPU decompresses DXT natively.
 *
 * HAP Variants:
 * - HAP: DXT1 (BC1) - RGB, 4:1 compression
 * - HAP Alpha: DXT5 (BC3) - RGBA with alpha
 * - HAP Q: Scaled DXT5 - Higher quality
 * - HAP Q Alpha: Scaled DXT5 with alpha
 */
class HAPDecoder {
public:
    HAPDecoder();
    ~HAPDecoder();

    // Non-copyable
    HAPDecoder(const HAPDecoder&) = delete;
    HAPDecoder& operator=(const HAPDecoder&) = delete;

    /**
     * @brief Open a HAP video file.
     * @param path Path to MOV/AVI file containing HAP codec.
     * @return true if opened successfully.
     */
    bool open(const std::string& path);

    /**
     * @brief Close the video and release resources.
     */
    void close();

    /**
     * @brief Check if a video is currently open.
     */
    bool isOpen() const { return formatCtx_ != nullptr; }

    /**
     * @brief Get video metadata.
     */
    const VideoInfo& info() const { return info_; }

    /**
     * @brief Seek to a specific time.
     * @param timeSeconds Time in seconds from start.
     * @return true if seek succeeded.
     */
    bool seek(double timeSeconds);

    /**
     * @brief Get the next frame and upload to texture as compressed DXT.
     * @param output Texture to receive the frame.
     * @param renderer Renderer for texture operations.
     * @return true if a new frame was decoded and uploaded.
     */
    bool getFrame(Texture& output, Renderer& renderer);

    /**
     * @brief Get current playback position.
     * @return Current time in seconds.
     */
    double currentTime() const { return currentTime_; }

    /**
     * @brief Check if a file contains HAP codec.
     * @param path Path to check.
     * @return true if file uses HAP codec.
     */
    static bool isHAPFile(const std::string& path);

private:
    // FFmpeg state
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    int videoStreamIndex_ = -1;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;

    // Video info
    VideoInfo info_;
    double currentTime_ = 0.0;
    double timeBase_ = 0.0;
};

} // namespace vivid

#endif // VIVID_HAS_FFMPEG
