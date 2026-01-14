// TreeMesh - L-System based procedural tree mesh generator
// Generates trees using L-System grammar expansion and turtle interpretation
// Rendering is handled by Render3D for unified shadows and lighting

#include <vivid/render3d/tree_mesh.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <vivid/io/image_loader.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>
#include <stack>
#include <iostream>

namespace vivid::render3d {

REGISTER_OPERATOR(TreeMesh, "3D Vegetation", "L-System procedural tree generator", false);

namespace {

// Number of sides for branch cylinder cross-section
constexpr int BRANCH_SIDES = 6;

// Rotate turtle around its heading vector (yaw)
void turtleYaw(TurtleState& t, float angle) {
    float rad = glm::radians(angle);
    float c = std::cos(rad);
    float s = std::sin(rad);
    glm::vec3 newLeft = t.left * c + t.up * s;
    glm::vec3 newUp = t.up * c - t.left * s;
    t.left = newLeft;
    t.up = newUp;
}

// Rotate turtle around its left vector (pitch)
void turtlePitch(TurtleState& t, float angle) {
    float rad = glm::radians(angle);
    float c = std::cos(rad);
    float s = std::sin(rad);
    glm::vec3 newHeading = t.heading * c + t.up * s;
    glm::vec3 newUp = t.up * c - t.heading * s;
    t.heading = glm::normalize(newHeading);
    t.up = glm::normalize(newUp);
}

// Rotate turtle around its up vector (roll)
void turtleRoll(TurtleState& t, float angle) {
    float rad = glm::radians(angle);
    float c = std::cos(rad);
    float s = std::sin(rad);
    glm::vec3 newHeading = t.heading * c + t.left * s;
    glm::vec3 newLeft = t.left * c - t.heading * s;
    t.heading = glm::normalize(newHeading);
    t.left = glm::normalize(newLeft);
}

} // namespace

TreeMesh::TreeMesh() {
    // Set default grammar
    applyPreset(TreeType::Deciduous);
}

TreeMesh::~TreeMesh() {
    releaseLeafTexture();
}

void TreeMesh::setTreeType(TreeType type) {
    if (m_treeType != type) {
        m_treeType = type;
        applyPreset(type);
        m_meshDirty = true;
        m_instancesDirty = true;
        markDirty();
    }
}

void TreeMesh::setGrammar(const LSystemGrammar& grammar) {
    m_grammar = grammar;
    m_treeType = TreeType::Custom;
    m_meshDirty = true;
    markDirty();
}

void TreeMesh::applyPreset(TreeType type) {
    m_grammar.rules.clear();

    switch (type) {
        case TreeType::Deciduous:
            // Spreading crown with multiple main branches, leaves at tips
            m_grammar.axiom = "FFFA";
            m_grammar.rules.push_back({'A', "[&FFAL]////[&FFAL]///////[&FFAL]"});
            m_grammar.angle = 25.0f;
            m_grammar.lengthScale = 0.9f;
            m_grammar.radiusScale = 0.7f;
            m_grammar.iterations = 4;

            trunkHeight = 2.0f;
            trunkRadius = 0.12f;
            branchAngle = 25.0f;
            lengthScale = 0.9f;
            radiusScale = 0.7f;
            lsystemIterations = 4;
            leafDensity = 20;
            leafSize = 0.12f;
            clusterRadius = 0.2f;

            trunkBaseColor[0] = 0.25f; trunkBaseColor[1] = 0.15f; trunkBaseColor[2] = 0.08f;
            trunkTipColor[0] = 0.35f; trunkTipColor[1] = 0.25f; trunkTipColor[2] = 0.15f;
            leafColor[0] = 0.15f; leafColor[1] = 0.4f; leafColor[2] = 0.1f;
            break;

        case TreeType::Conifer:
            // Pyramidal shape with horizontal branch layers
            m_grammar.axiom = "FFFFFA";
            m_grammar.rules.push_back({'A', "F[&&&L][^^^L]////A"});
            m_grammar.angle = 20.0f;
            m_grammar.lengthScale = 0.85f;
            m_grammar.radiusScale = 0.75f;
            m_grammar.iterations = 7;

            trunkHeight = 3.0f;
            trunkRadius = 0.1f;
            branchAngle = 20.0f;
            lengthScale = 0.85f;
            radiusScale = 0.75f;
            lsystemIterations = 7;
            leafDensity = 6;
            leafSize = 0.2f;
            clusterRadius = 0.25f;

            trunkBaseColor[0] = 0.2f; trunkBaseColor[1] = 0.12f; trunkBaseColor[2] = 0.06f;
            trunkTipColor[0] = 0.3f; trunkTipColor[1] = 0.2f; trunkTipColor[2] = 0.1f;
            leafColor[0] = 0.08f; leafColor[1] = 0.25f; leafColor[2] = 0.08f;
            break;

        case TreeType::Palm:
            // Tall trunk with frond crown at top
            m_grammar.axiom = "FFFFFFFFA";
            m_grammar.rules.push_back({'A', "[&&&&&L][^^^^^L]//[&&&&&L][^^^^^L]//[&&&&&L][^^^^^L]//"});
            m_grammar.angle = 35.0f;
            m_grammar.lengthScale = 0.8f;
            m_grammar.radiusScale = 0.9f;
            m_grammar.iterations = 1;

            trunkHeight = 4.0f;
            trunkRadius = 0.15f;
            branchAngle = 35.0f;
            lengthScale = 0.8f;
            radiusScale = 0.9f;
            lsystemIterations = 1;
            leafDensity = 12;
            leafSize = 0.5f;
            clusterRadius = 0.6f;

            trunkBaseColor[0] = 0.3f; trunkBaseColor[1] = 0.22f; trunkBaseColor[2] = 0.15f;
            trunkTipColor[0] = 0.35f; trunkTipColor[1] = 0.28f; trunkTipColor[2] = 0.18f;
            leafColor[0] = 0.1f; leafColor[1] = 0.35f; leafColor[2] = 0.08f;
            break;

        case TreeType::Willow:
            // Drooping branches with cascading foliage
            m_grammar.axiom = "FFFA";
            m_grammar.rules.push_back({'A', "FF[&&&&&&B][&&&&&&B]////[&&&&&&B]"});
            m_grammar.rules.push_back({'B', "F[&&&L]"});
            m_grammar.angle = 15.0f;
            m_grammar.lengthScale = 0.95f;
            m_grammar.radiusScale = 0.65f;
            m_grammar.iterations = 3;

            trunkHeight = 2.5f;
            trunkRadius = 0.12f;
            branchAngle = 15.0f;
            lengthScale = 0.95f;
            radiusScale = 0.65f;
            lsystemIterations = 3;
            leafDensity = 10;
            leafSize = 0.15f;
            clusterRadius = 0.3f;

            trunkBaseColor[0] = 0.22f; trunkBaseColor[1] = 0.15f; trunkBaseColor[2] = 0.08f;
            trunkTipColor[0] = 0.32f; trunkTipColor[1] = 0.22f; trunkTipColor[2] = 0.12f;
            leafColor[0] = 0.2f; leafColor[1] = 0.45f; leafColor[2] = 0.15f;
            break;

        case TreeType::Bushy:
            // Dense shrub-like structure
            m_grammar.axiom = "FFA";
            m_grammar.rules.push_back({'A', "[&FL]////[&FL]////[&FL]"});
            m_grammar.angle = 40.0f;
            m_grammar.lengthScale = 0.7f;
            m_grammar.radiusScale = 0.6f;
            m_grammar.iterations = 3;

            trunkHeight = 0.8f;
            trunkRadius = 0.08f;
            branchAngle = 40.0f;
            lengthScale = 0.7f;
            radiusScale = 0.6f;
            lsystemIterations = 3;
            leafDensity = 12;
            leafSize = 0.2f;
            clusterRadius = 0.3f;

            trunkBaseColor[0] = 0.18f; trunkBaseColor[1] = 0.12f; trunkBaseColor[2] = 0.06f;
            trunkTipColor[0] = 0.25f; trunkTipColor[1] = 0.18f; trunkTipColor[2] = 0.1f;
            leafColor[0] = 0.12f; leafColor[1] = 0.35f; leafColor[2] = 0.1f;
            break;

        case TreeType::Custom:
            // Keep current settings
            break;
    }
}

WindParams TreeMesh::getWindParams() const {
    WindParams params;
    params.strength = static_cast<float>(windStrength);
    params.speed = static_cast<float>(windSpeed);

    glm::vec2 dir(static_cast<float>(windDirX), static_cast<float>(windDirZ));
    if (glm::length(dir) > 0.001f) {
        dir = glm::normalize(dir);
    }
    params.direction = dir;

    // Trees have less stem curve effect than foliage
    params.stemCurve = 0.1f;
    params.stemLength = static_cast<float>(trunkHeight);

    return params;
}

std::string TreeMesh::expandLSystem() {
    std::string current = m_grammar.axiom;

    int iterations = static_cast<int>(lsystemIterations);
    if (iterations < 1) iterations = 1;
    if (iterations > 7) iterations = 7;

    for (int i = 0; i < iterations; i++) {
        std::string next;
        next.reserve(current.size() * 3);

        for (char c : current) {
            bool found = false;
            for (const auto& rule : m_grammar.rules) {
                if (rule.first == c) {
                    next += rule.second;
                    found = true;
                    break;
                }
            }
            if (!found) {
                next += c;
            }
        }
        current = std::move(next);
    }

    return current;
}

void TreeMesh::generateBranchSegment(const TurtleState& start, const TurtleState& end) {
    // Generate a tapered cylinder from start to end
    uint32_t baseIdx = static_cast<uint32_t>(m_treeMesh.vertices.size());

    // Calculate how "deep" we are in the tree for UV gradient
    float maxDepth = static_cast<float>(static_cast<int>(lsystemIterations) + 2);
    float uvStart = static_cast<float>(start.depth) / maxDepth;
    float uvEnd = static_cast<float>(end.depth) / maxDepth;

    // Generate circle vertices at start and end
    for (int ring = 0; ring < 2; ring++) {
        const TurtleState& t = (ring == 0) ? start : end;
        float uvY = (ring == 0) ? uvStart : uvEnd;

        for (int i = 0; i < BRANCH_SIDES; i++) {
            float angle = glm::two_pi<float>() * i / BRANCH_SIDES;
            float c = std::cos(angle);
            float s = std::sin(angle);

            // Position on circle
            glm::vec3 offset = t.left * c * t.radius + t.up * s * t.radius;
            glm::vec3 pos = t.position + offset;

            // Normal points outward
            glm::vec3 normal = glm::normalize(offset);

            Vertex3D v;
            v.position = pos;
            v.normal = normal;
            v.uv = glm::vec2(static_cast<float>(i) / BRANCH_SIDES, uvY);
            v.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);  // w=0 means branch (not billboard)

            m_treeMesh.vertices.push_back(v);
        }
    }

    // Generate triangles connecting the two rings
    for (int i = 0; i < BRANCH_SIDES; i++) {
        int next = (i + 1) % BRANCH_SIDES;

        uint32_t i0 = baseIdx + i;
        uint32_t i1 = baseIdx + next;
        uint32_t i2 = baseIdx + BRANCH_SIDES + i;
        uint32_t i3 = baseIdx + BRANCH_SIDES + next;

        // Two triangles per quad
        m_treeMesh.indices.push_back(i0);
        m_treeMesh.indices.push_back(i2);
        m_treeMesh.indices.push_back(i1);

        m_treeMesh.indices.push_back(i1);
        m_treeMesh.indices.push_back(i2);
        m_treeMesh.indices.push_back(i3);
    }
}

void TreeMesh::generateLeafCluster(const TurtleState& turtle) {
    int density = static_cast<int>(leafDensity);
    float size = static_cast<float>(leafSize);
    float radius = static_cast<float>(clusterRadius);

    // First, generate a small twig extending from the branch tip into the leaf cluster
    // This visually connects the leaves to the branch
    float twigLength = radius * 1.0f;
    float twigRadius = turtle.radius * 0.5f;
    if (twigRadius < 0.008f) twigRadius = 0.008f;

    TurtleState twigEnd = turtle;
    twigEnd.position = turtle.position + turtle.heading * twigLength;
    twigEnd.radius = twigRadius * 0.5f;
    twigEnd.depth = turtle.depth + 1;

    // Generate the connecting twig
    generateBranchSegment(turtle, twigEnd);

    // Use a simple seeded random for cluster variation
    std::hash<float> hasher;
    unsigned int clusterSeed = static_cast<unsigned int>(
        hasher(turtle.position.x) ^ hasher(turtle.position.y) ^ hasher(turtle.position.z));
    std::mt19937 rng(clusterSeed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> sizeDist(0.7f, 1.3f);
    std::uniform_real_distribution<float> forwardDist(0.2f, 1.0f);
    std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());

    // Generate leaves along and around the twig
    for (int i = 0; i < density; i++) {
        // Position along the twig - leaves attach directly to twig
        float t = forwardDist(rng);
        glm::vec3 attachPoint = turtle.position + turtle.heading * twigLength * t;

        // Radial direction from twig for leaf orientation
        // Create a rotation around the twig heading for radial placement
        float radialAngle = angleDist(rng);
        glm::vec3 radialDir = glm::normalize(
            turtle.left * std::cos(radialAngle) + turtle.up * std::sin(radialAngle));

        // Stem direction: points from leaf tip back toward twig attachment
        // Leaves angle outward and slightly downward from the twig
        float outwardAngle = 0.3f + dist(rng) * 0.2f;  // 15-30 degrees from perpendicular
        glm::vec3 stemDir = glm::normalize(
            -radialDir * std::cos(outwardAngle) +
            turtle.heading * std::sin(outwardAngle) * 0.5f);

        // Leaf position: slightly offset from twig along radial direction
        // Smaller offset keeps leaves visually connected to branch
        float stemLength = size * sizeDist(rng) * 0.3f;  // Short stem
        glm::vec3 leafPos = attachPoint + radialDir * stemLength;

        float leafSizeVar = size * sizeDist(rng);

        // Random rotation angle for variety in leaf orientation
        float leafRotation = angleDist(rng);

        // Create billboard quad (4 vertices)
        // The shader will expand this based on stem orientation
        uint32_t baseIdx = static_cast<uint32_t>(m_treeMesh.vertices.size());

        // UV coordinates for quad corners
        // UV.x=0.5 is the stem attachment point (bottom center)
        glm::vec2 uvs[4] = {
            {0.0f, 0.0f},  // Bottom-left
            {1.0f, 0.0f},  // Bottom-right
            {1.0f, 1.0f},  // Top-right
            {0.0f, 1.0f}   // Top-left
        };

        for (int j = 0; j < 4; j++) {
            Vertex3D v;
            v.position = leafPos;  // All vertices at same position initially

            // Store stem direction in normal (points toward twig attachment)
            v.normal = stemDir;

            // Store leaf "up" direction in tangent.xyz, rotation angle in tangent.w
            // Leaf up is perpendicular to stem, roughly following radial direction
            glm::vec3 leafUp = glm::normalize(glm::cross(stemDir, radialDir));
            if (glm::length(leafUp) < 0.001f) {
                leafUp = glm::vec3(0, 1, 0);
            }
            v.tangent = glm::vec4(leafUp, leafRotation);

            v.uv = uvs[j];
            // Store billboard size in color.w (positive = billboard, 0 = branch)
            v.color = glm::vec4(
                leafColor[0],
                leafColor[1],
                leafColor[2],
                leafSizeVar  // w > 0 signals billboard, value is size
            );

            m_treeMesh.vertices.push_back(v);
        }

        // Two triangles for the quad
        m_treeMesh.indices.push_back(baseIdx);
        m_treeMesh.indices.push_back(baseIdx + 1);
        m_treeMesh.indices.push_back(baseIdx + 2);

        m_treeMesh.indices.push_back(baseIdx);
        m_treeMesh.indices.push_back(baseIdx + 2);
        m_treeMesh.indices.push_back(baseIdx + 3);
    }
}

void TreeMesh::interpretLSystem(const std::string& lstring) {
    TurtleState turtle;
    turtle.position = glm::vec3(0.0f);
    turtle.heading = glm::vec3(0.0f, 1.0f, 0.0f);  // Y-up
    turtle.left = glm::vec3(1.0f, 0.0f, 0.0f);
    turtle.up = glm::vec3(0.0f, 0.0f, 1.0f);
    turtle.length = static_cast<float>(trunkHeight) * 0.3f;  // Segment length
    turtle.radius = static_cast<float>(trunkRadius);
    turtle.depth = 0;

    float angle = static_cast<float>(branchAngle);
    float lScale = static_cast<float>(lengthScale);
    float rScale = static_cast<float>(radiusScale);

    std::stack<TurtleState> stateStack;

    for (char c : lstring) {
        switch (c) {
            case 'F': {
                // Draw forward and move
                TurtleState start = turtle;

                turtle.position += turtle.heading * turtle.length;
                turtle.depth++;

                generateBranchSegment(start, turtle);

                // Scale down for next segment
                turtle.length *= lScale;
                turtle.radius *= rScale;
                break;
            }

            case '+':
                // Yaw left
                turtleYaw(turtle, angle);
                break;

            case '-':
                // Yaw right
                turtleYaw(turtle, -angle);
                break;

            case '^':
                // Pitch up
                turtlePitch(turtle, angle);
                break;

            case '&':
                // Pitch down
                turtlePitch(turtle, -angle);
                break;

            case '/':
                // Roll clockwise
                turtleRoll(turtle, angle);
                break;

            case '\\':
                // Roll counter-clockwise
                turtleRoll(turtle, -angle);
                break;

            case '[':
                // Push state (branch point)
                stateStack.push(turtle);
                break;

            case ']':
                // Pop state (return from branch)
                if (!stateStack.empty()) {
                    turtle = stateStack.top();
                    stateStack.pop();
                }
                break;

            case 'L':
                // Leaf cluster - only generate at sufficient depth (terminal branches)
                // This prevents leaves from appearing on the trunk/main branches
                if (turtle.depth >= 2) {
                    generateLeafCluster(turtle);
                }
                break;

            case '!':
                // Decrease radius
                turtle.radius *= rScale;
                break;

            case 'A':
            case 'B':
                // Production symbols - should be expanded away
                break;

            default:
                // Ignore unknown symbols
                break;
        }
    }
}

void TreeMesh::generateTree() {
    m_treeMesh.vertices.clear();
    m_treeMesh.indices.clear();

    // Expand L-System grammar
    std::string lstring = expandLSystem();

    // Interpret the L-System string to generate geometry
    interpretLSystem(lstring);
}

void TreeMesh::generateInstances() {
    int count = static_cast<int>(treeCount);
    int currentSeed = static_cast<int>(seed);
    float currentFieldWidth = static_cast<float>(fieldWidth);
    float currentFieldDepth = static_cast<float>(fieldDepth);

    bool instanceParamsChanged = (count != m_lastTreeCount ||
                                   currentSeed != m_lastSeed ||
                                   currentFieldWidth != m_lastFieldWidth ||
                                   currentFieldDepth != m_lastFieldDepth);

    if (!instanceParamsChanged && !m_instances.empty()) {
        return;
    }

    m_lastTreeCount = count;
    m_lastSeed = currentSeed;
    m_lastFieldWidth = currentFieldWidth;
    m_lastFieldDepth = currentFieldDepth;

    m_instances.clear();
    m_instances.reserve(count);

    std::mt19937 rng(currentSeed);
    std::uniform_real_distribution<float> distX(-currentFieldWidth * 0.5f,
                                                 currentFieldWidth * 0.5f);
    std::uniform_real_distribution<float> distZ(-currentFieldDepth * 0.5f,
                                                 currentFieldDepth * 0.5f);
    std::uniform_real_distribution<float> distRot(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> distScale(0.8f, 1.2f);
    std::uniform_real_distribution<float> distPhase(0.0f, 1.0f);

    for (int i = 0; i < count; i++) {
        ProceduralInstance inst;

        float x = distX(rng);
        float z = distZ(rng);
        float rot = distRot(rng);
        float scale = distScale(rng);
        float phase = distPhase(rng);

        // Build transform: translate and rotate around Y
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z));
        transform = glm::rotate(transform, rot, glm::vec3(0.0f, 1.0f, 0.0f));

        inst.transform = transform;
        inst.variation = glm::vec4(scale, scale, scale, phase);
        inst.color = glm::vec4(1.0f);

        m_instances.push_back(inst);
    }

    m_instancesDirty = true;
}

void TreeMesh::checkForChanges() {
    float currentTrunkHeight = static_cast<float>(trunkHeight);
    float currentTrunkRadius = static_cast<float>(trunkRadius);
    int currentIterations = static_cast<int>(lsystemIterations);
    float currentBranchAngle = static_cast<float>(branchAngle);
    float currentLengthScale = static_cast<float>(lengthScale);
    float currentRadiusScale = static_cast<float>(radiusScale);
    int currentLeafDensity = static_cast<int>(leafDensity);
    float currentLeafSize = static_cast<float>(leafSize);
    float currentClusterRadius = static_cast<float>(clusterRadius);

    bool geomChanged = (currentTrunkHeight != m_lastTrunkHeight ||
                        currentTrunkRadius != m_lastTrunkRadius ||
                        currentIterations != m_lastIterations ||
                        currentBranchAngle != m_lastBranchAngle ||
                        currentLengthScale != m_lastLengthScale ||
                        currentRadiusScale != m_lastRadiusScale ||
                        currentLeafDensity != m_lastLeafDensity ||
                        currentLeafSize != m_lastLeafSize ||
                        currentClusterRadius != m_lastClusterRadius);

    if (geomChanged) {
        m_meshDirty = true;
        m_lastTrunkHeight = currentTrunkHeight;
        m_lastTrunkRadius = currentTrunkRadius;
        m_lastIterations = currentIterations;
        m_lastBranchAngle = currentBranchAngle;
        m_lastLengthScale = currentLengthScale;
        m_lastRadiusScale = currentRadiusScale;
        m_lastLeafDensity = currentLeafDensity;
        m_lastLeafSize = currentLeafSize;
        m_lastClusterRadius = currentClusterRadius;
    }
}

void TreeMesh::init(Context& ctx) {
    if (m_initialized) return;

    m_ctx = &ctx;

    generateTree();
    generateInstances();

    // Load leaf texture if path was set before init
    if (m_leafTextureNeedsLoad && !m_leafTexturePath.empty()) {
        loadLeafTexture();
    }

    m_initialized = true;
}

void TreeMesh::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    checkForChanges();

    if (m_meshDirty) {
        generateTree();
        m_meshDirty = false;
    }

    generateInstances();

    didCook();
}

std::vector<ParamDecl> TreeMesh::params() {
    return {
        fieldWidth.decl(), fieldDepth.decl(), treeCount.decl(), seed.decl(),
        trunkHeight.decl(), trunkRadius.decl(),
        lsystemIterations.decl(), branchAngle.decl(), lengthScale.decl(), radiusScale.decl(),
        leafDensity.decl(), leafSize.decl(), clusterRadius.decl(),
        windStrength.decl(), windSpeed.decl(), windDirX.decl(), windDirZ.decl(),
        leafFlutter.decl()
    };
}

bool TreeMesh::getParam(const std::string& name, float out[4]) {
    if (name == "fieldWidth") { out[0] = static_cast<float>(fieldWidth); return true; }
    if (name == "fieldDepth") { out[0] = static_cast<float>(fieldDepth); return true; }
    if (name == "treeCount") { out[0] = static_cast<float>(static_cast<int>(treeCount)); return true; }
    if (name == "seed") { out[0] = static_cast<float>(static_cast<int>(seed)); return true; }
    if (name == "trunkHeight") { out[0] = static_cast<float>(trunkHeight); return true; }
    if (name == "trunkRadius") { out[0] = static_cast<float>(trunkRadius); return true; }
    if (name == "lsystemIterations") { out[0] = static_cast<float>(static_cast<int>(lsystemIterations)); return true; }
    if (name == "branchAngle") { out[0] = static_cast<float>(branchAngle); return true; }
    if (name == "lengthScale") { out[0] = static_cast<float>(lengthScale); return true; }
    if (name == "radiusScale") { out[0] = static_cast<float>(radiusScale); return true; }
    if (name == "leafDensity") { out[0] = static_cast<float>(static_cast<int>(leafDensity)); return true; }
    if (name == "leafSize") { out[0] = static_cast<float>(leafSize); return true; }
    if (name == "clusterRadius") { out[0] = static_cast<float>(clusterRadius); return true; }
    if (name == "windStrength") { out[0] = static_cast<float>(windStrength); return true; }
    if (name == "windSpeed") { out[0] = static_cast<float>(windSpeed); return true; }
    if (name == "windDirX") { out[0] = static_cast<float>(windDirX); return true; }
    if (name == "windDirZ") { out[0] = static_cast<float>(windDirZ); return true; }
    if (name == "leafFlutter") { out[0] = static_cast<float>(leafFlutter); return true; }
    return false;
}

bool TreeMesh::setParam(const std::string& name, const float value[4]) {
    if (name == "fieldWidth") { fieldWidth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "fieldDepth") { fieldDepth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "treeCount") { treeCount = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "seed") { seed = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "trunkHeight") { trunkHeight = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "trunkRadius") { trunkRadius = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "lsystemIterations") { lsystemIterations = static_cast<int>(value[0]); m_meshDirty = true; markDirty(); return true; }
    if (name == "branchAngle") { branchAngle = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "lengthScale") { lengthScale = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "radiusScale") { radiusScale = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "leafDensity") { leafDensity = static_cast<int>(value[0]); m_meshDirty = true; markDirty(); return true; }
    if (name == "leafSize") { leafSize = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "clusterRadius") { clusterRadius = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "windStrength") { windStrength = value[0]; markDirty(); return true; }
    if (name == "windSpeed") { windSpeed = value[0]; markDirty(); return true; }
    if (name == "windDirX") { windDirX = value[0]; markDirty(); return true; }
    if (name == "windDirZ") { windDirZ = value[0]; markDirty(); return true; }
    if (name == "leafFlutter") { leafFlutter = value[0]; markDirty(); return true; }
    return false;
}

void TreeMesh::setLeafTexture(const std::string& texturePath) {
    if (m_leafTexturePath == texturePath) return;

    m_leafTexturePath = texturePath;
    m_leafTextureNeedsLoad = true;

    // If already initialized, load immediately
    if (m_initialized && m_ctx) {
        loadLeafTexture();
    }
}

void TreeMesh::clearLeafTexture() {
    releaseLeafTexture();
    m_leafTexturePath.clear();
    m_leafTextureNeedsLoad = false;
}

void TreeMesh::loadLeafTexture() {
    if (!m_ctx || m_leafTexturePath.empty()) return;

    // Release any existing texture
    releaseLeafTexture();

    // Load texture using vivid::io
    auto imageData = vivid::io::loadImage(m_leafTexturePath);

    if (!imageData.valid()) {
        std::cerr << "TreeMesh: Failed to load leaf texture: " << m_leafTexturePath << std::endl;
        m_leafTextureNeedsLoad = false;
        return;
    }

    WGPUDevice device = m_ctx->device();

    // Create texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {static_cast<uint32_t>(imageData.width), static_cast<uint32_t>(imageData.height), 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    m_leafTexture = wgpuDeviceCreateTexture(device, &texDesc);

    if (!m_leafTexture) {
        std::cerr << "TreeMesh: Failed to create leaf texture" << std::endl;
        m_leafTextureNeedsLoad = false;
        return;
    }

    // Upload texture data
    WGPUQueue queue = m_ctx->queue();
    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.bytesPerRow = static_cast<uint32_t>(imageData.width * 4);
    dataLayout.rowsPerImage = static_cast<uint32_t>(imageData.height);

    WGPUExtent3D writeSize = {static_cast<uint32_t>(imageData.width), static_cast<uint32_t>(imageData.height), 1};
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_leafTexture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    wgpuQueueWriteTexture(queue, &destination, imageData.pixels.data(),
                          imageData.pixels.size(),
                          &dataLayout, &writeSize);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    m_leafTextureView = wgpuTextureCreateView(m_leafTexture, &viewDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW = WGPUAddressMode_Repeat;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    m_leafSampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    m_leafTextureNeedsLoad = false;
}

void TreeMesh::releaseLeafTexture() {
    if (m_leafSampler) {
        wgpuSamplerRelease(m_leafSampler);
        m_leafSampler = nullptr;
    }
    if (m_leafTextureView) {
        wgpuTextureViewRelease(m_leafTextureView);
        m_leafTextureView = nullptr;
    }
    if (m_leafTexture) {
        wgpuTextureDestroy(m_leafTexture);
        wgpuTextureRelease(m_leafTexture);
        m_leafTexture = nullptr;
    }
}

} // namespace vivid::render3d
