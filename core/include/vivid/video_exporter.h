#pragma once

/**
 * @file video_exporter.h
 * @brief Video recording and export functionality
 *
 * Captures frames from the render output and encodes them to video files.
 * Supports multiple codecs via platform-specific hardware acceleration.
 */

#include <webgpu/webgpu.h>
#include <string>
#include <functional>

namespace vivid {

/**
 * @brief Video export codec options
 */
enum class ExportCodec {
    Animation,  ///< ProRes 4444 - lossless, large files, best for editing
    H264,       ///< H.264/AVC - good quality, widely compatible
    H265        ///< H.265/HEVC - best compression, hardware accelerated
};

/**
 * @brief Video exporter for recording chain output
 *
 * Captures frames from WebGPU textures and encodes them to video files
 * using platform-specific hardware encoders (VideoToolbox on macOS).
 *
 * @par Example
 * @code
 * VideoExporter exporter;
 * exporter.start("output.mov", 1920, 1080, 60.0f, ExportCodec::H265);
 *
 * // In render loop:
 * if (exporter.isRecording()) {
 *     exporter.captureFrame(device, queue, outputTexture);
 * }
 *
 * exporter.stop();
 * @endcode
 */
class VideoExporter {
public:
    VideoExporter();
    ~VideoExporter();

    // Non-copyable
    VideoExporter(const VideoExporter&) = delete;
    VideoExporter& operator=(const VideoExporter&) = delete;

    /**
     * @brief Start recording to a file
     * @param path Output file path (.mov or .mp4)
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param fps Frames per second
     * @param codec Encoding codec to use
     * @return true if recording started successfully
     */
    bool start(const std::string& path, int width, int height,
               float fps, ExportCodec codec);

    /**
     * @brief Capture a frame from a WebGPU texture
     * @param device WebGPU device
     * @param queue WebGPU queue
     * @param texture Source texture to capture
     *
     * Call this once per frame while recording. The texture format
     * should be RGBA8 or BGRA8.
     */
    void captureFrame(WGPUDevice device, WGPUQueue queue, WGPUTexture texture);

    /**
     * @brief Stop recording and finalize the video file
     *
     * This flushes any pending frames and closes the output file.
     * May block briefly while encoder finishes.
     */
    void stop();

    /**
     * @brief Check if currently recording
     * @return true if recording is active
     */
    bool isRecording() const { return m_recording; }

    /**
     * @brief Get number of frames captured
     * @return Frame count since recording started
     */
    int frameCount() const { return m_frameCount; }

    /**
     * @brief Get recording duration in seconds
     * @return Duration based on frame count and fps
     */
    float duration() const;

    /**
     * @brief Get the output file path
     * @return Path to the output file, or empty if not recording
     */
    const std::string& outputPath() const { return m_outputPath; }

    /**
     * @brief Get any error message from the last operation
     * @return Error string, or empty if no error
     */
    const std::string& error() const { return m_error; }

    /**
     * @brief Generate an auto-named output path
     * @param directory Output directory (defaults to current directory)
     * @param codec Codec to use (affects file extension)
     * @return Path like "vivid_20241209_143022.mov"
     */
    static std::string generateOutputPath(const std::string& directory = ".",
                                          ExportCodec codec = ExportCodec::H264);

    // Internal: encode a frame from the mapped buffer (called from async callback)
    void encodeFrame(uint32_t width, uint32_t height, uint32_t bytesPerRow, uint32_t bytesPerPixel);

private:
    // Platform-specific implementation (opaque pointer)
    struct Impl;
    Impl* m_impl = nullptr;

    bool m_recording = false;
    int m_frameCount = 0;
    float m_fps = 60.0f;
    int m_width = 0;
    int m_height = 0;
    std::string m_outputPath;
    std::string m_error;

    // Frame readback buffer
    WGPUBuffer m_readbackBuffer = nullptr;
    size_t m_bufferSize = 0;
};

} // namespace vivid
