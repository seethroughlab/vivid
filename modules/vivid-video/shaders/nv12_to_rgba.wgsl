// NV12 to RGBA conversion compute shader
// NV12 format: Y plane (full resolution, R8) + UV plane (half resolution, RG8)
// Uses BT.709 color space conversion coefficients

@group(0) @binding(0) var yPlane: texture_2d<f32>;
@group(0) @binding(1) var uvPlane: texture_2d<f32>;
@group(0) @binding(2) var outputTexture: texture_storage_2d<rgba8unorm, write>;

// BT.709 YUV to RGB conversion coefficients
// R = Y + 1.5748 * (V - 0.5)
// G = Y - 0.1873 * (U - 0.5) - 0.4681 * (V - 0.5)
// B = Y + 1.8556 * (U - 0.5)

@compute @workgroup_size(16, 16)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let dims = textureDimensions(outputTexture);
    if (id.x >= dims.x || id.y >= dims.y) {
        return;
    }

    // Sample Y at full resolution
    let y = textureLoad(yPlane, vec2i(id.xy), 0).r;

    // Sample UV at half resolution (NV12 UV plane is half width and half height)
    let uvCoord = vec2i(id.xy) / 2;
    let uv = textureLoad(uvPlane, uvCoord, 0).rg;

    // U and V are in range [0, 1], shift to [-0.5, 0.5]
    let u = uv.r - 0.5;
    let v = uv.g - 0.5;

    // BT.709 conversion
    let r = y + 1.5748 * v;
    let g = y - 0.1873 * u - 0.4681 * v;
    let b = y + 1.8556 * u;

    // Clamp and write output
    let rgba = vec4f(
        clamp(r, 0.0, 1.0),
        clamp(g, 0.0, 1.0),
        clamp(b, 0.0, 1.0),
        1.0
    );

    textureStore(outputTexture, vec2i(id.xy), rgba);
}
