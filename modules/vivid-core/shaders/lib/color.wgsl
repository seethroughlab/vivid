// Vivid Core - Color Space Conversions
// HSV, RGB, and gamma correction utilities

// HSV to RGB conversion
// h: hue [0, 1], s: saturation [0, 1], v: value/brightness [0, 1]
fn hsv2rgb(hsv: vec3f) -> vec3f {
    let h = hsv.x;
    let s = hsv.y;
    let v = hsv.z;

    let c = v * s;
    let hp = h * 6.0;
    let x = c * (1.0 - abs(hp % 2.0 - 1.0));
    let m = v - c;

    var rgb: vec3f;
    if (hp < 1.0) {
        rgb = vec3f(c, x, 0.0);
    } else if (hp < 2.0) {
        rgb = vec3f(x, c, 0.0);
    } else if (hp < 3.0) {
        rgb = vec3f(0.0, c, x);
    } else if (hp < 4.0) {
        rgb = vec3f(0.0, x, c);
    } else if (hp < 5.0) {
        rgb = vec3f(x, 0.0, c);
    } else {
        rgb = vec3f(c, 0.0, x);
    }

    return rgb + vec3f(m, m, m);
}

// RGB to HSV conversion
fn rgb2hsv(rgb: vec3f) -> vec3f {
    let r = rgb.r;
    let g = rgb.g;
    let b = rgb.b;

    let maxC = max(max(r, g), b);
    let minC = min(min(r, g), b);
    let delta = maxC - minC;

    var h: f32 = 0.0;
    var s: f32 = 0.0;
    let v = maxC;

    if (delta > 0.0) {
        s = delta / maxC;

        if (r == maxC) {
            h = (g - b) / delta;
        } else if (g == maxC) {
            h = 2.0 + (b - r) / delta;
        } else {
            h = 4.0 + (r - g) / delta;
        }

        h = h / 6.0;
        if (h < 0.0) {
            h = h + 1.0;
        }
    }

    return vec3f(h, s, v);
}

// sRGB to linear color space (gamma decode)
fn srgbToLinear(c: vec3f) -> vec3f {
    return pow(c, vec3f(2.2));
}

// Linear to sRGB color space (gamma encode)
fn linearToSrgb(c: vec3f) -> vec3f {
    return pow(c, vec3f(1.0 / 2.2));
}

// More accurate sRGB to linear conversion
fn srgbToLinearAccurate(c: vec3f) -> vec3f {
    let cutoff = vec3f(0.04045);
    let linear = c / 12.92;
    let gamma = pow((c + 0.055) / 1.055, vec3f(2.4));
    return select(gamma, linear, c <= cutoff);
}

// More accurate linear to sRGB conversion
fn linearToSrgbAccurate(c: vec3f) -> vec3f {
    let cutoff = vec3f(0.0031308);
    let linear = c * 12.92;
    let gamma = 1.055 * pow(c, vec3f(1.0 / 2.4)) - 0.055;
    return select(gamma, linear, c <= cutoff);
}

// Luminance calculation (perceived brightness)
fn luminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

// Contrast adjustment
fn adjustContrast(c: vec3f, contrast: f32) -> vec3f {
    return (c - 0.5) * contrast + 0.5;
}

// Saturation adjustment
fn adjustSaturation(c: vec3f, saturation: f32) -> vec3f {
    let lum = luminance(c);
    return mix(vec3f(lum), c, saturation);
}
