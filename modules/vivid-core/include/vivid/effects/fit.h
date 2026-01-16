#pragma once

/**
 * @file fit.h
 * @brief Resolution fitting operator
 *
 * Fits a texture to a target resolution with various scaling modes
 * (letterbox, pillarbox, fill, stretch).
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>

namespace vivid::effects {

/**
 * @brief Fit scale mode determines how the input is scaled to fit the output
 *
 * Note: Named FitScaleMode to avoid conflict with FitMode in canvas.h
 */
enum class FitScaleMode {
    Fit,      ///< Scale to fit entirely within bounds (letterbox/pillarbox)
    Fill,     ///< Scale to fill bounds completely (may crop edges)
    Stretch,  ///< Stretch to exact bounds (ignores aspect ratio)
    Native    ///< Use input's native resolution (no scaling)
};

/**
 * @brief Horizontal justify for positioning
 */
enum class FitHJustify {
    Left,
    Center,
    Right
};

/**
 * @brief Vertical justify for positioning
 */
enum class FitVJustify {
    Top,
    Center,
    Bottom
};

/**
 * @brief Resolution fitting effect
 *
 * Fits a texture to a target resolution with various scaling modes.
 * Useful for adapting content to different aspect ratios, creating
 * letterbox/pillarbox effects, and preparing textures for output.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | width | int | 1-8192 | 1920 | Target output width |
 * | height | int | 1-8192 | 1080 | Target output height |
 * | fitMode | int | 0-3 | 0 | Fit mode (0=Fit, 1=Fill, 2=Stretch, 3=Native) |
 * | hJustify | int | 0-2 | 1 | Horizontal justify (0=Left, 1=Center, 2=Right) |
 * | vJustify | int | 0-2 | 1 | Vertical justify (0=Top, 1=Center, 2=Bottom) |
 * | backgroundColor | color | 0-1 | (0,0,0,1) | Background color for letterbox/pillarbox |
 *
 * @par Example
 * @code
 * auto& fit = chain.add<Fit>("fit");
 * fit.input("source");
 * fit.width = 1920;
 * fit.height = 1080;
 * fit.fitMode = static_cast<int>(FitScaleMode::Fit);  // Letterbox/pillarbox
 * fit.hJustify = static_cast<int>(FitHJustify::Center);
 * fit.vJustify = static_cast<int>(FitVJustify::Center);
 * @endcode
 *
 * @par Fit Modes
 * - **Fit**: Scales the input to fit entirely within the target bounds while
 *   preserving aspect ratio. May result in letterboxing (bars on top/bottom)
 *   or pillarboxing (bars on left/right).
 * - **Fill**: Scales the input to completely fill the target bounds while
 *   preserving aspect ratio. May crop edges of the input.
 * - **Stretch**: Stretches the input to exactly match the target bounds.
 *   Does not preserve aspect ratio.
 * - **Native**: Uses the input's native resolution. Output matches input size.
 *
 * @par Inputs
 * - Input 0: Source texture
 *
 * @par Output
 * Fitted texture at target resolution
 *
 * @par TouchDesigner Equivalent
 * Fit TOP
 */
class Fit : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> width{"width", 1920, 1, 8192};            ///< Target width
    Param<int> height{"height", 1080, 1, 8192};          ///< Target height
    Param<int> fitMode{"fitMode", 0, 0, 3};              ///< 0=Fit, 1=Fill, 2=Stretch, 3=Native
    Param<int> hJustify{"hJustify", 1, 0, 2};            ///< 0=Left, 1=Center, 2=Right
    Param<int> vJustify{"vJustify", 1, 0, 2};            ///< 0=Top, 1=Center, 2=Bottom
    ColorParam backgroundColor{"backgroundColor", 0.0f, 0.0f, 0.0f, 1.0f}; ///< Background color

    /// @}
    // -------------------------------------------------------------------------

    Fit() {
        registerParam(width);
        registerParam(height);
        registerParam(fitMode);
        registerParam(hJustify);
        registerParam(vJustify);
        registerParam(backgroundColor);
    }
    ~Fit() override;

    // -------------------------------------------------------------------------
    /// @name Fluent Configuration API
    /// @{

    /// Set target resolution
    Fit& resolution(int w, int h) { width = w; height = h; return *this; }

    /// Set fit mode
    Fit& mode(FitScaleMode m) { fitMode = static_cast<int>(m); return *this; }

    /// Set horizontal justify
    Fit& justifyH(FitHJustify j) { hJustify = static_cast<int>(j); return *this; }

    /// Set vertical justify
    Fit& justifyV(FitVJustify j) { vJustify = static_cast<int>(j); return *this; }

    /// Set background color (RGBA)
    Fit& background(float r, float g, float b, float a = 1.0f) {
        backgroundColor.set(r, g, b, a);
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Fit"; }

    /// @}

private:
    void createPipeline(Context& ctx);
    void updateOutputSize(Context& ctx);

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::effects
