// Drum Synthesis - Vivid Example
// Demonstrates: Kick, Snare, HiHat, Clap, Clock, Sequencer, AudioMixer
// 3D PBR visualization with pulsing asteroid and ring

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- MASTER CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);  // 16th notes
    clock.swing = 0.0f;
    clock.start();

    // ----- SEQUENCERS -----
    // NOTE: Sequencers must be added BEFORE drums for correct audio-thread execution order.
    // The AudioGraph processes operators in the order they were added, so sequencers
    // need to generate their triggered() state before drums check it.
    auto& kick_seq = chain.add<Sequencer>("kick_seq");
    kick_seq.steps = 16;
    // Classic four-on-the-floor: 1, 5, 9, 13 (0-indexed: 0, 4, 8, 12)
    kick_seq.setStep(0, true);
    kick_seq.setStep(4, true);
    kick_seq.setStep(8, true);
    kick_seq.setStep(12, true);

    auto& snare_seq = chain.add<Sequencer>("snare_seq");
    snare_seq.steps = 16;
    // Backbeat: 5, 13 (0-indexed: 4, 12)
    snare_seq.setStep(4, true);
    snare_seq.setStep(12, true);

    auto& hihat_seq = chain.add<Sequencer>("hihat_seq");
    hihat_seq.steps = 16;
    // Every 16th note
    for (int i = 0; i < 16; i++) {
        hihat_seq.setStep(i, true, (i % 2 == 0) ? 1.0f : 0.6f);  // Accent downbeats
    }

    auto& clap_seq = chain.add<Sequencer>("clap_seq");
    clap_seq.steps = 16;
    // Same as snare, slightly different timing
    clap_seq.setStep(4, true, 0.8f);
    clap_seq.setStep(12, true, 1.0f);

    // ----- DRUM VOICES -----
    // 808-style synthesized drums (added AFTER sequencers for correct execution order)
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 50.0f;        // Base frequency
    kick.pitchEnv = 150.0f;    // Pitch sweep amount
    kick.pitchDecay = 0.08f;   // Fast pitch decay
    kick.decay = 0.5f;         // Amplitude decay
    kick.click = 0.4f;         // Transient click
    kick.drive = 0.2f;         // Soft saturation
    kick.volume = 0.9f;

    auto& snare = chain.add<Snare>("snare");
    snare.tone = 0.4f;         // Tone/body mix amount (0-1)
    snare.noise = 0.8f;        // Noise/snare mix amount (0-1)
    snare.pitch = 180.0f;      // Body frequency in Hz
    snare.toneDecay = 0.1f;    // Tone decay
    snare.noiseDecay = 0.2f;   // Noise decay
    snare.snappy = 0.7f;       // Snare wire noise amount
    snare.volume = 0.75f;

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.05f;       // Short decay for closed hat
    hihat.tone = 0.3f;         // Metallic character
    hihat.volume = 0.5f;

    auto& clap = chain.add<Clap>("clap");
    clap.decay = 0.15f;        // Medium decay
    clap.spread = 0.03f;       // Timing spread of multiple hits
    clap.tone = 0.5f;          // Tonal character
    clap.volume = 0.7f;

    // ----- MIXER -----
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.setInput(0, "kick");
    mixer.setInput(1, "snare");
    mixer.setInput(2, "hihat");
    mixer.setInput(3, "clap");
    mixer.volume = 0.7f;

    // ----- MASTER VOLUME -----
    auto& master = chain.add<AudioGain>("master");
    master.input("mixer");
    master.gain = 0.5f;

    // ----- AUDIO OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.setInput("master");
    chain.audioOutput("audio_out");

    // ----- TRIGGER CONNECTIONS (audio-thread timing) -----
    // Sequencers advance on clock trigger
    kick_seq.setTriggerSource("clock");
    snare_seq.setTriggerSource("clock");
    hihat_seq.setTriggerSource("clock");
    clap_seq.setTriggerSource("clock");

    // Drums trigger on their sequencer output
    kick.setTriggerSource("kick_seq");
    snare.setTriggerSource("snare_seq");
    hihat.setTriggerSource("hihat_seq");
    clap.setTriggerSource("clap_seq");

    // =========================================================================
    // 3D VISUALS - PBR Asteroid with Ring
    // =========================================================================

    // ----- 3D SCENE -----
    auto& scene = SceneComposer::create(chain, "scene");

    // Asteroid sphere with noise displacement - high detail for craggy surface
    auto& asteroid = scene.add<Sphere>("asteroid",
        glm::mat4(1.0f),  // Identity transform (animated later)
        glm::vec4(0.8f, 0.4f, 0.2f, 1.0f));  // Orange base color
    asteroid.radius(1.0f);
    asteroid.segments(128);  // High poly for detailed displacement
    asteroid.noiseDisplacement(0.35f, 5.0f, 8);  // Deep craters, high frequency, many octaves

    // Ring around the asteroid (like Saturn's rings but vertical)
    auto& ring = scene.add<Torus>("ring",
        glm::rotate(glm::mat4(1.0f), 1.57f, glm::vec3(1, 0, 0)),  // Rotate to horizontal
        glm::vec4(1.0f, 0.7f, 0.3f, 1.0f));  // Golden
    ring.outerRadius(1.8f);
    ring.innerRadius(0.05f);
    ring.segments(64);
    ring.rings(8);

    // ----- CAMERA -----
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(5.0f);
    camera.elevation(0.3f);
    camera.azimuth(0.0f);
    camera.fov(45.0f);
    camera.nearPlane(0.1f);
    camera.farPlane(100.0f);

    // ----- KEY LIGHT (main illumination) -----
    auto& keyLight = chain.add<DirectionalLight>("keyLight");
    keyLight.direction(1, 1, 1);
    keyLight.color(1.0f, 0.95f, 0.9f);  // Warm white
    keyLight.intensity = 1.5f;

    // ----- RIM LIGHT (edge highlight) -----
    auto& rimLight = chain.add<DirectionalLight>("rimLight");
    rimLight.direction(-1, 0.5f, -1);
    rimLight.color(1.0f, 0.6f, 0.2f);  // Orange rim
    rimLight.intensity = 2.0f;

    // ----- 3D RENDERER -----
    auto& render3d = chain.add<Render3D>("render3d");
    render3d.setInput(&scene);
    render3d.setCameraInput(&camera);
    render3d.setLightInput(&keyLight);
    render3d.addLight(&rimLight);
    render3d.setShadingMode(ShadingMode::PBR);
    render3d.setAmbient(0.15f);
    render3d.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent for compositing
    render3d.setResolution(1280, 720);
    render3d.setDepthOutput(true);  // Enable depth output for masking!

    // =========================================================================
    // BACKGROUND
    // =========================================================================

    // Dark background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.0f, 0.0f, 0.0f, 1.0f);

    // Composite 3D render over background
    auto& comp3d = chain.add<Composite>("comp3d");
    comp3d.inputA("bg");
    comp3d.inputB("render3d");
    comp3d.mode = BlendMode::Over;

    // =========================================================================
    // BOLD 2D EFFECTS - Hard-edged rectangular bars (using loops)
    // =========================================================================

    // --- BACKGROUND BARS (behind asteroid, triggered by kick) ---
    std::vector<std::string> bgBarMaskedNames;
    float bgBarYPositions[] = {0.25f, 0.75f};
    for (int i = 0; i < 2; i++) {
        std::string name = "bar_bg_" + std::to_string(i);
        auto& bar = chain.add<Shape>(name);
        bar.type = ShapeType::Rectangle;
        bar.position.set(0.5f, bgBarYPositions[i]);
        bar.size.set(0.5f, 0.06f);
        bar.color.set(1.0f, 1.0f, 1.0f, 0.0f);
        bar.softness = 0.0f;

        std::string maskedName = name + "_masked";
        auto& masked = chain.add<DepthMask>(maskedName);
        masked.input(name);
        masked.setRender3D(&render3d);
        masked.mode(DepthMaskMode::Background);
        masked.softness = 0.05f;

        bgBarMaskedNames.push_back(maskedName);
    }

    // --- INSIDE BARS (cropped to asteroid silhouette, triggered by snare) ---
    std::vector<std::string> insideBarMaskedNames;
    float insideBarYPositions[] = {0.45f, 0.55f, 0.65f};
    for (int i = 0; i < 3; i++) {
        std::string name = "bar_inside_" + std::to_string(i);
        auto& bar = chain.add<Shape>(name);
        bar.type = ShapeType::Rectangle;
        bar.position.set(0.5f, insideBarYPositions[i]);
        bar.size.set(0.6f, 0.04f);
        bar.color.set(1.0f, 0.15f, 0.1f, 0.0f);
        bar.softness = 0.0f;

        std::string maskedName = name + "_masked";
        auto& masked = chain.add<DepthMask>(maskedName);
        masked.input(name);
        masked.setRender3D(&render3d);
        masked.mode(DepthMaskMode::Object);
        masked.softness = 0.05f;

        insideBarMaskedNames.push_back(maskedName);
    }

    // INVERSION FLASH - white solid for color inversion effect (clap)
    auto& invert_flash = chain.add<SolidColor>("invert_flash");
    invert_flash.color.set(1.0f, 1.0f, 1.0f, 0.0f);

    // RING OUTLINE - bold circular ring (hi-hat)
    auto& ring_outline = chain.add<Shape>("ring_outline");
    ring_outline.type = ShapeType::Ellipse;
    ring_outline.position.set(0.5f, 0.5f);
    ring_outline.size.set(0.45f, 0.45f);
    ring_outline.thickness = 0.015f;
    ring_outline.color.set(0.2f, 0.2f, 0.2f, 0.0f);
    ring_outline.softness = 0.0f;

    // --- COMPOSITE CHAIN (built dynamically) ---
    std::string lastComp = "comp3d";
    int compIdx = 1;

    // Composite background bars (behind asteroid)
    for (const auto& barName : bgBarMaskedNames) {
        std::string compName = "comp" + std::to_string(compIdx++);
        auto& comp = chain.add<Composite>(compName);
        comp.inputA(lastComp);
        comp.inputB(barName);
        comp.mode = BlendMode::Add;
        lastComp = compName;
    }

    // Composite inside bars (cropped to asteroid)
    for (const auto& barName : insideBarMaskedNames) {
        std::string compName = "comp" + std::to_string(compIdx++);
        auto& comp = chain.add<Composite>(compName);
        comp.inputA(lastComp);
        comp.inputB(barName);
        comp.mode = BlendMode::Add;
        lastComp = compName;
    }

    // Ring outline
    {
        std::string compName = "comp" + std::to_string(compIdx++);
        auto& comp = chain.add<Composite>(compName);
        comp.inputA(lastComp);
        comp.inputB("ring_outline");
        comp.mode = BlendMode::Over;
        lastComp = compName;
    }

    // Inversion flash
    {
        std::string compName = "comp" + std::to_string(compIdx++);
        auto& comp = chain.add<Composite>(compName);
        comp.inputA(lastComp);
        comp.inputB("invert_flash");
        comp.mode = BlendMode::Add;
        lastComp = compName;
    }

    // Final bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(lastComp);
    bloom.threshold = 0.4f;
    bloom.intensity = 1.8f;
    bloom.radius = 30.0f;

    chain.output("bloom");
}

// Visual decay values
static float kickDecay = 0.0f;
static float snareDecay = 0.0f;
static float hihatDecay = 0.0f;
static float clapDecay = 0.0f;

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Get operators for runtime control and visualization
    auto& clock = chain.get<Clock>("clock");
    auto& kick_seq = chain.get<Sequencer>("kick_seq");
    auto& snare_seq = chain.get<Sequencer>("snare_seq");
    auto& hihat_seq = chain.get<Sequencer>("hihat_seq");
    auto& clap_seq = chain.get<Sequencer>("clap_seq");

    // Mouse Y controls swing
    float mouseY = ctx.mouseNorm().y;  // 0-1
    clock.swing = mouseY * 0.5f;

    // Audio-thread triggering: sequencers and drums trigger automatically via setTriggerSource()
    // We just poll triggered() for visual feedback
    if (kick_seq.triggered()) kickDecay = 1.0f;
    if (snare_seq.triggered()) snareDecay = 1.0f;
    if (hihat_seq.triggered()) hihatDecay = 1.0f;
    if (clap_seq.triggered()) clapDecay = 1.0f;

    // Visual feedback - decay values (slower decay = effects stay visible longer)
    float decayRate = 1.0f - ctx.dt() * 4.0f;  // Slower decay
    kickDecay *= decayRate;
    snareDecay *= decayRate;
    hihatDecay *= decayRate;
    clapDecay *= decayRate;

    // =========================================================================
    // 3D ANIMATION
    // =========================================================================

    // Camera slowly orbits
    auto& camera = chain.get<CameraOperator>("camera");
    camera.azimuth(t * 0.15f);
    camera.elevation(0.25f + 0.1f * std::sin(t * 0.3f));
    camera.distance(4.5f + kickDecay * 0.5f);  // Zoom in on kick

    // Asteroid rotation and pulsing
    auto& scene = chain.get<SceneComposer>("scene");
    auto& asteroid = chain.get<Sphere>("asteroid");
    auto& entries = scene.entries();

    // Asteroid: slow rotation + scale pulse on kick
    float asteroidScale = 1.0f + kickDecay * 0.15f;
    entries[0].transform = glm::rotate(glm::mat4(1.0f), t * 0.1f, glm::vec3(0, 1, 0)) *
                          glm::rotate(glm::mat4(1.0f), 0.3f, glm::vec3(1, 0, 0)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(asteroidScale));
    // Asteroid color: brighter on kick
    entries[0].color = glm::vec4(0.6f + kickDecay * 0.4f,
                                  0.3f + kickDecay * 0.3f,
                                  0.15f + kickDecay * 0.1f,
                                  1.0f);

    // Update asteroid noise time for animation
    asteroid.noiseTime(t * 0.5f);

    // Ring: rotate opposite direction, pulse on snare
    float ringScale = 1.0f + snareDecay * 0.1f;
    entries[1].transform = glm::rotate(glm::mat4(1.0f), 1.57f, glm::vec3(1, 0, 0)) *
                          glm::rotate(glm::mat4(1.0f), -t * 0.2f, glm::vec3(0, 0, 1)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(ringScale));
    // Ring color: brighter on snare
    entries[1].color = glm::vec4(0.8f + snareDecay * 0.2f,
                                  0.5f + snareDecay * 0.3f,
                                  0.2f + snareDecay * 0.2f,
                                  1.0f);

    // Rim light intensity pulses with beat
    auto& rimLight = chain.get<DirectionalLight>("rimLight");
    rimLight.intensity = 1.5f + kickDecay * 2.0f;

    // =========================================================================
    // 2D EFFECT ANIMATION - Bold rectangular bars
    // =========================================================================

    // BACKGROUND BARS (kick) - BEHIND asteroid
    for (int i = 0; i < 2; i++) {
        auto& bar = chain.get<Shape>("bar_bg_" + std::to_string(i));
        bar.color.set(1.0f, 1.0f, 1.0f, kickDecay);
    }

    // INSIDE BARS (snare) - INSIDE asteroid, cropped by edges
    for (int i = 0; i < 3; i++) {
        auto& bar = chain.get<Shape>("bar_inside_" + std::to_string(i));
        bar.color.set(1.0f, 0.15f, 0.1f, snareDecay);
    }

    // RING OUTLINE (hi-hat) - appears on hi-hat, size pulses
    auto& ring_outline = chain.get<Shape>("ring_outline");
    float ringOutlineSize = 0.4f + hihatDecay * 0.15f;
    ring_outline.size.set(ringOutlineSize, ringOutlineSize);
    ring_outline.color.set(0.3f, 0.3f, 0.3f, hihatDecay * 0.9f);

    // INVERSION FLASH (clap) - full screen white flash
    auto& invert_flash = chain.get<SolidColor>("invert_flash");
    invert_flash.color.set(1.0f, 1.0f, 1.0f, clapDecay * 0.8f);
}

VIVID_CHAIN(setup, update)
