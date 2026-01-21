// Vivid Render3D - Tonemapping Library
// HDR to LDR tonemapping functions

// Reinhard tonemapping
fn reinhard(color: vec3f) -> vec3f {
    return color / (color + vec3f(1.0));
}

// Extended Reinhard with white point
fn reinhardExtended(color: vec3f, maxWhite: f32) -> vec3f {
    let numerator = color * (1.0 + color / (maxWhite * maxWhite));
    return numerator / (1.0 + color);
}

// ACES filmic tonemapping (approximate)
fn acesFilmic(color: vec3f) -> vec3f {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

// Gamma correction (linear to sRGB)
fn gammaCorrect(color: vec3f) -> vec3f {
    return pow(color, vec3f(1.0 / 2.2));
}

// Inverse gamma (sRGB to linear)
fn gammaToLinear(color: vec3f) -> vec3f {
    return pow(color, vec3f(2.2));
}
