#pragma once
#include "types.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

// Forward declarations
class Renderer;

class Context {
public:
    Context(Renderer& renderer, int width, int height);

    // Time information
    float time() const { return time_; }
    float dt() const { return dt_; }
    int frame() const { return frame_; }

    // Output resolution
    int width() const { return width_; }
    int height() const { return height_; }

    // Texture creation
    Texture createTexture(int width, int height);
    Texture createTexture();  // Uses default resolution

    // Shader execution
    void runShader(const std::string& shaderPath, Texture& output);
    void runShader(const std::string& shaderPath, const Texture* input, Texture& output);

    // Output storage for operator communication
    void setOutput(const std::string& name, const Texture& tex);
    void setOutput(const std::string& name, float value);
    void setOutput(const std::string& name, const std::vector<float>& values);

    // Input retrieval from other operators
    Texture* getInputTexture(const std::string& nodeId, const std::string& output = "out");
    float getInputValue(const std::string& nodeId, const std::string& output = "out", float defaultVal = 0.0f);
    std::vector<float> getInputValues(const std::string& nodeId, const std::string& output = "out");

    // Access to renderer for advanced use
    Renderer& renderer() { return renderer_; }

    // Called by runtime each frame
    void beginFrame(float time, float dt, int frame);
    void endFrame();

    // Clear all stored outputs (called between frames or on reload)
    void clearOutputs();

private:
    Renderer& renderer_;
    int width_;
    int height_;
    float time_ = 0;
    float dt_ = 0;
    int frame_ = 0;

    // Storage for operator outputs
    std::unordered_map<std::string, Texture> textureOutputs_;
    std::unordered_map<std::string, float> valueOutputs_;
    std::unordered_map<std::string, std::vector<float>> valueArrayOutputs_;
};

} // namespace vivid
