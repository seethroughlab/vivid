// Vivid Render3D - Flat/Gouraud Shader
// Supports Unlit, Flat, and Gouraud shading modes

struct Uniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    lightDir: vec3f,
    _pad1: f32,
    lightColor: vec3f,
    ambient: f32,
    baseColor: vec4f,
    shadingMode: u32,  // 0=Unlit, 1=Flat, 2=Gouraud
    _pad2: vec3f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
    @location(3) lighting: f32,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = uniforms.mvp * vec4f(in.position, 1.0);
    out.worldNormal = normalize((uniforms.model * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    out.color = in.color;

    // Gouraud: compute lighting per-vertex
    if (uniforms.shadingMode == 2u) {
        let NdotL = max(dot(out.worldNormal, uniforms.lightDir), 0.0);
        out.lighting = uniforms.ambient + NdotL;
    } else {
        out.lighting = 1.0;
    }
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var finalColor = uniforms.baseColor * in.color;

    if (uniforms.shadingMode == 0u) {
        // Unlit: just return the color
        return finalColor;
    } else if (uniforms.shadingMode == 1u) {
        // Flat: per-fragment lighting
        let NdotL = max(dot(normalize(in.worldNormal), uniforms.lightDir), 0.0);
        let lighting = uniforms.ambient + NdotL * uniforms.lightColor;
        return vec4f(finalColor.rgb * lighting, finalColor.a);
    } else {
        // Gouraud: use pre-computed vertex lighting
        return vec4f(finalColor.rgb * in.lighting * uniforms.lightColor, finalColor.a);
    }
}
