#pragma once
#include "types.h"
#include "graphics3d.h"
#include "animation.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

// Forward declarations
class Renderer;
class Window;
struct Shader;
class Renderer3DImpl;
class Renderer2DImpl;
class Renderer3DInstancedImpl;
class SkinnedMeshRendererImpl;
class Pipeline3DLit;
class Pipeline3DVertexLit;
class Pipeline3DDecal;
class CubemapProcessor;
class FontAtlas;
class TextRenderer;

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
    ~Context();  // Destructor needed for unique_ptr with forward-declared type

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
     * @brief Load raw image data from a file.
     * @param path Path to the image file (PNG, JPG, BMP, etc.).
     * @return ImageData with pixels in RGBA format, or invalid ImageData on failure.
     *
     * Use this when you need access to raw pixel data for CPU-side processing
     * (e.g., procedural texture blending, analysis).
     */
    ImageData loadImageData(const std::string& path);

    /**
     * @brief Check if a file is a supported image format.
     * @param path Path to check.
     * @return true if the extension is supported by loadImageAsTexture().
     */
    static bool isImageSupported(const std::string& path);
    /// @}

    /// @name Font & Text Rendering
    /// @{

    /**
     * @brief Load a font from a TTF file.
     * @param path Path to the TTF file.
     * @param fontSize Font size in pixels.
     * @param atlasSize Size of the font atlas texture (default 512).
     * @return Reference to the loaded FontAtlas, or nullptr on failure.
     *
     * Fonts are cached by path and size. Calling this multiple times with
     * the same parameters returns the cached font.
     */
    FontAtlas* loadFont(const std::string& path, float fontSize, int atlasSize = 512);

    /**
     * @brief Render text to a texture.
     * @param font The font atlas to use.
     * @param text The text string to render.
     * @param x X position in pixels.
     * @param y Y position in pixels.
     * @param color Text color (RGBA).
     * @param output Target texture.
     * @param clearColor Clear color (default = no clear, preserves existing content).
     */
    void renderText(FontAtlas& font, const std::string& text,
                    float x, float y, const glm::vec4& color,
                    Texture& output, const glm::vec4& clearColor = {0, 0, 0, -1});

    /**
     * @brief Render text centered at a position.
     * @param font The font atlas to use.
     * @param text The text string to render.
     * @param centerX Center X position in pixels.
     * @param centerY Center Y position in pixels.
     * @param color Text color (RGBA).
     * @param output Target texture.
     * @param clearColor Clear color (default = no clear).
     */
    void renderTextCentered(FontAtlas& font, const std::string& text,
                            float centerX, float centerY, const glm::vec4& color,
                            Texture& output, const glm::vec4& clearColor = {0, 0, 0, -1});

    /**
     * @brief Measure the size of rendered text.
     * @param font The font atlas to use.
     * @param text The text string to measure.
     * @return glm::vec2(width, height) in pixels.
     */
    glm::vec2 measureText(FontAtlas& font, const std::string& text);
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

    /**
     * @brief Run a shader with multiple input textures (up to 8).
     *
     * For multi-layer compositing and effects that need more than 2 inputs.
     * Inputs are bound to inputTexture, inputTexture2, inputTexture3, etc.
     *
     * @param shaderPath Path to the WGSL shader file.
     * @param inputs Vector of input textures (up to 8).
     * @param output The texture to render into.
     * @param params Shader parameters to pass as uniforms.
     */
    void runShaderMulti(const std::string& shaderPath,
                        const std::vector<const Texture*>& inputs,
                        Texture& output, const ShaderParams& params);
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

    /**
     * @brief Provide a texture to be used as input by a chain node.
     * @param nodeId The node ID that will consume this texture.
     * @param tex The texture to provide.
     * @param output The output name (default "out").
     *
     * This allows external textures (e.g., from Nuklear UI, video, etc.)
     * to be fed into the operator chain. The texture will be accessible
     * via getInputTexture(nodeId) by other nodes.
     *
     * Example:
     * @code
     * // In update():
     * ui.render(ctx, uiTexture);
     * ctx.setTextureForNode("ui_overlay", uiTexture);
     *
     * // In setup():
     * chain.add<Composite>("final").a("visual").b("ui_overlay");
     * @endcode
     */
    void setTextureForNode(const std::string& nodeId, const Texture& tex, const std::string& output = "out");
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

    /// @name 3D Graphics
    /// @{

    /**
     * @brief Create a 3D mesh from vertex and index data.
     * @param vertices Vector of Vertex3D data.
     * @param indices Vector of triangle indices (must be multiple of 3).
     * @return A valid Mesh3D if successful.
     */
    Mesh3D createMesh(const std::vector<Vertex3D>& vertices,
                      const std::vector<uint32_t>& indices);

    /**
     * @brief Create a cube mesh.
     * @return A unit cube mesh centered at origin.
     */
    Mesh3D createCube();

    /**
     * @brief Create a sphere mesh.
     * @param radius Sphere radius.
     * @param segments Horizontal segments.
     * @param rings Vertical rings.
     * @return A sphere mesh centered at origin.
     */
    Mesh3D createSphere(float radius = 0.5f, int segments = 32, int rings = 16);

    /**
     * @brief Create a plane mesh in the XZ plane.
     * @param width Width in X direction.
     * @param height Height in Z direction.
     * @return A plane mesh centered at origin.
     */
    Mesh3D createPlane(float width = 1.0f, float height = 1.0f);

    /**
     * @brief Create a torus mesh.
     * @param majorRadius Distance from center to tube center.
     * @param minorRadius Tube radius.
     * @return A torus mesh centered at origin.
     */
    Mesh3D createTorus(float majorRadius = 0.5f, float minorRadius = 0.2f);

    /**
     * @brief Create an elliptic torus mesh.
     * @param majorRadiusX Distance from center to tube center along X axis.
     * @param majorRadiusZ Distance from center to tube center along Z axis.
     * @param minorRadius Tube radius.
     * @return An elliptic torus mesh centered at origin.
     */
    Mesh3D createEllipticTorus(float majorRadiusX = 0.6f, float majorRadiusZ = 0.4f,
                               float minorRadius = 0.15f);

    /**
     * @brief Create a cylinder mesh.
     * @param radius Cylinder radius.
     * @param height Cylinder height.
     * @param segments Number of segments around the circumference.
     * @return A cylinder mesh centered at origin.
     */
    Mesh3D createCylinder(float radius = 0.5f, float height = 1.0f, int segments = 32);

    // Note: loadMesh() has been moved to the vivid-models addon.
    // Use vivid::models::parseModel() and ctx.createMesh() instead:
    //
    //   #include <vivid/models/model_loader.h>
    //   auto parsed = vivid::models::parseModel("model.fbx");
    //   auto mesh = ctx.createMesh(parsed.vertices, parsed.indices);

    /**
     * @brief Destroy a mesh and release resources.
     * @param mesh The mesh to destroy.
     */
    void destroyMesh(Mesh3D& mesh);

    /**
     * @brief Render a 3D scene to a texture.
     *
     * This is a high-level method that renders a mesh with a camera.
     * Uses the built-in normal visualization shader.
     *
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform (default identity).
     * @param output Target texture to render into.
     * @param clearColor Background color (default black).
     */
    void render3D(const Mesh3D& mesh, const Camera3D& camera,
                  const glm::mat4& transform, Texture& output,
                  const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render multiple meshes to a texture.
     *
     * @param meshes Vector of meshes to render.
     * @param transforms Transform for each mesh.
     * @param camera The camera viewpoint.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render3D(const std::vector<Mesh3D>& meshes,
                  const std::vector<glm::mat4>& transforms,
                  const Camera3D& camera, Texture& output,
                  const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render a mesh with Phong (Blinn-Phong) lighting.
     *
     * Classic ambient/diffuse/specular shading model.
     *
     * @code
     * // Setup lighting
     * SceneLighting lighting = SceneLighting::threePoint();
     *
     * // Create material
     * PhongMaterial material = PhongMaterial::shiny(glm::vec3(0.8f, 0.2f, 0.2f));
     *
     * // Render with lighting
     * ctx.render3DPhong(mesh, camera, transform, material, lighting, output);
     * @endcode
     *
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material Phong material properties.
     * @param lighting Scene lighting configuration.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render3DPhong(const Mesh3D& mesh, const Camera3D& camera,
                       const glm::mat4& transform,
                       const PhongMaterial& material,
                       const SceneLighting& lighting,
                       Texture& output,
                       const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render a mesh with retro vertex-lit shading.
     *
     * Simple N·L diffuse lighting with optional quantization for PS1/toon look.
     * No PBR, no environment mapping - just classic vertex lighting.
     *
     * @code
     * // Create retro material
     * VertexLitMaterial material = VertexLitMaterial::ps1(glm::vec3(0.8f, 0.2f, 0.2f));
     * material.diffuseMap = &liveryTexture;
     *
     * // Render with vertex lighting
     * glm::vec3 lightDir(0.5f, -1.0f, 0.3f);
     * ctx.render3DVertexLit(mesh, camera, transform, material, lightDir, output);
     * @endcode
     *
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material Vertex-lit material properties.
     * @param lightDir Directional light direction.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     * @param lightColor Light color (default white).
     */
    void render3DVertexLit(const Mesh3D& mesh, const Camera3D& camera,
                           const glm::mat4& transform,
                           const VertexLitMaterial& material,
                           const glm::vec3& lightDir,
                           Texture& output,
                           const glm::vec4& clearColor = {0, 0, 0, 1},
                           const glm::vec3& lightColor = {1, 1, 1});

    /**
     * @brief Render a mesh with PBR (Physically Based Rendering).
     *
     * Uses Cook-Torrance BRDF with metallic-roughness workflow.
     * Includes HDR tone mapping and gamma correction.
     *
     * @code
     * // Setup lighting
     * SceneLighting lighting = SceneLighting::outdoor();
     *
     * // Create PBR material
     * PBRMaterial material = PBRMaterial::gold();
     *
     * // Render with PBR
     * ctx.render3DPBR(mesh, camera, transform, material, lighting, output);
     * @endcode
     *
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material PBR material properties.
     * @param lighting Scene lighting configuration.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render3DPBR(const Mesh3D& mesh, const Camera3D& camera,
                     const glm::mat4& transform,
                     const PBRMaterial& material,
                     const SceneLighting& lighting,
                     Texture& output,
                     const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render a mesh with PBR and Image-Based Lighting (IBL).
     *
     * Uses IBL for ambient lighting, providing realistic reflections
     * and diffuse lighting from an environment map.
     *
     * @code
     * // Load HDR environment map once in setup()
     * Environment env = ctx.loadEnvironment("studio.hdr");
     *
     * // Render with IBL
     * ctx.render3DPBR(mesh, camera, transform, material, lighting, env, output);
     * @endcode
     *
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material PBR material properties.
     * @param lighting Direct light sources (combined with IBL).
     * @param environment IBL environment maps.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render3DPBR(const Mesh3D& mesh, const Camera3D& camera,
                     const glm::mat4& transform,
                     const PBRMaterial& material,
                     const SceneLighting& lighting,
                     const Environment& environment,
                     Texture& output,
                     const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Render a mesh with textured PBR and Image-Based Lighting (IBL).
     *
     * Uses texture maps for detailed material properties including:
     * - Albedo (base color) map
     * - Normal map for surface detail
     * - Metallic-Roughness map (glTF format: G=roughness, B=metallic)
     * - Ambient Occlusion (AO) map
     * - Emissive map for glowing surfaces
     *
     * @code
     * // Load textures and create material
     * TexturedPBRMaterial material;
     * material.albedo = glm::vec3(1.0f);  // Base tint
     * material.albedoMap = ctx.loadImageAsTexture("albedo.png");
     * material.normalMap = ctx.loadImageAsTexture("normal.png");
     * material.metallicRoughnessMap = ctx.loadImageAsTexture("metallic_roughness.png");
     *
     * // Render with textured PBR
     * ctx.render3DPBR(mesh, camera, transform, material, lighting, env, output);
     * @endcode
     *
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material Textured PBR material with optional texture maps.
     * @param lighting Direct light sources (combined with IBL).
     * @param environment IBL environment maps.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render3DPBR(const Mesh3D& mesh, const Camera3D& camera,
                     const glm::mat4& transform,
                     const TexturedPBRMaterial& material,
                     const SceneLighting& lighting,
                     const Environment& environment,
                     Texture& output,
                     const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief Load an HDR environment map for Image-Based Lighting.
     *
     * Loads an HDR equirectangular image and pre-computes the necessary
     * cubemaps for diffuse and specular IBL:
     * - Irradiance map for diffuse lighting
     * - Pre-filtered radiance map for specular reflections
     * - BRDF lookup table (cached, shared across environments)
     *
     * @param path Path to HDR file (.hdr format).
     * @return Environment ready for use with render3DPBR(), or invalid if failed.
     *
     * Supported formats: Radiance HDR (.hdr)
     * Recommended resolution: 2048x1024 or higher for quality reflections.
     */
    Environment loadEnvironment(const std::string& path);

    /**
     * @brief Destroy an environment and release GPU resources.
     * @param environment The environment to destroy.
     */
    void destroyEnvironment(Environment& environment);

    /**
     * @brief Render many instances of a mesh using GPU instancing.
     *
     * Efficiently renders thousands of instances in a single draw call.
     * Each instance has its own transform matrix and color.
     * Uses hemisphere lighting.
     *
     * @param mesh The mesh to render (shared by all instances).
     * @param instances Per-instance data (transform + color).
     * @param camera The camera viewpoint.
     * @param output Target texture to render into.
     * @param clearColor Background color (default dark blue).
     */
    void drawMeshInstanced(const Mesh3D& mesh,
                           const std::vector<Instance3D>& instances,
                           const Camera3D& camera, Texture& output,
                           const glm::vec4& clearColor = {0.05f, 0.05f, 0.1f, 1.0f});

    /**
     * @brief Get the scene depth texture from the last 3D render.
     *
     * Returns a handle to the internal depth buffer used by render3DPBR and other
     * 3D rendering functions. Use this for decal projection or depth-based effects.
     *
     * @code
     * // Render scene to get depth
     * ctx.render3DPBR(mesh, camera, transform, material, lighting, output);
     *
     * // Get depth buffer for decals
     * Texture depth = ctx.getSceneDepthTexture();
     *
     * // Apply decal using depth
     * ctx.renderDecal(decal, camera, depth, output);
     * @endcode
     *
     * @return Texture handle to the scene depth buffer, or invalid if no 3D render has occurred.
     */
    Texture getSceneDepthTexture();

    /**
     * @brief Render a decal onto an existing color buffer.
     *
     * Projects a texture onto 3D geometry using depth buffer reconstruction.
     * The decal is blended with the existing color based on its blend mode.
     *
     * @code
     * // Create a decal
     * Decal decal;
     * decal.texture = &logoTexture;
     * decal.position = glm::vec3(0.0f, 1.0f, 0.0f);
     * decal.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
     * decal.size = glm::vec3(2.0f, 2.0f, 1.0f);  // Width, height, depth
     * decal.blendMode = DecalBlendMode::Multiply;
     *
     * // Render scene first
     * ctx.render3DPBR(mesh, camera, transform, material, lighting, output);
     *
     * // Get depth and apply decal
     * ctx.renderDecal(decal, camera, ctx.getSceneDepthTexture(), output);
     * @endcode
     *
     * @param decal The decal configuration.
     * @param camera The camera used to render the scene.
     * @param depthTexture The depth buffer from the scene render (use getSceneDepthTexture()).
     * @param colorOutput The color buffer to blend the decal onto (read-write).
     */
    void renderDecal(const Decal& decal, const Camera3D& camera,
                     const Texture& depthTexture, Texture& colorOutput);

    /**
     * @brief Render multiple decals onto an existing color buffer.
     * @param decals Vector of decals to render.
     * @param camera The camera used to render the scene.
     * @param depthTexture The depth buffer from the scene render.
     * @param colorOutput The color buffer to blend decals onto.
     */
    void renderDecals(const std::vector<Decal>& decals, const Camera3D& camera,
                      const Texture& depthTexture, Texture& colorOutput);

    /// @}

    /// @name Skeletal Animation
    /// @{

    /**
     * @brief Create a skinned mesh from vertex and index data.
     *
     * Use the vivid-models addon to parse model files, then pass the parsed
     * data to this function to create GPU resources:
     *
     * @code
     * // Using vivid-models addon
     * auto parsed = vivid::models::parseSkinnedModel("model.fbx");
     * auto mesh = ctx.createSkinnedMesh(parsed.vertices, parsed.indices,
     *                                    parsed.skeleton, parsed.animations);
     *
     * // Set up animation system (from addon)
     * vivid::models::AnimationSystem animSystem;
     * animSystem.init(parsed.skeleton, parsed.animations);
     * animSystem.playAnimation(0, true);
     *
     * // In render loop
     * animSystem.update(ctx.deltaTime());
     * mesh.boneMatrices = animSystem.getBoneMatrices();
     * ctx.renderSkinned3D(mesh, camera, transform, output);
     * @endcode
     *
     * @param vertices Skinned vertex data with bone weights.
     * @param indices Index buffer.
     * @param skeleton Bone hierarchy (optional, for built-in animation).
     * @param animations Animation clips (optional, for built-in animation).
     * @return A valid SkinnedMesh3D ready for rendering.
     */
    SkinnedMesh3D createSkinnedMesh(
        const std::vector<SkinnedVertex3D>& vertices,
        const std::vector<uint32_t>& indices,
        const Skeleton& skeleton = {},
        const std::vector<AnimationClip>& animations = {});

    /**
     * @brief Destroy a skinned mesh and free GPU resources.
     * @param mesh The skinned mesh to destroy.
     */
    void destroySkinnedMesh(SkinnedMesh3D& mesh);

    /**
     * @brief Render a skinned mesh with bone transforms.
     *
     * Before calling, ensure the mesh has been updated with current bone matrices:
     * @code
     * mesh.update(ctx.deltaTime());  // Updates animation and bone matrices
     * ctx.renderSkinned3D(mesh, camera, transform, output);
     * @endcode
     *
     * @param mesh The skinned mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param output Target texture to render into.
     * @param clearColor Background color (default black).
     */
    void renderSkinned3D(SkinnedMesh3D& mesh, const Camera3D& camera,
                         const glm::mat4& transform, Texture& output,
                         const glm::vec4& clearColor = {0, 0, 0, 1});

    /// @}

    /// @name 2D Instanced Rendering
    /// @{

    /**
     * @brief Draw multiple circles using GPU instancing.
     *
     * Efficiently renders many circles in a single draw call.
     * Each circle has its own position, radius, and color.
     *
     * @param circles Vector of circle instances to draw.
     * @param output Target texture to render into.
     * @param clearColor Background color (default black).
     */
    void drawCircles(const std::vector<Circle2D>& circles, Texture& output,
                     const glm::vec4& clearColor = {0, 0, 0, 1});

    /// @}

    /// @name Project Path Resolution
    /// @{

    /**
     * @brief Set the project path for asset resolution.
     * @param projectPath Absolute path to the project directory.
     *
     * When set, relative paths in asset loading functions will be
     * resolved against this directory first.
     */
    void setProjectPath(const std::string& projectPath);

    /**
     * @brief Set the shared assets path for fallback resolution.
     * @param sharedPath Absolute path to shared assets directory.
     */
    void setSharedAssetsPath(const std::string& sharedPath);

    /**
     * @brief Resolve a relative path to an absolute path.
     * @param relativePath Relative path to resolve.
     * @return Absolute path - checks project folder first, then shared assets.
     *
     * Resolution order:
     * 1. If path is already absolute, return as-is
     * 2. Check projectPath/relativePath
     * 3. Check sharedAssetsPath/relativePath
     * 4. Return original path if not found (let caller handle error)
     */
    std::string resolvePath(const std::string& relativePath) const;

    /// @brief Get the current project path.
    const std::string& projectPath() const { return projectPath_; }

    /// @}

    /// @name GPU Access (Advanced)
    /// @{

    /**
     * @brief Get the raw WebGPU device handle.
     * @return Opaque pointer to WGPUDevice (cast to WGPUDevice when using).
     *
     * This is for advanced usage like ImGUI integration. The handle is valid
     * for the lifetime of the Context. Operators using this should include
     * webgpu.h and cast the return value:
     *
     *     WGPUDevice device = static_cast<WGPUDevice>(ctx.webgpuDevice());
     */
    void* webgpuDevice() const;

    /**
     * @brief Get the raw WebGPU queue handle.
     * @return Opaque pointer to WGPUQueue (cast to WGPUQueue when using).
     */
    void* webgpuQueue() const;

    /**
     * @brief Get the WebGPU texture format used for render targets.
     * @return The WGPUTextureFormat enum value as an integer.
     *
     * Returns the format used for output textures (typically RGBA8Unorm).
     * Cast to WGPUTextureFormat when using:
     *
     *     WGPUTextureFormat format = static_cast<WGPUTextureFormat>(ctx.webgpuTextureFormat());
     */
    int webgpuTextureFormat() const;

    /**
     * @brief Get the raw WebGPU texture view from a Texture.
     * @param texture The texture to get the view from.
     * @return Opaque pointer to WGPUTextureView, or nullptr if invalid.
     *
     * Used for ImGUI integration to render to a vivid Texture.
     */
    void* webgpuTextureView(const Texture& texture) const;

    /// @}

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

    // 3D rendering support (lazy initialized)
    std::unique_ptr<Renderer3DImpl> renderer3d_;
    Renderer3DImpl& getRenderer3D();

    // 2D instanced rendering support (lazy initialized)
    std::unique_ptr<Renderer2DImpl> renderer2d_;
    Renderer2DImpl& getRenderer2D();

    // 3D instanced rendering support (lazy initialized)
    std::unique_ptr<Renderer3DInstancedImpl> renderer3dInstanced_;
    Renderer3DInstancedImpl& getRenderer3DInstanced();

    // Skinned mesh rendering support (lazy initialized)
    std::unique_ptr<SkinnedMeshRendererImpl> skinnedMeshRenderer_;
    SkinnedMeshRendererImpl& getSkinnedMeshRenderer();

    // Lit 3D rendering pipelines (lazy initialized)
    std::unique_ptr<Pipeline3DLit> phongPipeline_;
    std::unique_ptr<Pipeline3DLit> pbrPipeline_;
    std::unique_ptr<Pipeline3DLit> pbrIBLPipeline_;
    std::unique_ptr<Pipeline3DLit> pbrIBLTexturedPipeline_;
    Pipeline3DLit& getPhongPipeline();
    Pipeline3DLit& getPBRPipeline();
    Pipeline3DLit& getPBRIBLPipeline();
    Pipeline3DLit& getPBRIBLTexturedPipeline();

    // Vertex-lit pipeline (lazy initialized)
    std::unique_ptr<Pipeline3DVertexLit> vertexLitPipeline_;
    Pipeline3DVertexLit& getVertexLitPipeline();

    // Decal rendering pipeline (lazy initialized)
    std::unique_ptr<Pipeline3DDecal> decalPipeline_;
    Pipeline3DDecal& getDecalPipeline();

    // Cubemap/IBL processing (lazy initialized)
    std::unique_ptr<CubemapProcessor> cubemapProcessor_;
    CubemapProcessor& getCubemapProcessor();

    // Text rendering (lazy initialized)
    std::unique_ptr<TextRenderer> textRenderer_;
    TextRenderer& getTextRenderer();
    std::unordered_map<std::string, std::unique_ptr<FontAtlas>> fontCache_;

    // Project path resolution
    std::string projectPath_;
    std::string sharedAssetsPath_;
};

} // namespace vivid
