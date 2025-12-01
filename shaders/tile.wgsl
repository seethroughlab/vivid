// Tile shader - repeats texture in a grid with per-tile transforms

struct Uniforms {
    time: f32,
    resolution: vec2<f32>,
    _pad0: f32,
    param0: f32,  // cols
    param1: f32,  // rows
    param2: f32,  // gapX
    param3: f32,  // gapY
    param4: f32,  // tileScale
    param5: f32,  // rotation
    param6: f32,  // flags (mirrorX, mirrorY, mirrorAlternate, randomRotation)
    param7: f32,  // scaleAnim
    vec0: vec2<f32>,  // oddRowOffset, oddColOffset
    vec1: vec2<f32>,  // unused
    mode: i32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTexture: texture_2d<f32>;

// Simple hash for pseudo-random rotation
fn hash(p: vec2<f32>) -> f32 {
    var h = dot(p, vec2<f32>(127.1, 311.7));
    return fract(sin(h) * 43758.5453);
}

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let cols = u.param0;
    let rows = u.param1;
    let gapX = u.param2;
    let gapY = u.param3;
    let tileScale = u.param4 * u.param7;  // Apply scale animation
    let rotation = u.param5;
    let flags = i32(u.param6);
    let oddRowOffset = u.vec0.x;
    let oddColOffset = u.vec0.y;

    // Decode flags
    let mirrorX = (flags & 1) != 0;
    let mirrorY = (flags & 2) != 0;
    let mirrorAlternate = (flags & 4) != 0;
    let randomRotation = (flags & 8) != 0;

    // Calculate cell size with gaps
    let cellWidth = (1.0 - gapX * (cols - 1.0)) / cols;
    let cellHeight = (1.0 - gapY * (rows - 1.0)) / rows;

    // Find which cell we're in
    var cellX = floor(uv.x / (cellWidth + gapX));
    var cellY = floor(uv.y / (cellHeight + gapY));

    // Apply odd row/col offset
    var adjustedUV = uv;
    if (i32(cellY) % 2 == 1 && oddRowOffset != 0.0) {
        adjustedUV.x = adjustedUV.x - oddRowOffset * (cellWidth + gapX);
        cellX = floor(adjustedUV.x / (cellWidth + gapX));
    }
    if (i32(cellX) % 2 == 1 && oddColOffset != 0.0) {
        adjustedUV.y = adjustedUV.y - oddColOffset * (cellHeight + gapY);
        cellY = floor(adjustedUV.y / (cellHeight + gapY));
    }

    // Clamp cell indices
    cellX = clamp(cellX, 0.0, cols - 1.0);
    cellY = clamp(cellY, 0.0, rows - 1.0);

    // Calculate position within cell
    var cellUV: vec2<f32>;
    cellUV.x = (adjustedUV.x - cellX * (cellWidth + gapX)) / cellWidth;
    cellUV.y = (adjustedUV.y - cellY * (cellHeight + gapY)) / cellHeight;

    // Check if we're in a gap
    if (cellUV.x < 0.0 || cellUV.x > 1.0 || cellUV.y < 0.0 || cellUV.y > 1.0) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }

    // Apply mirroring
    if (mirrorX || (mirrorAlternate && i32(cellX) % 2 == 1)) {
        cellUV.x = 1.0 - cellUV.x;
    }
    if (mirrorY || (mirrorAlternate && i32(cellY) % 2 == 1)) {
        cellUV.y = 1.0 - cellUV.y;
    }

    // Move to center for rotation/scale
    cellUV = cellUV - 0.5;

    // Apply per-tile rotation
    var rot = rotation;
    if (randomRotation) {
        rot = rot + hash(vec2<f32>(cellX, cellY)) * 6.28318;
    }

    let cosR = cos(rot);
    let sinR = sin(rot);
    let rotatedUV = vec2<f32>(
        cellUV.x * cosR - cellUV.y * sinR,
        cellUV.x * sinR + cellUV.y * cosR
    );

    // Apply scale
    var scaledUV = rotatedUV / tileScale;

    // Move back from center
    scaledUV = scaledUV + 0.5;

    // Check bounds after transformation
    if (scaledUV.x < 0.0 || scaledUV.x > 1.0 || scaledUV.y < 0.0 || scaledUV.y > 1.0) {
        return vec4<f32>(0.0, 0.0, 0.0, 1.0);
    }

    // Sample texture
    return textureSample(inputTexture, texSampler, scaledUV);
}

// Vertex shader (fullscreen triangle)
struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var pos = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0)
    );
    var uv = array<vec2<f32>, 3>(
        vec2<f32>(0.0, 1.0),
        vec2<f32>(2.0, 1.0),
        vec2<f32>(0.0, -1.0)
    );

    var output: VertexOutput;
    output.position = vec4<f32>(pos[vertexIndex], 0.0, 1.0);
    output.uv = uv[vertexIndex];
    return output;
}
