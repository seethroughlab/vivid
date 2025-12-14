#pragma once

/**
 * @file texture_operator.h
 * @brief Base class for operators that output textures
 *
 * TextureOperator provides common functionality for operators that
 * produce GPU textures as output, including texture creation, input
 * handling, and render pass management.
 */

#include <vivid/operator.h>
#include <vivid/param_registry.h>
#include <webgpu/webgpu.h>

namespace vivid::effects {

/// @brief Common texture format for effects pipeline (RGBA 16-bit float)
constexpr WGPUTextureFormat EFFECTS_FORMAT = WGPUTextureFormat_RGBA16Float;

/**
 * @brief Create a WGPUStringView from a C string
 * @param str C string to wrap
 * @return WGPUStringView pointing to the string
 */
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

/**
 * @brief Base class for texture-producing operators
 *
 * Provides common functionality for operators that output textures:
 * - Output texture creation and management
 * - Input texture access from connected operators
 * - Render pass helpers for full-screen effects
 *
 * @par Subclassing
 * @code
 * class MyEffect : public TextureOperator {
 * public:
 *     void init(Context& ctx) override {
 *         createOutput(ctx);  // Create output texture
 *         // Create pipeline...
 *     }
 *
 *     void process(Context& ctx) override {
 *         WGPURenderPassEncoder pass;
 *         WGPUCommandEncoder encoder;
 *         beginRenderPass(pass, encoder);
 *
 *         // Draw full-screen triangle...
 *
 *         endRenderPass(pass, encoder, ctx);
 *     }
 * };
 * @endcode
 */
class TextureOperator : public Operator, public ParamRegistry {
public:
    virtual ~TextureOperator();

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    OutputKind outputKind() const override { return OutputKind::Texture; }
    WGPUTextureView outputView() const override { return m_outputView; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameter Introspection (auto-generated from ParamRegistry)
    /// @{

    /**
     * @brief Get parameter declarations (uses ParamRegistry)
     * @return Vector of ParamDecl for all registered parameters
     *
     * Override only if you need to add non-registered parameters.
     */
    std::vector<ParamDecl> params() override { return registeredParams(); }

    /**
     * @brief Get parameter value (uses ParamRegistry)
     * @param name Parameter name
     * @param out Array to receive value
     * @return True if parameter found
     *
     * Override only if you need custom parameter access.
     */
    bool getParam(const std::string& name, float out[4]) override {
        return getRegisteredParam(name, out);
    }

    /**
     * @brief Set parameter value (uses ParamRegistry)
     * @param name Parameter name
     * @param value Array of values
     * @return True if parameter was set
     *
     * Automatically calls markDirty() if value changed.
     * Override only if you need custom parameter handling.
     */
    bool setParam(const std::string& name, const float value[4]) override {
        if (setRegisteredParam(name, value)) {
            markDirty();
            return true;
        }
        return false;
    }

    /**
     * @brief Generic setter for any registered parameter
     * @param name Parameter name
     * @param value Value to set
     *
     * @par Example
     * @code
     * auto& noise = chain.add<Noise>("noise");
     * noise.set("scale", 4.0f);
     * noise.set("speed", 0.5f);
     * @endcode
     */
    template<typename T>
    void set(const std::string& name, T value) {
        float v[4] = {static_cast<float>(value), 0, 0, 0};
        setParam(name, v);
    }

    /// @brief Generic setter for Vec2 parameters
    void set(const std::string& name, float x, float y) {
        float v[4] = {x, y, 0, 0};
        setParam(name, v);
    }

    /// @brief Generic setter for Vec3 parameters
    void set(const std::string& name, float x, float y, float z) {
        float v[4] = {x, y, z, 0};
        setParam(name, v);
    }

    /// @brief Generic setter for Color parameters
    void set(const std::string& name, float r, float g, float b, float a) {
        float v[4] = {r, g, b, a};
        setParam(name, v);
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output Texture
    /// @{

    /// @brief Get the raw output texture
    WGPUTexture outputTexture() const override { return m_output; }

    /// @brief Get output width in pixels
    int outputWidth() const { return m_width; }

    /// @brief Get output height in pixels
    int outputHeight() const { return m_height; }

    /**
     * @brief Set output resolution
     * @param w Width in pixels
     * @param h Height in pixels
     */
    void setResolution(int w, int h) { m_width = w; m_height = h; }

    /**
     * @brief DEPRECATED: No longer auto-resizes to window size
     * @param ctx The context (unused)
     * @return Always returns false
     *
     * @deprecated Operators should use their declared resolution. Processors
     * should use matchInputResolution() to inherit size from input.
     */
    bool checkResize(Context& ctx);

    /**
     * @brief Match resolution to input operator's output size
     * @param index Input slot index (default 0)
     * @return true if resize occurred, false if no input or same size
     *
     * Processors should call this to inherit their input's resolution.
     * Example: blur.process() calls matchInputResolution(0) to match input.
     */
    bool matchInputResolution(int index = 0);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input Access
    /// @{

    /**
     * @brief Get input texture view from connected operator
     * @param index Input slot index (default 0)
     * @return Texture view from input operator, or nullptr if none
     */
    WGPUTextureView inputView(int index = 0) const;

    /// @}

protected:
    // -------------------------------------------------------------------------
    /// @name Texture Management
    /// @{

    /**
     * @brief Create output texture with current resolution
     * @param ctx Runtime context for GPU access
     */
    void createOutput(Context& ctx);

    /**
     * @brief Create output texture with specific resolution
     * @param ctx Runtime context
     * @param width Texture width
     * @param height Texture height
     */
    void createOutput(Context& ctx, int width, int height);

    /// @brief Release output texture resources
    void releaseOutput();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Render Pass Helpers
    /// @{

    /**
     * @brief Begin a render pass targeting the output texture
     * @param[out] pass Render pass encoder
     * @param[out] encoder Command encoder
     */
    void beginRenderPass(WGPURenderPassEncoder& pass, WGPUCommandEncoder& encoder);

    /**
     * @brief End render pass and submit commands
     * @param pass Render pass encoder
     * @param encoder Command encoder
     * @param ctx Runtime context
     */
    void endRenderPass(WGPURenderPassEncoder pass, WGPUCommandEncoder encoder, Context& ctx);

    /// @}

    WGPUTexture m_output = nullptr;      ///< Output texture
    WGPUTextureView m_outputView = nullptr; ///< Output texture view

    int m_width = 1280;  ///< Output width
    int m_height = 720;  ///< Output height
};

} // namespace vivid::effects
