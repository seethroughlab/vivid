#include <vivid/context.h>
#include "renderer.h"

namespace vivid {

Context::Context(Renderer& renderer, int width, int height)
    : renderer_(renderer), width_(width), height_(height) {}

Texture Context::createTexture(int width, int height) {
    return renderer_.createTexture(width, height);
}

Texture Context::createTexture() {
    return renderer_.createTexture(width_, height_);
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
    // Load shader if not already loaded (with caching in future)
    Shader shader = renderer_.loadShaderFromFile(shaderPath);
    if (!shader.valid()) {
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

    renderer_.runShader(shader, output, input, uniforms);
    renderer_.destroyShader(shader);  // TODO: Cache shaders instead
}

void Context::runShader(const std::string& shaderPath, const Texture* input1,
                        const Texture* input2, Texture& output, const ShaderParams& params) {
    // For shaders that need two inputs, we use input1 as the primary input
    // input2 would need to be sampled separately (future enhancement)
    // For now, just use the first input
    runShader(shaderPath, input1, output, params);
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

} // namespace vivid
