// Vivid Effects 2D - SolidColor Operator Implementation

#include <vivid/effects/solid_color.h>

namespace vivid::effects {

const char* SolidColor::fragmentShader() const {
    return R"(
struct Uniforms {
    color: vec4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return uniforms.color;
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleGeneratorEffect<SolidColor, SolidColorUniforms>;

} // namespace vivid::effects
