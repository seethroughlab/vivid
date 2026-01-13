// GrassMesh - Procedural grass blade mesh generator
// Generates grass blade geometry for GPU instancing
// Rendering is handled by Render3D for unified shadows and lighting

#include <vivid/render3d/grass_mesh.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>

namespace vivid::render3d {

REGISTER_OPERATOR(GrassMesh, "3D Vegetation", "Procedural grass blade mesh generator", false);

namespace {

constexpr int BLADE_SEGMENTS = 4;  // Segments per blade for smooth bending

} // namespace

GrassMesh::GrassMesh() {
    // Parameters are exposed via params(), getParam(), setParam()
}

GrassMesh::~GrassMesh() = default;

WindParams GrassMesh::getWindParams() const {
    WindParams params;
    params.strength = static_cast<float>(windStrength);
    params.speed = static_cast<float>(windSpeed);

    glm::vec2 dir(static_cast<float>(windDirX), static_cast<float>(windDirZ));
    if (glm::length(dir) > 0.001f) {
        dir = glm::normalize(dir);
    }
    params.direction = dir;

    // Grass has minimal stem curve (mostly vertical)
    params.stemCurve = 0.2f;
    params.stemLength = static_cast<float>(bladeHeight);

    return params;
}

void GrassMesh::createBladeMesh() {
    m_bladeMesh.vertices.clear();
    m_bladeMesh.indices.clear();

    float height = 1.0f;  // Normalized, actual height via instance scale
    float width = static_cast<float>(bladeWidth);

    // Create tapered blade with BLADE_SEGMENTS
    for (int i = 0; i <= BLADE_SEGMENTS; i++) {
        float t = static_cast<float>(i) / BLADE_SEGMENTS;
        float y = t * height;
        float w = width * (1.0f - t * 0.8f);  // Taper toward tip

        // Normal points outward (toward camera typically)
        glm::vec3 normal(0.0f, 0.0f, 1.0f);

        // Left vertex
        Vertex3D left;
        left.position = glm::vec3(-w * 0.5f, y, 0.0f);
        left.normal = normal;
        left.uv = glm::vec2(0.0f, t);
        left.color = glm::vec4(1.0f);
        m_bladeMesh.vertices.push_back(left);

        // Right vertex
        Vertex3D right;
        right.position = glm::vec3(w * 0.5f, y, 0.0f);
        right.normal = normal;
        right.uv = glm::vec2(1.0f, t);
        right.color = glm::vec4(1.0f);
        m_bladeMesh.vertices.push_back(right);
    }

    // Create triangles
    for (int i = 0; i < BLADE_SEGMENTS; i++) {
        uint32_t base = i * 2;
        // First triangle
        m_bladeMesh.indices.push_back(base);
        m_bladeMesh.indices.push_back(base + 1);
        m_bladeMesh.indices.push_back(base + 2);
        // Second triangle
        m_bladeMesh.indices.push_back(base + 1);
        m_bladeMesh.indices.push_back(base + 3);
        m_bladeMesh.indices.push_back(base + 2);
    }
}

void GrassMesh::generateInstances() {
    int count = static_cast<int>(bladeCount);
    int currentSeed = static_cast<int>(seed);
    float currentFieldWidth = static_cast<float>(fieldWidth);
    float currentFieldDepth = static_cast<float>(fieldDepth);
    float currentBladeHeight = static_cast<float>(bladeHeight);
    float currentHeightVariation = static_cast<float>(heightVariation);

    // Only regenerate if parameters changed
    bool paramsChanged = (count != m_lastBladeCount ||
                          currentSeed != m_lastSeed ||
                          currentFieldWidth != m_lastFieldWidth ||
                          currentFieldDepth != m_lastFieldDepth ||
                          currentBladeHeight != m_lastBladeHeight ||
                          currentHeightVariation != m_lastHeightVariation);

    if (!paramsChanged && !m_instances.empty()) {
        return;
    }

    m_lastBladeCount = count;
    m_lastSeed = currentSeed;
    m_lastFieldWidth = currentFieldWidth;
    m_lastFieldDepth = currentFieldDepth;
    m_lastBladeHeight = currentBladeHeight;
    m_lastHeightVariation = currentHeightVariation;

    m_instances.clear();
    m_instances.reserve(count);

    std::mt19937 rng(currentSeed);
    std::uniform_real_distribution<float> distX(-currentFieldWidth * 0.5f,
                                                 currentFieldWidth * 0.5f);
    std::uniform_real_distribution<float> distZ(-currentFieldDepth * 0.5f,
                                                 currentFieldDepth * 0.5f);
    std::uniform_real_distribution<float> distRot(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> distHeight(1.0f - currentHeightVariation,
                                                     1.0f + currentHeightVariation);
    std::uniform_real_distribution<float> distPhase(0.0f, 1.0f);
    std::uniform_real_distribution<float> distColorVar(0.85f, 1.15f);

    for (int i = 0; i < count; i++) {
        ProceduralInstance inst;

        // Random position
        float x = distX(rng);
        float z = distZ(rng);

        // Random rotation around Y axis
        float rot = distRot(rng);

        // Random height scale
        float heightScale = distHeight(rng) * currentBladeHeight;

        // Build transform: translate, rotate, scale
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z));
        transform = glm::rotate(transform, rot, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::scale(transform, glm::vec3(1.0f, heightScale, 1.0f));
        inst.transform = transform;

        // Variation: xyz for scale variation, w for phase offset
        float phase = distPhase(rng);
        inst.variation = glm::vec4(1.0f, 1.0f, 1.0f, phase);

        // Color variation
        float colorVar = distColorVar(rng);
        inst.color = glm::vec4(colorVar, colorVar, colorVar, 1.0f);

        m_instances.push_back(inst);
    }

    m_instancesDirty = true;
}

void GrassMesh::init(Context& ctx) {
    if (m_initialized) return;

    createBladeMesh();
    generateInstances();

    m_initialized = true;
}

void GrassMesh::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Check if blade width changed (needs mesh regen)
    // Note: blade mesh uses normalized height, so only width affects mesh

    // Regenerate instances if needed
    generateInstances();

    didCook();
}

std::vector<ParamDecl> GrassMesh::params() {
    return {
        fieldWidth.decl(),
        fieldDepth.decl(),
        bladeCount.decl(),
        seed.decl(),
        bladeHeight.decl(),
        bladeWidth.decl(),
        heightVariation.decl(),
        windStrength.decl(),
        windSpeed.decl(),
        windDirX.decl(),
        windDirZ.decl()
    };
}

bool GrassMesh::getParam(const std::string& name, float out[4]) {
    if (name == "fieldWidth") { out[0] = static_cast<float>(fieldWidth); return true; }
    if (name == "fieldDepth") { out[0] = static_cast<float>(fieldDepth); return true; }
    if (name == "bladeCount") { out[0] = static_cast<float>(static_cast<int>(bladeCount)); return true; }
    if (name == "seed") { out[0] = static_cast<float>(static_cast<int>(seed)); return true; }
    if (name == "bladeHeight") { out[0] = static_cast<float>(bladeHeight); return true; }
    if (name == "bladeWidth") { out[0] = static_cast<float>(bladeWidth); return true; }
    if (name == "heightVariation") { out[0] = static_cast<float>(heightVariation); return true; }
    if (name == "windStrength") { out[0] = static_cast<float>(windStrength); return true; }
    if (name == "windSpeed") { out[0] = static_cast<float>(windSpeed); return true; }
    if (name == "windDirX") { out[0] = static_cast<float>(windDirX); return true; }
    if (name == "windDirZ") { out[0] = static_cast<float>(windDirZ); return true; }
    if (name == "baseColor") { out[0] = baseColor[0]; out[1] = baseColor[1]; out[2] = baseColor[2]; return true; }
    if (name == "tipColor") { out[0] = tipColor[0]; out[1] = tipColor[1]; out[2] = tipColor[2]; return true; }
    return false;
}

bool GrassMesh::setParam(const std::string& name, const float value[4]) {
    if (name == "fieldWidth") { fieldWidth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "fieldDepth") { fieldDepth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "bladeCount") { bladeCount = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "seed") { seed = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "bladeHeight") { bladeHeight = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "bladeWidth") { bladeWidth = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "heightVariation") { heightVariation = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "windStrength") { windStrength = value[0]; markDirty(); return true; }
    if (name == "windSpeed") { windSpeed = value[0]; markDirty(); return true; }
    if (name == "windDirX") { windDirX = value[0]; markDirty(); return true; }
    if (name == "windDirZ") { windDirZ = value[0]; markDirty(); return true; }
    if (name == "baseColor") { baseColor[0] = value[0]; baseColor[1] = value[1]; baseColor[2] = value[2]; markDirty(); return true; }
    if (name == "tipColor") { tipColor[0] = value[0]; tipColor[1] = value[1]; tipColor[2] = value[2]; markDirty(); return true; }
    return false;
}

} // namespace vivid::render3d
