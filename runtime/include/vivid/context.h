#pragma once
#include "types.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

// Forward declarations
class Renderer;
struct Shader;

/**
 * @brief Runtime context providing access to time, textures, shaders, and operator communication.
 *
 * The Context is passed to operators on each frame. Use it to:
 * - Query time and resolution
 * - Create and manage textures
 * - Run shaders
 * - Read inputs from other operators
 * - Set outputs for other operators to consume
 */
class Context {
public:
    Context(Renderer& renderer, int width, int height);

    /// @name Time and Frame Info
    /// @{

    /// @brief Get the current time in seconds since the runtime started.
    float time() const { return time_; }

    /// @brief Get the delta time (seconds since last frame).
    float dt() const { return dt_; }

    /// @brief Get the current frame number.
    int frame() const { return frame_; }
    /// @}

    /// @name Resolution
    /// @{

    /// @brief Get the output width in pixels.
    int width() const { return width_; }

    /// @brief Get the output height in pixels.
    int height() const { return height_; }
    /// @}

    /// @name Texture Creation
    /// @{

    /**
     * @brief Create a texture with specific dimensions.
     * @param width Texture width in pixels.
     * @param height Texture height in pixels.
     * @return A new texture handle.
     */
    Texture createTexture(int width, int height);

    /**
     * @brief Create a texture at the default output resolution.
     * @return A new texture handle at width() x height().
     */
    Texture createTexture();
    /// @}

    /// @name Shader Execution
    /// @{

    /**
     * @brief Run a shader with no input texture.
     * @param shaderPath Path to the .wgsl shader file (relative to project).
     * @param output The texture to render into.
     */
    void runShader(const std::string& shaderPath, Texture& output);

    /**
     * @brief Run a shader with one input texture.
     * @param shaderPath Path to the .wgsl shader file.
     * @param input Pointer to input texture (nullptr for no input).
     * @param output The texture to render into.
     */
    void runShader(const std::string& shaderPath, const Texture* input, Texture& output);

    /**
     * @brief Parameters passed to shaders via uniform buffer.
     *
     * These map to shader uniforms u.param0-u.param7, u.vec0, u.vec1, and u.mode.
     */
    struct ShaderParams {
        float param0 = 0.0f, param1 = 0.0f, param2 = 0.0f, param3 = 0.0f;
        float param4 = 0.0f, param5 = 0.0f, param6 = 0.0f, param7 = 0.0f;
        float vec0X = 0.0f, vec0Y = 0.0f;  ///< Maps to u.vec0
        float vec1X = 0.0f, vec1Y = 0.0f;  ///< Maps to u.vec1
        int mode = 0;                       ///< Maps to u.mode
    };

    /**
     * @brief Run a shader with one input texture and parameters.
     * @param shaderPath Path to the .wgsl shader file.
     * @param input Pointer to input texture (nullptr for no input).
     * @param output The texture to render into.
     * @param params Shader parameters to pass as uniforms.
     */
    void runShader(const std::string& shaderPath, const Texture* input,
                   Texture& output, const ShaderParams& params);

    /**
     * @brief Run a shader with two input textures and parameters.
     * @param shaderPath Path to the .wgsl shader file.
     * @param input1 First input texture (binding 2).
     * @param input2 Second input texture (binding 3).
     * @param output The texture to render into.
     * @param params Shader parameters to pass as uniforms.
     */
    void runShader(const std::string& shaderPath, const Texture* input1,
                   const Texture* input2, Texture& output, const ShaderParams& params);
    /// @}

    /// @name Output Storage
    /// @{

    /**
     * @brief Set a texture output for other operators to read.
     * @param name Output name (typically "out").
     * @param tex The texture to store.
     */
    void setOutput(const std::string& name, const Texture& tex);

    /**
     * @brief Set a scalar value output.
     * @param name Output name.
     * @param value The value to store.
     */
    void setOutput(const std::string& name, float value);

    /**
     * @brief Set a value array output.
     * @param name Output name.
     * @param values The values to store.
     */
    void setOutput(const std::string& name, const std::vector<float>& values);
    /// @}

    /// @name Input Retrieval
    /// @{

    /**
     * @brief Get a texture output from another operator.
     * @param nodeId The operator's ID (class name from VIVID_OPERATOR).
     * @param output The output name (default "out").
     * @return Pointer to the texture, or nullptr if not found.
     */
    Texture* getInputTexture(const std::string& nodeId, const std::string& output = "out");

    /**
     * @brief Get a scalar value from another operator.
     * @param nodeId The operator's ID.
     * @param output The output name (default "out").
     * @param defaultVal Value to return if not found.
     * @return The value, or defaultVal if not found.
     */
    float getInputValue(const std::string& nodeId, const std::string& output = "out", float defaultVal = 0.0f);

    /**
     * @brief Get a value array from another operator.
     * @param nodeId The operator's ID.
     * @param output The output name (default "out").
     * @return The values, or empty vector if not found.
     */
    std::vector<float> getInputValues(const std::string& nodeId, const std::string& output = "out");
    /// @}

    /// @brief Access the underlying renderer (advanced use only).
    Renderer& renderer() { return renderer_; }

    // Internal methods - called by runtime
    void beginFrame(float time, float dt, int frame);
    void endFrame();
    void clearOutputs();
    void clearShaderCache();

private:
    // Get or load a cached shader
    Shader* getCachedShader(const std::string& path);
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

    // Shader cache to avoid recompiling every frame
    std::unordered_map<std::string, std::unique_ptr<Shader>> shaderCache_;
};

} // namespace vivid
