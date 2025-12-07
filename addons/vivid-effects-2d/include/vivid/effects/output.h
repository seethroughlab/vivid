#pragma once

// Vivid Effects 2D - Output Operator
// Registers its input texture with the core for display

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

class Output : public TextureOperator {
public:
    Output() = default;
    ~Output() override = default;

    // Fluent API - connect input
    Output& input(TextureOperator* op) { setInput(0, op); return *this; }

    // Operator interface
    void init(Context& ctx) override {}
    void process(Context& ctx) override;
    void cleanup() override {}
    std::string name() const override { return "Output"; }

    // Override to return input's texture view
    WGPUTextureView outputView() const override {
        return inputView(0);
    }
};

} // namespace vivid::effects
