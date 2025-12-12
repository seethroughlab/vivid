// Multi-Canvas Cube Test Fixture
// Tests canvas resolution locking and texture integration with 3D
// 6 planes arranged as a cube, each with different canvas/video texture
//
// Expected behavior:
// - Each face maintains its declared resolution (not window size)
// - Text and shapes render correctly on each canvas
// - Video plays on top face with canvas overlay
// - Resizing window should NOT change canvas/video resolutions

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;
using namespace vivid::video;

// Canvas drawing helpers
static const float LETTER_SPACING = 0.0f;  // No extra spacing needed with proper fonts

static void drawFront(Canvas& c, float time) {
    c.clear(0.8f, 0.2f, 0.2f, 1.0f);  // Red
    c.textCentered("FRONT", 256, 256, glm::vec4(1, 1, 1, 1), LETTER_SPACING);
    c.circleFilled(256 + 100 * std::sin(time), 350, 40, glm::vec4(1, 1, 1, 0.8f));
}

static void drawBack(Canvas& c, float time) {
    c.clear(0.2f, 0.2f, 0.8f, 1.0f);  // Blue
    c.textCentered("BACK", 256, 256, glm::vec4(1, 1, 1, 1), LETTER_SPACING);
    float offset = 50 * std::sin(time * 0.8f);
    c.rectFilled(156 + offset, 320, 80, 80, glm::vec4(1, 1, 1, 0.7f));
    c.rectFilled(276 - offset, 320, 80, 80, glm::vec4(1, 1, 0, 0.7f));
}

static void drawLeft(Canvas& c, float time) {
    c.clear(0.2f, 0.7f, 0.2f, 1.0f);  // Green
    c.textCentered("LEFT", 128, 128, glm::vec4(1, 1, 1, 1), LETTER_SPACING);
    // Diagonal lines
    for (int i = 0; i < 5; i++) {
        float offset = 40 * i + 20 * std::sin(time + i);
        c.line(0, offset, 256, offset + 100, 3, glm::vec4(1, 1, 1, 0.5f));
    }
}

static void drawRight(Canvas& c, float time) {
    c.clear(0.8f, 0.8f, 0.2f, 1.0f);  // Yellow
    c.textCentered("RIGHT", 128, 128, glm::vec4(0, 0, 0, 1), LETTER_SPACING);
    // Triangles
    float wobble = 20 * std::sin(time * 1.2f);
    c.triangleFilled(
        glm::vec2(128, 60 + wobble),
        glm::vec2(80, 200),
        glm::vec2(176, 200),
        glm::vec4(0, 0, 0, 0.6f)
    );
}

static void drawBottom(Canvas& c, float time, int frame) {
    c.clear(0.9f, 0.9f, 0.9f, 1.0f);  // White
    c.textCentered("BOTTOM", 512, 400, glm::vec4(0, 0, 0, 1), LETTER_SPACING);
    // Frame counter
    char buf[64];
    snprintf(buf, sizeof(buf), "Frame: %d", frame);
    c.textCentered(buf, 512, 550, glm::vec4(0.3f, 0.3f, 0.3f, 1), LETTER_SPACING);
    // Pulsing circle
    float radius = 80 + 30 * std::sin(time * 2);
    c.circleFilled(512, 700, radius, glm::vec4(0.2f, 0.5f, 0.8f, 0.7f));
}

static void drawTopOverlay(Canvas& c, float time) {
    // Transparent canvas to overlay on video
    c.clear(0, 0, 0, 0);  // Fully transparent
    c.textCentered("VIDEO", 256, 80, glm::vec4(1, 1, 1, 0.9f), LETTER_SPACING);
    // Animated border
    float pulse = 0.5f + 0.3f * std::sin(time * 3);
    c.rect(20, 20, 472, 472, 4, glm::vec4(1, 1, 0, pulse));
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Set window size from chain (can be overridden by --window command-line arg)
    chain.windowSize(1280, 720);

    // Load font for all canvases
    auto& frontCanvas = chain.add<Canvas>("front").size(512, 512);
    frontCanvas.loadFont(ctx, "assets/fonts/space age.ttf", 32);

    auto& backCanvas = chain.add<Canvas>("back").size(512, 512);
    backCanvas.loadFont(ctx, "assets/fonts/space age.ttf", 32);

    auto& leftCanvas = chain.add<Canvas>("left").size(256, 256);
    leftCanvas.loadFont(ctx, "assets/fonts/space age.ttf", 20);

    auto& rightCanvas = chain.add<Canvas>("right").size(256, 256);
    rightCanvas.loadFont(ctx, "assets/fonts/space age.ttf", 20);

    auto& bottomCanvas = chain.add<Canvas>("bottom").size(1024, 1024);
    bottomCanvas.loadFont(ctx, "assets/fonts/space age.ttf", 48);

    // Top face: Video + Canvas overlay
    auto& video = chain.add<VideoPlayer>("video")
        .file("assets/videos/hap-1080p-audio.mov")
        .loop(true)
        .volume(0.0f);  // Mute audio for test fixture

    auto& topOverlay = chain.add<Canvas>("topOverlay").size(512, 512);
    topOverlay.loadFont(ctx, "assets/fonts/space age.ttf", 32);

    auto& topComposite = chain.add<Composite>("top")
        .input(0, &video)
        .input(1, &topOverlay)
        .mode(BlendMode::Over);

    // Create materials for each face
    // Using metallicFactor(0) makes surfaces diffuse (non-reflective)
    auto& matFront = chain.add<TexturedMaterial>("matFront")
        .baseColorInput(&frontCanvas)
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f)
        .doubleSided(true);
    auto& matBack = chain.add<TexturedMaterial>("matBack")
        .baseColorInput(&backCanvas)
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f)
        .doubleSided(true);
    auto& matLeft = chain.add<TexturedMaterial>("matLeft")
        .baseColorInput(&leftCanvas)
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f)
        .doubleSided(true);
    auto& matRight = chain.add<TexturedMaterial>("matRight")
        .baseColorInput(&rightCanvas)
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f)
        .doubleSided(true);
    auto& matTop = chain.add<TexturedMaterial>("matTop")
        .baseColorInput(&topComposite)
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f)
        .doubleSided(true);
    auto& matBottom = chain.add<TexturedMaterial>("matBottom")
        .baseColorInput(&bottomCanvas)
        .metallicFactor(0.0f)
        .roughnessFactor(1.0f)
        .doubleSided(true);

    // Create scene with 6 planes as cube faces
    auto& scene = SceneComposer::create(chain, "scene");

    // MeshBuilder::plane() creates a plane in XZ plane (Y=0) facing +Y
    // Vertex winding is CCW when viewed from +Y (front face toward +Y)
    // To preserve correct winding after rotation, we must be careful about rotation direction
    // Faces pointing toward negative axes need rotation that preserves front-face winding

    // Front (Z+): rotate +90° around X to face +Z with correct UV orientation
    glm::mat4 frontT = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0.5f));
    frontT = glm::rotate(frontT, glm::half_pi<float>(), glm::vec3(1, 0, 0));  // +90° faces +Z with correct UVs
    scene.add<Plane>("planeFront", frontT)
        .size(1.0f, 1.0f);
    scene.setMaterial(scene.entries().size() - 1, &matFront);

    // Back (Z-): rotate -90° around X to face -Z with correct UV orientation
    glm::mat4 backT = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -0.5f));
    backT = glm::rotate(backT, -glm::half_pi<float>(), glm::vec3(1, 0, 0));  // -90° faces -Z with correct UVs
    scene.add<Plane>("planeBack", backT)
        .size(1.0f, 1.0f);
    scene.setMaterial(scene.entries().size() - 1, &matBack);

    // Left (X-): rotate -90° around Z (face +X), then 180° around Y (flip to -X)
    glm::mat4 leftT = glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0, 0));
    leftT = glm::rotate(leftT, glm::pi<float>(), glm::vec3(0, 1, 0));
    leftT = glm::rotate(leftT, -glm::half_pi<float>(), glm::vec3(0, 0, 1));
    scene.add<Plane>("planeLeft", leftT)
        .size(1.0f, 1.0f);
    scene.setMaterial(scene.entries().size() - 1, &matLeft);

    // Right (X+): rotate -90° around Z to face +X
    glm::mat4 rightT = glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0, 0));
    rightT = glm::rotate(rightT, -glm::half_pi<float>(), glm::vec3(0, 0, 1));
    scene.add<Plane>("planeRight", rightT)
        .size(1.0f, 1.0f);
    scene.setMaterial(scene.entries().size() - 1, &matRight);

    // Top (Y+): plane already faces +Y, just translate
    glm::mat4 topT = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.5f, 0));
    scene.add<Plane>("planeTop", topT)
        .size(1.0f, 1.0f);
    scene.setMaterial(scene.entries().size() - 1, &matTop);

    // Bottom (Y-): rotate 180° around Z to face -Y (preserves winding better than X rotation)
    glm::mat4 bottomT = glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.5f, 0));
    bottomT = glm::rotate(bottomT, glm::pi<float>(), glm::vec3(0, 0, 1));
    scene.add<Plane>("planeBottom", bottomT)
        .size(1.0f, 1.0f);
    scene.setMaterial(scene.entries().size() - 1, &matBottom);

    // Camera
    auto& camera = chain.add<CameraOperator>("camera")
        .distance(3.0f)
        .elevation(0.4f)
        .azimuth(0.0f)
        .fov(50.0f);

    // Light - high intensity for bright diffuse surfaces
    auto& light = chain.add<DirectionalLight>("light")
        .direction(1.0f, 1.0f, 1.0f)
        .intensity(3.0f);

    // Render with high ambient for even lighting on all faces
    auto& render = chain.add<Render3D>("render")
        .input(&scene)
        .cameraInput(&camera)
        .lightInput(&light)
        .shadingMode(ShadingMode::PBR)
        .ambient(1.0f)
        .clearColor(0.1f, 0.1f, 0.15f);

    chain.output("render");
}

static int frameCount = 0;
static float cameraAzimuth = 0.4f;
static float cameraElevation = 0.4f;
static glm::vec2 lastMousePos = {0, 0};
static bool wasDragging = false;

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());
    frameCount++;

    // Update all canvas contents
    auto& frontCanvas = chain.get<Canvas>("front");
    auto& backCanvas = chain.get<Canvas>("back");
    auto& leftCanvas = chain.get<Canvas>("left");
    auto& rightCanvas = chain.get<Canvas>("right");
    auto& bottomCanvas = chain.get<Canvas>("bottom");
    auto& topOverlay = chain.get<Canvas>("topOverlay");

    drawFront(frontCanvas, time);
    drawBack(backCanvas, time);
    drawLeft(leftCanvas, time);
    drawRight(rightCanvas, time);
    drawBottom(bottomCanvas, time, frameCount);
    drawTopOverlay(topOverlay, time);

    // Mouse drag to orbit camera
    glm::vec2 mousePos = ctx.mouse();
    bool isDragging = ctx.mouseButton(0).held;

    if (isDragging && wasDragging) {
        glm::vec2 delta = mousePos - lastMousePos;
        cameraAzimuth -= delta.x * 0.01f;
        cameraElevation -= delta.y * 0.01f;
        // Clamp elevation to avoid flipping
        cameraElevation = glm::clamp(cameraElevation, -1.5f, 1.5f);
    }

    lastMousePos = mousePos;
    wasDragging = isDragging;

    // Update camera
    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(cameraAzimuth);
    camera.elevation(cameraElevation);
}

VIVID_CHAIN(setup, update)
