#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

// =============================================================================
// Geometry generators — inline, all produce Vertex3D with CCW winding
// =============================================================================

static constexpr float kPi  = 3.14159265358979323846f;
static constexpr float kTau = 6.28318530717958647692f;

// ---------------------------------------------------------------------------
// Cube: 24 vertices (4 per face for flat normals), 36 indices
// ---------------------------------------------------------------------------
static void generate_cube(std::vector<vivid::gpu::Vertex3D>& verts,
                          std::vector<uint32_t>& indices) {
    using V = vivid::gpu::Vertex3D;
    verts.clear();
    indices.clear();

    // Face data: normal, tangent (v0→v1 direction), then 4 corner positions
    struct Face {
        float nx, ny, nz;
        float tx, ty, tz;
        float v[4][3];
    };
    static const Face faces[] = {
        // +Z front   tangent: +X
        { 0,0,1,  1,0,0, { {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f} } },
        // -Z back    tangent: -X
        { 0,0,-1, -1,0,0, { { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f} } },
        // +X right   tangent: -Z
        { 1,0,0,  0,0,-1, { { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f} } },
        // -X left    tangent: +Z
        {-1,0,0,  0,0,1, { {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f} } },
        // +Y top     tangent: +X
        { 0,1,0,  1,0,0, { {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f} } },
        // -Y bottom  tangent: +X
        { 0,-1,0, 1,0,0, { {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f} } },
    };

    static const float uvs[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };

    for (int f = 0; f < 6; ++f) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        for (int i = 0; i < 4; ++i) {
            V v{};
            v.position[0] = faces[f].v[i][0];
            v.position[1] = faces[f].v[i][1];
            v.position[2] = faces[f].v[i][2];
            v.normal[0] = faces[f].nx;
            v.normal[1] = faces[f].ny;
            v.normal[2] = faces[f].nz;
            v.tangent[0] = faces[f].tx;
            v.tangent[1] = faces[f].ty;
            v.tangent[2] = faces[f].tz;
            v.tangent[3] = 1.0f;
            v.uv[0] = uvs[i][0];
            v.uv[1] = uvs[i][1];
            verts.push_back(v);
        }
        // Two triangles per face (CCW)
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

// ---------------------------------------------------------------------------
// Sphere (UV sphere): radius 0.5
// ---------------------------------------------------------------------------
static void generate_sphere(std::vector<vivid::gpu::Vertex3D>& verts,
                            std::vector<uint32_t>& indices, int detail) {
    using V = vivid::gpu::Vertex3D;
    verts.clear();
    indices.clear();

    int stacks = detail / 2;
    int slices = detail;
    if (stacks < 2) stacks = 2;
    if (slices < 3) slices = 3;

    float radius = 0.5f;

    for (int i = 0; i <= stacks; ++i) {
        float phi = kPi * static_cast<float>(i) / static_cast<float>(stacks);
        float sp = std::sin(phi), cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = kTau * static_cast<float>(j) / static_cast<float>(slices);
            float st = std::sin(theta), ct = std::cos(theta);

            V v{};
            v.normal[0] = sp * ct;
            v.normal[1] = cp;
            v.normal[2] = sp * st;
            v.position[0] = radius * v.normal[0];
            v.position[1] = radius * v.normal[1];
            v.position[2] = radius * v.normal[2];
            // Tangent = dP/dtheta direction; degenerate at poles (sp≈0)
            if (sp > 1e-6f) {
                v.tangent[0] = -st;
                v.tangent[1] = 0.0f;
                v.tangent[2] = ct;
            } else {
                v.tangent[0] = 1.0f;
                v.tangent[1] = 0.0f;
                v.tangent[2] = 0.0f;
            }
            v.tangent[3] = 1.0f;
            v.uv[0] = static_cast<float>(j) / static_cast<float>(slices);
            v.uv[1] = static_cast<float>(i) / static_cast<float>(stacks);
            verts.push_back(v);
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (slices + 1) + j);
            uint32_t b = a + 1;
            uint32_t c = static_cast<uint32_t>((i + 1) * (slices + 1) + j);
            uint32_t d = c + 1;
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(b);
        }
    }
}

// ---------------------------------------------------------------------------
// Torus: major R=0.35, minor r=0.15
// ---------------------------------------------------------------------------
static void generate_torus(std::vector<vivid::gpu::Vertex3D>& verts,
                           std::vector<uint32_t>& indices, int detail) {
    using V = vivid::gpu::Vertex3D;
    verts.clear();
    indices.clear();

    int rings = detail;
    int sides = detail / 2;
    if (rings < 3) rings = 3;
    if (sides < 3) sides = 3;

    float R = 0.35f;  // major radius
    float r = 0.15f;  // minor radius

    for (int i = 0; i <= rings; ++i) {
        float u = kTau * static_cast<float>(i) / static_cast<float>(rings);
        float cu = std::cos(u), su = std::sin(u);
        for (int j = 0; j <= sides; ++j) {
            float v_angle = kTau * static_cast<float>(j) / static_cast<float>(sides);
            float cv = std::cos(v_angle), sv = std::sin(v_angle);

            // Ring center
            float cx = R * cu;
            float cz = R * su;

            V v{};
            v.position[0] = (R + r * cv) * cu;
            v.position[1] = r * sv;
            v.position[2] = (R + r * cv) * su;

            // Normal = normalize(position - ring_center)
            float nx = v.position[0] - cx;
            float ny = v.position[1];
            float nz = v.position[2] - cz;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            v.normal[0] = nx;
            v.normal[1] = ny;
            v.normal[2] = nz;
            // Tangent = dP/du (major ring direction)
            v.tangent[0] = -su;
            v.tangent[1] = 0.0f;
            v.tangent[2] = cu;
            v.tangent[3] = 1.0f;

            v.uv[0] = static_cast<float>(i) / static_cast<float>(rings);
            v.uv[1] = static_cast<float>(j) / static_cast<float>(sides);
            verts.push_back(v);
        }
    }

    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sides; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (sides + 1) + j);
            uint32_t b = a + 1;
            uint32_t c = static_cast<uint32_t>((i + 1) * (sides + 1) + j);
            uint32_t d = c + 1;
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(b);
        }
    }
}

// ---------------------------------------------------------------------------
// Plane: XZ at Y=0, extents [-0.5, 0.5]
// ---------------------------------------------------------------------------
static void generate_plane(std::vector<vivid::gpu::Vertex3D>& verts,
                           std::vector<uint32_t>& indices, int detail) {
    using V = vivid::gpu::Vertex3D;
    verts.clear();
    indices.clear();

    int sub = detail;
    if (sub < 1) sub = 1;

    for (int i = 0; i <= sub; ++i) {
        float z = -0.5f + static_cast<float>(i) / static_cast<float>(sub);
        for (int j = 0; j <= sub; ++j) {
            float x = -0.5f + static_cast<float>(j) / static_cast<float>(sub);
            V v{};
            v.position[0] = x;
            v.position[1] = 0.0f;
            v.position[2] = z;
            v.normal[0] = 0.0f;
            v.normal[1] = 1.0f;
            v.normal[2] = 0.0f;
            v.tangent[0] = 1.0f;
            v.tangent[1] = 0.0f;
            v.tangent[2] = 0.0f;
            v.tangent[3] = 1.0f;
            v.uv[0] = static_cast<float>(j) / static_cast<float>(sub);
            v.uv[1] = static_cast<float>(i) / static_cast<float>(sub);
            verts.push_back(v);
        }
    }

    for (int i = 0; i < sub; ++i) {
        for (int j = 0; j < sub; ++j) {
            uint32_t a = static_cast<uint32_t>(i * (sub + 1) + j);
            uint32_t b = a + 1;
            uint32_t c = static_cast<uint32_t>((i + 1) * (sub + 1) + j);
            uint32_t d = c + 1;
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(d);
            indices.push_back(a);
            indices.push_back(d);
            indices.push_back(b);
        }
    }
}

// ---------------------------------------------------------------------------
// Cylinder: radius 0.5, height 1.0, Y in [-0.5, 0.5]
// Body + top cap + bottom cap
// ---------------------------------------------------------------------------
static void generate_cylinder(std::vector<vivid::gpu::Vertex3D>& verts,
                              std::vector<uint32_t>& indices, int detail) {
    using V = vivid::gpu::Vertex3D;
    verts.clear();
    indices.clear();

    int slices = detail;
    if (slices < 3) slices = 3;

    float radius = 0.5f;
    float half_h = 0.5f;

    // --- Body ---
    for (int i = 0; i <= 1; ++i) {
        float y = (i == 0) ? -half_h : half_h;
        for (int j = 0; j <= slices; ++j) {
            float theta = kTau * static_cast<float>(j) / static_cast<float>(slices);
            float ct = std::cos(theta), st = std::sin(theta);

            V v{};
            v.position[0] = radius * ct;
            v.position[1] = y;
            v.position[2] = radius * st;
            v.normal[0] = ct;
            v.normal[1] = 0.0f;
            v.normal[2] = st;
            // Tangent = circumferential direction
            v.tangent[0] = -st;
            v.tangent[1] = 0.0f;
            v.tangent[2] = ct;
            v.tangent[3] = 1.0f;
            v.uv[0] = static_cast<float>(j) / static_cast<float>(slices);
            v.uv[1] = static_cast<float>(i);
            verts.push_back(v);
        }
    }

    // Body indices
    for (int j = 0; j < slices; ++j) {
        uint32_t a = static_cast<uint32_t>(j);
        uint32_t b = a + 1;
        uint32_t c = static_cast<uint32_t>(slices + 1 + j);
        uint32_t d = c + 1;
        indices.push_back(a);
        indices.push_back(c);
        indices.push_back(d);
        indices.push_back(a);
        indices.push_back(d);
        indices.push_back(b);
    }

    // --- Top cap (Y = +half_h, normal = +Y) ---
    {
        uint32_t center = static_cast<uint32_t>(verts.size());
        V cv{};
        cv.position[0] = 0; cv.position[1] = half_h; cv.position[2] = 0;
        cv.normal[0] = 0; cv.normal[1] = 1; cv.normal[2] = 0;
        cv.tangent[0] = 1; cv.tangent[1] = 0; cv.tangent[2] = 0; cv.tangent[3] = 1;
        cv.uv[0] = 0.5f; cv.uv[1] = 0.5f;
        verts.push_back(cv);

        uint32_t ring_start = static_cast<uint32_t>(verts.size());
        for (int j = 0; j <= slices; ++j) {
            float theta = kTau * static_cast<float>(j) / static_cast<float>(slices);
            float ct = std::cos(theta), st = std::sin(theta);
            V v{};
            v.position[0] = radius * ct;
            v.position[1] = half_h;
            v.position[2] = radius * st;
            v.normal[0] = 0; v.normal[1] = 1; v.normal[2] = 0;
            v.tangent[0] = ct; v.tangent[1] = 0; v.tangent[2] = st; v.tangent[3] = 1;
            v.uv[0] = ct * 0.5f + 0.5f;
            v.uv[1] = st * 0.5f + 0.5f;
            verts.push_back(v);
        }

        // CCW when viewed from +Y (looking down)
        for (int j = 0; j < slices; ++j) {
            indices.push_back(center);
            indices.push_back(ring_start + static_cast<uint32_t>(j));
            indices.push_back(ring_start + static_cast<uint32_t>(j + 1));
        }
    }

    // --- Bottom cap (Y = -half_h, normal = -Y) ---
    {
        uint32_t center = static_cast<uint32_t>(verts.size());
        V cv{};
        cv.position[0] = 0; cv.position[1] = -half_h; cv.position[2] = 0;
        cv.normal[0] = 0; cv.normal[1] = -1; cv.normal[2] = 0;
        cv.tangent[0] = 1; cv.tangent[1] = 0; cv.tangent[2] = 0; cv.tangent[3] = 1;
        cv.uv[0] = 0.5f; cv.uv[1] = 0.5f;
        verts.push_back(cv);

        uint32_t ring_start = static_cast<uint32_t>(verts.size());
        for (int j = 0; j <= slices; ++j) {
            float theta = kTau * static_cast<float>(j) / static_cast<float>(slices);
            float ct = std::cos(theta), st = std::sin(theta);
            V v{};
            v.position[0] = radius * ct;
            v.position[1] = -half_h;
            v.position[2] = radius * st;
            v.normal[0] = 0; v.normal[1] = -1; v.normal[2] = 0;
            v.tangent[0] = ct; v.tangent[1] = 0; v.tangent[2] = st; v.tangent[3] = 1;
            v.uv[0] = ct * 0.5f + 0.5f;
            v.uv[1] = st * 0.5f + 0.5f;
            verts.push_back(v);
        }

        // Reversed winding for bottom cap (CCW when viewed from -Y)
        for (int j = 0; j < slices; ++j) {
            indices.push_back(center);
            indices.push_back(ring_start + static_cast<uint32_t>(j + 1));
            indices.push_back(ring_start + static_cast<uint32_t>(j));
        }
    }
}

// ---------------------------------------------------------------------------
// Cone: base radius 0.5 at Y=-0.5, apex at Y=0.5
// Body as per-slice triangles (flat-shaded), plus bottom cap
// ---------------------------------------------------------------------------
static void generate_cone(std::vector<vivid::gpu::Vertex3D>& verts,
                          std::vector<uint32_t>& indices, int detail) {
    using V = vivid::gpu::Vertex3D;
    verts.clear();
    indices.clear();

    int slices = detail;
    if (slices < 3) slices = 3;

    float radius = 0.5f;
    float half_h = 0.5f;
    float height = 1.0f;

    // Slant normal: ny = R/sqrt(R²+H²), horizontal = H/sqrt(R²+H²)
    float slant_len = std::sqrt(radius * radius + height * height);
    float ny_slant  = radius / slant_len;
    float nh_slant  = height / slant_len;

    // --- Body: per-slice triangles from apex to base ring ---
    for (int j = 0; j < slices; ++j) {
        float theta0 = kTau * static_cast<float>(j) / static_cast<float>(slices);
        float theta1 = kTau * static_cast<float>(j + 1) / static_cast<float>(slices);
        float mid_theta = (theta0 + theta1) * 0.5f;

        float ct0 = std::cos(theta0), st0 = std::sin(theta0);
        float ct1 = std::cos(theta1), st1 = std::sin(theta1);
        float ctm = std::cos(mid_theta), stm = std::sin(mid_theta);

        // Face normal at mid-angle (flat shading)
        float nx = nh_slant * ctm;
        float nz = nh_slant * stm;

        // Tangent: circumferential direction at mid-angle
        float tx = -stm, tz = ctm;

        uint32_t base = static_cast<uint32_t>(verts.size());

        // Apex vertex
        V v_apex{};
        v_apex.position[0] = 0.0f;
        v_apex.position[1] = half_h;
        v_apex.position[2] = 0.0f;
        v_apex.normal[0] = nx; v_apex.normal[1] = ny_slant; v_apex.normal[2] = nz;
        v_apex.tangent[0] = tx; v_apex.tangent[1] = 0.0f; v_apex.tangent[2] = tz;
        v_apex.tangent[3] = 1.0f;
        v_apex.uv[0] = (static_cast<float>(j) + 0.5f) / static_cast<float>(slices);
        v_apex.uv[1] = 0.0f;
        verts.push_back(v_apex);

        // Base vertex 0
        V v_b0{};
        v_b0.position[0] = radius * ct0;
        v_b0.position[1] = -half_h;
        v_b0.position[2] = radius * st0;
        v_b0.normal[0] = nx; v_b0.normal[1] = ny_slant; v_b0.normal[2] = nz;
        v_b0.tangent[0] = tx; v_b0.tangent[1] = 0.0f; v_b0.tangent[2] = tz;
        v_b0.tangent[3] = 1.0f;
        v_b0.uv[0] = static_cast<float>(j) / static_cast<float>(slices);
        v_b0.uv[1] = 1.0f;
        verts.push_back(v_b0);

        // Base vertex 1
        V v_b1{};
        v_b1.position[0] = radius * ct1;
        v_b1.position[1] = -half_h;
        v_b1.position[2] = radius * st1;
        v_b1.normal[0] = nx; v_b1.normal[1] = ny_slant; v_b1.normal[2] = nz;
        v_b1.tangent[0] = tx; v_b1.tangent[1] = 0.0f; v_b1.tangent[2] = tz;
        v_b1.tangent[3] = 1.0f;
        v_b1.uv[0] = static_cast<float>(j + 1) / static_cast<float>(slices);
        v_b1.uv[1] = 1.0f;
        verts.push_back(v_b1);

        // Triangle: apex, b0, b1 (CCW when viewed from outside)
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    }

    // --- Bottom cap (Y = -half_h, normal = -Y) ---
    {
        uint32_t center = static_cast<uint32_t>(verts.size());
        V cv{};
        cv.position[0] = 0; cv.position[1] = -half_h; cv.position[2] = 0;
        cv.normal[0] = 0; cv.normal[1] = -1; cv.normal[2] = 0;
        cv.tangent[0] = 1; cv.tangent[1] = 0; cv.tangent[2] = 0; cv.tangent[3] = 1;
        cv.uv[0] = 0.5f; cv.uv[1] = 0.5f;
        verts.push_back(cv);

        uint32_t ring_start = static_cast<uint32_t>(verts.size());
        for (int j = 0; j <= slices; ++j) {
            float theta = kTau * static_cast<float>(j) / static_cast<float>(slices);
            float ct = std::cos(theta), st = std::sin(theta);
            V v{};
            v.position[0] = radius * ct;
            v.position[1] = -half_h;
            v.position[2] = radius * st;
            v.normal[0] = 0; v.normal[1] = -1; v.normal[2] = 0;
            v.tangent[0] = ct; v.tangent[1] = 0; v.tangent[2] = st; v.tangent[3] = 1;
            v.uv[0] = ct * 0.5f + 0.5f;
            v.uv[1] = st * 0.5f + 0.5f;
            verts.push_back(v);
        }

        // Reversed winding for bottom cap (CCW when viewed from -Y)
        for (int j = 0; j < slices; ++j) {
            indices.push_back(center);
            indices.push_back(ring_start + static_cast<uint32_t>(j + 1));
            indices.push_back(ring_start + static_cast<uint32_t>(j));
        }
    }
}

// ---------------------------------------------------------------------------
// Pyramid: square base at Y=-0.5, apex at Y=0.5, extents [-0.5, 0.5]
// 4 flat-shaded triangular side faces + 1 square base
// 16 vertices, 18 indices (fixed topology)
// ---------------------------------------------------------------------------
static void generate_pyramid(std::vector<vivid::gpu::Vertex3D>& verts,
                             std::vector<uint32_t>& indices) {
    using V = vivid::gpu::Vertex3D;
    verts.clear();
    indices.clear();

    float half = 0.5f;

    // Apex
    float apex[3] = {0.0f, half, 0.0f};

    // Base corners (Y = -half, CCW when viewed from below)
    //  c0=(-h,-h,-h)  c1=(h,-h,-h)  c2=(h,-h,h)  c3=(-h,-h,h)
    float c0[3] = {-half, -half, -half};
    float c1[3] = { half, -half, -half};
    float c2[3] = { half, -half,  half};
    float c3[3] = {-half, -half,  half};

    // Helper: compute face normal from two edge vectors (CCW)
    auto cross_norm = [](const float a[3], const float b[3], const float c[3], float out[3]) {
        float e1[3] = {b[0]-a[0], b[1]-a[1], b[2]-a[2]};
        float e2[3] = {c[0]-a[0], c[1]-a[1], c[2]-a[2]};
        out[0] = e1[1]*e2[2] - e1[2]*e2[1];
        out[1] = e1[2]*e2[0] - e1[0]*e2[2];
        out[2] = e1[0]*e2[1] - e1[1]*e2[0];
        float len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
        if (len > 1e-8f) { out[0] /= len; out[1] /= len; out[2] /= len; }
    };

    // Helper: add a flat-shaded triangle
    auto add_tri = [&](const float p0[3], const float p1[3], const float p2[3],
                       const float n[3], const float t[3]) {
        uint32_t base = static_cast<uint32_t>(verts.size());
        float uvs[3][2] = {{0.5f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
        for (int i = 0; i < 3; ++i) {
            V v{};
            const float* p = (i == 0) ? p0 : (i == 1) ? p1 : p2;
            v.position[0] = p[0]; v.position[1] = p[1]; v.position[2] = p[2];
            v.normal[0] = n[0]; v.normal[1] = n[1]; v.normal[2] = n[2];
            v.tangent[0] = t[0]; v.tangent[1] = t[1]; v.tangent[2] = t[2];
            v.tangent[3] = 1.0f;
            v.uv[0] = uvs[i][0]; v.uv[1] = uvs[i][1];
            verts.push_back(v);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
    };

    // 4 side faces (apex, base_edge_start, base_edge_end) — CCW from outside
    // Front face: apex, c1, c0 (facing -Z)
    {
        float n[3]; cross_norm(apex, c1, c0, n);
        float t[3] = {1, 0, 0};
        add_tri(apex, c1, c0, n, t);
    }
    // Right face: apex, c2, c1 (facing +X)
    {
        float n[3]; cross_norm(apex, c2, c1, n);
        float t[3] = {0, 0, -1};
        add_tri(apex, c2, c1, n, t);
    }
    // Back face: apex, c3, c2 (facing +Z)
    {
        float n[3]; cross_norm(apex, c3, c2, n);
        float t[3] = {-1, 0, 0};
        add_tri(apex, c3, c2, n, t);
    }
    // Left face: apex, c0, c3 (facing -X)
    {
        float n[3]; cross_norm(apex, c0, c3, n);
        float t[3] = {0, 0, 1};
        add_tri(apex, c0, c3, n, t);
    }

    // Base face (Y = -half, normal = -Y): c0, c3, c2, c1 — 2 triangles (CCW from below)
    {
        uint32_t base = static_cast<uint32_t>(verts.size());
        float n[3] = {0, -1, 0};
        float t[3] = {1,  0, 0};
        float bp[4][3] = {
            {c0[0], c0[1], c0[2]},
            {c3[0], c3[1], c3[2]},
            {c2[0], c2[1], c2[2]},
            {c1[0], c1[1], c1[2]},
        };
        float uvs[4][2] = {{0,0}, {0,1}, {1,1}, {1,0}};
        for (int i = 0; i < 4; ++i) {
            V v{};
            v.position[0] = bp[i][0]; v.position[1] = bp[i][1]; v.position[2] = bp[i][2];
            v.normal[0] = n[0]; v.normal[1] = n[1]; v.normal[2] = n[2];
            v.tangent[0] = t[0]; v.tangent[1] = t[1]; v.tangent[2] = t[2];
            v.tangent[3] = 1.0f;
            v.uv[0] = uvs[i][0]; v.uv[1] = uvs[i][1];
            verts.push_back(v);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

// =============================================================================
// Shape3D Operator
// =============================================================================

struct Shape3D : vivid::OperatorBase {
    static constexpr const char* kName   = "Shape3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    // Shape
    vivid::Param<int>   shape  {"shape",  0, {"Cube", "Sphere", "Torus", "Plane", "Cylinder", "Cone", "Pyramid"}};
    vivid::Param<int>   detail {"detail", 32, 4, 128};

    // Color (warm orange default — reads well under Blinn-Phong)
    vivid::Param<float> r {"r", 0.8f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 0.2f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 1.0f, 0.0f, 1.0f};

    // Material
    vivid::Param<float> roughness {"roughness", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> metallic  {"metallic",  0.0f, 0.0f, 1.0f};
    vivid::Param<float> emission  {"emission",  0.0f, 0.0f, 5.0f};
    vivid::Param<int>   unlit     {"unlit",     0, {"Off", "On"}};
    vivid::Param<int>   shading   {"shading",   0, {"Default", "Toon"}};
    vivid::Param<float> toon_levels {"toon_levels", 4.0f, 2.0f, 8.0f};

    // Transform
    vivid::Param<float> pos_x   {"pos_x",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_y   {"pos_y",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_z   {"pos_z",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> rot_x   {"rot_x",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> rot_y   {"rot_y",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> rot_z   {"rot_z",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> scale_x {"scale_x", 1.0f,  0.01f, 50.0f};
    vivid::Param<float> scale_y {"scale_y", 1.0f,  0.01f, 50.0f};
    vivid::Param<float> scale_z {"scale_z", 1.0f,  0.01f, 50.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(shape, "Shape");
        vivid::param_group(detail, "Shape");

        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);

        vivid::param_group(roughness, "Material");
        vivid::param_group(metallic, "Material");
        vivid::param_group(emission, "Material");
        vivid::param_group(unlit, "Material");
        vivid::param_group(shading, "Material");
        vivid::param_group(toon_levels, "Material");

        vivid::param_group(pos_x, "Transform");
        vivid::param_group(pos_y, "Transform");
        vivid::param_group(pos_z, "Transform");
        vivid::param_group(rot_x, "Transform");
        vivid::param_group(rot_y, "Transform");
        vivid::param_group(rot_z, "Transform");
        vivid::param_group(scale_x, "Transform");
        vivid::param_group(scale_y, "Transform");
        vivid::param_group(scale_z, "Transform");

        out.push_back(&shape);
        out.push_back(&detail);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
        out.push_back(&roughness);
        out.push_back(&metallic);
        out.push_back(&emission);
        out.push_back(&unlit);
        out.push_back(&shading);
        out.push_back(&toon_levels);
        out.push_back(&pos_x);
        out.push_back(&pos_y);
        out.push_back(&pos_z);
        out.push_back(&rot_x);
        out.push_back(&rot_y);
        out.push_back(&rot_z);
        out.push_back(&scale_x);
        out.push_back(&scale_y);
        out.push_back(&scale_z);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"scene", VIVID_PORT_GPU_SCENE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        int cur_shape  = shape.int_value();
        int cur_detail = detail.int_value();

        // Rebuild geometry if shape or detail changed
        if (cur_shape != cached_shape_ || cur_detail != cached_detail_) {
            rebuild_geometry(gpu, cur_shape, cur_detail);
            cached_shape_  = cur_shape;
            cached_detail_ = cur_detail;
        }

        if (!vertex_buffer_ || !index_buffer_) return;

        // Build model matrix: T * Rz * Ry * Rx * S
        float sx = scale_x.value, sy = scale_y.value, sz = scale_z.value;
        float rx = rot_x.value,   ry = rot_y.value,   rz = rot_z.value;
        float px = pos_x.value,   py = pos_y.value,   pz = pos_z.value;

        mat4x4 S, tmp;
        mat4x4_identity(S);
        mat4x4_scale_aniso(S, S, sx, sy, sz);
        mat4x4_rotate_X(tmp, S, rx);
        mat4x4_rotate_Y(S, tmp, ry);
        mat4x4_rotate_Z(tmp, S, rz);

        mat4x4 T;
        mat4x4_translate(T, px, py, pz);
        mat4x4_mul(fragment_.model_matrix, T, tmp);

        // Set color + material
        fragment_.color[0] = r.value;
        fragment_.color[1] = g.value;
        fragment_.color[2] = b.value;
        fragment_.color[3] = a.value;
        fragment_.roughness = roughness.value;
        fragment_.metallic  = metallic.value;
        fragment_.emission  = emission.value;
        fragment_.unlit     = unlit.int_value() != 0;
        fragment_.shading_mode = (shading.int_value() == 1) ? 1.0f : 0.0f;
        fragment_.toon_levels  = toon_levels.value;

        // Set geometry
        fragment_.vertex_buffer   = vertex_buffer_;
        fragment_.vertex_buf_size = vertex_buf_size_;
        fragment_.index_buffer    = index_buffer_;
        fragment_.index_count     = index_count_;

        // CPU vertex/index cache for downstream Deformer/Boolean3D
        fragment_.cpu_vertices     = cpu_verts_.data();
        fragment_.cpu_vertex_count = static_cast<uint32_t>(cpu_verts_.size());
        fragment_.cpu_indices      = cpu_indices_.data();
        fragment_.cpu_index_count  = static_cast<uint32_t>(cpu_indices_.size());

        // Leave pipeline/material_binds as nullptr — Render3D uses defaults
        fragment_.pipeline       = nullptr;
        fragment_.material_binds = nullptr;

        gpu->output_scene = &fragment_;
    }

    ~Shape3D() override {
        vivid::gpu::release(vertex_buffer_);
        vivid::gpu::release(index_buffer_);
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    WGPUBuffer   vertex_buffer_  = nullptr;
    WGPUBuffer   index_buffer_   = nullptr;
    uint64_t     vertex_buf_size_ = 0;
    uint32_t     index_count_     = 0;
    int          cached_shape_    = -1;
    int          cached_detail_   = -1;
    std::vector<vivid::gpu::Vertex3D> cpu_verts_;
    std::vector<uint32_t> cpu_indices_;

    void rebuild_geometry(VividGpuState* gpu, int shape_type, int det) {
        std::vector<vivid::gpu::Vertex3D> verts;
        std::vector<uint32_t> idx;

        switch (shape_type) {
            case 0: generate_cube(verts, idx);         break;
            case 1: generate_sphere(verts, idx, det);  break;
            case 2: generate_torus(verts, idx, det);   break;
            case 3: generate_plane(verts, idx, det);   break;
            case 4: generate_cylinder(verts, idx, det); break;
            case 5: generate_cone(verts, idx, det);     break;
            case 6: generate_pyramid(verts, idx);       break;
            default: generate_cube(verts, idx);         break;
        }

        // Release old buffers
        vivid::gpu::release(vertex_buffer_);
        vivid::gpu::release(index_buffer_);

        cpu_verts_ = verts;
        cpu_indices_ = idx;

        vertex_buf_size_ = verts.size() * sizeof(vivid::gpu::Vertex3D);
        index_count_ = static_cast<uint32_t>(idx.size());

        vertex_buffer_ = vivid::gpu::create_vertex_buffer(
            gpu->device, gpu->queue, verts.data(), vertex_buf_size_, "Shape3D VB");
        index_buffer_ = vivid::gpu::create_index_buffer(
            gpu->device, gpu->queue, idx.data(), index_count_, "Shape3D IB");
    }
};

VIVID_REGISTER(Shape3D)
