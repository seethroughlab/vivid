// FoliageMesh - Procedural frond mesh generator
// Generates plant geometry with curved stems and tapered leaflets
// Rendering is handled by Render3D for unified shadows and lighting

#include <vivid/render3d/foliage_mesh.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>

namespace vivid::render3d {

REGISTER_OPERATOR(FoliageMesh, "3D Vegetation", "Procedural frond mesh generator", false);

namespace {

// Evaluate quadratic bezier curve for stem shape
glm::vec3 evalStemCurve(float t, float length, float curve) {
    // Start at origin, curve outward and down
    float y = length * t * (1.0f - curve * t);  // Parabolic droop
    float z = length * t;  // Forward extension (will be rotated per-instance)
    return glm::vec3(0.0f, y, z);
}

// Get tangent direction along stem
glm::vec3 evalStemTangent(float t, float length, float curve) {
    float dy = length * (1.0f - 2.0f * curve * t);
    float dz = length;
    return glm::normalize(glm::vec3(0.0f, dy, dz));
}

} // namespace

FoliageMesh::FoliageMesh() {
    // Parameters are exposed via params(), getParam(), setParam()
}

FoliageMesh::~FoliageMesh() = default;

void FoliageMesh::setPlantType(PlantType type) {
    if (m_plantType != type) {
        m_plantType = type;
        applyPreset(type);
        m_meshDirty = true;
        m_instancesDirty = true;
        markDirty();
    }
}

void FoliageMesh::applyPreset(PlantType type) {
    switch (type) {
        case PlantType::Fern:
            stemLength = 0.8f;
            stemCurve = 0.35f;
            leafletPairs = 10;
            leafletWidth = 0.08f;
            leafletLength = 0.2f;
            leafletAngle = 50.0f;
            windStrength = 0.35f;
            windSpeed = 1.0f;
            baseColor[0] = 0.06f; baseColor[1] = 0.15f; baseColor[2] = 0.03f;
            tipColor[0] = 0.12f; tipColor[1] = 0.3f; tipColor[2] = 0.06f;
            break;

        case PlantType::PalmFrond:
            stemLength = 1.5f;
            stemCurve = 0.5f;
            leafletPairs = 12;
            leafletWidth = 0.06f;
            leafletLength = 0.4f;
            leafletAngle = 35.0f;
            windStrength = 0.25f;
            windSpeed = 0.6f;
            baseColor[0] = 0.04f; baseColor[1] = 0.12f; baseColor[2] = 0.02f;
            tipColor[0] = 0.08f; tipColor[1] = 0.22f; tipColor[2] = 0.04f;
            break;

        case PlantType::Grass:
            stemLength = 0.5f;
            stemCurve = 0.2f;
            leafletPairs = 0;  // No leaflets, just stem
            leafletWidth = 0.03f;
            leafletLength = 0.0f;
            leafletAngle = 0.0f;
            windStrength = 0.5f;
            windSpeed = 1.2f;
            baseColor[0] = 0.1f; baseColor[1] = 0.25f; baseColor[2] = 0.05f;
            tipColor[0] = 0.2f; tipColor[1] = 0.4f; tipColor[2] = 0.1f;
            break;

        case PlantType::Custom:
            break;
    }
}

WindParams FoliageMesh::getWindParams() const {
    WindParams params;
    params.strength = static_cast<float>(windStrength);
    params.speed = static_cast<float>(windSpeed);

    glm::vec2 dir(static_cast<float>(windDirX), static_cast<float>(windDirZ));
    if (glm::length(dir) > 0.001f) {
        dir = glm::normalize(dir);
    }
    params.direction = dir;

    params.stemCurve = static_cast<float>(stemCurve);
    params.stemLength = static_cast<float>(stemLength);

    return params;
}

void FoliageMesh::generateFrondMesh() {
    m_frondMesh.vertices.clear();
    m_frondMesh.indices.clear();

    float length = static_cast<float>(stemLength);
    float curve = static_cast<float>(stemCurve);
    int pairs = static_cast<int>(leafletPairs);
    float lWidth = static_cast<float>(leafletWidth);
    float lLength = static_cast<float>(leafletLength);
    float lAngle = glm::radians(static_cast<float>(leafletAngle));

    // Stem segments (for smooth curve)
    const int stemSegments = std::max(pairs * 2, 8);
    const float stemWidth = 0.015f;  // Thin stem

    // Generate stem vertices
    std::vector<glm::vec3> stemPositions;
    std::vector<glm::vec3> stemTangents;

    for (int i = 0; i <= stemSegments; i++) {
        float t = static_cast<float>(i) / stemSegments;
        stemPositions.push_back(evalStemCurve(t, length, curve));
        stemTangents.push_back(evalStemTangent(t, length, curve));
    }

    // Create stem geometry (thin quad strip)
    for (int i = 0; i <= stemSegments; i++) {
        float t = static_cast<float>(i) / stemSegments;
        glm::vec3 pos = stemPositions[i];
        glm::vec3 tangent = stemTangents[i];

        // Perpendicular direction for stem width
        glm::vec3 right = glm::normalize(glm::cross(tangent, glm::vec3(1, 0, 0)));
        if (glm::length(right) < 0.01f) {
            right = glm::normalize(glm::cross(tangent, glm::vec3(0, 0, 1)));
        }

        float width = stemWidth * (1.0f - t * 0.5f);  // Taper stem

        // Left and right vertices
        Vertex3D left, rightV;
        left.position = pos - right * width;
        left.normal = glm::vec3(0, 0, 1);  // Face camera roughly
        left.uv = glm::vec2(0, t);
        left.color = glm::vec4(1);

        rightV.position = pos + right * width;
        rightV.normal = glm::vec3(0, 0, 1);
        rightV.uv = glm::vec2(1, t);
        rightV.color = glm::vec4(1);

        m_frondMesh.vertices.push_back(left);
        m_frondMesh.vertices.push_back(rightV);
    }

    // Create stem triangles
    for (int i = 0; i < stemSegments; i++) {
        uint32_t base = i * 2;
        m_frondMesh.indices.push_back(base);
        m_frondMesh.indices.push_back(base + 1);
        m_frondMesh.indices.push_back(base + 2);
        m_frondMesh.indices.push_back(base + 1);
        m_frondMesh.indices.push_back(base + 3);
        m_frondMesh.indices.push_back(base + 2);
    }

    // Generate leaflets (pinnae) along the stem
    if (pairs > 0) {
        for (int p = 0; p < pairs; p++) {
            // Position along stem (skip the very base and tip)
            float t = 0.1f + 0.8f * static_cast<float>(p + 1) / (pairs + 1);

            // Size falloff toward tip
            float sizeFactor = 1.0f - 0.6f * t;
            float currentLength = lLength * sizeFactor;
            float currentWidth = lWidth * sizeFactor;

            glm::vec3 stemPos = evalStemCurve(t, length, curve);
            glm::vec3 stemTangent = evalStemTangent(t, length, curve);

            // Create left and right leaflets
            for (int side = 0; side < 2; side++) {
                float sideSign = (side == 0) ? -1.0f : 1.0f;

                // Leaflet base direction (perpendicular to stem, angled down)
                glm::vec3 perpDir = glm::normalize(glm::cross(stemTangent, glm::vec3(0, 1, 0)));
                if (glm::length(perpDir) < 0.01f) {
                    perpDir = glm::vec3(1, 0, 0);
                }
                perpDir *= sideSign;

                // Angle the leaflet downward
                glm::vec3 leafletDir = glm::normalize(
                    perpDir * std::cos(lAngle) +
                    glm::vec3(0, -1, 0) * std::sin(lAngle) * 0.5f +
                    stemTangent * 0.3f  // Slight forward angle
                );

                // Leaflet tip position
                glm::vec3 tipPos = stemPos + leafletDir * currentLength;

                // Create tapered leaflet (triangle)
                glm::vec3 leafletPerp = glm::normalize(glm::cross(leafletDir, stemTangent));

                uint32_t baseIdx = static_cast<uint32_t>(m_frondMesh.vertices.size());

                // Leaflet normal (facing outward)
                glm::vec3 leafletNormal = glm::normalize(glm::cross(leafletDir, leafletPerp));
                if (leafletNormal.y < 0) leafletNormal = -leafletNormal;  // Face up-ish

                // Base left vertex
                Vertex3D v0;
                v0.position = stemPos - leafletPerp * currentWidth * 0.5f;
                v0.normal = leafletNormal;
                v0.uv = glm::vec2(0, t);  // UV.y = position along stem
                v0.color = glm::vec4(1);

                // Base right vertex
                Vertex3D v1;
                v1.position = stemPos + leafletPerp * currentWidth * 0.5f;
                v1.normal = leafletNormal;
                v1.uv = glm::vec2(1, t);
                v1.color = glm::vec4(1);

                // Tip vertex
                Vertex3D v2;
                v2.position = tipPos;
                v2.normal = leafletNormal;
                v2.uv = glm::vec2(0.5f, t + 0.15f);  // Slightly higher UV for tip color
                v2.color = glm::vec4(1);

                m_frondMesh.vertices.push_back(v0);
                m_frondMesh.vertices.push_back(v1);
                m_frondMesh.vertices.push_back(v2);

                // Triangle indices (both sides for double-sided)
                m_frondMesh.indices.push_back(baseIdx);
                m_frondMesh.indices.push_back(baseIdx + 1);
                m_frondMesh.indices.push_back(baseIdx + 2);

                // Back face
                m_frondMesh.indices.push_back(baseIdx + 2);
                m_frondMesh.indices.push_back(baseIdx + 1);
                m_frondMesh.indices.push_back(baseIdx);
            }
        }
    }
}

void FoliageMesh::generateInstances() {
    int count = static_cast<int>(frondCount);
    int currentSeed = static_cast<int>(seed);
    float currentBaseHeight = static_cast<float>(baseHeight);
    float currentFieldWidth = static_cast<float>(fieldWidth);
    float currentFieldDepth = static_cast<float>(fieldDepth);
    float currentSizeVariation = static_cast<float>(sizeVariation);

    bool instanceParamsChanged = (count != m_lastFrondCount ||
                                   currentSeed != m_lastSeed ||
                                   currentBaseHeight != m_lastBaseHeight ||
                                   currentFieldWidth != m_lastFieldWidth ||
                                   currentFieldDepth != m_lastFieldDepth ||
                                   currentSizeVariation != m_lastSizeVariation);

    if (!instanceParamsChanged && !m_instances.empty()) {
        return;
    }

    m_lastFrondCount = count;
    m_lastSeed = currentSeed;
    m_lastBaseHeight = currentBaseHeight;
    m_lastFieldWidth = currentFieldWidth;
    m_lastFieldDepth = currentFieldDepth;
    m_lastSizeVariation = currentSizeVariation;

    m_instances.clear();
    m_instances.reserve(count);

    std::mt19937 rng(currentSeed);
    std::uniform_real_distribution<float> distX(-currentFieldWidth * 0.5f,
                                                 currentFieldWidth * 0.5f);
    std::uniform_real_distribution<float> distZ(-currentFieldDepth * 0.5f,
                                                 currentFieldDepth * 0.5f);
    std::uniform_real_distribution<float> distRot(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> distTilt(-0.2f, 0.3f);  // Slight tilt variation
    std::uniform_real_distribution<float> distScale(1.0f - currentSizeVariation,
                                                    1.0f + currentSizeVariation);
    std::uniform_real_distribution<float> distPhase(0.0f, 1.0f);

    for (int i = 0; i < count; i++) {
        ProceduralInstance inst;

        float x = distX(rng);
        float z = distZ(rng);
        float rot = distRot(rng);
        float tilt = distTilt(rng);
        float scale = distScale(rng);
        float phase = distPhase(rng);

        // Build transform: translate, rotate around Y, slight tilt
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, currentBaseHeight, z));
        transform = glm::rotate(transform, rot, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, tilt, glm::vec3(1.0f, 0.0f, 0.0f));

        inst.transform = transform;
        inst.variation = glm::vec4(scale, scale, scale, phase);
        inst.color = glm::vec4(1.0f);  // Use gradient colors from getBaseColor/getTipColor

        m_instances.push_back(inst);
    }

    m_instancesDirty = true;
}

void FoliageMesh::checkForChanges() {
    // Check if geometry params changed
    float currentStemLength = static_cast<float>(stemLength);
    float currentStemCurve = static_cast<float>(stemCurve);
    int currentPairs = static_cast<int>(leafletPairs);
    float currentLeafletWidth = static_cast<float>(leafletWidth);
    float currentLeafletLength = static_cast<float>(leafletLength);
    float currentLeafletAngle = static_cast<float>(leafletAngle);

    bool geomChanged = (currentStemLength != m_lastStemLength ||
                        currentStemCurve != m_lastStemCurve ||
                        currentPairs != m_lastLeafletPairs ||
                        currentLeafletWidth != m_lastLeafletWidth ||
                        currentLeafletLength != m_lastLeafletLength ||
                        currentLeafletAngle != m_lastLeafletAngle);

    if (geomChanged) {
        m_meshDirty = true;
        m_lastStemLength = currentStemLength;
        m_lastStemCurve = currentStemCurve;
        m_lastLeafletPairs = currentPairs;
        m_lastLeafletWidth = currentLeafletWidth;
        m_lastLeafletLength = currentLeafletLength;
        m_lastLeafletAngle = currentLeafletAngle;
    }
}

void FoliageMesh::init(Context& ctx) {
    if (m_initialized) return;

    generateFrondMesh();
    generateInstances();

    m_initialized = true;
}

void FoliageMesh::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Check for parameter changes
    checkForChanges();

    // Regenerate mesh if dirty
    if (m_meshDirty) {
        generateFrondMesh();
        m_meshDirty = false;
    }

    // Regenerate instances if needed
    generateInstances();

    didCook();
}

std::vector<ParamDecl> FoliageMesh::params() {
    return {
        fieldWidth.decl(), fieldDepth.decl(), frondCount.decl(), seed.decl(),
        baseHeight.decl(), stemLength.decl(), stemCurve.decl(), leafletPairs.decl(),
        leafletWidth.decl(), leafletLength.decl(), leafletAngle.decl(),
        sizeVariation.decl(), windStrength.decl(), windSpeed.decl(),
        windDirX.decl(), windDirZ.decl()
    };
}

bool FoliageMesh::getParam(const std::string& name, float out[4]) {
    if (name == "fieldWidth") { out[0] = static_cast<float>(fieldWidth); return true; }
    if (name == "fieldDepth") { out[0] = static_cast<float>(fieldDepth); return true; }
    if (name == "frondCount") { out[0] = static_cast<float>(static_cast<int>(frondCount)); return true; }
    if (name == "seed") { out[0] = static_cast<float>(static_cast<int>(seed)); return true; }
    if (name == "baseHeight") { out[0] = static_cast<float>(baseHeight); return true; }
    if (name == "stemLength") { out[0] = static_cast<float>(stemLength); return true; }
    if (name == "stemCurve") { out[0] = static_cast<float>(stemCurve); return true; }
    if (name == "leafletPairs") { out[0] = static_cast<float>(static_cast<int>(leafletPairs)); return true; }
    if (name == "leafletWidth") { out[0] = static_cast<float>(leafletWidth); return true; }
    if (name == "leafletLength") { out[0] = static_cast<float>(leafletLength); return true; }
    if (name == "leafletAngle") { out[0] = static_cast<float>(leafletAngle); return true; }
    if (name == "sizeVariation") { out[0] = static_cast<float>(sizeVariation); return true; }
    if (name == "windStrength") { out[0] = static_cast<float>(windStrength); return true; }
    if (name == "windSpeed") { out[0] = static_cast<float>(windSpeed); return true; }
    if (name == "windDirX") { out[0] = static_cast<float>(windDirX); return true; }
    if (name == "windDirZ") { out[0] = static_cast<float>(windDirZ); return true; }
    return false;
}

bool FoliageMesh::setParam(const std::string& name, const float value[4]) {
    if (name == "fieldWidth") { fieldWidth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "fieldDepth") { fieldDepth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "frondCount") { frondCount = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "seed") { seed = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "baseHeight") { baseHeight = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "stemLength") { stemLength = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "stemCurve") { stemCurve = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletPairs") { leafletPairs = static_cast<int>(value[0]); m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletWidth") { leafletWidth = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletLength") { leafletLength = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletAngle") { leafletAngle = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "sizeVariation") { sizeVariation = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "windStrength") { windStrength = value[0]; markDirty(); return true; }
    if (name == "windSpeed") { windSpeed = value[0]; markDirty(); return true; }
    if (name == "windDirX") { windDirX = value[0]; markDirty(); return true; }
    if (name == "windDirZ") { windDirZ = value[0]; markDirty(); return true; }
    return false;
}

} // namespace vivid::render3d
