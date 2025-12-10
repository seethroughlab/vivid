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
     * @brief Start recording to a file (video only)
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
     * @brief Start recording to a file with audio
     * @param path Output file path (.mov or .mp4)
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param fps Frames per second
     * @param codec Encoding codec to use
     * @param audioSampleRate Audio sample rate in Hz (typically 48000)
     * @param audioChannels Audio channel count (1=mono, 2=stereo)
     * @return true if recording started successfully
     */
    bool startWithAudio(const std::string& path, int width, int height,
                        float fps, ExportCodec codec,
                        uint32_t audioSampleRate, uint32_t audioChannels);

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
     * @brief Push audio samples for the current frame
     * @param samples Interleaved float samples [-1.0, 1.0]
     * @param frameCount Number of audio frames (samples per channel)
     *
     * Call this after captureFrame() to add audio to the video.
     * Audio is only recorded if startWithAudio() was used.
     */
    void pushAudioSamples(const float* samples, uint32_t frameCount);

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
     * @brief Check if recording includes audio
     * @return true if audio is being recorded
     */
    bool hasAudio() const { return m_audioEnabled; }

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

    // Double-buffered async readback
    static constexpr int NUM_READBACK_BUFFERS = 2;
    WGPUBuffer m_readbackBuffers[NUM_READBACK_BUFFERS] = {nullptr, nullptr};
    bool m_bufferMapped[NUM_READBACK_BUFFERS] = {false, false};  // Track mapped state
    size_t m_bufferSize = 0;
    int m_currentBuffer = 0;  // Buffer being written to
    int m_pendingBuffer = -1; // Buffer waiting to be encoded (-1 = none)
    bool m_hasPendingFrame = false;
    uint32_t m_pendingWidth = 0;
    uint32_t m_pendingHeight = 0;
    uint32_t m_pendingBytesPerRow = 0;
    uint32_t m_pendingBytesPerPixel = 0;
    WGPUDevice m_device = nullptr;  // Cached for async polling

    // Legacy single buffer (for compatibility)
    WGPUBuffer m_readbackBuffer = nullptr;

    // Audio settings
    bool m_audioEnabled = false;
    uint32_t m_audioSampleRate = 48000;
    uint32_t m_audioChannels = 2;
    uint64_t m_audioFrameCount = 0;  // Total audio frames written

    // Async readback helpers
    void submitCopyCommand(WGPUDevice device, WGPUQueue queue, WGPUTexture texture,
                           int bufferIndex, uint32_t width, uint32_t height,
                           uint32_t bytesPerRow, uint32_t bytesPerPixel);
    bool tryEncodePendingFrame();
};

} // namespace vivid
