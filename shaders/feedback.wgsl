// Feedback shader (trails/echo effect)
// Uses uniforms:
//   u.param0 = decay (0-1, how much previous frame persists)
//   u.param1 = zoom (1.0 = no zoom)
//   u.param2 = rotate (radians)
//   u.vec0 = translate

// Note: inputTexture contains the new frame
// For the feedback buffer (previous frame), we'd need a second texture
// For now, we'll implement the transform part and max blend

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let decay = u.param0;
    let zoom = u.param1;
    let rotate = u.param2;
    let translate = u.vec0;

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

    // Sample "feedback" (for now, just use transformed input as a demo)
    // In real implementation, we'd sample from a second texture (previous frame)
    var feedback = vec4f(0.0);
    if (fbUv.x >= 0.0 && fbUv.x <= 1.0 && fbUv.y >= 0.0 && fbUv.y <= 1.0) {
        feedback = textureSample(inputTexture, inputSampler, fbUv) * decay;
    }

    // Composite: new input over decayed feedback
    return max(input, feedback);
}
