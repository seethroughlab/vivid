// Vivid - Operator Base Class Implementation

#include <vivid/operator.h>
#include <vivid/context.h>

namespace vivid {

void Operator::processAndRegister(Context& ctx, const std::string& registerName) {
    // Auto-register on first call
    if (!m_registered) {
        std::string regName = registerName.empty() ? name() : registerName;
        ctx.registerOperator(regName, this);
        m_registered = true;
    }

    // Call the actual process implementation
    process(ctx);
}

} // namespace vivid
