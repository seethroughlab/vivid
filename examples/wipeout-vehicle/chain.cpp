// See SPEC.md for project description
// Wipeout 2097-style hover vehicle generator with audio reactivity
#include <vivid/vivid.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <iostream>

using namespace vivid;

// === GLOBALS ===
// GOAL: Define scene objects and vehicle meshes

// Vehicle meshes
static Mesh3D hullMesh;
static Mesh3D cockpitMesh;
static Mesh3D leftEngineMesh;
static Mesh3D rightEngineMesh;
static Mesh3D leftFinMesh;
static Mesh3D rightFinMesh;

// Scene objects
static Camera3D camera;
static Texture output;
static SceneLighting lighting;

// Camera control
static float cameraYaw = 0.5f;
static float cameraPitch = 0.3f;
static float cameraDistance = 8.0f;
static float lastMouseX = 0;
static float lastMouseY = 0;
static bool isDragging = false;

// Audio-reactive state
static float engineGlow = 0.0f;
static float hoverOffset = 0.0f;
static float colorPhase = 0.0f;

// === MESH GENERATION HELPERS ===
// GOAL: Functions to create procedural vehicle geometry

void addQuadSingleSide(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
                        glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
                        glm::vec3 normal) {
    uint32_t base = static_cast<uint32_t>(verts.size());

    verts.push_back({p0, normal, {0, 0}});
    verts.push_back({p1, normal, {1, 0}});
    verts.push_back({p2, normal, {1, 1}});
    verts.push_back({p3, normal, {0, 1}});

    // Two triangles for the quad
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

// Double-sided quad to ensure visibility from any angle
void addQuad(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
             glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3,
             glm::vec3 normal) {
    // Front face
    addQuadSingleSide(verts, indices, p0, p1, p2, p3, normal);
    // Back face (reversed winding, flipped normal)
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

// Double-sided triangle to ensure visibility from any angle
void addTriangle(std::vector<Vertex3D>& verts, std::vector<uint32_t>& indices,
                 glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 normal) {
    // Front face
    addTriangleSingleSide(verts, indices, p0, p1, p2, normal);
    // Back face (reversed winding, flipped normal)
    addTriangleSingleSide(verts, indices, p0, p2, p1, -normal);
}

// Build the main hull - angular wedge shape
Mesh3D buildHull(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    // Hull dimensions
    float length = 3.0f;      // Front to back
    float width = 1.2f;       // Side to side
    float height = 0.4f;      // Top to bottom
    float noseLength = 1.5f;  // Tapered front section

    // Back section (rectangular)
    glm::vec3 bl = {-length/2, -height/2, -width/2};   // Back-left-bottom
    glm::vec3 br = {-length/2, -height/2,  width/2};   // Back-right-bottom
    glm::vec3 tl = {-length/2,  height/2, -width/2};   // Back-left-top
    glm::vec3 tr = {-length/2,  height/2,  width/2};   // Back-right-top

    // Mid section (where nose taper begins)
    float midX = noseLength - length/2;
    glm::vec3 mbl = {midX, -height/2, -width/2};
    glm::vec3 mbr = {midX, -height/2,  width/2};
    glm::vec3 mtl = {midX,  height/2, -width/2};
    glm::vec3 mtr = {midX,  height/2,  width/2};

    // Nose point
    glm::vec3 nose = {length/2, 0, 0};

    // === Back face ===
    addQuad(verts, indices, bl, tl, tr, br, {-1, 0, 0});

    // === Main body sides (back to mid) ===
    addQuad(verts, indices, bl, br, mbr, mbl, {0, -1, 0});  // Bottom
    addQuad(verts, indices, tl, mtl, mtr, tr, {0, 1, 0});   // Top
    addQuad(verts, indices, bl, mbl, mtl, tl, {0, 0, -1});  // Left
    addQuad(verts, indices, br, tr, mtr, mbr, {0, 0, 1});   // Right

    // === Nose section (tapered triangles) ===
    // Calculate normals for angled faces
    glm::vec3 topNoseNorm = glm::normalize(glm::vec3(height, length/2 - midX, 0));
    glm::vec3 botNoseNorm = glm::normalize(glm::vec3(-height, length/2 - midX, 0));
    glm::vec3 leftNoseNorm = glm::normalize(glm::cross(nose - mbl, mtl - mbl));
    glm::vec3 rightNoseNorm = glm::normalize(glm::cross(mtr - mbr, nose - mbr));

    // Top nose triangle
    addTriangle(verts, indices, mtl, mtr, nose, topNoseNorm);
    // Bottom nose triangle
    addTriangle(verts, indices, mbr, mbl, nose, botNoseNorm);
    // Left nose triangle
    addTriangle(verts, indices, mbl, nose, mtl, leftNoseNorm);
    // Right nose triangle
    addTriangle(verts, indices, mbr, mtr, nose, rightNoseNorm);

    return ctx.createMesh(verts, indices);
}

// Build cockpit canopy (double-sided UV sphere)
Mesh3D buildCockpit(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float radius = 0.35f;
    int segments = 16;  // Longitude
    int rings = 8;      // Latitude

    const float PI = 3.14159265359f;

    // Generate sphere vertices
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = PI * static_cast<float>(ring) / rings;
        float y = radius * std::cos(phi);
        float ringRadius = radius * std::sin(phi);

        for (int seg = 0; seg <= segments; ++seg) {
            float theta = 2.0f * PI * static_cast<float>(seg) / segments;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(pos);
            glm::vec2 uv(static_cast<float>(seg) / segments,
                        static_cast<float>(ring) / rings);

            verts.push_back({pos, normal, uv});
        }
    }

    // Generate front-facing triangles (visible from outside)
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            // First triangle
            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            // Second triangle
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    // Generate back-facing triangles (visible from inside) - reversed winding
    uint32_t frontCount = static_cast<uint32_t>(verts.size());

    // Duplicate vertices with inverted normals for back faces
    for (int ring = 0; ring <= rings; ++ring) {
        float phi = PI * static_cast<float>(ring) / rings;
        float y = radius * std::cos(phi);
        float ringRadius = radius * std::sin(phi);

        for (int seg = 0; seg <= segments; ++seg) {
            float theta = 2.0f * PI * static_cast<float>(seg) / segments;
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            glm::vec3 pos(x, y, z);
            glm::vec3 normal = -glm::normalize(pos);  // Inverted normal
            glm::vec2 uv(static_cast<float>(seg) / segments,
                        static_cast<float>(ring) / rings);

            verts.push_back({pos, normal, uv});
        }
    }

    // Generate back-facing triangles with reversed winding
    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            uint32_t current = frontCount + ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            // First triangle (reversed winding)
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);

            // Second triangle (reversed winding)
            indices.push_back(current + 1);
            indices.push_back(next + 1);
            indices.push_back(next);
        }
    }

    return ctx.createMesh(verts, indices);
}

// Build engine nacelle (hexagonal prism - double-sided)
Mesh3D buildEngine(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float radius = 0.2f;
    float length = 0.8f;
    int segments = 6;  // Hexagonal

    const float PI = 3.14159265359f;
    float halfLen = length * 0.5f;

    // Generate vertices for front and back hex caps
    std::vector<glm::vec3> frontRing, backRing;
    for (int i = 0; i < segments; ++i) {
        float theta = 2.0f * PI * static_cast<float>(i) / segments;
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);
        frontRing.push_back({halfLen, x, z});
        backRing.push_back({-halfLen, x, z});
    }

    glm::vec3 frontCenter = {halfLen, 0, 0};
    glm::vec3 backCenter = {-halfLen, 0, 0};

    // Side faces (quads between front and back rings)
    for (int i = 0; i < segments; ++i) {
        int next = (i + 1) % segments;
        glm::vec3 normal = glm::normalize(frontRing[i] - glm::vec3(frontRing[i].x, 0, 0));
        addQuad(verts, indices,
            frontRing[i], frontRing[next], backRing[next], backRing[i],
            normal);
    }

    // Front cap (triangles from center to ring)
    for (int i = 0; i < segments; ++i) {
        int next = (i + 1) % segments;
        addTriangle(verts, indices,
            frontCenter, frontRing[i], frontRing[next],
            {1, 0, 0});
    }

    // Back cap (triangles from center to ring, reversed winding)
    for (int i = 0; i < segments; ++i) {
        int next = (i + 1) % segments;
        addTriangle(verts, indices,
            backCenter, backRing[next], backRing[i],
            {-1, 0, 0});
    }

    return ctx.createMesh(verts, indices);
}

// Build stabilizer fin (thin wedge)
Mesh3D buildFin(Context& ctx) {
    std::vector<Vertex3D> verts;
    std::vector<uint32_t> indices;

    float finHeight = 0.6f;
    float finLength = 0.8f;
    float finThickness = 0.03f;

    // Fin vertices (thin vertical wedge)
    glm::vec3 base_front = {finLength/2, 0, 0};
    glm::vec3 base_back = {-finLength/2, 0, 0};
    glm::vec3 tip = {-finLength/4, finHeight, 0};

    // Left face
    glm::vec3 leftNorm = {0, 0, -1};
    addTriangle(verts, indices,
        base_front + glm::vec3(0, 0, -finThickness/2),
        base_back + glm::vec3(0, 0, -finThickness/2),
        tip + glm::vec3(0, 0, -finThickness/2),
        leftNorm);

    // Right face
    glm::vec3 rightNorm = {0, 0, 1};
    addTriangle(verts, indices,
        base_back + glm::vec3(0, 0, finThickness/2),
        base_front + glm::vec3(0, 0, finThickness/2),
        tip + glm::vec3(0, 0, finThickness/2),
        rightNorm);

    return ctx.createMesh(verts, indices);
}

void updateCamera() {
    float x = std::cos(cameraYaw) * std::cos(cameraPitch) * cameraDistance;
    float y = std::sin(cameraPitch) * cameraDistance;
    float z = std::sin(cameraYaw) * std::cos(cameraPitch) * cameraDistance;

    camera.position = glm::vec3(x, y, z);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
}

void setup(Chain& chain) {
    // === AUDIO INPUT ===
    // GOAL: Capture audio for reactive effects
    chain.add<AudioIn>("audio")
        .device(-1)
        .gain(2.0f)
        .fftSize(1024)
        .smoothing(0.85f);

    // Direct output (not using bloom for now to debug rendering)
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // === SETUP (first frame) ===
    // GOAL: Create meshes, materials, camera
    if (!output.valid()) {
        output = ctx.createTexture();

        std::cout << "[wipeout-vehicle] Building procedural meshes...\n";

        // Build vehicle parts
        hullMesh = buildHull(ctx);
        cockpitMesh = buildCockpit(ctx);
        leftEngineMesh = buildEngine(ctx);
        rightEngineMesh = buildEngine(ctx);
        leftFinMesh = buildFin(ctx);
        rightFinMesh = buildFin(ctx);

        std::cout << "[wipeout-vehicle] Meshes created\n";

        // Camera setup
        camera.fov = 45.0f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.0f;
        updateCamera();

        // Lighting setup - dramatic 3-point
        lighting.ambientColor = glm::vec3(0.1f, 0.1f, 0.15f);
        lighting.ambientIntensity = 0.3f;

        // Key light (warm from above-front)
        lighting.addLight(Light::directional(
            glm::vec3(-0.3f, -1.0f, -0.5f),
            glm::vec3(1.0f, 0.95f, 0.9f),
            1.0f
        ));

        // Fill light (cool from side)
        lighting.addLight(Light::directional(
            glm::vec3(0.8f, -0.2f, 0.5f),
            glm::vec3(0.6f, 0.7f, 1.0f),
            0.4f
        ));

        // Rim light (from behind-below)
        lighting.addLight(Light::directional(
            glm::vec3(0.0f, 0.5f, 1.0f),
            glm::vec3(1.0f, 0.8f, 0.6f),
            0.5f
        ));

        std::cout << "\n=== Wipeout Vehicle Demo ===\n";
        std::cout << "Drag mouse to orbit, scroll to zoom\n";
        std::cout << "Audio reactive: connect mic/line-in\n\n";
    }

    // === UPDATE ===
    // GOAL: Animation and interaction logic

    // Camera orbit via mouse drag
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

    // Zoom via scroll
    float scroll = ctx.scrollDeltaY();
    if (scroll != 0) {
        cameraDistance = glm::clamp(cameraDistance - scroll * 0.5f, 3.0f, 20.0f);
        updateCamera();
    }

    // Audio reactivity
    float level = ctx.getInputValue("audio", "level");
    float bass = ctx.getInputValue("audio", "bass");
    float mid = ctx.getInputValue("audio", "mid");
    float high = ctx.getInputValue("audio", "high");

    // Smooth the values
    engineGlow = engineGlow * 0.9f + (level * 5.0f) * 0.1f;
    hoverOffset = hoverOffset * 0.95f + (bass * 0.3f) * 0.05f;
    colorPhase += mid * 0.1f;

    float t = ctx.time();

    // Base hover animation
    float baseHover = std::sin(t * 2.0f) * 0.05f;
    float totalHover = baseHover + hoverOffset;

    // === RENDER ===
    // GOAL: Render the vehicle with PBR materials

    // Clear with dark gradient
    glm::vec4 clearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // Team colors (cycle with audio)
    float hue = std::fmod(colorPhase, 1.0f);
    glm::vec3 teamColor;
    if (hue < 0.33f) {
        teamColor = glm::vec3(0.8f, 0.1f, 0.1f);  // Red team
    } else if (hue < 0.66f) {
        teamColor = glm::vec3(0.1f, 0.3f, 0.9f);  // Blue team
    } else {
        teamColor = glm::vec3(0.1f, 0.8f, 0.3f);  // Green team
    }

    // Hull material - metallic paint
    PBRMaterial hullMat;
    hullMat.albedo = teamColor;
    hullMat.metallic = 0.7f;
    hullMat.roughness = 0.3f;

    // Cockpit material - tinted glass
    PBRMaterial cockpitMat;
    cockpitMat.albedo = glm::vec3(0.1f, 0.15f, 0.2f);
    cockpitMat.metallic = 0.0f;
    cockpitMat.roughness = 0.1f;

    // Engine material - chrome with emissive glow
    PBRMaterial engineMat;
    engineMat.albedo = glm::vec3(0.8f, 0.8f, 0.85f);
    engineMat.metallic = 1.0f;
    engineMat.roughness = 0.2f;
    engineMat.emissive = glm::vec3(1.0f, 0.5f, 0.2f) * engineGlow;

    // Fin material - darker accent
    PBRMaterial finMat;
    finMat.albedo = teamColor * 0.3f + glm::vec3(0.1f);
    finMat.metallic = 0.5f;
    finMat.roughness = 0.4f;

    // Vehicle rotation (slow spin when no input)
    float vehicleRotation = t * 0.2f;

    // Hull transform
    glm::mat4 hullXform = glm::mat4(1.0f);
    hullXform = glm::translate(hullXform, glm::vec3(0, totalHover, 0));
    hullXform = glm::rotate(hullXform, vehicleRotation, glm::vec3(0, 1, 0));

    // Render hull (clears buffer)
    ctx.render3DPBR(hullMesh, camera, hullXform, hullMat, lighting, output, clearColor);

    // Don't clear for subsequent renders
    glm::vec4 noClear(0, 0, 0, -1);

    // Cockpit transform (on top of hull, toward front)
    glm::mat4 cockpitXform = hullXform;
    cockpitXform = glm::translate(cockpitXform, glm::vec3(0.3f, 0.3f, 0));
    ctx.render3DPBR(cockpitMesh, camera, cockpitXform, cockpitMat, lighting, output, noClear);

    // Left engine transform
    glm::mat4 leftEngineXform = hullXform;
    leftEngineXform = glm::translate(leftEngineXform, glm::vec3(-0.8f, -0.1f, -0.7f));
    leftEngineXform = glm::rotate(leftEngineXform, glm::radians(90.0f), glm::vec3(0, 0, 1));
    ctx.render3DPBR(leftEngineMesh, camera, leftEngineXform, engineMat, lighting, output, noClear);

    // Right engine transform
    glm::mat4 rightEngineXform = hullXform;
    rightEngineXform = glm::translate(rightEngineXform, glm::vec3(-0.8f, -0.1f, 0.7f));
    rightEngineXform = glm::rotate(rightEngineXform, glm::radians(90.0f), glm::vec3(0, 0, 1));
    ctx.render3DPBR(rightEngineMesh, camera, rightEngineXform, engineMat, lighting, output, noClear);

    // Left fin transform
    glm::mat4 leftFinXform = hullXform;
    leftFinXform = glm::translate(leftFinXform, glm::vec3(-1.0f, 0.2f, -0.6f));
    leftFinXform = glm::rotate(leftFinXform, glm::radians(-15.0f), glm::vec3(1, 0, 0));
    ctx.render3DPBR(leftFinMesh, camera, leftFinXform, finMat, lighting, output, noClear);

    // Right fin transform
    glm::mat4 rightFinXform = hullXform;
    rightFinXform = glm::translate(rightFinXform, glm::vec3(-1.0f, 0.2f, 0.6f));
    rightFinXform = glm::rotate(rightFinXform, glm::radians(15.0f), glm::vec3(1, 0, 0));
    ctx.render3DPBR(rightFinMesh, camera, rightFinXform, finMat, lighting, output, noClear);

    // Output the rendered scene
    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
