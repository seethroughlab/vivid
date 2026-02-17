// Textured PBR Demo - Showcases all PBR materials in a grid
// Each sphere uses a different material from assets/materials
// Press SPACE to cycle through close-up views of each material
// Press 1/2/3 to change rotation axis (X/Y/Z)

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <vivid/render3d/ibl_environment.h>
#include <GLFW/glfw3.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// State for close-up mode
static bool closeupMode = false;
static int currentSphereIndex = 0;
static float rotationAngle = 0.0f;
static const int NUM_SPHERES = 19;
static glm::vec3 rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);  // Default: X axis

// Grid layout parameters
static const float spacing = 2.5f;
static const int cols = 5;
static const float startX = -spacing * (cols - 1) / 2.0f;
static const float startZ = -spacing * 1.5f;

// Get sphere position by index
glm::vec3 getSpherePosition(int index) {
    int col = index % cols;
    int row = index / cols;
    return glm::vec3(startX + col * spacing, 0.0f, startZ + row * spacing);
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // IBL environment (now a proper chain operator)
    auto& ibl = chain.add<IBLEnvironment>("ibl");
    ibl.setHdrFile("tests/assets/hdris/bryanston_park_sunrise_4k.hdr");

    // Create scene composer
    auto& scene = SceneComposer::create(chain, "scene");

    // Helper lambda to create sphere with material
    int index = 0;
    auto addMaterialSphere = [&](const std::string& name, TexturedMaterial& mat) {
        glm::vec3 pos = getSpherePosition(index);

        auto& s = scene.add<Sphere>(name + "_sphere",
            glm::translate(glm::mat4(1.0f), pos));
        s.radius(1.0f);
        s.segments(48);
        s.computeTangents();

        // Assign material to the last entry
        scene.entries().back().material = &mat;
        index++;
    };

    // 1. Alien Panels
    auto& alienPanels = chain.add<TexturedMaterial>("alienPanels");
    alienPanels.baseColor("tests/assets/materials/alien-panels-bl/alien-panels_albedo.png");
    alienPanels.normal("tests/assets/materials/alien-panels-bl/alien-panels_normal-ogl.png");
    alienPanels.metallic("tests/assets/materials/alien-panels-bl/alien-panels_metallic.png");
    alienPanels.roughness("tests/assets/materials/alien-panels-bl/alien-panels_roughness.png");
    alienPanels.ao("tests/assets/materials/alien-panels-bl/alien-panels_ao.png");
    addMaterialSphere("alienPanels", alienPanels);

    // 2. Bronze
    auto& bronze = chain.add<TexturedMaterial>("bronze");
    bronze.baseColor("tests/assets/materials/bronze-bl/bronze_albedo.png");
    bronze.normal("tests/assets/materials/bronze-bl/bronze_normal-ogl.png");
    bronze.metallic("tests/assets/materials/bronze-bl/bronze_metallic.png");
    bronze.roughness("tests/assets/materials/bronze-bl/bronze_roughness.png");
    bronze.ao("tests/assets/materials/bronze-bl/bronze_ao.png");
    addMaterialSphere("bronze", bronze);

    // 3. Cheap Plywood
    auto& cheapPlywood = chain.add<TexturedMaterial>("cheapPlywood");
    cheapPlywood.baseColor("tests/assets/materials/cheap-plywood1-bl/cheap_plywood1r_albedo.png");
    cheapPlywood.normal("tests/assets/materials/cheap-plywood1-bl/cheap_plywood1r_Normal-ogl.png");
    cheapPlywood.metallic("tests/assets/materials/cheap-plywood1-bl/cheap_plywood1r_Metallic.png");
    cheapPlywood.roughness("tests/assets/materials/cheap-plywood1-bl/cheap_plywood1r_Roughness.png");
    cheapPlywood.ao("tests/assets/materials/cheap-plywood1-bl/cheap_plywood1r_ao.png");
    addMaterialSphere("cheapPlywood", cheapPlywood);

    // 4. Corkboard
    auto& corkboard = chain.add<TexturedMaterial>("corkboard");
    corkboard.baseColor("tests/assets/materials/corkboard3b-bl/corkboard3b-albedo.png");
    corkboard.normal("tests/assets/materials/corkboard3b-bl/corkboard3b-normal.png");
    corkboard.metallic("tests/assets/materials/corkboard3b-bl/corkboard3b-metalness.png");
    corkboard.roughness("tests/assets/materials/corkboard3b-bl/corkboard3b-roughnness.png");
    corkboard.ao("tests/assets/materials/corkboard3b-bl/corkboard3b-ao.png");
    addMaterialSphere("corkboard", corkboard);

    // 5. Cracking Painted Asphalt
    auto& asphalt = chain.add<TexturedMaterial>("asphalt");
    asphalt.baseColor("tests/assets/materials/cracking-painted-asphalt1-bl/cracking_painted_asphalt_albedo.png");
    asphalt.normal("tests/assets/materials/cracking-painted-asphalt1-bl/cracking_painted_asphalt_Normal-ogl.png");
    asphalt.metallic("tests/assets/materials/cracking-painted-asphalt1-bl/cracking_painted_asphalt_Metallic.png");
    asphalt.roughness("tests/assets/materials/cracking-painted-asphalt1-bl/cracking_painted_asphalt_Roughness.png");
    asphalt.ao("tests/assets/materials/cracking-painted-asphalt1-bl/cracking_painted_asphalt_ao.png");
    addMaterialSphere("asphalt", asphalt);

    // 6. Futuristic Hex Armor
    auto& hexArmor = chain.add<TexturedMaterial>("hexArmor");
    hexArmor.baseColor("tests/assets/materials/futuristic-hex-armor-bl/futuristic-hex-armor_albedo.png");
    hexArmor.normal("tests/assets/materials/futuristic-hex-armor-bl/futuristic-hex-armor_normal-ogl.png");
    hexArmor.metallic("tests/assets/materials/futuristic-hex-armor-bl/futuristic-hex-armor_metallic.png");
    hexArmor.roughness("tests/assets/materials/futuristic-hex-armor-bl/futuristic-hex-armor_roughness.png");
    hexArmor.ao("tests/assets/materials/futuristic-hex-armor-bl/futuristic-hex-armor_ao.png");
    addMaterialSphere("hexArmor", hexArmor);

    // 7. Hammered Gold
    auto& hammeredGold = chain.add<TexturedMaterial>("hammeredGold");
    hammeredGold.baseColor("tests/assets/materials/hammered-gold-bl/hammered-gold_albedo.png");
    hammeredGold.normal("tests/assets/materials/hammered-gold-bl/hammered-gold_normal-ogl.png");
    hammeredGold.metallic("tests/assets/materials/hammered-gold-bl/hammered-gold_metallic.png");
    hammeredGold.roughness("tests/assets/materials/hammered-gold-bl/hammered-gold_roughness.png");
    hammeredGold.ao("tests/assets/materials/hammered-gold-bl/hammered-gold_ao.png");
    addMaterialSphere("hammeredGold", hammeredGold);

    // 8. Hexagon Pavers
    auto& hexPavers = chain.add<TexturedMaterial>("hexPavers");
    hexPavers.baseColor("tests/assets/materials/hexagon-pavers1-bl/hexagon-pavers1_albedo.png");
    hexPavers.normal("tests/assets/materials/hexagon-pavers1-bl/hexagon-pavers1_normal-ogl.png");
    hexPavers.metallic("tests/assets/materials/hexagon-pavers1-bl/hexagon-pavers1_metallic.png");
    hexPavers.roughness("tests/assets/materials/hexagon-pavers1-bl/hexagon-pavers1_roughness.png");
    hexPavers.ao("tests/assets/materials/hexagon-pavers1-bl/hexagon-pavers1_ao.png");
    addMaterialSphere("hexPavers", hexPavers);

    // 9. Metal Roof
    auto& metalRoof = chain.add<TexturedMaterial>("metalRoof");
    metalRoof.baseColor("tests/assets/materials/metal-roof-bl/metal-roof_albedo.png");
    metalRoof.normal("tests/assets/materials/metal-roof-bl/metal-roof_normal-ogl.png");
    metalRoof.metallic("tests/assets/materials/metal-roof-bl/metal-roof_metallic.png");
    metalRoof.roughness("tests/assets/materials/metal-roof-bl/metal-roof_roughness.png");
    metalRoof.ao("tests/assets/materials/metal-roof-bl/metal-roof_ao.png");
    addMaterialSphere("metalRoof", metalRoof);

    // 10. Metal Splotchy (no AO map)
    auto& metalSplotchy = chain.add<TexturedMaterial>("metalSplotchy");
    metalSplotchy.baseColor("tests/assets/materials/metal-slpotchy-bl/metal-splotchy-albedo.png");
    metalSplotchy.normal("tests/assets/materials/metal-slpotchy-bl/metal-splotchy-normal-ogl.png");
    metalSplotchy.metallic("tests/assets/materials/metal-slpotchy-bl/metal-splotchy-metal.png");
    metalSplotchy.roughness("tests/assets/materials/metal-slpotchy-bl/metal-splotchy-rough.png");
    addMaterialSphere("metalSplotchy", metalSplotchy);

    // 11. Oily Tubework
    auto& oilyTubework = chain.add<TexturedMaterial>("oilyTubework");
    oilyTubework.baseColor("tests/assets/materials/oily-tubework-bl/oily-tubework_albedo.png");
    oilyTubework.normal("tests/assets/materials/oily-tubework-bl/oily-tubework_normal-ogl.png");
    oilyTubework.metallic("tests/assets/materials/oily-tubework-bl/oily-tubework_metallic.png");
    oilyTubework.roughness("tests/assets/materials/oily-tubework-bl/oily-tubework_roughness.png");
    oilyTubework.ao("tests/assets/materials/oily-tubework-bl/oily-tubework_ao.png");
    addMaterialSphere("oilyTubework", oilyTubework);

    // 12. Plywood
    auto& plywood = chain.add<TexturedMaterial>("plywood");
    plywood.baseColor("tests/assets/materials/plywood1-bl/plywood-albedo2b.png");
    plywood.normal("tests/assets/materials/plywood1-bl/plywood-normal1.png");
    plywood.metallic("tests/assets/materials/plywood1-bl/plywood-metalness.png");
    plywood.roughness("tests/assets/materials/plywood1-bl/plywood-rough.png");
    plywood.ao("tests/assets/materials/plywood1-bl/plywood-ao.png");
    addMaterialSphere("plywood", plywood);

    // 13. Rough Rock Face
    auto& rockFace = chain.add<TexturedMaterial>("rockFace");
    rockFace.baseColor("tests/assets/materials/roughrockface2-bl/roughrockface2_Base_Color.png");
    rockFace.normal("tests/assets/materials/roughrockface2-bl/roughrockface2_Normal.png");
    rockFace.metallic("tests/assets/materials/roughrockface2-bl/roughrockface2_Metallic.png");
    rockFace.roughness("tests/assets/materials/roughrockface2-bl/roughrockface2_Roughness.png");
    rockFace.ao("tests/assets/materials/roughrockface2-bl/roughrockface2_Ambient_Occlusion.png");
    addMaterialSphere("rockFace", rockFace);

    // 14. Speckled Granite Tiles
    auto& granite = chain.add<TexturedMaterial>("granite");
    granite.baseColor("tests/assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_albedo.png");
    granite.normal("tests/assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_normal-ogl.png");
    granite.metallic("tests/assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_metallic.png");
    granite.roughness("tests/assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_roughness.png");
    granite.ao("tests/assets/materials/speckled-granite-tiles-bl/speckled-granite-tiles_ao.png");
    addMaterialSphere("granite", granite);

    // 15. Square Damp Blocks
    auto& dampBlocks = chain.add<TexturedMaterial>("dampBlocks");
    dampBlocks.baseColor("tests/assets/materials/square-damp-blocks-bl/square-damp-blocks_albedo.png");
    dampBlocks.normal("tests/assets/materials/square-damp-blocks-bl/square-damp-blocks_normal-ogl.png");
    dampBlocks.metallic("tests/assets/materials/square-damp-blocks-bl/square-damp-blocks_metallic.png");
    dampBlocks.roughness("tests/assets/materials/square-damp-blocks-bl/square-damp-blocks_roughness.png");
    dampBlocks.ao("tests/assets/materials/square-damp-blocks-bl/square-damp-blocks_ao.png");
    addMaterialSphere("dampBlocks", dampBlocks);

    // 16. Titanium Scuffed (no AO map)
    auto& titanium = chain.add<TexturedMaterial>("titanium");
    titanium.baseColor("tests/assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_basecolor.png");
    titanium.normal("tests/assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_normal.png");
    titanium.metallic("tests/assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_metallic.png");
    titanium.roughness("tests/assets/materials/Titanium-Scuffed-bl/Titanium-Scuffed_roughness.png");
    addMaterialSphere("titanium", titanium);

    // 17. Wispy Grass Meadow
    auto& grass = chain.add<TexturedMaterial>("grass");
    grass.baseColor("tests/assets/materials/whispy-grass-meadow-bl/wispy-grass-meadow_albedo.png");
    grass.normal("tests/assets/materials/whispy-grass-meadow-bl/wispy-grass-meadow_normal-ogl.png");
    grass.metallic("tests/assets/materials/whispy-grass-meadow-bl/wispy-grass-meadow_metallic.png");
    grass.roughness("tests/assets/materials/whispy-grass-meadow-bl/wispy-grass-meadow_roughness.png");
    grass.ao("tests/assets/materials/whispy-grass-meadow-bl/wispy-grass-meadow_ao.png");
    addMaterialSphere("grass", grass);

    // 18. Worn Rusted Painted
    auto& rustedPainted = chain.add<TexturedMaterial>("rustedPainted");
    rustedPainted.baseColor("tests/assets/materials/worn-rusted-painted-bl/worn-rusted-painted_albedo.png");
    rustedPainted.normal("tests/assets/materials/worn-rusted-painted-bl/worn-rusted-painted_normal-ogl.png");
    rustedPainted.metallic("tests/assets/materials/worn-rusted-painted-bl/worn-rusted-painted_metallic.png");
    rustedPainted.roughness("tests/assets/materials/worn-rusted-painted-bl/worn-rusted-painted_roughness.png");
    rustedPainted.ao("tests/assets/materials/worn-rusted-painted-bl/worn-rusted-painted_ao.png");
    addMaterialSphere("rustedPainted", rustedPainted);

    // 19. Worn Shiny Metal
    auto& shinyMetal = chain.add<TexturedMaterial>("shinyMetal");
    shinyMetal.baseColor("tests/assets/materials/worn-shiny-metal-bl/worn-shiny-metal-albedo.png");
    shinyMetal.normal("tests/assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Normal-ogl.png");
    shinyMetal.metallic("tests/assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Metallic.png");
    shinyMetal.roughness("tests/assets/materials/worn-shiny-metal-bl/worn-shiny-metal-Roughness.png");
    shinyMetal.ao("tests/assets/materials/worn-shiny-metal-bl/worn-shiny-metal-ao.png");
    addMaterialSphere("shinyMetal", shinyMetal);

    // Camera - starts with overview of grid (azimuth 0 = looking straight at the grid)
    auto& camera = chain.add<CameraOperator>("camera");
    camera.distance(14.0f);
    camera.elevation(35.0f);
    camera.azimuth(0.0f);
    camera.target(0.0f, 0.0f, 1.25f);
    camera.fov(50.0f);

    // Directional light from top-right-front
    auto& light = chain.add<DirectionalLight>("sun");
    light.direction(1.0f, 1.5f, 1.0f);
    light.color(1.0f, 0.98f, 0.95f);
    light.intensity = 2.0f;

    // Render with PBR + IBL + skybox
    auto& render = chain.add<Render3D>("render");
    render.input("scene");
    render.setCameraInput(&camera);
    render.setLightInput(&light);
    render.setShadingMode(ShadingMode::PBR);
    render.setEnvironmentInput(&ibl);
    render.setIbl(true);
    render.setShowSkybox(true);
    render.setAmbient(1.0f);
    render.setClearColor(0.08f, 0.08f, 0.1f);

    chain.output("render");
}

void update(Context& ctx) {
    float dt = static_cast<float>(ctx.dt());

    // Update rotation angle
    rotationAngle += dt * 0.5f;  // Slow rotation

    // Handle space bar - cycle through close-ups
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        if (!closeupMode) {
            // Enter close-up mode starting at first sphere
            closeupMode = true;
            currentSphereIndex = 0;
        } else {
            // Cycle to next sphere, or exit if we've seen them all
            currentSphereIndex++;
            if (currentSphereIndex >= NUM_SPHERES) {
                closeupMode = false;
                currentSphereIndex = 0;
            }
        }
    }

    // Handle 1/2/3 keys - change rotation axis
    if (ctx.key(GLFW_KEY_1).pressed) {
        rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);  // X axis
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);  // Y axis
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        rotationAxis = glm::vec3(0.0f, 0.0f, 1.0f);  // Z axis
    }

    // Update sphere transforms (rotation around selected axis)
    auto& scene = ctx.chain().get<SceneComposer>("scene");
    auto& entries = scene.entries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        glm::vec3 pos = getSpherePosition(i);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), pos);
        transform = glm::rotate(transform, rotationAngle, rotationAxis);
        entries[i].transform = transform;
    }

    // Update camera based on mode
    auto& camera = ctx.chain().get<CameraOperator>("camera");
    if (closeupMode) {
        // Close-up view of current sphere
        glm::vec3 spherePos = getSpherePosition(currentSphereIndex);
        camera.target(spherePos.x, spherePos.y, spherePos.z);
        camera.distance(3.0f);
        camera.elevation(15.0f);
        camera.fov(45.0f);
    } else {
        // Overview of entire grid
        camera.target(0.0f, 0.0f, 1.25f);
        camera.distance(14.0f);
        camera.elevation(35.0f);
        camera.azimuth(0.0f);
        camera.fov(50.0f);
    }
}

VIVID_CHAIN(setup, update)
