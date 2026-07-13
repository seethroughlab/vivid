#include "gpu/builtin_ops.h"

#include "gpu/op_runtime.h"

// The visuals catalog is now 100% auto-discovered package operators — every op that
// once lived here (Plasma, Feedback, Blur, Video, Output, Tint, Shape, Gradient,
// Transform, Kaleidoscope, Composite, ShapeGrid, Lines, VectorText, Mesh, Text,
// CustomShader) has been migrated to a dylib under app/operators/packages/core-visuals/,
// scanned + dlopen'd at launch (see main.cpp / operator_scan). This hook remains as the
// registration point for any future compiled-in built-in, but the visual catalog carries
// none.
namespace vivid {

void register_builtin_ops(OpRegistry& /*reg*/) {
    // Intentionally empty — visual operators are auto-discovered packages.
}

}  // namespace vivid
