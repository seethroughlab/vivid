#pragma once
// CPU-side geometry types + column-major 4x4 math, dependency-free (no glTF/json/stb, no wgpu). The
// lightweight core every mesh operator shares: the render helpers (mesh_render.h) need only this,
// while the glTF parser (gltf_mesh.h) builds on it and adds json/stb. Splitting them keeps geometry-
// only ops (MeshRender, MeshDisplace) from having to link nlohmann/json just to reuse the math.
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vivid::geom {

// Interleaved vertex: position + normal + uv (shader locations 0,1,2). The single vertex layout
// every mesh operator agrees on (see mesh_render.h's attribute table).
struct MeshVertex { float px, py, pz, nx, ny, nz, u, v; };

struct CpuMesh {
    std::vector<MeshVertex> verts;
    std::vector<uint32_t>   indices;
    std::vector<uint8_t>    tex_rgba;   // empty => untextured (caller uses a white fallback)
    int tw = 0, th = 0;
};

// ---- column-major 4x4 math (WGSL convention: (row i, col j) = m[j*4+i]) --------------------------
using Mat4 = std::array<float, 16>;
inline Mat4 identity() { Mat4 m{}; m[0] = m[5] = m[10] = m[15] = 1.f; return m; }
inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 c{};
    for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
        float s = 0.f; for (int k = 0; k < 4; ++k) s += a[k * 4 + i] * b[j * 4 + k];
        c[j * 4 + i] = s;
    }
    return c;
}
inline Mat4 rot_x(float a) { Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a); m[5]=c; m[9]=-s; m[6]=s; m[10]=c; return m; }
inline Mat4 rot_y(float a) { Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a); m[0]=c; m[8]=s; m[2]=-s; m[10]=c; return m; }
inline Mat4 translate(float x, float y, float z) { Mat4 m = identity(); m[12]=x; m[13]=y; m[14]=z; return m; }
inline Mat4 scale3(float s) { Mat4 m = identity(); m[0]=m[5]=m[10]=s; return m; }
// WebGPU perspective (clip z in [0,1]). Matches vivid-3d's perspective_wgpu (Render3D): world +Y maps
// to clip +Y so the render lands upright in the canonical top-down texture convention (see
// fullscreenTriangle in gpu_common.h). Previously m[5] was negated to cancel the per-pass Y flip that
// the old fullscreen convention introduced; that flip is gone, so the negate is too.
inline Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
    Mat4 m{}; const float f = 1.f / std::tan(fovy * 0.5f);
    m[0] = f / aspect; m[5] = f;
    m[10] = zfar / (znear - zfar); m[11] = -1.f;
    m[14] = (znear * zfar) / (znear - zfar);
    return m;
}
inline void mul_point(const Mat4& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0]*x + m[4]*y + m[8]*z + m[12];
    oy = m[1]*x + m[5]*y + m[9]*z + m[13];
    oz = m[2]*x + m[6]*y + m[10]*z + m[14];
}
inline void mul_dir(const Mat4& m, float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = m[0]*x + m[4]*y + m[8]*z;
    oy = m[1]*x + m[5]*y + m[9]*z;
    oz = m[2]*x + m[6]*y + m[10]*z;
}
inline Mat4 from_trs(const std::array<float,3>& t, const std::array<float,4>& q, const std::array<float,3>& s) {
    const float x=q[0], y=q[1], z=q[2], w=q[3];
    Mat4 r = identity();
    r[0]=1-2*(y*y+z*z); r[1]=2*(x*y+z*w);   r[2]=2*(x*z-y*w);
    r[4]=2*(x*y-z*w);   r[5]=1-2*(x*x+z*z); r[6]=2*(y*z+x*w);
    r[8]=2*(x*z+y*w);   r[9]=2*(y*z-x*w);   r[10]=1-2*(x*x+y*y);
    Mat4 sc = identity(); sc[0]=s[0]; sc[5]=s[1]; sc[10]=s[2];
    return mul(translate(t[0],t[1],t[2]), mul(r, sc));
}

}  // namespace vivid::geom
