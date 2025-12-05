#pragma once

#include "vivid/operator.h"
#include "vivid/export.h"

namespace vivid {

/// Outputs a texture to the screen (swap chain)
class VIVID_API Output : public Operator {
public:
    Output() = default;

    std::string typeName() const override { return "Output"; }

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    OutputKind outputKind() const override { return OutputKind::Texture; }

    // Fluent API - set input by name
    Output& input(const std::string& name) { inputName_ = name; return *this; }

private:
    std::string inputName_;

    Diligent::IPipelineState* pso_ = nullptr;
    Diligent::IShaderResourceBinding* srb_ = nullptr;
};

} // namespace vivid
