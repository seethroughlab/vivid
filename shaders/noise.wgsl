// Simple noise shader for testing
// Uses the uniforms: u.time, u.resolution, u.frame

// Hash function for pseudo-random noise
fn hash(p: vec2f) -> f32 {
    let p2 = vec2f(dot(p, vec2f(127.1, 311.7)), dot(p, vec2f(269.5, 183.3)));
    return fract(sin(dot(p2, vec2f(12.9898, 78.233))) * 43758.5453);
}

// Value noise
fn noise(p: vec2f) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u = f * f * (3.0 - 2.0 * f);

    return mix(
        mix(hash(i + vec2f(0.0, 0.0)), hash(i + vec2f(1.0, 0.0)), u.x),
        mix(hash(i + vec2f(0.0, 1.0)), hash(i + vec2f(1.0, 1.0)), u.x),
        u.y
    );
}

// Fractal Brownian Motion
fn fbm(p: vec2f) -> f32 {
    var value = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    var pos = p;

    for (var i = 0; i < 5; i++) {
        value += amplitude * noise(pos * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let uv = in.uv;

    // Animate noise over time
    let t = u.time * 0.5;

    // Create flowing noise pattern
    let n1 = fbm(uv * 4.0 + vec2f(t, 0.0));
    let n2 = fbm(uv * 4.0 + vec2f(0.0, t * 0.7));
    let n3 = fbm(uv * 4.0 + vec2f(t * 0.3, t * 0.5));

    // Color gradient based on noise
    let color = vec3f(
        n1 * 0.5 + 0.3,
        n2 * 0.4 + 0.2,
        n3 * 0.6 + 0.4
    );

    return vec4f(color, 1.0);
}
