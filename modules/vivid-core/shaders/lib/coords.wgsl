// Vivid Core - Coordinate Transformation Utilities
// Common coordinate space conversions for 2D effects

// Convert pixel coordinates to NDC (Normalized Device Coordinates)
// Input: pixel position, screen resolution
// Output: NDC in range [-1, 1] with Y pointing up
fn pixelToNdc(pixel: vec2f, resolution: vec2f) -> vec2f {
    return vec2f(
        (pixel.x / resolution.x) * 2.0 - 1.0,
        1.0 - (pixel.y / resolution.y) * 2.0
    );
}

// Convert NDC to UV coordinates
// Input: NDC in range [-1, 1]
// Output: UV in range [0, 1] with Y=0 at top
fn ndcToUv(ndc: vec2f) -> vec2f {
    return vec2f(
        (ndc.x + 1.0) * 0.5,
        (1.0 - ndc.y) * 0.5
    );
}

// Convert UV to NDC
// Input: UV in range [0, 1]
// Output: NDC in range [-1, 1]
fn uvToNdc(uv: vec2f) -> vec2f {
    return vec2f(
        uv.x * 2.0 - 1.0,
        1.0 - uv.y * 2.0
    );
}

// Apply aspect ratio correction to UV coordinates
// Makes circular shapes appear circular instead of stretched
fn correctAspect(uv: vec2f, resolution: vec2f) -> vec2f {
    let aspect = resolution.x / resolution.y;
    return vec2f((uv.x - 0.5) * aspect + 0.5, uv.y);
}

// Uncorrect aspect ratio (inverse of correctAspect)
fn uncorrectAspect(uv: vec2f, resolution: vec2f) -> vec2f {
    let aspect = resolution.x / resolution.y;
    return vec2f((uv.x - 0.5) / aspect + 0.5, uv.y);
}

// Rotate UV coordinates around a center point
fn rotateUv(uv: vec2f, center: vec2f, angle: f32) -> vec2f {
    let offset = uv - center;
    let cosA = cos(angle);
    let sinA = sin(angle);
    return vec2f(
        offset.x * cosA - offset.y * sinA,
        offset.x * sinA + offset.y * cosA
    ) + center;
}

// Scale UV coordinates around a center point
fn scaleUv(uv: vec2f, center: vec2f, scale: f32) -> vec2f {
    return (uv - center) * scale + center;
}
