@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(u.r, u.g, u.b, u.a);
}
