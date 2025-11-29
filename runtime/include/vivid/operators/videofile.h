#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <sys/stat.h>
#include <iostream>
#include <cmath>
#include <string>

namespace vivid {

/**
 * @brief Video file playback operator.
 *
 * Loads and plays video files with full playback controls including
 * play/pause, seeking, looping, and variable speed playback.
 *
 * Outputs:
 * - "out": Current video frame as texture
 * - "duration": Total video duration in seconds
 * - "position": Current playback position in seconds
 * - "progress": Normalized position (0.0 to 1.0)
 * - "fps": Video frame rate
 * - "width": Video width in pixels
 * - "height": Video height in pixels
 * - "playing": 1.0 if playing, 0.0 if paused
 *
 * Example:
 * @code
 * VideoFile video_;
 * video_.path("video.mp4").loop(true).play();
 * @endcode
 */
class VideoFile : public Operator {
public:
    VideoFile() = default;

    /// Set the video file path
    VideoFile& path(const std::string& p) {
        if (p != path_) {
            path_ = p;
            needsLoad_ = true;
        }
        return *this;
    }

    /// Alias for path()
    VideoFile& file(const std::string& p) { return path(p); }

    /// Enable or disable looping
    VideoFile& loop(bool enabled = true) {
        loop_ = enabled;
        return *this;
    }

    /// Set playback speed (1.0 = normal, negative = reverse)
    VideoFile& speed(float s) {
        speed_ = s;
        return *this;
    }

    /// Start playback
    VideoFile& play() {
        playing_ = true;
        return *this;
    }

    /// Pause playback
    VideoFile& pause() {
        playing_ = false;
        return *this;
    }

    /// Toggle play/pause state
    VideoFile& toggle() {
        playing_ = !playing_;
        return *this;
    }

    /// Seek to normalized position (0.0 to 1.0)
    VideoFile& seek(float normalizedPosition) {
        seekTarget_ = glm::clamp(normalizedPosition, 0.0f, 1.0f);
        needsSeek_ = true;
        return *this;
    }

    /// Seek to specific time in seconds
    VideoFile& seekTime(float seconds) {
        seekTimeTarget_ = seconds;
        needsSeekTime_ = true;
        return *this;
    }

    void init(Context& ctx) override {
        // Player will be created on first process
    }

    void process(Context& ctx) override {
        if (path_.empty()) {
            ctx.setOutput("out", Texture{});
            return;
        }

        // Check if file changed on disk (hot-reload)
        if (checkFileChanged()) {
            needsLoad_ = true;
        }

        // Load video if needed
        if (needsLoad_) {
            loadVideo(ctx);
            needsLoad_ = false;
        }

        if (!player_.valid()) {
            ctx.setOutput("out", Texture{});
            return;
        }

        // Handle seeking
        if (needsSeek_ && duration_ > 0) {
            double seekTime = seekTarget_ * duration_;
            ctx.videoSeek(player_, seekTime);
            playhead_ = seekTime;
            lastFrameTime_ = -1.0;
            needsSeek_ = false;
        }

        if (needsSeekTime_) {
            ctx.videoSeek(player_, seekTimeTarget_);
            playhead_ = seekTimeTarget_;
            lastFrameTime_ = -1.0;
            needsSeekTime_ = false;
        }

        // Advance playhead if playing
        if (playing_ && duration_ > 0) {
            playhead_ += ctx.dt() * speed_;

            // Handle looping
            if (playhead_ >= duration_) {
                if (loop_) {
                    playhead_ = std::fmod(playhead_, duration_);
                    ctx.videoSeek(player_, playhead_);
                    lastFrameTime_ = -1.0;
                } else {
                    playhead_ = duration_;
                    playing_ = false;
                }
            } else if (playhead_ < 0) {
                if (loop_) {
                    playhead_ = duration_ + std::fmod(playhead_, duration_);
                    ctx.videoSeek(player_, playhead_);
                    lastFrameTime_ = -1.0;
                } else {
                    playhead_ = 0;
                    playing_ = false;
                }
            }
        }

        // Only decode new frame when needed (based on video frame rate)
        double frameInterval = frameRate_ > 0 ? 1.0 / frameRate_ : 1.0 / 30.0;
        bool needNewFrame = (playhead_ - lastFrameTime_) >= frameInterval || lastFrameTime_ < 0;

        if (needNewFrame) {
            if (ctx.videoGetFrame(player_, output_)) {
                lastFrameTime_ = playhead_;
            }
        }

        // Output current frame
        if (output_.valid()) {
            ctx.setOutput("out", output_);
        } else {
            ctx.setOutput("out", Texture{});
        }

        // Output video metadata
        ctx.setOutput("duration", static_cast<float>(duration_));
        ctx.setOutput("position", static_cast<float>(playhead_));
        ctx.setOutput("progress", duration_ > 0 ? static_cast<float>(playhead_ / duration_) : 0.0f);
        ctx.setOutput("fps", static_cast<float>(frameRate_));
        ctx.setOutput("width", static_cast<float>(width_));
        ctx.setOutput("height", static_cast<float>(height_));
        ctx.setOutput("playing", playing_ ? 1.0f : 0.0f);
    }

    // Note: Video player cleanup is handled by the destructor and
    // when loadVideo() replaces the player. The Context may not be
    // available during cleanup() since it takes no parameters.

    std::vector<ParamDecl> params() override {
        return {
            stringParam("path", path_),
            boolParam("loop", loop_),
            floatParam("speed", speed_, -4.0f, 4.0f),
            boolParam("playing", playing_),
            floatParam("seek", seekTarget_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override {
        return OutputKind::Texture;
    }

private:
    bool checkFileChanged() {
        if (path_.empty()) return false;
        struct stat st;
        if (stat(path_.c_str(), &st) != 0) return false;
        time_t currentMtime = st.st_mtime;
        if (currentMtime != lastMtime_) {
            lastMtime_ = currentMtime;
            return lastMtime_ != 0;
        }
        return false;
    }

    void loadVideo(Context& ctx) {
        if (player_.valid()) {
            ctx.destroyVideoPlayer(player_);
        }

        player_ = ctx.createVideoPlayer(path_);
        if (!player_.valid()) {
            std::cerr << "[VideoFile] Failed to open: " << path_ << "\n";
            return;
        }

        VideoInfo info = ctx.getVideoInfo(player_);
        width_ = info.width;
        height_ = info.height;
        duration_ = info.duration;
        frameRate_ = info.frameRate;
        playhead_ = 0;
        lastFrameTime_ = -1.0;

        struct stat st;
        if (stat(path_.c_str(), &st) == 0) {
            lastMtime_ = st.st_mtime;
        }

        std::cout << "[VideoFile] Loaded " << path_
                  << " (" << width_ << "x" << height_
                  << ", " << duration_ << "s"
                  << ", " << frameRate_ << "fps)\n";
    }

    // Parameters
    std::string path_;
    bool loop_ = true;
    float speed_ = 1.0f;
    bool playing_ = true;
    float seekTarget_ = 0.0f;
    float seekTimeTarget_ = 0.0f;
    bool needsSeek_ = false;
    bool needsSeekTime_ = false;

    // State
    VideoPlayer player_;
    Texture output_;
    bool needsLoad_ = false;
    time_t lastMtime_ = 0;

    // Video info
    int width_ = 0;
    int height_ = 0;
    double duration_ = 0;
    double frameRate_ = 0;
    double playhead_ = 0;
    double lastFrameTime_ = -1.0;
};

} // namespace vivid
