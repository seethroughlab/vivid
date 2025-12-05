#pragma once

#include <string>
#include <memory>
#include <cstdint>

// Forward declarations for Diligent types
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct ITexture;
    struct ITextureView;
}

namespace vivid {

// Forward declaration
class Context;

namespace video {

/**
 * @brief Video player for hardware-accelerated video playback.
 *
 * Uses platform-native decoders:
 * - macOS: AVFoundation
 * - Windows: Media Foundation (future)
 * - Linux: FFmpeg (future)
 *
 * Usage:
 *   VideoPlayer player;
 *   player.open(ctx, "video.mp4");
 *   while (!player.isFinished()) {
 *       player.update(ctx);
 *       // Use player.textureView() in your shader
 *   }
 */
class VideoPlayer {
public:
    VideoPlayer();
    ~VideoPlayer();

    // Non-copyable
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // Move semantics
    VideoPlayer(VideoPlayer&& other) noexcept;
    VideoPlayer& operator=(VideoPlayer&& other) noexcept;

    /**
     * @brief Open a video file
     * @param ctx Vivid context for GPU resources
     * @param path Path to video file
     * @param loop Whether to loop playback
     * @return true if successfully opened
     */
    bool open(Context& ctx, const std::string& path, bool loop = false);

    /**
     * @brief Close the video and release resources
     */
    void close();

    /**
     * @brief Update video playback and upload current frame to texture
     * @param ctx Vivid context
     * Call this every frame to advance playback
     */
    void update(Context& ctx);

    /**
     * @brief Seek to a specific time
     * @param seconds Time in seconds from start
     */
    void seek(float seconds);

    /**
     * @brief Pause playback
     */
    void pause();

    /**
     * @brief Resume playback
     */
    void play();

    /**
     * @brief Check if video is currently playing
     */
    bool isPlaying() const;

    /**
     * @brief Check if video has finished (and not looping)
     */
    bool isFinished() const;

    /**
     * @brief Check if video is valid and open
     */
    bool isOpen() const;

    /**
     * @brief Get current playback time in seconds
     */
    float currentTime() const;

    /**
     * @brief Get total duration in seconds
     */
    float duration() const;

    /**
     * @brief Get video width in pixels
     */
    int width() const;

    /**
     * @brief Get video height in pixels
     */
    int height() const;

    /**
     * @brief Get frame rate (frames per second)
     */
    float frameRate() const;

    /**
     * @brief Get the texture containing the current video frame
     * Use this in your shader bindings
     */
    Diligent::ITexture* texture() const;

    /**
     * @brief Get the shader resource view for the current frame
     * Use this for binding to shaders
     */
    Diligent::ITextureView* textureView() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Video texture source operator for use in operator chains.
 *
 * Integrates with the vivid operator system so you can use it
 * with Output or other operators.
 *
 * Usage:
 *   VideoSource video;
 *   video.open(ctx, "video.mp4");
 *   output.setInput(&video);
 *
 *   // In update:
 *   video.process(ctx);  // Advances video and uploads frame
 *   output.process(ctx); // Displays video on screen
 */
class VideoSource {
public:
    VideoSource();
    ~VideoSource();

    /**
     * @brief Open a video file
     * @param ctx Vivid context
     * @param path Path to video file
     * @param loop Whether to loop playback
     * @return true if successful
     */
    bool open(Context& ctx, const std::string& path, bool loop = false);

    /**
     * @brief Close and release resources
     */
    void close();

    /**
     * @brief Process - advances video and updates texture
     * Call this every frame
     */
    void process(Context& ctx);

    /**
     * @brief Get the output texture view (for operator connections)
     * Compatible with Operator::getOutputSRV()
     */
    Diligent::ITextureView* getOutputSRV() const;

    /**
     * @brief Get the underlying video player for additional control
     */
    VideoPlayer& player();
    const VideoPlayer& player() const;

    /**
     * @brief Check if video is open
     */
    bool isOpen() const;

private:
    VideoPlayer player_;
};

/**
 * @brief Simple video display that renders video to screen.
 *
 * All-in-one class for video playback without needing the operator system.
 *
 * Usage:
 *   VideoDisplay display;
 *   display.open(ctx, "video.mp4", true);  // Open with looping
 *
 *   // In update:
 *   display.update(ctx);  // Updates and renders video
 */
class VideoDisplay {
public:
    VideoDisplay();
    ~VideoDisplay();

    // Non-copyable
    VideoDisplay(const VideoDisplay&) = delete;
    VideoDisplay& operator=(const VideoDisplay&) = delete;

    /**
     * @brief Open a video file
     * @param ctx Vivid context
     * @param path Path to video file
     * @param loop Whether to loop playback
     * @return true if successful
     */
    bool open(Context& ctx, const std::string& path, bool loop = false);

    /**
     * @brief Close and release resources
     */
    void close();

    /**
     * @brief Update video and render to screen
     * Call this every frame
     */
    void update(Context& ctx);

    /**
     * @brief Pause playback
     */
    void pause();

    /**
     * @brief Resume playback
     */
    void play();

    /**
     * @brief Seek to time
     */
    void seek(float seconds);

    /**
     * @brief Check if playing
     */
    bool isPlaying() const;

    /**
     * @brief Check if finished (and not looping)
     */
    bool isFinished() const;

    /**
     * @brief Get current time
     */
    float currentTime() const;

    /**
     * @brief Get duration
     */
    float duration() const;

    /**
     * @brief Get the underlying player
     */
    VideoPlayer& player();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace video
} // namespace vivid
