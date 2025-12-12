#pragma once

/**
 * @file downsample.h
 * @brief Resolution reduction operator
 *
 * Renders at a lower resolution and upscales for retro or performance effects.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Upscale filter modes
 */
enum class FilterMode {
    Nearest,   ///< Point sampling - pixelated look
    Linear     ///< Bilinear interpolation - smooth scaling
};

/**
 * @brief Low-resolution rendering with upscale
 *
 * Renders the input at a lower resolution and upscales to output size.
 * Useful for retro pixel art aesthetics or performance optimization.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | targetW | int | 16-1920 | 320 | Target width in pixels |
 * | targetH | int | 16-1080 | 240 | Target height in pixels |
 *
 * @par Example
 * @code
 * chain.add<Downsample>("lowres")
 *     .input("source")
 *     .resolution(160, 120)
 *     .filter(FilterMode::Nearest);
 * @endcode
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Downsampled and upscaled texture
 */
class Downsample : public TextureOperator {
public:
    Downsample() = default;
    ~Downsample() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input texture
     * @param op Source operator
     * @return Reference for chaining
     */
    Downsample& input(TextureOperator* op) { setInput(0, op); return *this; }

    /**
     * @brief Set target resolution
     * @param w Target width in pixels
     * @param h Target height in pixels
     * @return Reference for chaining
     */
    Downsample& resolution(int w, int h) {
        if (m_targetW != w || m_targetH != h) {
            m_targetW = w;
            m_targetH = h;
            markDirty();
        }
        return *this;
    }

    /**
     * @brief Set upscale filter mode
     * @param f Filter mode (Nearest = pixelated, Linear = smooth)
     * @return Reference for chaining
     */
    Downsample& filter(FilterMode f) { if (m_filter != f) { m_filter = f; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Downsample"; }

    std::vector<ParamDecl> params() override {
        return { m_targetW.decl(), m_targetH.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "targetW") { out[0] = m_targetW; return true; }
        if (name == "targetH") { out[0] = m_targetH; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "targetW") {
            int w = static_cast<int>(value[0]);
            if (m_targetW != w) { m_targetW = w; markDirty(); }
            return true;
        }
        if (name == "targetH") {
            int h = static_cast<int>(value[0]);
            if (m_targetH != h) { m_targetH = h; markDirty(); }
            return true;
        }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    Param<int> m_targetW{"targetW", 320, 16, 1920};
    Param<int> m_targetH{"targetH", 240, 16, 1080};
    FilterMode m_filter = FilterMode::Nearest;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
