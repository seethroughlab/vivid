#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Identity operator - passes input through unchanged
class Passthrough : public TextureOperator {
public:
    std::string typeName() const override { return "Passthrough"; }

    std::vector<ParamDecl> params() override {
        return {};  // No parameters
    }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;
};

} // namespace vivid
