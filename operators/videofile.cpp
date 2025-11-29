// VideoFile Operator
// Loads and plays video files with playback controls
// Supports hot-reload: automatically reloads when the file changes

#include <vivid/vivid.h>
#include <sys/stat.h>
#include <iostream>
#include <cmath>

using namespace vivid;

class VideoFile : public Operator {
public:
    VideoFile() = default;

    // Fluent API
    VideoFile& path(const std::string& p) {
        if (p != path_) {
            path_ = p;
            needsLoad_ = true;
        }
        return *this;
    }

    VideoFile& file(const std::string& p) { return path(p); }

    VideoFile& loop(bool enabled = true) {
        loop_ = enabled;
        return *this;
    }

    VideoFile& speed(float s) {
        speed_ = s;
        return *this;
    }

    VideoFile& play() {
        playing_ = true;
        return *this;
    }

    VideoFile& pause() {
        playing_ = false;
        return *this;
    }

    VideoFile& toggle() {
        playing_ = !playing_;
        return *this;
    }

    VideoFile& seek(float normalizedPosition) {
        seekTarget_ = glm::clamp(normalizedPosition, 0.0f, 1.0f);
        needsSeek_ = true;
        return *this;
    }

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

        // Check if file changed on disk
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
            needsSeek_ = false;
        }

        if (needsSeekTime_) {
            ctx.videoSeek(player_, seekTimeTarget_);
            playhead_ = seekTimeTarget_;
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
                } else {
                    playhead_ = duration_;
                    playing_ = false;
                }
            } else if (playhead_ < 0) {
                if (loop_) {
                    playhead_ = duration_ + std::fmod(playhead_, duration_);
                    ctx.videoSeek(player_, playhead_);
                } else {
                    playhead_ = 0;
                    playing_ = false;
                }
            }
        }

        // Get the current frame
        if (ctx.videoGetFrame(player_, output_)) {
            ctx.setOutput("out", output_);
        } else if (output_.valid()) {
            // No new frame, output last frame
            ctx.setOutput("out", output_);
        } else {
            ctx.setOutput("out", Texture{});
        }

        // Output video metadata as values
        ctx.setOutput("duration", static_cast<float>(duration_));
        ctx.setOutput("position", static_cast<float>(playhead_));
        ctx.setOutput("progress", duration_ > 0 ? static_cast<float>(playhead_ / duration_) : 0.0f);
        ctx.setOutput("fps", static_cast<float>(frameRate_));
        ctx.setOutput("width", static_cast<float>(width_));
        ctx.setOutput("height", static_cast<float>(height_));
        ctx.setOutput("playing", playing_ ? 1.0f : 0.0f);
    }

    void cleanup(Context& ctx) override {
        if (player_.valid()) {
            ctx.destroyVideoPlayer(player_);
        }
    }

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
        if (stat(path_.c_str(), &st) != 0) {
            return false;
        }

        time_t currentMtime = st.st_mtime;
        if (currentMtime != lastMtime_) {
            lastMtime_ = currentMtime;
            return lastMtime_ != 0;  // Don't trigger on first check
        }
        return false;
    }

    void loadVideo(Context& ctx) {
        // Destroy existing player
        if (player_.valid()) {
            ctx.destroyVideoPlayer(player_);
        }

        // Create new player
        player_ = ctx.createVideoPlayer(path_);
        if (!player_.valid()) {
            std::cerr << "[VideoFile] Failed to open: " << path_ << "\n";
            return;
        }

        // Get video info
        VideoInfo info = ctx.getVideoInfo(player_);
        width_ = info.width;
        height_ = info.height;
        duration_ = info.duration;
        frameRate_ = info.frameRate;
        playhead_ = 0;

        // Update mtime for hot-reload detection
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
};

VIVID_OPERATOR(VideoFile)
