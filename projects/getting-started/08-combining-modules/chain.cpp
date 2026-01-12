// Lesson 08: Combining Modules
// Audio-reactive 3D with post-processing
//
// Run: ./build/bin/vivid projects/getting-started/08-combining-modules
//
// Play music or make sounds - watch the 3D scene react!

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================
    // AUDIO - Capture and analyze
    // =========================================

    auto& audio = chain.add<AudioIn>("audio");
    audio.volume = 1.0f;

    auto& levels = chain.add<Levels>("levels");
    levels.input("audio");
    levels.smoothing = 0.85f;

    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audio");
    bands.smoothing = 0.8f;

    // =========================================
    // 3D SCENE - Geometry that will react
    // =========================================

    auto& scene = SceneComposer::create(chain, "scene");

    // Main cube - will scale with bass
    scene.add<Box>("cube",
        glm::mat4(1.0f),
        glm::vec4(0.8f, 0.3f, 0.2f, 1.0f)
    ).size(1.0f, 1.0f, 1.0f).flatShading(true);

    // Orbiting sphere - color will change
    scene.add<Sphere>("sphere",
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0, 0)),
        glm::vec4(0.2f, 0.6f, 0.9f, 1.0f)
    ).radius(0.3f).segments(16);

    // =========================================
    // CAMERA & LIGHTING
    // =========================================

    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(5.0f);
    camera.elevation(0.3f);
    camera.fov(50.0f);

    auto& light = chain.add<DirectionalLight>("light");
    light.direction(1, 2, 1);
    light.color(1.0f, 1.0f, 1.0f);
    light.intensity = 1.0f;

    // =========================================
    // RENDER 3D
    // =========================================

    auto& render = chain.add<Render3D>("render3d");
    render.setInput(&scene);
    render.setCameraInput(&camera);
    render.setLightInput(&light);
    render.setShadingMode(ShadingMode::Flat);
    render.setAmbient(0.15f);
    render.setClearColor(0.05f, 0.05f, 0.08f);
    render.setResolution(1920, 1080);

    // =========================================
    // 2D POST-PROCESSING
    // =========================================

    // Add bloom - intensity reacts to audio
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("render3d");
    bloom.threshold = 0.3f;
    bloom.intensity = 0.3f;
    bloom.radius = 10.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // =========================================
    // GET AUDIO VALUES
    // =========================================

    auto& levels = chain.get<Levels>("levels");
    auto& bands = chain.get<BandSplit>("bands");

    float rms = levels.rms();
    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();

    // =========================================
    // AUDIO-REACTIVE 3D
    // =========================================

    auto& scene = chain.get<SceneComposer>("scene");
    auto& entries = scene.entries();

    // Cube: scale with bass, rotate with time
    float cubeScale = 1.0f + bass * 0.5f;
    entries[0].transform =
        glm::scale(glm::mat4(1.0f), glm::vec3(cubeScale)) *
        glm::rotate(glm::mat4(1.0f), time * 0.5f, glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1.0f), time * 0.3f, glm::vec3(1, 0, 0));

    // Cube color changes with audio
    entries[0].color = glm::vec4(
        0.5f + bass * 0.5f,    // Red from bass
        0.3f + mid * 0.4f,     // Green from mids
        0.2f + high * 0.6f,    // Blue from highs
        1.0f
    );

    // Sphere: orbit faster with energy
    float orbitSpeed = 0.5f + rms * 2.0f;
    float orbitRadius = 2.0f + mid * 0.5f;
    entries[1].transform = glm::translate(glm::mat4(1.0f),
        glm::vec3(
            cos(time * orbitSpeed) * orbitRadius,
            sin(time * orbitSpeed * 0.5f) * 0.5f,
            sin(time * orbitSpeed) * orbitRadius
        )
    );

    // =========================================
    // AUDIO-REACTIVE CAMERA
    // =========================================

    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(time * 0.2f);

    // =========================================
    // AUDIO-REACTIVE POST-PROCESSING
    // =========================================

    auto& bloom = chain.get<Bloom>("bloom");
    bloom.intensity = 0.2f + high * 0.5f;

    // Fullscreen toggle
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }
}

VIVID_CHAIN(setup, update)
