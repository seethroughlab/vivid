// See SPEC.md for project description
// Wipeout 2097-style hover vehicle generator with audio reactivity
#include <vivid/vivid.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <iostream>
#include "livery_gen.h"  // Team palettes

using namespace vivid;

// === GLOBALS ===

// Vehicle meshes - many more parts for complex craft
static Mesh3D fuselageMesh;
static Mesh3D cockpitMesh;
static Mesh3D leftPodMesh;
static Mesh3D rightPodMesh;
static Mesh3D leftEngineMesh;
static Mesh3D rightEngineMesh;
static Mesh3D leftFinMesh;
static Mesh3D rightFinMesh;
static Mesh3D rearWingMesh;
static Mesh3D leftCanardMesh;
static Mesh3D rightCanardMesh;

// Scene objects
static Camera3D camera;
static Texture output;
static SceneLighting lighting;

// Grime textures for weathered look
static Texture grimeBody;      // Main body grime
static Texture grimePods;      // Side pods grime
static Texture grimeDetail;    // Fins/details grime
static Environment iblEnvironment;
static bool hasIBL = false;

// Metal025 PBR textures for engine/metallic parts
static Texture metalAlbedo;
static Texture metalRoughness;
static Texture metalMetallic;
static Texture metalNormal;
static bool hasMetalTextures = false;

// Procedural livery texture (generated with team colors, numbers, stripes)
static Texture liveryTexture;
static int liveryTeam = -1;  // Track which team the livery was generated for

// Camera control
static float cameraYaw = 0.5f;
static float cameraPitch = 0.25f;
static float cameraDistance = 10.0f;
static float lastMouseX = 0;
static float lastMouseY = 0;
static bool isDragging = false;

// Audio-reactive state
static float engineGlow = 0.0f;
static float hoverOffset = 0.0f;
static float colorPhase = 0.0f;

// === TEAM PALETTES (from livery generator) ===

static const livery::TeamPalette* palettes[] = {
    &livery::FEISAR,   // Blue/White
    &livery::AG_SYS,   // Yellow/Blue
    &livery::AURICOM,  // Red/White
    &livery::QIREX,    // Purple/Cyan
    &livery::PIRANHA,  // Black/Orange
};
static int currentTeam = 4;  // Start with PIRANHA (black/orange) for visibility

// === MESH GENERATION HELPERS ===

void addQuadSingleSide(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
                        glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                        glm::vec3 normal) {
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({p0, normal, {0, 0}});
    verts.push_back({p1, normal, {1, 0}});
    verts.push_back({p2, normal, {1, 1}});
    verts.push_back({p3, normal, {0, 1}});
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

void addQuad(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
             glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
             glm::vec3 normal) {
    addQuadSingleSide(verts, indices, p0, p1, p2, p3, normal);
    addQuadSingleSide(verts, indices, p0, p3, p2, p1, -normal);
}

void addTriangleSingleSide(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
                            glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 normal) {
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({p0, normal, {0, 0}});
    verts.push_back({p1, normal, {1, 0}});
    verts.push_back({p2, normal, {0.5f, 1}});
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

void addTriangle(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
                 glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 normal) {
    addTriangleSingleSide(verts, indices, p0, p1, p2, normal);
    addTriangleSingleSide(verts, indices, p0, p2, p1, -normal);
}

// Calculate face normal from 3 points
glm::vec3 faceNormal(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2) {
    return glm::normalize(glm::cross(p1 - p0, p2 - p0));
}

// Add quad with explicit UV coordinates (for proper texture mapping)
void addQuadUV(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
               glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
               glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2, glm::vec2 uv3,
               glm::vec3 normal) {
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({p0, normal, uv0});
    verts.push_back({p1, normal, uv1});
    verts.push_back({p2, normal, uv2});
    verts.push_back({p3, normal, uv3});
    // Front face
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
    // Back face
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 1);
    indices.push_back(base + 0);
    indices.push_back(base + 3);
    indices.push_back(base + 2);
}

// Add triangle with explicit UV coordinates
void addTriangleUV(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
                   glm::vec3 p0, glm::vec3 p1, glm::vec3 p2,
                   glm::vec2 uv0, glm::vec2 uv1, glm::vec2 uv2,
                   glm::vec3 normal) {
    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({p0, normal, uv0});
    verts.push_back({p1, normal, uv1});
    verts.push_back({p2, normal, uv2});
    // Front face
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    // Back face
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 1);
}

// === FUSELAGE ===
// Long, aerodynamic body with raised center spine and multiple segments

Mesh3D buildFuselage(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    // Fuselage dimensions (longer and sleeker)
    float length = 5.0f;
    float width = 0.8f;
    float height = 0.35f;
    float spineHeight = 0.15f;
    int segments = 8;

    // Cross-section profile at each segment
    // Format: {x-position, width-scale, height-scale, spine-scale}
    float profile[][4] = {
        {-0.50f, 0.00f, 0.50f, 0.0f},   // Nose tip
        {-0.35f, 0.30f, 0.70f, 0.3f},   // Nose taper
        {-0.20f, 0.60f, 0.85f, 0.6f},   // Front section
        {-0.05f, 0.85f, 1.00f, 0.9f},   // Cockpit area
        { 0.10f, 1.00f, 1.00f, 1.0f},   // Max width
        { 0.25f, 0.95f, 0.95f, 0.9f},   // Mid body
        { 0.40f, 0.80f, 0.85f, 0.7f},   // Rear taper start
        { 0.50f, 0.50f, 0.70f, 0.4f},   // Rear end
    };

    // V coordinates for each ring point (around the cross-section)
    // Maps the 7 points (0-6) to V range 0-1
    float ringV[] = {0.0f, 0.15f, 0.35f, 0.5f, 0.65f, 0.85f, 1.0f};

    // Generate cross-section rings with UV data
    std::vector<std::vector<glm::vec3>> rings;
    std::vector<float> ringU;  // U coordinate for each segment (along length)

    for (int s = 0; s < segments; ++s) {
        float x = profile[s][0] * length;
        float w = profile[s][1] * width;
        float h = profile[s][2] * height;
        float sp = profile[s][3] * spineHeight;

        // U coordinate: 0 at nose, 1 at rear
        float u = (profile[s][0] + 0.5f);  // profile x goes -0.5 to 0.5, map to 0-1
        ringU.push_back(u);

        std::vector<glm::vec3> ring;
        ring.push_back({x, -h, -w});          // 0: bottom-left
        ring.push_back({x, 0, -w * 1.1f});    // 1: left bulge
        ring.push_back({x, h, -w * 0.3f});    // 2: top-left
        ring.push_back({x, h + sp, 0});       // 3: spine peak
        ring.push_back({x, h, w * 0.3f});     // 4: top-right
        ring.push_back({x, 0, w * 1.1f});     // 5: right bulge
        ring.push_back({x, -h, w});           // 6: bottom-right
        rings.push_back(ring);
    }

    // Connect rings with quads using proper UVs
    for (int s = 0; s < segments - 1; ++s) {
        auto& r0 = rings[s];
        auto& r1 = rings[s + 1];
        float u0 = ringU[s];
        float u1 = ringU[s + 1];

        // Connect each pair of adjacent points
        for (int i = 0; i < 6; ++i) {
            int j = i + 1;
            float v0 = ringV[i];
            float v1 = ringV[j];

            glm::vec3 n = faceNormal(r0[i], r0[j], r1[i]);
            addQuadUV(verts, indices, r0[i], r0[j], r1[j], r1[i],
                      {u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}, n);
        }
        // Bottom panel (connects point 6 back to point 0)
        glm::vec3 bn = faceNormal(r0[0], r0[6], r1[0]);
        addQuadUV(verts, indices, r0[6], r0[0], r1[0], r1[6],
                  {u0, ringV[6]}, {u0, ringV[0]}, {u1, ringV[0]}, {u1, ringV[6]}, bn);
    }

    // Nose cap (first ring)
    auto& nose = rings[0];
    glm::vec3 noseTip = {-length * 0.5f - 0.1f, 0, 0};
    float noseU = 0.0f;
    for (int i = 0; i < 6; ++i) {
        int j = i + 1;
        glm::vec3 n = faceNormal(noseTip, nose[i], nose[j]);
        addTriangleUV(verts, indices, noseTip, nose[i], nose[j],
                      {noseU, 0.5f}, {ringU[0], ringV[i]}, {ringU[0], ringV[j]}, n);
    }
    // Nose bottom
    addTriangleUV(verts, indices, noseTip, nose[6], nose[0],
                  {noseU, 0.5f}, {ringU[0], ringV[6]}, {ringU[0], ringV[0]}, {0, -1, 0});

    // Rear cap (last ring)
    auto& rear = rings[segments - 1];
    float rearU = 1.0f;
    glm::vec3 rearTip = {length * 0.5f, 0, 0};
    for (int i = 0; i < 6; ++i) {
        int j = i + 1;
        glm::vec3 n = faceNormal(rear[0], rear[j], rear[i]);
        addTriangleUV(verts, indices, rearTip, rear[j], rear[i],
                      {rearU, 0.5f}, {ringU[segments-1], ringV[j]}, {ringU[segments-1], ringV[i]}, n);
    }
    addTriangleUV(verts, indices, rearTip, rear[0], rear[6],
                  {rearU, 0.5f}, {ringU[segments-1], ringV[0]}, {ringU[segments-1], ringV[6]}, {0, -1, 0});

    return ctx.createMesh(verts, indices);
}

// === SIDE POD ===
// Aerodynamic pod with air intake scoop

Mesh3D buildSidePod(Context& ctx, float side) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float podLength = 2.5f;
    float podWidth = 0.5f;
    float podHeight = 0.4f;
    float intakeDepth = 0.3f;

    // Pod body profile (from front to back)
    float profile[][3] = {
        {-0.50f, 0.3f, 0.5f},   // Front (intake)
        {-0.30f, 0.8f, 0.9f},   // Intake rear
        {-0.10f, 1.0f, 1.0f},   // Max
        { 0.20f, 0.9f, 0.9f},   // Taper
        { 0.50f, 0.4f, 0.6f},   // Rear (engine mount)
    };

    // V coordinates for ring points (around cross-section)
    float ringV[] = {0.0f, 0.33f, 0.66f, 1.0f};

    int segments = 5;
    std::vector<std::vector<glm::vec3>> rings;
    std::vector<float> ringU;

    for (int s = 0; s < segments; ++s) {
        float x = profile[s][0] * podLength;
        float w = profile[s][1] * podWidth;
        float h = profile[s][2] * podHeight;

        // U coordinate along length
        float u = (profile[s][0] + 0.5f);
        ringU.push_back(u);

        std::vector<glm::vec3> ring;
        ring.push_back({x, -h, side * w * 0.8f});   // bottom-inner
        ring.push_back({x, -h * 0.3f, side * w});   // outer-bottom
        ring.push_back({x, h * 0.5f, side * w});    // outer-top
        ring.push_back({x, h, side * w * 0.5f});    // top
        rings.push_back(ring);
    }

    // Connect rings with proper UVs
    for (int s = 0; s < segments - 1; ++s) {
        auto& r0 = rings[s];
        auto& r1 = rings[s + 1];
        float u0 = ringU[s];
        float u1 = ringU[s + 1];

        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) % 4;
            float v0 = ringV[i];
            float v1 = ringV[j];
            glm::vec3 n = faceNormal(r0[i], r0[j], r1[i]);
            addQuadUV(verts, indices, r0[i], r0[j], r1[j], r1[i],
                      {u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}, n);
        }
    }

    // Front face with intake scoop
    auto& front = rings[0];
    glm::vec3 intakeCenter = {front[0].x - intakeDepth, 0, side * podWidth * 0.5f};
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        glm::vec3 n = faceNormal(intakeCenter, front[j], front[i]);
        addTriangleUV(verts, indices, intakeCenter, front[j], front[i],
                      {0.0f, 0.5f}, {ringU[0], ringV[j]}, {ringU[0], ringV[i]}, n);
    }

    // Rear face
    auto& rear = rings[segments - 1];
    glm::vec3 rearCenter = {rear[0].x + 0.1f, 0, side * podWidth * 0.3f};
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        glm::vec3 n = faceNormal(rear[0], rear[i], rear[j]);
        addTriangleUV(verts, indices, rearCenter, rear[i], rear[j],
                      {1.0f, 0.5f}, {ringU[segments-1], ringV[i]}, {ringU[segments-1], ringV[j]}, n);
    }

    return ctx.createMesh(verts, indices);
}

// === ENGINE NACELLE ===
// Smooth cylindrical exhaust with internal rings for detail

Mesh3D buildEngine(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float outerRadius = 0.28f;
    float innerRadius = 0.20f;
    float length = 0.6f;
    int segments = 32;  // High segment count for smooth appearance

    const float PI = 3.14159265359f;

    // Helper to add a vertex with smooth radial normal
    auto addSmoothVert = [&](glm::vec3 pos, float nx, float ny, float nz, float u, float v) -> uint32_t {
        uint32_t idx = static_cast<uint32_t>(verts.size());
        verts.push_back({pos, glm::normalize(glm::vec3(nx, ny, nz)), {u, v}});
        return idx;
    };

    // Create vertex rings with smooth normals (shared vertices)
    std::vector<uint32_t> frontOuterIdx, backOuterIdx, frontInnerIdx, backInnerIdx, deepInnerIdx;
    std::vector<uint32_t> frontRimOuterIdx, frontRimInnerIdx;  // Front face vertices (normal = +X)
    std::vector<uint32_t> backCapOuterIdx, backCapInnerIdx;    // Back face vertices (normal = -X)

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        float theta = 2.0f * PI * t;
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        // Outer shell - smooth outward normals
        glm::vec3 outerNorm(0, cosT, sinT);
        frontOuterIdx.push_back(addSmoothVert(
            {length * 0.5f, cosT * outerRadius, sinT * outerRadius},
            0, cosT, sinT, t, 0));
        backOuterIdx.push_back(addSmoothVert(
            {-length * 0.5f, cosT * outerRadius, sinT * outerRadius},
            0, cosT, sinT, t, 1));

        // Inner tube - smooth inward normals
        frontInnerIdx.push_back(addSmoothVert(
            {length * 0.5f, cosT * innerRadius, sinT * innerRadius},
            0, -cosT, -sinT, t, 0));
        backInnerIdx.push_back(addSmoothVert(
            {-length * 0.3f, cosT * innerRadius, sinT * innerRadius},
            0, -cosT, -sinT, t, 1));

        // Deep inner (narrowing cone) - angled inward normals
        float deepRadius = innerRadius * 0.6f;
        glm::vec3 deepNorm = glm::normalize(glm::vec3(-0.5f, -cosT, -sinT));
        deepInnerIdx.push_back(addSmoothVert(
            {-length * 0.5f, cosT * deepRadius, sinT * deepRadius},
            deepNorm.x, deepNorm.y, deepNorm.z, t, 1));

        // Front rim vertices (flat normal pointing forward)
        frontRimOuterIdx.push_back(addSmoothVert(
            {length * 0.5f, cosT * outerRadius, sinT * outerRadius},
            1, 0, 0, t, 0));
        frontRimInnerIdx.push_back(addSmoothVert(
            {length * 0.5f, cosT * innerRadius, sinT * innerRadius},
            1, 0, 0, t, 1));

        // Back cap vertices (flat normal pointing backward)
        backCapOuterIdx.push_back(addSmoothVert(
            {-length * 0.5f, cosT * outerRadius, sinT * outerRadius},
            -1, 0, 0, t, 0));
        backCapInnerIdx.push_back(addSmoothVert(
            {-length * 0.5f, cosT * deepRadius, sinT * deepRadius},
            -1, 0, 0, t, 1));
    }

    // Helper to add a quad with both windings (double-sided)
    auto addDoubleSidedQuad = [&](uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        // First winding
        indices.push_back(a); indices.push_back(b); indices.push_back(c);
        indices.push_back(a); indices.push_back(c); indices.push_back(d);
        // Reverse winding
        indices.push_back(a); indices.push_back(c); indices.push_back(b);
        indices.push_back(a); indices.push_back(d); indices.push_back(c);
    };

    // Generate quads by connecting adjacent vertices in rings
    for (int i = 0; i < segments; ++i) {
        int j = i + 1;

        // Outer shell (double-sided)
        addDoubleSidedQuad(frontOuterIdx[i], frontOuterIdx[j], backOuterIdx[j], backOuterIdx[i]);

        // Inner tube (double-sided)
        addDoubleSidedQuad(frontInnerIdx[i], frontInnerIdx[j], backInnerIdx[j], backInnerIdx[i]);

        // Inner to deep narrowing (double-sided)
        addDoubleSidedQuad(backInnerIdx[i], backInnerIdx[j], deepInnerIdx[j], deepInnerIdx[i]);

        // Front rim (double-sided)
        addDoubleSidedQuad(frontRimOuterIdx[i], frontRimOuterIdx[j], frontRimInnerIdx[j], frontRimInnerIdx[i]);

        // Back cap (double-sided)
        addDoubleSidedQuad(backCapOuterIdx[i], backCapOuterIdx[j], backCapInnerIdx[j], backCapInnerIdx[i]);
    }

    return ctx.createMesh(verts, indices);
}

// === VERTICAL FIN ===
// Swept triangular fin

Mesh3D buildFin(Context& ctx, float side) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float finHeight = 0.7f;
    float finLength = 0.9f;
    float finThickness = 0.05f;
    float sweep = 0.3f;

    float halfT = finThickness / 2.0f;

    // Fin profile (triangular with swept tip)
    glm::vec3 frontBot = {finLength * 0.5f, 0, side * halfT};
    glm::vec3 backBot = {-finLength * 0.5f, 0, side * halfT};
    glm::vec3 tip = {-finLength * 0.3f + sweep, finHeight, side * halfT * 0.5f};

    glm::vec3 frontBotI = {finLength * 0.5f, 0, -side * halfT};
    glm::vec3 backBotI = {-finLength * 0.5f, 0, -side * halfT};
    glm::vec3 tipI = {-finLength * 0.3f + sweep, finHeight, -side * halfT * 0.5f};

    // UV mapping based on position (X along length, Y along height)
    auto finUV = [&](glm::vec3 p) -> glm::vec2 {
        float u = (p.x / finLength) + 0.5f;  // 0-1 along length
        float v = p.y / finHeight;            // 0-1 along height
        return {u, v};
    };

    // Outer face
    addTriangleUV(verts, indices, frontBot, backBot, tip,
                  finUV(frontBot), finUV(backBot), finUV(tip), {0, 0, side});

    // Inner face
    addTriangleUV(verts, indices, backBotI, frontBotI, tipI,
                  finUV(backBotI), finUV(frontBotI), finUV(tipI), {0, 0, -side});

    // Bottom edge
    addQuadUV(verts, indices, frontBot, frontBotI, backBotI, backBot,
              finUV(frontBot), finUV(frontBotI), finUV(backBotI), finUV(backBot), {0, -1, 0});

    // Front edge
    glm::vec3 frontN = faceNormal(frontBot, tip, frontBotI);
    addQuadUV(verts, indices, frontBot, tip, tipI, frontBotI,
              finUV(frontBot), finUV(tip), finUV(tipI), finUV(frontBotI), frontN);

    // Back edge
    glm::vec3 backN = faceNormal(backBot, backBotI, tip);
    addQuadUV(verts, indices, backBot, backBotI, tipI, tip,
              finUV(backBot), finUV(backBotI), finUV(tipI), finUV(tip), backN);

    return ctx.createMesh(verts, indices);
}

// === REAR WING ===
// Wide spanning wing with endplates

Mesh3D buildRearWing(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float span = 2.8f;
    float chord = 0.5f;
    float thickness = 0.06f;
    float sweep = 0.15f;
    float endplateHeight = 0.25f;

    float halfSpan = span / 2.0f;
    float halfT = thickness / 2.0f;

    // UV mapping based on position (Z across span, X along chord)
    auto wingUV = [&](glm::vec3 p) -> glm::vec2 {
        float u = (p.z / span) + 0.5f;  // 0-1 across span
        float v = (p.x / chord) + 0.5f; // 0-1 along chord
        return {u, v};
    };

    // Main wing surface
    glm::vec3 frontL = {chord / 2, halfT, -halfSpan};
    glm::vec3 frontR = {chord / 2, halfT, halfSpan};
    glm::vec3 backL = {-chord / 2 - sweep, halfT, -halfSpan};
    glm::vec3 backR = {-chord / 2 - sweep, halfT, halfSpan};

    glm::vec3 frontLB = {chord / 2, -halfT, -halfSpan};
    glm::vec3 frontRB = {chord / 2, -halfT, halfSpan};
    glm::vec3 backLB = {-chord / 2 - sweep, -halfT, -halfSpan};
    glm::vec3 backRB = {-chord / 2 - sweep, -halfT, halfSpan};

    // Top surface
    addQuadUV(verts, indices, frontL, frontR, backR, backL,
              wingUV(frontL), wingUV(frontR), wingUV(backR), wingUV(backL), {0, 1, 0});

    // Bottom surface
    addQuadUV(verts, indices, frontRB, frontLB, backLB, backRB,
              wingUV(frontRB), wingUV(frontLB), wingUV(backLB), wingUV(backRB), {0, -1, 0});

    // Front edge
    addQuadUV(verts, indices, frontL, frontLB, frontRB, frontR,
              {0, 1}, {0, 0}, {1, 0}, {1, 1}, {1, 0, 0});

    // Back edge
    addQuadUV(verts, indices, backR, backRB, backLB, backL,
              {1, 1}, {1, 0}, {0, 0}, {0, 1}, {-1, 0, 0});

    // Left endplate
    glm::vec3 epLT = {chord / 2, halfT + endplateHeight, -halfSpan};
    glm::vec3 epLB = {-chord / 2 - sweep, halfT + endplateHeight, -halfSpan};
    addQuadUV(verts, indices, frontL, backL, epLB, epLT,
              {1, 0}, {0, 0}, {0, 1}, {1, 1}, {0, 0, -1});
    addQuadUV(verts, indices, epLT, epLB, backL, frontL,
              {1, 1}, {0, 1}, {0, 0}, {1, 0}, {0, 0, 1});

    // Right endplate
    glm::vec3 epRT = {chord / 2, halfT + endplateHeight, halfSpan};
    glm::vec3 epRB = {-chord / 2 - sweep, halfT + endplateHeight, halfSpan};
    addQuadUV(verts, indices, backR, frontR, epRT, epRB,
              {0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0, 1});
    addQuadUV(verts, indices, frontR, backR, epRB, epRT,
              {1, 0}, {0, 0}, {0, 1}, {1, 1}, {0, 0, -1});

    // Endplate tops
    addQuadUV(verts, indices, epLT, epLB, epRB, epRT,
              {0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 1, 0});

    return ctx.createMesh(verts, indices);
}

// === FRONT CANARD ===
// Small angular front wing

Mesh3D buildCanard(Context& ctx, float side) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float span = 0.6f;
    float chord = 0.25f;
    float thickness = 0.04f;
    float angle = -0.15f;

    float halfT = thickness / 2.0f;

    // UV mapping based on position
    auto canardUV = [&](glm::vec3 p) -> glm::vec2 {
        float u = std::abs(p.z) / span;        // 0-1 along span
        float v = (p.x / chord) + 0.5f;        // 0-1 along chord
        return {u, v};
    };

    // Wing points (angled down at tip)
    glm::vec3 rootFront = {chord / 2, halfT, 0};
    glm::vec3 rootBack = {-chord / 2, halfT, 0};
    glm::vec3 tipFront = {chord / 3, halfT + angle, side * span};
    glm::vec3 tipBack = {-chord / 2, halfT + angle, side * span};

    glm::vec3 rootFrontB = {chord / 2, -halfT, 0};
    glm::vec3 rootBackB = {-chord / 2, -halfT, 0};
    glm::vec3 tipFrontB = {chord / 3, -halfT + angle, side * span};
    glm::vec3 tipBackB = {-chord / 2, -halfT + angle, side * span};

    // Top
    addQuadUV(verts, indices, rootFront, tipFront, tipBack, rootBack,
              canardUV(rootFront), canardUV(tipFront), canardUV(tipBack), canardUV(rootBack), {0, 1, 0});

    // Bottom
    addQuadUV(verts, indices, tipFrontB, rootFrontB, rootBackB, tipBackB,
              canardUV(tipFrontB), canardUV(rootFrontB), canardUV(rootBackB), canardUV(tipBackB), {0, -1, 0});

    // Front edge
    addQuadUV(verts, indices, rootFront, rootFrontB, tipFrontB, tipFront,
              {0, 1}, {0, 0}, {1, 0}, {1, 1}, {1, 0, 0});

    // Back edge
    addQuadUV(verts, indices, tipBack, tipBackB, rootBackB, rootBack,
              {1, 1}, {1, 0}, {0, 0}, {0, 1}, {-1, 0, 0});

    // Tip
    addQuadUV(verts, indices, tipFront, tipFrontB, tipBackB, tipBack,
              {1, 1}, {1, 0}, {0, 0}, {0, 1}, {0, 0, side});

    // Root (attaches to body)
    addQuadUV(verts, indices, rootBack, rootBackB, rootFrontB, rootFront,
              {0, 1}, {0, 0}, {1, 0}, {1, 1}, {0, 0, -side});

    return ctx.createMesh(verts, indices);
}

// === COCKPIT ===
// Low-profile angular canopy

Mesh3D buildCockpit(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float length = 0.8f;
    float width = 0.35f;
    float height = 0.25f;

    // Angular canopy shape (6 segments for faceted look)
    glm::vec3 frontTip = {length * 0.5f, height * 0.3f, 0};
    glm::vec3 frontL = {length * 0.3f, height * 0.5f, -width * 0.7f};
    glm::vec3 frontR = {length * 0.3f, height * 0.5f, width * 0.7f};
    glm::vec3 peakL = {0, height, -width};
    glm::vec3 peakR = {0, height, width};
    glm::vec3 backL = {-length * 0.4f, height * 0.7f, -width * 0.8f};
    glm::vec3 backR = {-length * 0.4f, height * 0.7f, width * 0.8f};
    glm::vec3 backTip = {-length * 0.5f, height * 0.4f, 0};

    // Base (where it mounts to fuselage)
    glm::vec3 baseFL = {length * 0.4f, 0, -width * 0.5f};
    glm::vec3 baseFR = {length * 0.4f, 0, width * 0.5f};
    glm::vec3 baseBL = {-length * 0.45f, 0, -width * 0.6f};
    glm::vec3 baseBR = {-length * 0.45f, 0, width * 0.6f};

    // Front facet
    addTriangle(verts, indices, frontTip, frontL, frontR, faceNormal(frontTip, frontL, frontR));

    // Front-left facet
    addQuad(verts, indices, frontTip, baseFL, peakL, frontL, faceNormal(frontTip, baseFL, peakL));

    // Front-right facet
    addQuad(verts, indices, frontR, peakR, baseFR, frontTip, faceNormal(frontR, peakR, baseFR));

    // Left side
    addQuad(verts, indices, frontL, peakL, backL, backTip, faceNormal(frontL, peakL, backL));
    addTriangle(verts, indices, frontL, backTip, frontTip, faceNormal(frontL, backTip, frontTip));

    // Right side
    addQuad(verts, indices, backTip, backR, peakR, frontR, faceNormal(backTip, backR, peakR));
    addTriangle(verts, indices, frontTip, backTip, frontR, faceNormal(frontTip, backTip, frontR));

    // Top center
    addQuad(verts, indices, frontL, frontR, peakR, peakL, faceNormal(frontL, frontR, peakR));
    addQuad(verts, indices, peakL, peakR, backR, backL, faceNormal(peakL, peakR, backR));

    // Back facet
    addTriangle(verts, indices, backL, backR, backTip, faceNormal(backL, backR, backTip));

    return ctx.createMesh(verts, indices);
}

// === CAMERA ===

void updateCamera() {
    float x = std::cos(cameraYaw) * std::cos(cameraPitch) * cameraDistance;
    float y = std::sin(cameraPitch) * cameraDistance;
    float z = std::sin(cameraYaw) * std::cos(cameraPitch) * cameraDistance;

    camera.position = glm::vec3(x, y, z);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
}

// === LIVERY GENERATION ===

void regenerateLivery(Context& ctx) {
    if (liveryTeam == currentTeam) return;  // Already up to date

    const livery::TeamPalette& palette = *palettes[currentTeam];
    int teamNumber = 10 + currentTeam * 7;  // Different number per team

    livery::LiveryGenerator gen(512, 512);
    gen.setPalette(palette);
    gen.setTeamNumber(teamNumber);
    gen.setGrimePath("examples/wipeout-vehicle/textures/grime/cement_concrete_wall.jpg");
    gen.generate(&ctx);  // Pass context for grime texture loading
    gen.uploadTo(ctx, liveryTexture);

    liveryTeam = currentTeam;
    std::cout << "[livery] Generated livery for team " << currentTeam
              << " (#" << teamNumber << ")\n";
}

// === SETUP ===

void setup(Chain& chain) {
    chain.add<AudioIn>("audio")
        .device(-1)
        .gain(2.0f)
        .fftSize(1024)
        .smoothing(0.85f);

    chain.setOutput("out");
}

// === UPDATE ===

void update(Chain& chain, Context& ctx) {
    if (!output.valid()) {
        output = ctx.createTexture();

        std::cout << "[wipeout-vehicle] Building complex procedural craft...\n";

        // Build all vehicle parts
        fuselageMesh = buildFuselage(ctx);
        cockpitMesh = buildCockpit(ctx);
        leftPodMesh = buildSidePod(ctx, -1.0f);
        rightPodMesh = buildSidePod(ctx, 1.0f);
        leftEngineMesh = buildEngine(ctx);
        rightEngineMesh = buildEngine(ctx);
        leftFinMesh = buildFin(ctx, -1.0f);
        rightFinMesh = buildFin(ctx, 1.0f);
        rearWingMesh = buildRearWing(ctx);
        leftCanardMesh = buildCanard(ctx, -1.0f);
        rightCanardMesh = buildCanard(ctx, 1.0f);

        std::cout << "[wipeout-vehicle] All meshes created\n";

        camera.fov = 50.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;
        updateCamera();

        // Dramatic lighting
        lighting.ambientColor = glm::vec3(0.15f, 0.15f, 0.20f);
        lighting.ambientIntensity = 0.4f;

        // Key light
        lighting.addLight(Light::directional(
            glm::vec3(-0.3f, -1.0f, -0.5f),
            glm::vec3(1.0f, 0.98f, 0.95f),
            1.2f
        ));

        // Fill light
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.2f, 0.5f),
            glm::vec3(0.5f, 0.6f, 1.0f),
            0.5f
        ));

        // Rim light
        lighting.addLight(Light::directional(
            glm::vec3(0.0f, 0.5f, 1.0f),
            glm::vec3(1.0f, 0.7f, 0.5f),
            0.6f
        ));

        // Load grime textures for weathered PS1-style look
        std::cout << "[wipeout-vehicle] Loading grime textures...\n";
        grimeBody = ctx.loadImageAsTexture("textures/grime/DarkGrunge_Textures01.jpg");
        grimePods = ctx.loadImageAsTexture("textures/grime/DarkGrunge_Textures03.jpg");
        grimeDetail = ctx.loadImageAsTexture("textures/grime/cement_concrete_wall.jpg");

        if (grimeBody.valid()) std::cout << "  - Body grime loaded\n";
        if (grimePods.valid()) std::cout << "  - Pod grime loaded\n";
        if (grimeDetail.valid()) std::cout << "  - Detail grime loaded\n";

        // Try to load IBL environment for reflections
        iblEnvironment = ctx.loadEnvironment("environment.hdr");
        if (iblEnvironment.valid()) {
            hasIBL = true;
            std::cout << "  - IBL environment loaded\n";
        } else {
            std::cout << "  - No IBL environment (grime textures disabled)\n";
        }

        // Load Metal025 PBR textures for engine/metallic parts
        std::cout << "[wipeout-vehicle] Loading Metal025 PBR textures...\n";
        metalAlbedo = ctx.loadImageAsTexture("textures/Metal025_1K-JPG/Metal025_1K-JPG_Color.jpg");
        metalRoughness = ctx.loadImageAsTexture("textures/Metal025_1K-JPG/Metal025_1K-JPG_Roughness.jpg");
        metalMetallic = ctx.loadImageAsTexture("textures/Metal025_1K-JPG/Metal025_1K-JPG_Metalness.jpg");
        metalNormal = ctx.loadImageAsTexture("textures/Metal025_1K-JPG/Metal025_1K-JPG_NormalGL.jpg");

        if (metalAlbedo.valid() && metalRoughness.valid() && metalMetallic.valid()) {
            hasMetalTextures = true;
            std::cout << "  - Metal025 albedo, roughness, metallic loaded\n";
            if (metalNormal.valid()) std::cout << "  - Metal025 normal map loaded\n";
        } else {
            std::cout << "  - Metal025 textures not found\n";
        }

        std::cout << "\n=== Wipeout Anti-Gravity Racer ===\n";
        std::cout << "Drag mouse to orbit, scroll to zoom\n";
        std::cout << "Press 1-5 to change team colors\n\n";
    }

    // Camera control
    float mouseX = ctx.mouseX();
    float mouseY = ctx.mouseY();

    if (ctx.isMouseDown(0)) {
        if (!isDragging) {
            isDragging = true;
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        } else {
            float dx = (mouseX - lastMouseX) * 0.01f;
            float dy = (mouseY - lastMouseY) * 0.01f;
            cameraYaw += dx;
            cameraPitch = glm::clamp(cameraPitch + dy, -1.2f, 1.2f);
            updateCamera();
            lastMouseX = mouseX;
            lastMouseY = mouseY;
        }
    } else {
        isDragging = false;
    }

    float scroll = ctx.scrollDeltaY();
    if (scroll != 0) {
        cameraDistance = glm::clamp(cameraDistance - scroll * 0.5f, 4.0f, 25.0f);
        updateCamera();
    }

    // Team selection via number keys 1-5
    static const char* teamNames[] = {"FEISAR", "AG-SYS", "AURICOM", "QIREX", "PIRANHA"};
    if (ctx.wasKeyPressed(Key::Num1)) { currentTeam = 0; std::cout << "Team: " << teamNames[0] << "\n"; }
    if (ctx.wasKeyPressed(Key::Num2)) { currentTeam = 1; std::cout << "Team: " << teamNames[1] << "\n"; }
    if (ctx.wasKeyPressed(Key::Num3)) { currentTeam = 2; std::cout << "Team: " << teamNames[2] << "\n"; }
    if (ctx.wasKeyPressed(Key::Num4)) { currentTeam = 3; std::cout << "Team: " << teamNames[3] << "\n"; }
    if (ctx.wasKeyPressed(Key::Num5)) { currentTeam = 4; std::cout << "Team: " << teamNames[4] << "\n"; }

    float t = ctx.time();

    // Audio reactivity
    float level = ctx.getInputValue("audio", "level");
    float bass = ctx.getInputValue("audio", "bass");
    float mid = ctx.getInputValue("audio", "mid");

    engineGlow = engineGlow * 0.9f + (level * 5.0f) * 0.1f;
    hoverOffset = hoverOffset * 0.95f + (bass * 0.2f) * 0.05f;
    colorPhase += mid * 0.02f;

    float baseHover = std::sin(t * 1.5f) * 0.03f;
    float totalHover = baseHover + hoverOffset;

    // Get current team palette
    const livery::TeamPalette& palette = *palettes[currentTeam];

    // Regenerate livery texture if team changed
    regenerateLivery(ctx);

    // === MATERIALS (PS1-style with procedural livery textures) ===

    // Check if we have textured materials available
    bool useLivery = liveryTexture.valid() && hasIBL;

    // Main body - use procedural livery texture (white base, texture provides color)
    TexturedPBRMaterial bodyMat;
    bodyMat.albedo = useLivery ? glm::vec3(1.0f) : palette.primary;
    bodyMat.metallic = 0.3f;
    bodyMat.roughness = 0.5f;
    if (useLivery) bodyMat.albedoMap = &liveryTexture;

    // Side pods - use livery texture (mapped to pod region)
    TexturedPBRMaterial podMat;
    podMat.albedo = useLivery ? glm::vec3(1.0f) : palette.secondary;
    podMat.metallic = 0.4f;
    podMat.roughness = 0.4f;
    if (useLivery) podMat.albedoMap = &liveryTexture;

    // Cockpit - tinted glass (no livery on glass)
    TexturedPBRMaterial cockpitMat;
    cockpitMat.albedo = glm::vec3(0.05f, 0.08f, 0.12f);
    cockpitMat.metallic = 0.1f;
    cockpitMat.roughness = 0.05f;

    // Engines - use Metal025 PBR textures for realistic worn metal look
    TexturedPBRMaterial engineMat;
    if (hasMetalTextures) {
        engineMat.albedo = glm::vec3(1.0f);  // White base, texture provides color
        engineMat.metallic = 1.0f;
        engineMat.roughness = 0.3f;
        engineMat.albedoMap = &metalAlbedo;
        engineMat.roughnessMap = &metalRoughness;
        engineMat.metallicMap = &metalMetallic;
        engineMat.normalMap = &metalNormal;
    } else {
        engineMat.albedo = glm::vec3(0.9f, 0.9f, 0.92f);
        engineMat.metallic = 1.0f;
        engineMat.roughness = 0.15f;
    }
    engineMat.emissive = glm::vec3(1.0f, 0.5f, 0.2f) * (0.5f + engineGlow);

    // Fins - use livery texture (accent region)
    TexturedPBRMaterial finMat;
    finMat.albedo = useLivery ? glm::vec3(1.0f) : palette.accent;
    finMat.metallic = 0.3f;
    finMat.roughness = 0.45f;
    if (useLivery) finMat.albedoMap = &liveryTexture;

    // Rear wing - use Metal025 PBR textures for industrial metal look
    TexturedPBRMaterial wingMat;
    if (hasMetalTextures) {
        wingMat.albedo = glm::vec3(0.8f);  // Slightly darker base
        wingMat.metallic = 0.9f;
        wingMat.roughness = 0.4f;
        wingMat.albedoMap = &metalAlbedo;
        wingMat.roughnessMap = &metalRoughness;
        wingMat.metallicMap = &metalMetallic;
        wingMat.normalMap = &metalNormal;
    } else {
        wingMat.albedo = palette.accent;
        wingMat.metallic = 0.3f;
        wingMat.roughness = 0.45f;
    }

    // Canards - use livery (darker area)
    TexturedPBRMaterial canardMat;
    canardMat.albedo = useLivery ? glm::vec3(0.8f) : palette.primary * 0.6f;
    canardMat.metallic = 0.4f;
    canardMat.roughness = 0.4f;
    if (useLivery) canardMat.albedoMap = &liveryTexture;

    // === TRANSFORMS ===

    float vehicleRotation = t * 0.15f;

    glm::mat4 baseXform = glm::mat4(1.0f);
    baseXform = glm::translate(baseXform, glm::vec3(0, totalHover, 0));
    baseXform = glm::rotate(baseXform, vehicleRotation, glm::vec3(0, 1, 0));

    glm::vec4 clearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glm::vec4 noClear(0, 0, 0, -1);

    // Fuselage (main body)
    ctx.render3DPBR(fuselageMesh, camera, baseXform, bodyMat, lighting, iblEnvironment, output, clearColor);

    // Cockpit (on fuselage, toward front)
    glm::mat4 cockpitXform = baseXform;
    cockpitXform = glm::translate(cockpitXform, glm::vec3(-0.8f, 0.35f, 0));
    ctx.render3DPBR(cockpitMesh, camera, cockpitXform, cockpitMat, lighting, iblEnvironment, output, noClear);

    // Side pods
    glm::mat4 leftPodXform = baseXform;
    leftPodXform = glm::translate(leftPodXform, glm::vec3(0.3f, -0.15f, -0.9f));
    ctx.render3DPBR(leftPodMesh, camera, leftPodXform, podMat, lighting, iblEnvironment, output, noClear);

    glm::mat4 rightPodXform = baseXform;
    rightPodXform = glm::translate(rightPodXform, glm::vec3(0.3f, -0.15f, 0.9f));
    ctx.render3DPBR(rightPodMesh, camera, rightPodXform, podMat, lighting, iblEnvironment, output, noClear);

    // Engines (at rear of pods)
    glm::mat4 leftEngineXform = baseXform;
    leftEngineXform = glm::translate(leftEngineXform, glm::vec3(1.8f, -0.1f, -1.0f));
    leftEngineXform = glm::rotate(leftEngineXform, glm::radians(90.0f), glm::vec3(0, 0, 1));
    ctx.render3DPBR(leftEngineMesh, camera, leftEngineXform, engineMat, lighting, iblEnvironment, output, noClear);

    glm::mat4 rightEngineXform = baseXform;
    rightEngineXform = glm::translate(rightEngineXform, glm::vec3(1.8f, -0.1f, 1.0f));
    rightEngineXform = glm::rotate(rightEngineXform, glm::radians(90.0f), glm::vec3(0, 0, 1));
    ctx.render3DPBR(rightEngineMesh, camera, rightEngineXform, engineMat, lighting, iblEnvironment, output, noClear);

    // Vertical fins
    glm::mat4 leftFinXform = baseXform;
    leftFinXform = glm::translate(leftFinXform, glm::vec3(1.5f, 0.2f, -1.0f));
    ctx.render3DPBR(leftFinMesh, camera, leftFinXform, finMat, lighting, iblEnvironment, output, noClear);

    glm::mat4 rightFinXform = baseXform;
    rightFinXform = glm::translate(rightFinXform, glm::vec3(1.5f, 0.2f, 1.0f));
    ctx.render3DPBR(rightFinMesh, camera, rightFinXform, finMat, lighting, iblEnvironment, output, noClear);

    // Rear wing
    glm::mat4 wingXform = baseXform;
    wingXform = glm::translate(wingXform, glm::vec3(2.0f, 0.35f, 0));
    ctx.render3DPBR(rearWingMesh, camera, wingXform, wingMat, lighting, iblEnvironment, output, noClear);

    // Front canards
    glm::mat4 leftCanardXform = baseXform;
    leftCanardXform = glm::translate(leftCanardXform, glm::vec3(-2.0f, 0.1f, -0.4f));
    ctx.render3DPBR(leftCanardMesh, camera, leftCanardXform, canardMat, lighting, iblEnvironment, output, noClear);

    glm::mat4 rightCanardXform = baseXform;
    rightCanardXform = glm::translate(rightCanardXform, glm::vec3(-2.0f, 0.1f, 0.4f));
    ctx.render3DPBR(rightCanardMesh, camera, rightCanardXform, canardMat, lighting, iblEnvironment, output, noClear);

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
