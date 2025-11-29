#include <vivid/context.h>
#include "renderer.h"
#include "window.h"
#include "image_loader.h"
#include "video_loader.h"

namespace vivid {

Context::Context(Renderer& renderer, int width, int height)
    : renderer_(renderer), width_(width), height_(height) {}

Context::Context(Renderer& renderer, Window& window, int width, int height)
    : renderer_(renderer), window_(&window), width_(width), height_(height) {}

Texture Context::createTexture(int width, int height) {
    return renderer_.createTexture(width, height);
}

Texture Context::createTexture() {
    return renderer_.createTexture(width_, height_);
}

Texture Context::createTextureMatching(const Texture& matchTexture) {
    if (matchTexture.valid()) {
        return renderer_.createTexture(matchTexture.width, matchTexture.height);
    }
    return renderer_.createTexture(width_, height_);
}

Texture Context::createTextureMatching(const std::string& nodeId, const std::string& output) {
    Texture* input = getInputTexture(nodeId, output);
    if (input && input->valid()) {
        return renderer_.createTexture(input->width, input->height);
    }
    return renderer_.createTexture(width_, height_);
}

Texture Context::loadImageAsTexture(const std::string& path) {
    ImageLoader loader;
    return loader.loadAsTexture(path, renderer_);
}

void Context::uploadTexturePixels(Texture& texture, const uint8_t* pixels, int width, int height) {
    renderer_.uploadTexturePixels(texture, pixels, width, height);
}

bool Context::isImageSupported(const std::string& path) {
    return ImageLoader::isSupported(path);
}

// Video playback methods
VideoPlayer Context::createVideoPlayer(const std::string& path) {
    VideoPlayer player;
    auto loader = createVideoLoaderForPath(path);  // Auto-detects HAP
    if (loader && loader->open(path)) {
        player.handle = loader.release();  // Transfer ownership
    }
    return player;
}

void Context::destroyVideoPlayer(VideoPlayer& player) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        loader->close();
        delete loader;
        player.handle = nullptr;
    }
}

VideoInfo Context::getVideoInfo(const VideoPlayer& player) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->info();
    }
    return VideoInfo{};
}

bool Context::videoSeek(VideoPlayer& player, double timeSeconds) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->seek(timeSeconds);
    }
    return false;
}

bool Context::videoGetFrame(VideoPlayer& player, Texture& output) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->getFrame(output, renderer_);
    }
    return false;
}

double Context::videoGetTime(const VideoPlayer& player) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->currentTime();
    }
    return 0.0;
}

bool Context::isVideoSupported(const std::string& path) {
    return VideoLoader::isSupported(path);
}

void Context::runShader(const std::string& shaderPath, Texture& output) {
    runShader(shaderPath, nullptr, output);
}

void Context::runShader(const std::string& shaderPath, const Texture* input, Texture& output) {
    ShaderParams defaultParams;
    runShader(shaderPath, input, output, defaultParams);
}

void Context::runShader(const std::string& shaderPath, const Texture* input,
                        Texture& output, const ShaderParams& params) {
    // Get shader from cache (or load and cache it)
    Shader* shader = getCachedShader(shaderPath);
    if (!shader || !shader->valid()) {
        return;
    }

    // Set up uniforms with core values
    Uniforms uniforms;
    uniforms.time = time_;
    uniforms.deltaTime = dt_;
    uniforms.resolutionX = static_cast<float>(output.width);
    uniforms.resolutionY = static_cast<float>(output.height);
    uniforms.frame = frame_;
    uniforms.mode = params.mode;

    // Copy operator parameters
    uniforms.param0 = params.param0;
    uniforms.param1 = params.param1;
    uniforms.param2 = params.param2;
    uniforms.param3 = params.param3;
    uniforms.param4 = params.param4;
    uniforms.param5 = params.param5;
    uniforms.param6 = params.param6;
    uniforms.param7 = params.param7;
    uniforms.vec0X = params.vec0X;
    uniforms.vec0Y = params.vec0Y;
    uniforms.vec1X = params.vec1X;
    uniforms.vec1Y = params.vec1Y;

    renderer_.runShader(*shader, output, input, uniforms);
}

void Context::runShader(const std::string& shaderPath, const Texture* input1,
                        const Texture* input2, Texture& output, const ShaderParams& params) {
    // Get shader from cache (or load and cache it)
    Shader* shader = getCachedShader(shaderPath);
    if (!shader || !shader->valid()) {
        return;
    }

    // Set up uniforms with core values
    Uniforms uniforms;
    uniforms.time = time_;
    uniforms.deltaTime = dt_;
    uniforms.resolutionX = static_cast<float>(output.width);
    uniforms.resolutionY = static_cast<float>(output.height);
    uniforms.frame = frame_;
    uniforms.mode = params.mode;

    // Copy operator parameters
    uniforms.param0 = params.param0;
    uniforms.param1 = params.param1;
    uniforms.param2 = params.param2;
    uniforms.param3 = params.param3;
    uniforms.param4 = params.param4;
    uniforms.param5 = params.param5;
    uniforms.param6 = params.param6;
    uniforms.param7 = params.param7;
    uniforms.vec0X = params.vec0X;
    uniforms.vec0Y = params.vec0Y;
    uniforms.vec1X = params.vec1X;
    uniforms.vec1Y = params.vec1Y;

    renderer_.runShader(*shader, output, input1, input2, uniforms);
}

void Context::setOutput(const std::string& name, const Texture& tex) {
    textureOutputs_[name] = tex;
}

void Context::setOutput(const std::string& name, float value) {
    valueOutputs_[name] = value;
}

void Context::setOutput(const std::string& name, const std::vector<float>& values) {
    valueArrayOutputs_[name] = values;
}

Texture* Context::getInputTexture(const std::string& nodeId, const std::string& output) {
    std::string key = nodeId + "." + output;
    auto it = textureOutputs_.find(key);
    if (it != textureOutputs_.end()) {
        return &it->second;
    }
    // Also check without the output suffix
    it = textureOutputs_.find(nodeId);
    if (it != textureOutputs_.end()) {
        return &it->second;
    }
    return nullptr;
}

float Context::getInputValue(const std::string& nodeId, const std::string& output, float defaultVal) {
    std::string key = nodeId + "." + output;
    auto it = valueOutputs_.find(key);
    if (it != valueOutputs_.end()) {
        return it->second;
    }
    it = valueOutputs_.find(nodeId);
    if (it != valueOutputs_.end()) {
        return it->second;
    }
    return defaultVal;
}

std::vector<float> Context::getInputValues(const std::string& nodeId, const std::string& output) {
    std::string key = nodeId + "." + output;
    auto it = valueArrayOutputs_.find(key);
    if (it != valueArrayOutputs_.end()) {
        return it->second;
    }
    it = valueArrayOutputs_.find(nodeId);
    if (it != valueArrayOutputs_.end()) {
        return it->second;
    }
    return {};
}

void Context::beginFrame(float time, float dt, int frame) {
    time_ = time;
    dt_ = dt;
    frame_ = frame;
}

void Context::endFrame() {
    // Nothing to do for now
}

void Context::clearOutputs() {
    textureOutputs_.clear();
    valueOutputs_.clear();
    valueArrayOutputs_.clear();
}

void Context::clearShaderCache() {
    for (auto& [path, shader] : shaderCache_) {
        if (shader) {
            renderer_.destroyShader(*shader);
        }
    }
    shaderCache_.clear();
}

bool Context::isKeyDown(int key) const {
    return window_ ? window_->isKeyDown(key) : false;
}

bool Context::wasKeyPressed(int key) const {
    return window_ ? window_->wasKeyPressed(key) : false;
}

bool Context::wasKeyReleased(int key) const {
    return window_ ? window_->wasKeyReleased(key) : false;
}

Shader* Context::getCachedShader(const std::string& path) {
    auto it = shaderCache_.find(path);
    if (it != shaderCache_.end()) {
        return it->second.get();
    }

    // Load and cache the shader
    auto shader = std::make_unique<Shader>(renderer_.loadShaderFromFile(path));
    if (!shader->valid()) {
        return nullptr;
    }

    Shader* result = shader.get();
    shaderCache_[path] = std::move(shader);
    return result;
}

} // namespace vivid
