#pragma once

/**
 * @file output.h
 * @brief Final output operator
 *
 * Registers its input texture with the context for display to screen.
 * Every chain needs exactly one Output operator.
 */

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

/**
 * @brief Final output operator
 *
 * Passes its input texture to the runtime for display. Every chain
 * needs exactly one Output operator as the final node.
 *
 * @par Example
 * @code
 * chain->add<Noise>("noise").scale(4.0f);
 * chain->add<HSV>("color").input("noise");
 * chain->add<Output>("out").input("color");  // Display "color"
 * @endcode
 *
 * @par Inputs
 * - input (required): Texture to display
 *
 * @par Output
 * Passes through input texture to display
 */
class Output : public TextureOperator {
public:
    Output() = default;
    ~Output() override = default;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override {}
    void process(Context& ctx) override;
    void cleanup() override {}
    std::string name() const override { return "Output"; }

    /// @brief Returns input's texture view (pass-through)
    WGPUTextureView outputView() const override {
        return inputView(0);
    }

    /// @}
};

} // namespace vivid::effects
