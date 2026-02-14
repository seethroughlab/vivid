// Blit shader - renders a texture to the screen using a full-screen triangle
// Supports multiple display scaling modes for aspect ratio handling
// Shows checkerboard pattern for transparent areas (like Photoshop)

// @include "lib/fullscreen.wgsl"

// Display mode uniforms
struct BlitUniforms {
    screenSize: vec2f,
    textureSize: vec2f,
    displayMode: u32,
};

@group(0) @binding(0) var textureSampler: sampler;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var<uniform> uniforms: BlitUniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

// Full-screen triangle that covers the viewport
// Uses vertex ID to generate positions (no vertex buffer needed)
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangleBit(vertexIndex, true);
    var output: VertexOutput;
    output.position = fs.position;
    output.uv = fs.uv;
    return output;
}

// Generate checkerboard pattern for transparent/outside areas
fn checkerboard(pos: vec2f) -> vec3f {
    let size = 8.0;  // Checker size in pixels
    let checker = floor(pos.x / size) + floor(pos.y / size);
    let isLight = (i32(checker) & 1) == 0;
    return select(vec3f(0.3, 0.3, 0.3), vec3f(0.5, 0.5, 0.5), isLight);
}

// Calculate UV coordinates based on display mode
// Mode 0: Stretch - fill window, ignore aspect ratio
// Mode 1: Fit - maintain aspect ratio, letterbox/pillarbox
// Mode 2: Fill - maintain aspect ratio, crop to fill
// Mode 3: FillHorizontal - fill width, may crop top/bottom
// Mode 4: FillVertical - fill height, may crop left/right
fn calculateUV(baseUV: vec2f) -> vec2f {
    let screenAspect = uniforms.screenSize.x / uniforms.screenSize.y;
    let textureAspect = uniforms.textureSize.x / uniforms.textureSize.y;

    // Mode 0: Stretch - return as-is
    if (uniforms.displayMode == 0u) {
        return baseUV;
    }

    var scale = vec2f(1.0);

    if (uniforms.displayMode == 1u) {
        // Fit: scale to fit entirely within screen (letterbox/pillarbox)
        if (screenAspect > textureAspect) {
            // Screen is wider than texture - pillarbox (bars on sides)
            scale.x = textureAspect / screenAspect;
        } else {
            // Screen is taller than texture - letterbox (bars on top/bottom)
            scale.y = screenAspect / textureAspect;
        }
    } else if (uniforms.displayMode == 2u) {
        // Fill: scale to cover entire screen (crop edges)
        if (screenAspect > textureAspect) {
            // Screen is wider - crop top/bottom
            scale.y = screenAspect / textureAspect;
        } else {
            // Screen is taller - crop left/right
            scale.x = textureAspect / screenAspect;
        }
    } else if (uniforms.displayMode == 3u) {
        // FillHorizontal: always fill width, crop or letterbox vertically
        scale.y = screenAspect / textureAspect;
    } else if (uniforms.displayMode == 4u) {
        // FillVertical: always fill height, crop or pillarbox horizontally
        scale.x = textureAspect / screenAspect;
    }

    // Apply scale centered around 0.5
    return (baseUV - 0.5) / scale + 0.5;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = calculateUV(input.uv);

    // Check if UV is outside texture bounds (for letterbox/pillarbox modes)
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        // Show checkerboard for out-of-bounds areas
        return vec4f(checkerboard(input.position.xy), 1.0);
    }

    let texColor = textureSample(inputTexture, textureSampler, uv);

    // Blend texture over checkerboard pattern based on alpha
    let bg = checkerboard(input.position.xy);
    let blended = mix(bg, texColor.rgb, texColor.a);

    return vec4f(blended, 1.0);
}
