// Constant shader - outputs a solid color
// Uses: u.param0-3 = RGBA color

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return vec4f(u.param0, u.param1, u.param2, u.param3);
}
