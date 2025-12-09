#pragma once

/**
 * @file pixelate.h
 * @brief Mosaic/pixelation operator
 *
 * Creates a pixelated mosaic effect by sampling at lower resolution.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Mosaic/pixelation effect
 *
 * Reduces effective resolution by sampling pixels in blocks,
 * creating a mosaic or retro pixel art appearance.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | size | vec2 | 1-100 | (10,10) | Pixel block size in screen pixels |
 *
 * @par Example
 * @code
 * chain.add<Pixelate>("pixels")
 *     .input("source")
 *     .size(16.0f);  // 16x16 pixel blocks
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Pixelated texture
 */
class Pixelate : public TextureOperator {
public:
    Pixelate() = default;
    ~Pixelate() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Pixelate& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set uniform pixel block size
     * @param s Block size in pixels (applies to both axes)
     * @return Reference for chaining
     */
    Pixelate& size(float s) { m_size.set(s, s); return *this; }

    /**
     * @brief Set non-uniform pixel block size
     * @param x Block width in pixels
     * @param y Block height in pixels
     * @return Reference for chaining
     */
    Pixelate& size(float x, float y) { m_size.set(x, y); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Pixelate"; }

    std::vector<ParamDecl> params() override {
        return { m_size.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "size") { out[0] = m_size.x(); out[1] = m_size.y(); return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "size") { m_size.set(value[0], value[1]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Vec2Param m_size{"size", 10.0f, 10.0f, 1.0f, 100.0f};

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
