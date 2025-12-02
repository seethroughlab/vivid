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

    // Generate cross-section rings
    std::vector<std::vector<glm::vec3>> rings;
    for (int s = 0; s < segments; ++s) {
        float x = profile[s][0] * length;
        float w = profile[s][1] * width;
        float h = profile[s][2] * height;
        float sp = profile[s][3] * spineHeight;

        std::vector<glm::vec3> ring;
        // 6-point cross section: bottom-left, left, top-left-spine, top-right-spine, right, bottom-right
        ring.push_back({x, -h, -w});          // 0: bottom-left
        ring.push_back({x, 0, -w * 1.1f});    // 1: left bulge
        ring.push_back({x, h, -w * 0.3f});    // 2: top-left
        ring.push_back({x, h + sp, 0});       // 3: spine peak
        ring.push_back({x, h, w * 0.3f});     // 4: top-right
        ring.push_back({x, 0, w * 1.1f});     // 5: right bulge
        ring.push_back({x, -h, w});           // 6: bottom-right
        rings.push_back(ring);
    }

    // Connect rings with quads
    for (int s = 0; s < segments - 1; ++s) {
        auto& r0 = rings[s];
        auto& r1 = rings[s + 1];

        // Connect each pair of adjacent points
        for (int i = 0; i < 6; ++i) {
            int j = (i + 1) % 7;
            if (i == 6) j = 0;  // Wrap around

            glm::vec3 n = faceNormal(r0[i], r0[j], r1[i]);
            addQuad(verts, indices, r0[i], r0[j], r1[j], r1[i], n);
        }
        // Bottom panel
        glm::vec3 bn = faceNormal(r0[0], r0[6], r1[0]);
        addQuad(verts, indices, r0[6], r0[0], r1[0], r1[6], bn);
    }

    // Nose cap (first ring)
    auto& nose = rings[0];
    glm::vec3 noseTip = {-length * 0.5f - 0.1f, 0, 0};
    for (int i = 0; i < 6; ++i) {
        int j = (i + 1) % 7;
        if (i == 6) j = 0;
        glm::vec3 n = faceNormal(noseTip, nose[i], nose[j]);
        addTriangle(verts, indices, noseTip, nose[i], nose[j], n);
    }
    // Nose bottom
    addTriangle(verts, indices, noseTip, nose[6], nose[0], {0, -1, 0});

    // Rear cap (last ring)
    auto& rear = rings[segments - 1];
    for (int i = 0; i < 6; ++i) {
        int j = (i + 1) % 7;
        if (i == 6) j = 0;
        glm::vec3 n = faceNormal(rear[0], rear[j], rear[i]);
        addTriangle(verts, indices, {length * 0.5f, 0, 0}, rear[j], rear[i], n);
    }
    addTriangle(verts, indices, {length * 0.5f, 0, 0}, rear[0], rear[6], {0, -1, 0});

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

    int segments = 5;
    std::vector<std::vector<glm::vec3>> rings;

    for (int s = 0; s < segments; ++s) {
        float x = profile[s][0] * podLength;
        float w = profile[s][1] * podWidth;
        float h = profile[s][2] * podHeight;

        std::vector<glm::vec3> ring;
        // 4-point cross section
        ring.push_back({x, -h, side * w * 0.8f});   // bottom-inner
        ring.push_back({x, -h * 0.3f, side * w});   // outer-bottom
        ring.push_back({x, h * 0.5f, side * w});    // outer-top
        ring.push_back({x, h, side * w * 0.5f});    // top
        rings.push_back(ring);
    }

    // Connect rings
    for (int s = 0; s < segments - 1; ++s) {
        auto& r0 = rings[s];
        auto& r1 = rings[s + 1];

        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) % 4;
            glm::vec3 n = faceNormal(r0[i], r0[j], r1[i]);
            addQuad(verts, indices, r0[i], r0[j], r1[j], r1[i], n);
        }
    }

    // Front face with intake scoop (dark recessed area)
    auto& front = rings[0];
    glm::vec3 intakeCenter = {front[0].x - intakeDepth, 0, side * podWidth * 0.5f};
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        glm::vec3 n = faceNormal(intakeCenter, front[j], front[i]);
        addTriangle(verts, indices, intakeCenter, front[j], front[i], n);
    }

    // Rear face
    auto& rear = rings[segments - 1];
    for (int i = 0; i < 4; ++i) {
        int j = (i + 1) % 4;
        glm::vec3 n = faceNormal(rear[0], rear[i], rear[j]);
        addTriangle(verts, indices, {rear[0].x + 0.1f, 0, side * podWidth * 0.3f}, rear[i], rear[j], n);
    }

    return ctx.createMesh(verts, indices);
}

// === ENGINE NACELLE ===
// Hexagonal exhaust with internal rings for detail

Mesh3D buildEngine(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float outerRadius = 0.28f;
    float innerRadius = 0.20f;
    float length = 0.6f;
    int segments = 6;

    const float PI = 3.14159265359f;

    // Generate rings for outer shell
    std::vector<glm::vec3> frontOuter, backOuter, frontInner, backInner, deepInner;

    for (int i = 0; i < segments; ++i) {
        float theta = 2.0f * PI * static_cast<float>(i) / segments;
        float x = std::cos(theta);
        float y = std::sin(theta);

        frontOuter.push_back({length * 0.5f, x * outerRadius, y * outerRadius});
        backOuter.push_back({-length * 0.5f, x * outerRadius, y * outerRadius});
        frontInner.push_back({length * 0.5f, x * innerRadius, y * innerRadius});
        backInner.push_back({-length * 0.3f, x * innerRadius, y * innerRadius});
        deepInner.push_back({-length * 0.5f, x * innerRadius * 0.6f, y * innerRadius * 0.6f});
    }

    // Outer shell sides
    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        glm::vec3 n = glm::normalize(glm::vec3(0, frontOuter[i].y, frontOuter[i].z));
        addQuad(verts, indices, frontOuter[i], frontOuter[j], backOuter[j], backOuter[i], n);
    }

    // Front rim (outer to inner)
    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        addQuad(verts, indices, frontOuter[i], frontInner[i], frontInner[j], frontOuter[j], {1, 0, 0});
    }

    // Inner tube
    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        glm::vec3 n = -glm::normalize(glm::vec3(0, frontInner[i].y, frontInner[i].z));
        addQuad(verts, indices, frontInner[i], backInner[i], backInner[j], frontInner[j], n);
    }

    // Inner to deep (narrowing)
    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        glm::vec3 n = faceNormal(backInner[i], backInner[j], deepInner[i]);
        addQuad(verts, indices, backInner[i], backInner[j], deepInner[j], deepInner[i], n);
    }

    // Back cap (between outer and deep inner)
    for (int i = 0; i < segments; ++i) {
        int j = (i + 1) % segments;
        addQuad(verts, indices, backOuter[j], backOuter[i], deepInner[i], deepInner[j], {-1, 0, 0});
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
    float sweep = 0.3f;  // How far back the tip is

    float halfT = finThickness / 2.0f;

    // Fin profile (triangular with swept tip)
    glm::vec3 frontBot = {finLength * 0.5f, 0, side * halfT};
    glm::vec3 backBot = {-finLength * 0.5f, 0, side * halfT};
    glm::vec3 tip = {-finLength * 0.3f + sweep, finHeight, side * halfT * 0.5f};

    glm::vec3 frontBotI = {finLength * 0.5f, 0, -side * halfT};
    glm::vec3 backBotI = {-finLength * 0.5f, 0, -side * halfT};
    glm::vec3 tipI = {-finLength * 0.3f + sweep, finHeight, -side * halfT * 0.5f};

    // Outer face
    addTriangle(verts, indices, frontBot, backBot, tip, {0, 0, side});

    // Inner face
    addTriangle(verts, indices, backBotI, frontBotI, tipI, {0, 0, -side});

    // Bottom edge
    addQuad(verts, indices, frontBot, frontBotI, backBotI, backBot, {0, -1, 0});

    // Front edge
    glm::vec3 frontN = faceNormal(frontBot, tip, frontBotI);
    addQuad(verts, indices, frontBot, tip, tipI, frontBotI, frontN);

    // Back edge
    glm::vec3 backN = faceNormal(backBot, backBotI, tip);
    addQuad(verts, indices, backBot, backBotI, tipI, tip, backN);

    return ctx.createMesh(verts, indices);
}

// === REAR WING ===
// Wide spanning wing with endplates

Mesh3D buildRearWing(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float span = 2.8f;       // Total width
    float chord = 0.5f;      // Front-to-back
    float thickness = 0.06f;
    float sweep = 0.15f;     // Swept back angle
    float endplateHeight = 0.25f;

    float halfSpan = span / 2.0f;
    float halfT = thickness / 2.0f;

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
    addQuad(verts, indices, frontL, frontR, backR, backL, {0, 1, 0});

    // Bottom surface
    addQuad(verts, indices, frontRB, frontLB, backLB, backRB, {0, -1, 0});

    // Front edge
    addQuad(verts, indices, frontL, frontLB, frontRB, frontR, {1, 0, 0});

    // Back edge
    addQuad(verts, indices, backR, backRB, backLB, backL, {-1, 0, 0});

    // Left endplate
    glm::vec3 epLT = {chord / 2, halfT + endplateHeight, -halfSpan};
    glm::vec3 epLB = {-chord / 2 - sweep, halfT + endplateHeight, -halfSpan};
    addQuad(verts, indices, frontL, backL, epLB, epLT, {0, 0, -1});
    addQuad(verts, indices, epLT, epLB, backL, frontL, {0, 0, 1});  // Inner face

    // Right endplate
    glm::vec3 epRT = {chord / 2, halfT + endplateHeight, halfSpan};
    glm::vec3 epRB = {-chord / 2 - sweep, halfT + endplateHeight, halfSpan};
    addQuad(verts, indices, backR, frontR, epRT, epRB, {0, 0, 1});
    addQuad(verts, indices, frontR, backR, epRB, epRT, {0, 0, -1});  // Inner face

    // Endplate tops
    addQuad(verts, indices, epLT, epLB, epRB, epRT, {0, 1, 0});

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
    float angle = -0.15f;  // Angled down slightly

    float halfT = thickness / 2.0f;

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
    addQuad(verts, indices, rootFront, tipFront, tipBack, rootBack, {0, 1, 0});

    // Bottom
    addQuad(verts, indices, tipFrontB, rootFrontB, rootBackB, tipBackB, {0, -1, 0});

    // Front edge
    addQuad(verts, indices, rootFront, rootFrontB, tipFrontB, tipFront, {1, 0, 0});

    // Back edge
    addQuad(verts, indices, tipBack, tipBackB, rootBackB, rootBack, {-1, 0, 0});

    // Tip
    addQuad(verts, indices, tipFront, tipFrontB, tipBackB, tipBack, {0, 0, side});

    // Root (attaches to body)
    addQuad(verts, indices, rootBack, rootBackB, rootFrontB, rootFront, {0, 0, -side});

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
    gen.generate();
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

    // Engines - chrome with glow (clean metal, no livery)
    TexturedPBRMaterial engineMat;
    engineMat.albedo = glm::vec3(0.9f, 0.9f, 0.92f);
    engineMat.metallic = 1.0f;
    engineMat.roughness = 0.15f;
    engineMat.emissive = glm::vec3(1.0f, 0.5f, 0.2f) * (0.5f + engineGlow);

    // Fins and wing - use livery texture (accent region)
    TexturedPBRMaterial accentMat;
    accentMat.albedo = useLivery ? glm::vec3(1.0f) : palette.accent;
    accentMat.metallic = 0.3f;
    accentMat.roughness = 0.45f;
    if (useLivery) accentMat.albedoMap = &liveryTexture;

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
    ctx.render3DPBR(leftFinMesh, camera, leftFinXform, accentMat, lighting, iblEnvironment, output, noClear);

    glm::mat4 rightFinXform = baseXform;
    rightFinXform = glm::translate(rightFinXform, glm::vec3(1.5f, 0.2f, 1.0f));
    ctx.render3DPBR(rightFinMesh, camera, rightFinXform, accentMat, lighting, iblEnvironment, output, noClear);

    // Rear wing
    glm::mat4 wingXform = baseXform;
    wingXform = glm::translate(wingXform, glm::vec3(2.0f, 0.35f, 0));
    ctx.render3DPBR(rearWingMesh, camera, wingXform, accentMat, lighting, iblEnvironment, output, noClear);

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
