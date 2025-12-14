#pragma once

/**
 * @file plexus.h
 * @brief GPU-based plexus effect - particles connected by proximity lines
 *
 * Creates a network visualization where nodes (particles) are connected
 * by lines when within a specified distance. All rendering is GPU-based.
 */

#include <vivid/effects/texture_operator.h>
#include <glm/glm.hpp>
#include <vector>
#include <random>

namespace vivid::effects {

/**
 * @brief GPU-accelerated plexus network effect
 *
 * Renders a particle network where nearby nodes are connected by lines.
 * Both particles and connections are rendered on the GPU using instancing.
 *
 * @par Example
 * @code
 * auto& plexus = chain.add<Plexus>("net");
 * plexus.nodeCount(300)
 *       .connectionDistance(0.1f)
 *       .nodeColor(0.0f, 0.8f, 1.0f, 1.0f)
 *       .lineColor(0.0f, 0.6f, 0.9f, 0.4f)
 *       .turbulence(0.1f);
 * @endcode
 */
class Plexus : public TextureOperator {
public:
    Plexus();
    ~Plexus() override;

    // -------------------------------------------------------------------------
    /// @name Node Configuration
    /// @{

    /// Set number of nodes
    Plexus& nodeCount(int count) { m_nodeCount = count; return *this; }

    /// Set node size (normalized, 0-1)
    Plexus& nodeSize(float size) { m_nodeSize = size; return *this; }

    /// Set node color
    Plexus& nodeColor(float r, float g, float b, float a = 1.0f) {
        m_nodeColor = {r, g, b, a};
        return *this;
    }
    Plexus& nodeColor(const glm::vec4& c) { m_nodeColor = c; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Connection Configuration
    /// @{

    /// Maximum distance for connections (normalized, 0-1)
    Plexus& connectionDistance(float dist) { m_connectionDist = dist; return *this; }

    /// Line width in pixels
    Plexus& lineWidth(float width) { m_lineWidth = width; return *this; }

    /// Line color (alpha used for max opacity, fades with distance)
    Plexus& lineColor(float r, float g, float b, float a = 0.5f) {
        m_lineColor = {r, g, b, a};
        return *this;
    }
    Plexus& lineColor(const glm::vec4& c) { m_lineColor = c; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Physics
    /// @{

    /// Noise-based movement
    Plexus& turbulence(float t) { m_turbulence = t; return *this; }

    /// Velocity damping
    Plexus& drag(float d) { m_drag = d; return *this; }

    /// Center attraction strength
    Plexus& centerAttraction(float s) { m_centerAttraction = s; return *this; }

    /// Spawn area (0-1, centered)
    Plexus& spread(float s) { m_spread = s; return *this; }

    /// Depth spread for 3D mode (0 = flat, higher = more depth)
    Plexus& depth(float d) { m_depth = d; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Camera
    /// @{

    /// Enable 3D mode with camera orbit
    Plexus& enable3D(bool enable) { m_3dEnabled = enable; return *this; }

    /// Camera distance from center
    Plexus& cameraDistance(float d) { m_cameraDistance = d; return *this; }

    /// Auto-rotate camera (radians per second)
    Plexus& autoRotate(float speed) { m_autoRotateSpeed = speed; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Rendering
    /// @{

    /// Background color (use alpha=0 for transparent overlay)
    Plexus& clearColor(float r, float g, float b, float a = 1.0f) {
        m_clearColor = {r, g, b, a};
        return *this;
    }

    /// Random seed for reproducible patterns
    Plexus& seed(int s) { m_seed = s; m_rng.seed(s); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Plexus"; }

    /// @}

private:
    struct Node {
        glm::vec3 position;
        glm::vec3 velocity;
    };

    struct LineInstance {
        glm::vec4 start;  // xyz + pad
        glm::vec4 end;    // xyz + alpha
    };

    struct NodeInstance {
        glm::vec4 position;  // xyz + size
        glm::vec4 color;
    };

    void initNodes();
    void updateNodes(float dt, float time);
    void findConnections();
    void createPipelines(WGPUDevice device);
    void renderLines(Context& ctx);
    void renderNodes(Context& ctx);

    // Configuration
    int m_nodeCount = 200;
    float m_nodeSize = 0.004f;
    glm::vec4 m_nodeColor{1.0f, 1.0f, 1.0f, 0.8f};
    float m_connectionDist = 0.1f;
    float m_lineWidth = 1.0f;
    glm::vec4 m_lineColor{1.0f, 1.0f, 1.0f, 0.4f};
    float m_turbulence = 0.1f;
    float m_drag = 0.5f;
    float m_centerAttraction = 0.02f;
    float m_spread = 0.8f;
    float m_depth = 0.5f;
    glm::vec4 m_clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    int m_seed = 42;

    // 3D Camera
    bool m_3dEnabled = false;
    float m_cameraDistance = 2.5f;
    float m_autoRotateSpeed = 0.2f;
    float m_cameraAngle = 0.0f;

    // State
    std::vector<Node> m_nodes;
    std::vector<LineInstance> m_lines;
    std::vector<NodeInstance> m_nodeInstances;
    std::mt19937 m_rng;
    bool m_initialized = false;
    bool m_nodesInitialized = false;

    // GPU resources - Lines
    WGPURenderPipeline m_linePipeline = nullptr;
    WGPUBuffer m_lineVertexBuffer = nullptr;
    WGPUBuffer m_lineInstanceBuffer = nullptr;
    WGPUBuffer m_lineUniformBuffer = nullptr;
    WGPUBindGroupLayout m_lineBindGroupLayout = nullptr;
    WGPUBindGroup m_lineBindGroup = nullptr;
    size_t m_lineInstanceCapacity = 0;

    // GPU resources - Nodes
    WGPURenderPipeline m_nodePipeline = nullptr;
    WGPUBuffer m_nodeVertexBuffer = nullptr;
    WGPUBuffer m_nodeIndexBuffer = nullptr;
    WGPUBuffer m_nodeInstanceBuffer = nullptr;
    WGPUBuffer m_nodeUniformBuffer = nullptr;
    WGPUBindGroupLayout m_nodeBindGroupLayout = nullptr;
    WGPUBindGroup m_nodeBindGroup = nullptr;
    size_t m_nodeInstanceCapacity = 0;
    uint32_t m_nodeIndexCount = 0;
};

} // namespace vivid::effects
