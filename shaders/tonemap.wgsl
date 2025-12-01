// Tonemap shader - HDR to SDR tone mapping
// Uses uniforms:
//   u.param0 = exposure (exposure adjustment, 0.1-10)
//   u.param1 = gamma (gamma correction, 1.0-3.0, default 2.2)
//   u.mode = 0: ACES Filmic, 1: Reinhard, 2: Uncharted 2, 3: Simple exposure

// ACES Filmic tone mapping curve
// Based on: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
fn acesFilm(x: vec3f) -> vec3f {
    let a = 2.51;
    let b = 0.03;
    let c = 2.43;
    let d = 0.59;
    let e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3f(0.0), vec3f(1.0));
}

// Reinhard tone mapping (simple version)
fn reinhard(x: vec3f) -> vec3f {
    return x / (x + vec3f(1.0));
}

// Extended Reinhard with white point
fn reinhardExtended(x: vec3f, whitePoint: f32) -> vec3f {
    let wp2 = whitePoint * whitePoint;
    let numerator = x * (1.0 + x / wp2);
    return numerator / (1.0 + x);
}

// Uncharted 2 tone mapping (John Hable's formula)
fn uncharted2Partial(x: vec3f) -> vec3f {
    let A = 0.15;  // Shoulder strength
    let B = 0.50;  // Linear strength
    let C = 0.10;  // Linear angle
    let D = 0.20;  // Toe strength
    let E = 0.02;  // Toe numerator
    let F = 0.30;  // Toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

fn uncharted2(x: vec3f) -> vec3f {
    let W = 11.2;  // Linear white point
    let exposureBias = 2.0;
    let curr = uncharted2Partial(x * exposureBias);
    let whiteScale = vec3f(1.0) / uncharted2Partial(vec3f(W));
    return curr * whiteScale;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let exposure = u.param0;
    let gamma = u.param1;
    let mode = u.mode;

    var color = textureSample(inputTexture, inputSampler, in.uv).rgb;

    // Apply exposure
    color = color * exposure;

    // Apply tone mapping based on mode
    var mapped: vec3f;
    if (mode == 0) {
        // ACES Filmic (default, best for general use)
        mapped = acesFilm(color);
    } else if (mode == 1) {
        // Reinhard (simple, preserves color ratios)
        mapped = reinhard(color);
    } else if (mode == 2) {
        // Uncharted 2 (good for games, nice S-curve)
        mapped = uncharted2(color);
    } else {
        // Simple exposure only (linear clamp)
        mapped = clamp(color, vec3f(0.0), vec3f(1.0));
    }

    // Apply gamma correction
    let invGamma = 1.0 / gamma;
    let result = pow(mapped, vec3f(invGamma));

    return vec4f(result, 1.0);
}
