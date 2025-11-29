// Feedback shader (trails/echo effect)
// Creates video feedback by blending current frame with transformed previous frame
//
// Uses uniforms:
//   u.param0 = decay (0-1, how much previous frame persists, default 0.95)
//   u.param1 = zoom (>1 = zoom in, <1 = zoom out, default 1.0)
//   u.param2 = rotate (radians per frame)
//   u.vec0 = translate (x, y offset per frame)
//   u.mode = blend mode (0=max, 1=add, 2=screen, 3=mix)
//
// Inputs:
//   inputTexture = new frame (current input)
//   inputTexture2 = previous frame (feedback buffer)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var decay = u.param0;
    var zoom = u.param1;
    let rotate = u.param2;
    let translate = u.vec0;
    let blendMode = u.mode;

    // Apply defaults
    if (decay <= 0.0) { decay = 0.95; }
    if (zoom <= 0.0) { zoom = 1.0; }

    // Transform UV for feedback sampling (where to read previous frame)
    var fbUv = in.uv - 0.5;  // Center

    // Apply zoom
    fbUv = fbUv / zoom;

    // Apply rotation
    let c = cos(rotate);
    let s = sin(rotate);
    fbUv = vec2f(fbUv.x * c - fbUv.y * s, fbUv.x * s + fbUv.y * c);

    // Apply translation
    fbUv = fbUv + translate;

    fbUv = fbUv + 0.5;  // Uncenter

    // Sample new input
    let input = textureSample(inputTexture, inputSampler, in.uv);

    // Sample previous frame (feedback buffer) with transform
    var feedback = vec4f(0.0);
    if (fbUv.x >= 0.0 && fbUv.x <= 1.0 && fbUv.y >= 0.0 && fbUv.y <= 1.0) {
        feedback = textureSample(inputTexture2, inputSampler, fbUv) * decay;
    }

    // Composite based on blend mode
    var result: vec3f;
    switch (blendMode) {
        case 1: {
            // Add - bright areas accumulate
            result = input.rgb + feedback.rgb;
        }
        case 2: {
            // Screen - softer additive
            result = 1.0 - (1.0 - input.rgb) * (1.0 - feedback.rgb);
        }
        case 3: {
            // Mix - true blend
            result = mix(input.rgb, feedback.rgb, decay);
        }
        case 0, default: {
            // Max - whichever is brighter wins
            result = max(input.rgb, feedback.rgb);
        }
    }

    return vec4f(result, max(input.a, feedback.a));
}
