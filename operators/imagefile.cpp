// ImageFile Operator
// Loads an image from disk and outputs it as a texture
// Supports hot-reload: automatically reloads when the file changes

#include <vivid/vivid.h>
#include <sys/stat.h>
#include <iostream>

using namespace vivid;

class ImageFile : public Operator {
public:
    ImageFile() = default;

    // Fluent API
    ImageFile& path(const std::string& p) {
        path_ = p;
        needsLoad_ = true;
        return *this;
    }

    // Alias for path
    ImageFile& file(const std::string& p) {
        return path(p);
    }

    void init(Context& ctx) override {
        // Don't create texture yet - wait for process() to know dimensions
    }

    void process(Context& ctx) override {
        if (path_.empty()) {
            // No path set - output invalid texture
            ctx.setOutput("out", Texture{});
            return;
        }

        // Check if file has changed
        if (checkFileChanged()) {
            needsLoad_ = true;
        }

        // Load image if needed
        if (needsLoad_) {
            loadImage(ctx);
            needsLoad_ = false;
        }

        // Output the loaded texture
        if (output_.valid()) {
            ctx.setOutput("out", output_);
            // Also output dimensions as values for resolution-independent pipelines
            ctx.setOutput("width", static_cast<float>(output_.width));
            ctx.setOutput("height", static_cast<float>(output_.height));
        } else {
            ctx.setOutput("out", Texture{});
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            stringParam("path", path_)
        };
    }

    OutputKind outputKind() override {
        return OutputKind::Texture;
    }

    bool needsUpdate(Context& ctx) override {
        // Always process to check for file changes
        return true;
    }

private:
    bool checkFileChanged() {
        if (path_.empty()) return false;

        struct stat st;
        if (stat(path_.c_str(), &st) != 0) {
            return false;  // File doesn't exist
        }

        time_t currentMtime = st.st_mtime;
        if (currentMtime != lastMtime_) {
            lastMtime_ = currentMtime;
            return true;
        }
        return false;
    }

    void loadImage(Context& ctx) {
        // Use Context API to load image
        Texture newTex = ctx.loadImageAsTexture(path_);

        if (!newTex.valid()) {
            std::cerr << "[ImageFile] Failed to load: " << path_ << "\n";
            return;
        }

        // Replace old texture with new one
        output_ = newTex;

        std::cout << "[ImageFile] Loaded " << path_
                  << " (" << output_.width << "x" << output_.height << ")\n";
    }

    std::string path_;
    Texture output_;
    time_t lastMtime_ = 0;
    bool needsLoad_ = false;
};

VIVID_OPERATOR(ImageFile)
