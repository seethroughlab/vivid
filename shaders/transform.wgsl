// Transform shader (translate, scale, rotate)
// Uses uniforms:
//   u.vec0 = translate
//   u.vec1 = scale
//   u.param0 = rotate (radians)
//   u.param1 = pivotX, u.param2 = pivotY

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let translate = u.vec0;
    let scale = u.vec1;
    let rotate = u.param0;
    let pivot = vec2f(u.param1, u.param2);

    // Transform UV (inverse transform to sample source)
    var uv = in.uv;

    // Move to pivot
    uv = uv - pivot;

    // Inverse scale
    uv = uv / scale;

    // Inverse rotation
    let c = cos(-rotate);
    let s = sin(-rotate);
    uv = vec2f(uv.x * c - uv.y * s, uv.x * s + uv.y * c);

    // Inverse translation
    uv = uv - translate;

    // Move back from pivot
    uv = uv + pivot;

    // Sample with border handling
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    return textureSample(inputTexture, inputSampler, uv);
}
