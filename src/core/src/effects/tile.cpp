// Vivid Effects 2D - Tile Operator Implementation

#include <vivid/effects/tile.h>

namespace vivid::effects {

const char* Tile::fragmentShader() const {
    return R"(
struct Uniforms {
    repeatX: f32,
    repeatY: f32,
    offsetX: f32,
    offsetY: f32,
    mirror: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var uv = input.uv * vec2f(uniforms.repeatX, uniforms.repeatY);
    uv += vec2f(uniforms.offsetX, uniforms.offsetY);

    if (uniforms.mirror != 0) {
        // Mirror at tile boundaries
        let tile = floor(uv);
        let f = fract(uv);
        // Flip on odd tiles
        let flipX = i32(tile.x) % 2;
        let flipY = i32(tile.y) % 2;
        uv = vec2f(
            select(f.x, 1.0 - f.x, flipX != 0),
            select(f.y, 1.0 - f.y, flipY != 0)
        );
    } else {
        uv = fract(uv);
    }

    return textureSample(inputTex, texSampler, uv);
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleTextureEffect<Tile, TileUniforms>;

} // namespace vivid::effects
