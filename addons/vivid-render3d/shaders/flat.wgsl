// Vivid Render3D - Non-PBR Shader
// Supports Unlit, Flat, Gouraud, VertexLit, and Toon shading modes

struct Uniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    lightDir: vec3f,
    _pad1: f32,
    lightColor: vec3f,
    ambient: f32,
    baseColor: vec4f,
    shadingMode: u32,  // 0=Unlit, 1=Flat, 2=Gouraud, 3=VertexLit, 4=Toon
    toonLevels: u32,   // Number of toon shading bands (2-8)
    _pad2: vec2f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,  // xyz = tangent, w = handedness (unused in flat shading)
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
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

    // Per-vertex lighting modes
    if (uniforms.shadingMode == 2u) {
        // Gouraud: per-vertex lighting
        let NdotL = max(dot(out.worldNormal, uniforms.lightDir), 0.0);
        out.lighting = uniforms.ambient + NdotL;
    } else if (uniforms.shadingMode == 3u) {
        // VertexLit: simple N·L diffuse
        let NdotL = max(dot(out.worldNormal, uniforms.lightDir), 0.0);
        out.lighting = NdotL;
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
    } else if (uniforms.shadingMode == 2u) {
        // Gouraud: use pre-computed vertex lighting with ambient
        return vec4f(finalColor.rgb * in.lighting * uniforms.lightColor, finalColor.a);
    } else if (uniforms.shadingMode == 3u) {
        // VertexLit: simple per-vertex diffuse
        return vec4f(finalColor.rgb * in.lighting * uniforms.lightColor, finalColor.a);
    } else if (uniforms.shadingMode == 4u) {
        // Toon: quantized cel-shading
        let NdotL = max(dot(normalize(in.worldNormal), uniforms.lightDir), 0.0);
        let levels = f32(uniforms.toonLevels);
        let quantized = floor(NdotL * levels + 0.5) / levels;
        let lighting = uniforms.ambient + quantized * uniforms.lightColor;
        return vec4f(finalColor.rgb * lighting, finalColor.a);
    } else {
        // Default fallback
        return vec4f(finalColor.rgb * in.lighting * uniforms.lightColor, finalColor.a);
    }
}
