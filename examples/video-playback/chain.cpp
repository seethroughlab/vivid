// Video Playback Example using VideoFile operator
// Demonstrates the built-in VideoFile operator with playback controls

#include <vivid/vivid.h>
#include <iostream>
#include <cmath>
#include <sys/stat.h>

using namespace vivid;

// VideoFile operator - copied from operators/videofile.cpp for testing
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
        std::cerr << "[VideoFile] Getting frame..." << std::flush;
        bool gotFrame = ctx.videoGetFrame(player_, output_);
        std::cerr << (gotFrame ? "OK" : "FAIL") << "\n" << std::flush;

        if (gotFrame) {
            ctx.setOutput("out", output_);
        } else if (output_.valid()) {
            ctx.setOutput("out", output_);
        } else {
            ctx.setOutput("out", Texture{});
        }

        // Output video metadata
        ctx.setOutput("duration", static_cast<float>(duration_));
        ctx.setOutput("position", static_cast<float>(playhead_));
        ctx.setOutput("progress", duration_ > 0 ? static_cast<float>(playhead_ / duration_) : 0.0f);
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

        struct stat st;
        if (stat(path_.c_str(), &st) == 0) {
            lastMtime_ = st.st_mtime;
        }

        std::cout << "[VideoFile] Loaded " << path_
                  << " (" << width_ << "x" << height_
                  << ", " << duration_ << "s"
                  << ", " << frameRate_ << "fps)\n";
    }

    std::string path_;
    bool loop_ = true;
    float speed_ = 1.0f;
    bool playing_ = true;

    VideoPlayer player_;
    Texture output_;
    bool needsLoad_ = false;
    time_t lastMtime_ = 0;

    int width_ = 0;
    int height_ = 0;
    double duration_ = 0;
    double frameRate_ = 0;
    double playhead_ = 0;
};

// Main chain that uses VideoFile
class VideoChain : public Operator {
public:
    void init(Context& ctx) override {
        // Configure video player with test video (H.264 MP4)
        video_.path("examples/video-playback/assets/road_30fps.mp4")
              .loop(true)
              .speed(1.0f)
              .play();
    }

    void process(Context& ctx) override {
        video_.process(ctx);
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    VideoFile video_;
};

VIVID_OPERATOR(VideoChain)
