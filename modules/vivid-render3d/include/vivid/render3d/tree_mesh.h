#pragma once

/**
 * @file tree_mesh.h
 * @brief L-System based procedural tree mesh generator
 *
 * Generates tree geometry using L-System grammar expansion and turtle
 * interpretation. Supports multiple tree types with configurable branching
 * patterns and billboard leaf clusters.
 *
 * Used with Render3D for unified rendering with shadows and lighting.
 */

#include <vivid/render3d/procedural_mesh.h>
#include <vivid/param.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <utility>

namespace vivid::render3d {

/**
 * @brief L-System grammar for procedural tree generation
 *
 * Defines the production rules that generate tree branching structure.
 *
 * Symbol set:
 * - F: Draw branch segment forward
 * - +/-: Yaw rotation (around Y)
 * - ^/&: Pitch rotation (tilt up/down)
 * - [/]: Push/pop state (branch point)
 * - L: Leaf cluster (billboard)
 * - !: Decrease radius
 */
struct LSystemGrammar {
    std::string axiom = "FA";                    ///< Starting string
    std::vector<std::pair<char, std::string>> rules;  ///< Production rules
    float angle = 25.0f;                         ///< Branch angle in degrees
    float lengthScale = 0.9f;                    ///< Length reduction per iteration
    float radiusScale = 0.7f;                    ///< Radius reduction per iteration
    int iterations = 4;                          ///< Number of rule applications
};

/**
 * @brief Turtle state for L-System interpretation
 *
 * Tracks position and orientation during L-System string traversal.
 * Used internally for generating branch geometry.
 */
struct TurtleState {
    glm::vec3 position{0.0f};           ///< Current position in 3D space
    glm::vec3 heading{0.0f, 1.0f, 0.0f}; ///< Forward direction (up for trees)
    glm::vec3 left{1.0f, 0.0f, 0.0f};    ///< Left direction
    glm::vec3 up{0.0f, 0.0f, 1.0f};      ///< Up direction (perpendicular to heading)
    float length = 1.0f;                 ///< Current segment length
    float radius = 0.1f;                 ///< Current branch radius
    int depth = 0;                       ///< Branch depth (0 = trunk)
};

/**
 * @brief L-System based procedural tree mesh generator
 *
 * Generates tree geometry using L-System grammar expansion and turtle
 * graphics interpretation. Each tree type has a preset grammar that
 * produces characteristic branching patterns.
 *
 * Trees are rendered with:
 * - Tapered cylindrical branches with smooth joints
 * - Billboard leaf clusters that face the camera
 * - Wind animation for both branches and leaves
 *
 * @par Example
 * @code
 * auto& camera = chain.add<CameraOperator>("camera");
 * auto& sun = chain.add<DirectionalLight>("sun");
 *
 * auto& trees = chain.add<TreeMesh>("trees");
 * trees.setTreeType(TreeMesh::TreeType::Deciduous);
 * trees.treeCount = 10;
 *
 * auto& render = chain.add<Render3D>("render");
 * render.setCameraInput(&camera);
 * render.setLightInput(&sun);
 * render.addProceduralMesh(&trees);
 * @endcode
 */
class TreeMesh : public ProceduralMesh {
public:
    /**
     * @brief Tree type presets with tuned L-System grammars
     */
    enum class TreeType {
        Deciduous,  ///< Spreading crown with multiple main branches
        Conifer,    ///< Pyramidal shape with horizontal branch layers
        Palm,       ///< Tall trunk with frond crown at top
        Willow,     ///< Drooping branches with cascading foliage
        Bushy,      ///< Dense shrub-like structure
        Custom      ///< User-defined L-System grammar
    };

    TreeMesh();
    ~TreeMesh() override;

    // =========================================================================
    /// @name Tree Type
    /// @{

    /// Set tree type preset (adjusts L-System grammar and parameters)
    void setTreeType(TreeType type);

    /// Get current tree type
    TreeType treeType() const { return m_treeType; }

    /// Set custom L-System grammar (switches to Custom type)
    void setGrammar(const LSystemGrammar& grammar);

    /// Get current L-System grammar
    const LSystemGrammar& grammar() const { return m_grammar; }

    /// @}
    // =========================================================================
    /// @name Field Parameters
    /// @{

    /// Width of the tree field (X axis)
    Param<float> fieldWidth{"fieldWidth", 20.0f, 1.0f, 200.0f};

    /// Depth of the tree field (Z axis)
    Param<float> fieldDepth{"fieldDepth", 20.0f, 1.0f, 200.0f};

    /// Number of trees
    Param<int> treeCount{"treeCount", 5, 1, 100};

    /// Random seed for placement and variation
    Param<int> seed{"seed", 12345, 0, 99999};

    /// @}
    // =========================================================================
    /// @name Trunk Parameters
    /// @{

    /// Height of the initial trunk segment
    Param<float> trunkHeight{"trunkHeight", 2.0f, 0.5f, 10.0f};

    /// Radius of the trunk at base
    Param<float> trunkRadius{"trunkRadius", 0.15f, 0.02f, 0.5f};

    /// @}
    // =========================================================================
    /// @name Branch Parameters
    /// @{

    /// Number of L-System iterations (more = more branches)
    Param<int> lsystemIterations{"lsystemIterations", 4, 1, 7};

    /// Branch angle in degrees
    Param<float> branchAngle{"branchAngle", 25.0f, 10.0f, 60.0f};

    /// Length scale factor per branch level (0.5 = half length each level)
    Param<float> lengthScale{"lengthScale", 0.9f, 0.5f, 1.0f};

    /// Radius scale factor per branch level
    Param<float> radiusScale{"radiusScale", 0.7f, 0.4f, 0.9f};

    /// @}
    // =========================================================================
    /// @name Leaf Parameters
    /// @{

    /// Number of leaf billboards per cluster
    Param<int> leafDensity{"leafDensity", 8, 1, 30};

    /// Size of individual leaf billboards
    Param<float> leafSize{"leafSize", 0.3f, 0.05f, 1.0f};

    /// Spread radius of leaf clusters
    Param<float> clusterRadius{"clusterRadius", 0.4f, 0.1f, 1.5f};

    /// @}
    // =========================================================================
    /// @name Wind Parameters
    /// @{

    /// Branch wind strength (displacement amount)
    Param<float> windStrength{"windStrength", 0.2f, 0.0f, 1.0f};

    /// Wind animation speed
    Param<float> windSpeed{"windSpeed", 0.8f, 0.1f, 3.0f};

    /// Wind direction X component
    Param<float> windDirX{"windDirX", 1.0f, -1.0f, 1.0f};

    /// Wind direction Z component
    Param<float> windDirZ{"windDirZ", 0.3f, -1.0f, 1.0f};

    /// Leaf flutter intensity (additional high-frequency motion for leaves)
    Param<float> leafFlutter{"leafFlutter", 0.3f, 0.0f, 1.0f};

    /// @}
    // =========================================================================
    /// @name Color Parameters
    /// @{

    /// Trunk base color (at ground level)
    float trunkBaseColor[3] = {0.25f, 0.15f, 0.08f};

    /// Trunk tip color (at branch tips)
    float trunkTipColor[3] = {0.35f, 0.25f, 0.15f};

    /// Leaf color (billboard tint)
    float leafColor[3] = {0.15f, 0.4f, 0.1f};

    /// @}
    // =========================================================================
    /// @name Leaf Texture
    /// @{

    /**
     * @brief Set leaf texture for alpha-masked billboards
     *
     * When set, leaf billboards will sample from this texture and discard
     * fragments with alpha < 0.5, creating natural leaf shapes.
     *
     * @param texturePath Path to texture file (PNG with alpha channel)
     */
    void setLeafTexture(const std::string& texturePath);

    /**
     * @brief Clear leaf texture (revert to solid color)
     */
    void clearLeafTexture();

    /**
     * @brief Get the current leaf texture path
     */
    const std::string& leafTexturePath() const { return m_leafTexturePath; }

    /// @}
    // =========================================================================
    /// @name ProceduralMesh Interface
    /// @{

    /// Get the procedural tree mesh
    const Mesh& getMesh() const override { return m_treeMesh; }

    /// Get instance transforms and variations
    const std::vector<ProceduralInstance>& getInstances() const override { return m_instances; }

    /// Get wind animation parameters
    WindParams getWindParams() const override;

    /// Get base color for gradient (trunk base)
    glm::vec3 getBaseColor() const override {
        return glm::vec3(trunkBaseColor[0], trunkBaseColor[1], trunkBaseColor[2]);
    }

    /// Get tip color for gradient (trunk tip)
    glm::vec3 getTipColor() const override {
        return glm::vec3(trunkTipColor[0], trunkTipColor[1], trunkTipColor[2]);
    }

    /// Get leaf color for billboards
    glm::vec3 getLeafColor() const {
        return glm::vec3(leafColor[0], leafColor[1], leafColor[2]);
    }

    /// Get leaf flutter intensity (for shader)
    float getLeafFlutter() const { return static_cast<float>(leafFlutter); }

    /// Check if leaf texture is set
    bool hasLeafTexture() const override { return m_leafTextureView != nullptr; }

    /// Get leaf texture view for shader
    WGPUTextureView getLeafTextureView() const override { return m_leafTextureView; }

    /// Get leaf sampler for shader
    WGPUSampler getLeafSampler() const override { return m_leafSampler; }

    /// @}
    // =========================================================================
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override {}
    std::string name() const override { return "TreeMesh"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    void generateTree();
    std::string expandLSystem();
    void interpretLSystem(const std::string& lstring);
    void generateBranchSegment(const TurtleState& start, const TurtleState& end);
    void generateLeafCluster(const TurtleState& turtle);
    void generateInstances();
    void applyPreset(TreeType type);
    void checkForChanges();

    // Tree type and grammar
    TreeType m_treeType = TreeType::Deciduous;
    LSystemGrammar m_grammar;

    // Procedural mesh
    Mesh m_treeMesh;

    // Instance data
    std::vector<ProceduralInstance> m_instances;

    // Track parameters for change detection
    float m_lastTrunkHeight = 0;
    float m_lastTrunkRadius = 0;
    int m_lastIterations = 0;
    float m_lastBranchAngle = 0;
    float m_lastLengthScale = 0;
    float m_lastRadiusScale = 0;
    int m_lastLeafDensity = 0;
    float m_lastLeafSize = 0;
    float m_lastClusterRadius = 0;

    // Track instance params for change detection
    int m_lastTreeCount = 0;
    int m_lastSeed = 0;
    float m_lastFieldWidth = 0;
    float m_lastFieldDepth = 0;

    // Leaf texture
    std::string m_leafTexturePath;
    WGPUTexture m_leafTexture = nullptr;
    WGPUTextureView m_leafTextureView = nullptr;
    WGPUSampler m_leafSampler = nullptr;
    bool m_leafTextureNeedsLoad = false;

    // Context pointer for texture loading
    Context* m_ctx = nullptr;

    bool m_initialized = false;

    // Helper for texture loading
    void loadLeafTexture();
    void releaseLeafTexture();
};

} // namespace vivid::render3d
