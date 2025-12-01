// Color Grade shader - Math-based color grading (lift/gamma/gain, shadows/midtones/highlights)
// Uses uniforms:
//   u.param0 = saturation (0-2, 1 = normal)
//   u.param1 = contrast (0-2, 1 = normal)
//   u.param2 = temperature (-1 to 1, negative = cool, positive = warm)
//   u.param3 = tint (-1 to 1, negative = green, positive = magenta)
//   u.vec0.xy = shadows color offset (r, g) - negative values shift to cyan/blue
//   u.vec0.zw = midtones color offset (r, g)
//   u.vec1.xy = highlights color offset (r, g)
//   u.mode = 0: Full color grade, 1: Vintage film, 2: Teal & Orange, 3: Bleach bypass

// Convert RGB to HSL
fn rgbToHsl(c: vec3f) -> vec3f {
    let maxC = max(c.r, max(c.g, c.b));
    let minC = min(c.r, min(c.g, c.b));
    let delta = maxC - minC;

    let l = (maxC + minC) * 0.5;

    var h = 0.0;
    var s = 0.0;

    if (delta > 0.0001) {
        s = delta / (1.0 - abs(2.0 * l - 1.0));

        if (maxC == c.r) {
            h = (c.g - c.b) / delta;
            if (c.g < c.b) { h = h + 6.0; }
        } else if (maxC == c.g) {
            h = (c.b - c.r) / delta + 2.0;
        } else {
            h = (c.r - c.g) / delta + 4.0;
        }
        h = h / 6.0;
    }

    return vec3f(h, s, l);
}

// Convert HSL to RGB
fn hslToRgb(hsl: vec3f) -> vec3f {
    let h = hsl.x;
    let s = hsl.y;
    let l = hsl.z;

    if (s < 0.0001) {
        return vec3f(l);
    }

    let q = select(l + s - l * s, l * (1.0 + s), l < 0.5);
    let p = 2.0 * l - q;

    let hue2rgb = |t: f32| -> f32 {
        var tt = t;
        if (tt < 0.0) { tt = tt + 1.0; }
        if (tt > 1.0) { tt = tt - 1.0; }
        if (tt < 1.0/6.0) { return p + (q - p) * 6.0 * tt; }
        if (tt < 0.5) { return q; }
        if (tt < 2.0/3.0) { return p + (q - p) * (2.0/3.0 - tt) * 6.0; }
        return p;
    };

    return vec3f(
        hue2rgb(h + 1.0/3.0),
        hue2rgb(h),
        hue2rgb(h - 1.0/3.0)
    );
}

fn hue2rgbHelper(p: f32, q: f32, t: f32) -> f32 {
    var tt = t;
    if (tt < 0.0) { tt = tt + 1.0; }
    if (tt > 1.0) { tt = tt - 1.0; }
    if (tt < 1.0/6.0) { return p + (q - p) * 6.0 * tt; }
    if (tt < 0.5) { return q; }
    if (tt < 2.0/3.0) { return p + (q - p) * (2.0/3.0 - tt) * 6.0; }
    return p;
}

// Apply temperature/tint (white balance adjustment)
fn applyTemperatureTint(c: vec3f, temp: f32, tint: f32) -> vec3f {
    // Temperature shifts blue-yellow
    // Tint shifts green-magenta
    var result = c;
    result.r = result.r + temp * 0.1;
    result.b = result.b - temp * 0.1;
    result.g = result.g - tint * 0.1;
    result.r = result.r + tint * 0.05;
    result.b = result.b + tint * 0.05;
    return result;
}

// Get luminance weight for shadows/midtones/highlights
fn getShadowWeight(lum: f32) -> f32 {
    return 1.0 - smoothstep(0.0, 0.5, lum);
}

fn getMidtoneWeight(lum: f32) -> f32 {
    return 1.0 - abs(lum - 0.5) * 2.0;
}

fn getHighlightWeight(lum: f32) -> f32 {
    return smoothstep(0.5, 1.0, lum);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let saturation = u.param0;
    let contrast = u.param1;
    let temperature = u.param2;
    let tint = u.param3;
    let shadowOffset = u.vec0.xy;
    let midtoneOffset = u.vec0.zw;
    let highlightOffset = u.vec1.xy;
    let mode = u.mode;

    var color = textureSample(inputTexture, inputSampler, in.uv).rgb;

    // Apply preset modes
    if (mode == 1) {
        // Vintage film: warm shadows, faded blacks, soft contrast
        let lum = dot(color, vec3f(0.2126, 0.7152, 0.0722));
        color = mix(vec3f(lum), color, 0.85);  // Desaturate slightly
        color = color * 0.9 + 0.05;  // Fade blacks, reduce whites
        color = applyTemperatureTint(color, 0.3, 0.1);  // Warm tint
    } else if (mode == 2) {
        // Teal & Orange: classic cinema look
        let lum = dot(color, vec3f(0.2126, 0.7152, 0.0722));
        let shadowW = getShadowWeight(lum);
        let highlightW = getHighlightWeight(lum);
        color.r = color.r + highlightW * 0.1;  // Orange highlights
        color.g = color.g + highlightW * 0.05;
        color.r = color.r - shadowW * 0.05;    // Teal shadows
        color.g = color.g + shadowW * 0.03;
        color.b = color.b + shadowW * 0.08;
    } else if (mode == 3) {
        // Bleach bypass: desaturated, high contrast
        let lum = dot(color, vec3f(0.2126, 0.7152, 0.0722));
        color = mix(color, vec3f(lum), 0.5);  // 50% desaturate
        color = (color - 0.5) * 1.5 + 0.5;    // Increase contrast
    } else {
        // Full color grade with all controls

        // Apply temperature and tint
        color = applyTemperatureTint(color, temperature, tint);

        // Apply contrast
        color = (color - 0.5) * contrast + 0.5;

        // Apply saturation
        let lum = dot(color, vec3f(0.2126, 0.7152, 0.0722));
        color = mix(vec3f(lum), color, saturation);

        // Apply shadows/midtones/highlights color shift
        let shadowW = getShadowWeight(lum);
        let midtoneW = getMidtoneWeight(lum);
        let highlightW = getHighlightWeight(lum);

        color.r = color.r + shadowOffset.x * shadowW * 0.2;
        color.g = color.g + shadowOffset.y * shadowW * 0.2;
        color.r = color.r + midtoneOffset.x * midtoneW * 0.15;
        color.g = color.g + midtoneOffset.y * midtoneW * 0.15;
        color.r = color.r + highlightOffset.x * highlightW * 0.2;
        color.g = color.g + highlightOffset.y * highlightW * 0.2;
    }

    return vec4f(clamp(color, vec3f(0.0), vec3f(1.0)), 1.0);
}
