#pragma once
#include "types.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

// Forward declarations
class Renderer;
class Window;
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
    Context(Renderer& renderer, Window& window, int width, int height);

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

    /// @name Display Settings
    /// @{

    /**
     * @brief Enable or disable vertical synchronization.
     * @param enabled true for VSync on (60fps cap), false for uncapped.
     *
     * VSync prevents screen tearing by synchronizing frame presentation
     * with the monitor's refresh rate. Disable for higher framerates
     * when testing performance.
     */
    void setVSync(bool enabled);

    /// @brief Check if VSync is currently enabled.
    bool vsyncEnabled() const;
    /// @}

    /// @name Keyboard Input
    /// @{

    /// @brief Check if a key is currently held down.
    /// @param key GLFW key code (e.g., GLFW_KEY_SPACE, GLFW_KEY_A).
    /// @return true if the key is currently pressed.
    bool isKeyDown(int key) const;

    /// @brief Check if a key was just pressed this frame.
    /// @param key GLFW key code.
    /// @return true if the key was pressed this frame (edge trigger).
    bool wasKeyPressed(int key) const;

    /// @brief Check if a key was just released this frame.
    /// @param key GLFW key code.
    /// @return true if the key was released this frame (edge trigger).
    bool wasKeyReleased(int key) const;
    /// @}

    /// @name Mouse Input
    /// @{

    /// @brief Get the mouse X position in pixels.
    float mouseX() const;

    /// @brief Get the mouse Y position in pixels.
    float mouseY() const;

    /// @brief Get the mouse X position normalized to [0, 1].
    float mouseNormX() const;

    /// @brief Get the mouse Y position normalized to [0, 1].
    float mouseNormY() const;

    /// @brief Check if a mouse button is currently held down.
    /// @param button Button index (0=left, 1=right, 2=middle).
    bool isMouseDown(int button = 0) const;

    /// @brief Check if a mouse button was just pressed this frame.
    /// @param button Button index (0=left, 1=right, 2=middle).
    bool wasMousePressed(int button = 0) const;

    /// @brief Check if a mouse button was just released this frame.
    /// @param button Button index (0=left, 1=right, 2=middle).
    bool wasMouseReleased(int button = 0) const;

    /// @brief Get horizontal scroll delta this frame.
    float scrollDeltaX() const;

    /// @brief Get vertical scroll delta this frame.
    float scrollDeltaY() const;
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

    /**
     * @brief Create a texture matching the dimensions of another texture.
     * @param matchTexture The texture to match dimensions from.
     * @return A new texture handle with matching width and height.
     *
     * Useful for resolution-independent operators that should adapt
     * to their input's resolution.
     */
    Texture createTextureMatching(const Texture& matchTexture);

    /**
     * @brief Create a texture matching the dimensions of an input from another operator.
     * @param nodeId The operator's ID to get input dimensions from.
     * @param output The output name (default "out").
     * @return A new texture, or default resolution if input not found.
     */
    Texture createTextureMatching(const std::string& nodeId, const std::string& output = "out");

    /**
     * @brief Load an image file and create a texture from it.
     * @param path Path to the image file (PNG, JPG, BMP, etc.).
     * @return A valid texture if successful, invalid texture on failure.
     *
     * Supports PNG, JPG, JPEG, BMP, TGA, GIF, PSD, HDR, PIC formats.
     * The texture dimensions match the image dimensions.
     */
    Texture loadImageAsTexture(const std::string& path);

    /**
     * @brief Upload pixel data to an existing texture.
     * @param texture The texture to upload to.
     * @param pixels RGBA8 pixel data (width * height * 4 bytes).
     * @param width Image width (must match texture width).
     * @param height Image height (must match texture height).
     */
    void uploadTexturePixels(Texture& texture, const uint8_t* pixels, int width, int height);

    /**
     * @brief Check if a file is a supported image format.
     * @param path Path to check.
     * @return true if the extension is supported by loadImageAsTexture().
     */
    static bool isImageSupported(const std::string& path);
    /// @}

    /// @name Video Playback
    /// @{

    /**
     * @brief Create a video player for a video file.
     * @param path Path to the video file (MP4, MOV, etc.).
     * @return A valid VideoPlayer if successful, invalid on failure.
     *
     * Supports MP4, MOV, M4V, AVI, MKV, WebM (platform-dependent).
     * HAP codec is detected automatically.
     */
    VideoPlayer createVideoPlayer(const std::string& path);

    /**
     * @brief Destroy a video player and release resources.
     * @param player The video player to destroy.
     */
    void destroyVideoPlayer(VideoPlayer& player);

    /**
     * @brief Get video metadata.
     * @param player The video player.
     * @return VideoInfo with dimensions, duration, codec, etc.
     */
    VideoInfo getVideoInfo(const VideoPlayer& player);

    /**
     * @brief Seek to a position in the video.
     * @param player The video player.
     * @param timeSeconds Time in seconds from the start.
     * @return true if seek succeeded.
     */
    bool videoSeek(VideoPlayer& player, double timeSeconds);

    /**
     * @brief Get the next frame from the video.
     * @param player The video player.
     * @param output Texture to receive the frame.
     * @return true if a new frame was decoded.
     */
    bool videoGetFrame(VideoPlayer& player, Texture& output);

    /**
     * @brief Get current playback position.
     * @param player The video player.
     * @return Current time in seconds.
     */
    double videoGetTime(const VideoPlayer& player);

    /**
     * @brief Check if a file is a supported video format.
     * @param path Path to check.
     * @return true if the extension is a known video format.
     */
    static bool isVideoSupported(const std::string& path);
    /// @}

    /// @name Camera Capture
    /// @{

    /**
     * @brief Enumerate available camera devices.
     * @return List of available cameras.
     */
    std::vector<CameraDevice> enumerateCameras();

    /**
     * @brief Create a camera capture using the default camera.
     * @param width Requested capture width (default 1280).
     * @param height Requested capture height (default 720).
     * @param frameRate Requested frame rate (default 30).
     * @return A valid Camera if successful, invalid on failure.
     */
    Camera createCamera(int width = 1280, int height = 720, float frameRate = 30.0f);

    /**
     * @brief Create a camera capture using a specific device.
     * @param deviceId Device ID from enumerateCameras().
     * @param width Requested capture width (default 1280).
     * @param height Requested capture height (default 720).
     * @param frameRate Requested frame rate (default 30).
     * @return A valid Camera if successful, invalid on failure.
     */
    Camera createCamera(const std::string& deviceId, int width = 1280, int height = 720, float frameRate = 30.0f);

    /**
     * @brief Destroy a camera capture and release resources.
     * @param camera The camera to destroy.
     */
    void destroyCamera(Camera& camera);

    /**
     * @brief Get camera metadata.
     * @param camera The camera capture.
     * @return CameraInfo with dimensions, frame rate, etc.
     */
    CameraInfo getCameraInfo(const Camera& camera);

    /**
     * @brief Get the latest frame from the camera.
     * @param camera The camera capture.
     * @param output Texture to receive the frame.
     * @return true if a new frame was available.
     *
     * This is non-blocking - returns false if no new frame since last call.
     */
    bool cameraGetFrame(Camera& camera, Texture& output);

    /**
     * @brief Start camera capture.
     * @param camera The camera capture.
     * @return true if capture started successfully.
     */
    bool cameraStart(Camera& camera);

    /**
     * @brief Stop camera capture.
     * @param camera The camera capture.
     */
    void cameraStop(Camera& camera);
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

    /// @brief Set the current node name for output prefixing (used by Chain API)
    void setCurrentNode(const std::string& nodeId) { currentNode_ = nodeId; }

    /// @brief Clear the current node name
    void clearCurrentNode() { currentNode_.clear(); }

private:
    // Get or load a cached shader
    Shader* getCachedShader(const std::string& path);
    Renderer& renderer_;
    Window* window_ = nullptr;  // Optional, for keyboard input
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

    // Current node name for Chain API output prefixing
    std::string currentNode_;
};

} // namespace vivid
