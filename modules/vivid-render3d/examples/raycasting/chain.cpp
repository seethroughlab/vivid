// 3D Raycasting / Picking Example
// Demonstrates: Screen-to-world ray conversion, sphere intersection, custom shader
//
// Click on spheres to select them. Hover shows preview highlighting.
// Spheres animate to prove raycasting tracks moving objects.
// Custom outline shader applied as post-process effect.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/effects/simple_texture_effect.h>
#include <vivid/render3d/render3d.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <vector>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// =============================================================================
// CUSTOM OUTLINE EFFECT (Example of project-local shader)
// =============================================================================

/// Uniform buffer for the outline shader
struct OutlineUniforms {
    float thickness;      // Outline spread in pixels
    float threshold;      // Alpha threshold for edge detection
    float texelW;         // 1.0 / texture width
    float texelH;         // 1.0 / texture height
    float colorR;         // Outline color R
    float colorG;         // Outline color G
    float colorB;         // Outline color B
    float colorA;         // Outline color A
};

/**
 * @brief Custom outline effect using blur-based alpha edge detection
 *
 * This demonstrates how to create a custom shader effect in your project.
 * The effect detects edges by sampling alpha in a blur pattern - where the
 * current pixel is transparent but nearby pixels are opaque, it draws an outline.
 */
class OutlineEffect : public SimpleTextureEffect<OutlineEffect, OutlineUniforms> {
public:
    float thickness = 2.0f;
    float threshold = 0.5f;
    glm::vec4 color = glm::vec4(1.0f);

    OutlineUniforms getUniforms() const {
        return {
            thickness,
            threshold,
            1.0f / m_width,
            1.0f / m_height,
            color.r, color.g, color.b, color.a
        };
    }

    std::string name() const override { return "OutlineEffect"; }

    const char* fragmentShader() const override {
        return R"(
struct Uniforms {
    thickness: f32,
    threshold: f32,
    texelW: f32,
    texelH: f32,
    colorR: f32,
    colorG: f32,
    colorB: f32,
    colorA: f32,
};

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let pixel = textureSample(inputTex, texSampler, input.uv);

    // If this pixel is mostly transparent, check if it's near an edge
    if (pixel.a <= u.threshold) {
        let size = vec2f(textureDimensions(inputTex, 0));
        let uv_px = input.uv * size;

        // Sample 9x9 neighborhood for alpha
        var sum: f32 = 0.0;
        for (var y: i32 = -4; y <= 4; y++) {
            var h_sum: f32 = 0.0;
            let sample_y = uv_px.y + f32(y) * u.thickness;
            for (var x: i32 = -4; x <= 4; x++) {
                let sample_x = uv_px.x + f32(x) * u.thickness;
                let sample_uv = vec2f(sample_x, sample_y) / size;
                h_sum += textureSample(inputTex, texSampler, sample_uv).a;
            }
            sum += h_sum / 9.0;
        }

        // If nearby pixels have alpha, draw outline
        if (sum / 9.0 >= 0.0001) {
            return vec4f(u.colorR, u.colorG, u.colorB, u.colorA);
        }
    }

    return pixel;
}
)";
    }
};

// =============================================================================
// PICKING STATE
// =============================================================================

struct PickableSphere {
    glm::vec3 basePosition;
    float radius;
    glm::vec4 baseColor;
    bool selected = false;
    float animPhase;
};

static std::vector<PickableSphere> g_spheres;
static int g_selectedIndex = -1;
static int g_hoveredIndex = -1;

// =============================================================================
// RAY-SPHERE INTERSECTION
// =============================================================================

void screenToWorldRay(float screenX, float screenY, const Camera3D& camera,
                      glm::vec3& rayOrigin, glm::vec3& rayDir) {
    float ndcX = screenX * 2.0f - 1.0f;
    float ndcY = 1.0f - screenY * 2.0f;

    glm::mat4 invVP = glm::inverse(camera.viewProjectionMatrix());
    glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);

    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;

    rayOrigin = glm::vec3(nearPoint);
    rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));
}

bool raySphereIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        const glm::vec3& sphereCenter, float sphereRadius,
                        float& hitDistance) {
    glm::vec3 oc = rayOrigin - sphereCenter;
    float a = glm::dot(rayDir, rayDir);
    float b = 2.0f * glm::dot(oc, rayDir);
    float c = glm::dot(oc, oc) - sphereRadius * sphereRadius;
    float discriminant = b * b - 4.0f * a * c;

    if (discriminant < 0.0f) return false;

    float t = (-b - std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0.0f) t = (-b + std::sqrt(discriminant)) / (2.0f * a);
    if (t < 0.0f) return false;

    hitDistance = t;
    return true;
}

int pickSphere(float screenX, float screenY, const Camera3D& camera,
               const std::vector<glm::vec3>& positions) {
    glm::vec3 rayOrigin, rayDir;
    screenToWorldRay(screenX, screenY, camera, rayOrigin, rayDir);

    int closestIndex = -1;
    float closestDist = std::numeric_limits<float>::max();

    for (size_t i = 0; i < g_spheres.size(); ++i) {
        float hitDist;
        if (raySphereIntersect(rayOrigin, rayDir, positions[i],
                               g_spheres[i].radius, hitDist)) {
            if (hitDist < closestDist) {
                closestDist = hitDist;
                closestIndex = static_cast<int>(i);
            }
        }
    }

    return closestIndex;
}

// =============================================================================
// SETUP
// =============================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create pickable spheres
    g_spheres.clear();
    g_spheres.push_back({{-2.0f, 0.0f, 0.0f}, 0.6f, {1.0f, 0.3f, 0.3f, 1.0f}, false, 0.0f});
    g_spheres.push_back({{0.0f, 0.0f, 0.0f}, 0.8f, {0.3f, 1.0f, 0.3f, 1.0f}, false, 1.0f});
    g_spheres.push_back({{2.0f, 0.0f, 0.0f}, 0.5f, {0.3f, 0.3f, 1.0f, 1.0f}, false, 2.0f});
    g_spheres.push_back({{0.0f, 1.5f, 0.0f}, 0.4f, {1.0f, 1.0f, 0.3f, 1.0f}, false, 3.0f});
    g_spheres.push_back({{0.0f, -1.5f, 1.0f}, 0.7f, {1.0f, 0.3f, 1.0f, 1.0f}, false, 4.0f});

    // Main scene
    auto& scene = SceneComposer::create(chain, "scene");
    for (size_t i = 0; i < g_spheres.size(); ++i) {
        std::string name = "sphere" + std::to_string(i);
        auto& s = scene.add<Sphere>(name,
            glm::translate(glm::mat4(1.0f), g_spheres[i].basePosition),
            g_spheres[i].baseColor);
        s.radius(g_spheres[i].radius);
        s.segments(32);
    }

    scene.add<Box>("ground",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -2.5f, 0.0f)) *
        glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 0.1f, 10.0f)),
        glm::vec4(0.3f, 0.3f, 0.35f, 1.0f));

    // Mask scene (selected objects only, for outline)
    auto& maskScene = SceneComposer::create(chain, "maskScene");
    for (size_t i = 0; i < g_spheres.size(); ++i) {
        std::string name = "maskSphere" + std::to_string(i);
        auto& s = maskScene.add<Sphere>(name,
            glm::translate(glm::mat4(1.0f), g_spheres[i].basePosition),
            glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));  // Start invisible
        s.radius(g_spheres[i].radius);
        s.segments(32);
    }

    // Camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(8.0f);
    camera.elevation(0.4f);
    camera.azimuth(0.5f);
    camera.fov(50.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // Lighting
    auto& sun = chain.add<DirectionalLight>("sun");
    sun.direction(1.0f, 2.0f, 1.5f);
    sun.color(1.0f, 1.0f, 1.0f);
    sun.intensity = 1.0f;

    // Main render
    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&sun);
    render.setShadingMode(ShadingMode::Gouraud);
    render.setAmbient(0.25f);
    render.setClearColor(0.1f, 0.1f, 0.15f);
    render.setResolution(1920, 1080);

    // Mask render (selected objects for outline)
    auto& maskRender = chain.add<Render3D>("maskRender");
    maskRender.setInput(&maskScene);
    maskRender.setCameraInput(&camera);
    maskRender.setShadingMode(ShadingMode::Unlit);
    maskRender.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    maskRender.setResolution(1920, 1080);

    // Custom outline effect on mask
    auto& outline = chain.add<OutlineEffect>("outline");
    outline.setInput(0, &maskRender);
    outline.thickness = 3.0f;
    outline.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan

    // Composite outline over main scene
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("render3d");
    comp.inputB("outline");
    comp.mode = BlendMode::Over;
    comp.setResolution(1920, 1080);

    // Canvas for UI
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "comp");

    chain.output("canvas");
}

// =============================================================================
// UPDATE
// =============================================================================

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");
    auto& scene = chain.get<SceneComposer>("scene");
    auto& maskScene = chain.get<SceneComposer>("maskScene");
    auto& outline = chain.get<OutlineEffect>("outline");
    auto& entries = scene.entries();
    auto& maskEntries = maskScene.entries();

    float time = ctx.time();

    // Animate spheres
    std::vector<glm::vec3> currentPositions(g_spheres.size());
    for (size_t i = 0; i < g_spheres.size(); ++i) {
        float bob = std::sin(time * 2.0f + g_spheres[i].animPhase) * 0.2f;
        float rotY = time * 0.5f + g_spheres[i].animPhase * 0.3f;

        glm::vec3 pos = g_spheres[i].basePosition;
        pos.y += bob;
        currentPositions[i] = pos;

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
        transform = glm::rotate(transform, rotY, glm::vec3(0.0f, 1.0f, 0.0f));

        entries[i].transform = transform;
        maskEntries[i].transform = transform;
    }

    // Camera control
    static float azimuth = 0.5f;
    static float elevation = 0.4f;

    auto rightBtn = ctx.mouseButton(1);
    if (rightBtn.held) {
        glm::vec2 delta = ctx.mouseDeltaNorm();
        azimuth -= delta.x * 3.0f;
        elevation += delta.y * 2.0f;
        elevation = glm::clamp(elevation, -1.5f, 1.5f);
    }

    camera.azimuth(azimuth);
    camera.elevation(elevation);
    camera.process(ctx);

    // Picking
    const Camera3D& cam3d = camera.outputCamera();
    const_cast<Camera3D&>(cam3d).aspect(static_cast<float>(ctx.width()) / ctx.height());

    glm::vec2 mouseNorm = ctx.mouseNorm();
    g_hoveredIndex = pickSphere(mouseNorm.x, mouseNorm.y, cam3d, currentPositions);

    // Snapshot
    if (ctx.key(GLFW_KEY_S).pressed) {
        ctx.snapshot();
    }

    // Click to select
    auto leftBtn = ctx.mouseButton(0);
    if (leftBtn.pressed) {
        if (g_hoveredIndex >= 0) {
            if (g_selectedIndex == g_hoveredIndex) {
                g_spheres[g_selectedIndex].selected = false;
                g_selectedIndex = -1;
            } else {
                if (g_selectedIndex >= 0) {
                    g_spheres[g_selectedIndex].selected = false;
                }
                g_selectedIndex = g_hoveredIndex;
                g_spheres[g_selectedIndex].selected = true;
            }
        } else {
            if (g_selectedIndex >= 0) {
                g_spheres[g_selectedIndex].selected = false;
                g_selectedIndex = -1;
            }
        }
    }

    // Update visuals
    for (size_t i = 0; i < g_spheres.size(); ++i) {
        glm::vec4 color = g_spheres[i].baseColor;
        bool isHovered = (static_cast<int>(i) == g_hoveredIndex);
        bool isSelected = g_spheres[i].selected;

        if (isSelected) {
            color = glm::mix(color, glm::vec4(1.0f), 0.3f);
        } else if (isHovered) {
            color = glm::mix(color, glm::vec4(1.0f), 0.15f);
        }

        entries[i].color = color;

        // Show in mask if selected or hovered
        if (isSelected) {
            maskEntries[i].color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            outline.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // White for selected
        } else if (isHovered) {
            maskEntries[i].color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            outline.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan for hovered
        } else {
            maskEntries[i].color = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);  // Hidden
        }
    }

    scene.markDirty();
    maskScene.markDirty();

    // UI
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0, 0, 0, 0);

    auto& comp = chain.get<Composite>("comp");
    canvas.drawImage(comp, 0, 0, ctx.width(), ctx.height());

    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.75f);
    canvas.fillRect(10, 10, 350, 180);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("3D Raycasting + Custom Outline Shader", 20, 35);

    canvas.fillStyle(0.8f, 0.8f, 0.8f, 1.0f);
    char mouseInfo[128];
    snprintf(mouseInfo, sizeof(mouseInfo), "Mouse: (%.3f, %.3f)", mouseNorm.x, mouseNorm.y);
    canvas.fillText(mouseInfo, 20, 60);

    if (g_hoveredIndex >= 0) {
        canvas.fillStyle(0.5f, 1.0f, 0.5f, 1.0f);
        char hoverInfo[64];
        snprintf(hoverInfo, sizeof(hoverInfo), "Hovering: Sphere %d", g_hoveredIndex);
        canvas.fillText(hoverInfo, 20, 85);
    } else {
        canvas.fillStyle(0.6f, 0.6f, 0.6f, 1.0f);
        canvas.fillText("Hovering: None", 20, 85);
    }

    if (g_selectedIndex >= 0) {
        canvas.fillStyle(1.0f, 1.0f, 0.5f, 1.0f);
        char selectInfo[64];
        snprintf(selectInfo, sizeof(selectInfo), "Selected: Sphere %d", g_selectedIndex);
        canvas.fillText(selectInfo, 20, 110);
    } else {
        canvas.fillStyle(0.6f, 0.6f, 0.6f, 1.0f);
        canvas.fillText("Selected: None", 20, 110);
    }

    canvas.fillStyle(0.7f, 0.9f, 1.0f, 1.0f);
    canvas.fillText("Controls:", 20, 140);
    canvas.fillStyle(0.7f, 0.7f, 0.7f, 1.0f);
    canvas.fillText("  Left-click: Select  |  Right-drag: Orbit", 20, 160);
    canvas.fillText("  S: Save snapshot", 20, 175);
}

VIVID_CHAIN(setup, update)
