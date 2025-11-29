#pragma once
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace vivid {

// Forward declarations
class Renderer;

/**
 * @brief Standard 3D vertex format supporting normal mapping.
 *
 * Layout matches WGSL vertex input:
 * - position: vec3f @ location(0)
 * - normal: vec3f @ location(1)
 * - uv: vec2f @ location(2)
 * - tangent: vec4f @ location(3) - w component stores bitangent handedness
 */
struct Vertex3D {
    glm::vec3 position{0.0f};   // World-space position
    glm::vec3 normal{0.0f, 1.0f, 0.0f};    // Surface normal
    glm::vec2 uv{0.0f};         // Texture coordinates
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f}; // Tangent (xyz) + bitangent sign (w)
};

/**
 * @brief Axis-aligned bounding box.
 */
struct BoundingBox {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }
    float radius() const { return glm::length(size()) * 0.5f; }
};

/**
 * @brief 3D mesh with vertex and index buffers.
 *
 * Create meshes using Mesh::create() or primitive generators.
 * Meshes are rendered using Mesh::draw() within a render pass.
 */
class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    // Non-copyable, movable
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    /**
     * @brief Create a mesh from vertex and index data.
     * @param renderer The renderer to create GPU resources with.
     * @param vertices Vector of Vertex3D data.
     * @param indices Vector of triangle indices (must be multiple of 3).
     * @return true if creation succeeded.
     */
    bool create(Renderer& renderer,
                const std::vector<Vertex3D>& vertices,
                const std::vector<uint32_t>& indices);

    /**
     * @brief Release GPU resources.
     */
    void destroy();

    /**
     * @brief Draw this mesh in the current render pass.
     * @param encoder Active render pass encoder.
     * @param instanceCount Number of instances to draw (default 1).
     */
    void draw(WGPURenderPassEncoder encoder, uint32_t instanceCount = 1) const;

    /**
     * @brief Check if this mesh has valid GPU resources.
     */
    bool valid() const { return vertexBuffer_ != nullptr && indexBuffer_ != nullptr; }

    // Accessors
    uint32_t vertexCount() const { return vertexCount_; }
    uint32_t indexCount() const { return indexCount_; }
    const BoundingBox& bounds() const { return bounds_; }

    /**
     * @brief Get the vertex buffer layout for pipeline creation.
     * @return Vertex buffer layout descriptor.
     */
    static WGPUVertexBufferLayout getVertexLayout();

private:
    WGPUBuffer vertexBuffer_ = nullptr;
    WGPUBuffer indexBuffer_ = nullptr;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
    BoundingBox bounds_;
};

// Primitive generators
namespace primitives {

/**
 * @brief Generate a unit cube centered at origin.
 * @param vertices Output vertex data.
 * @param indices Output index data.
 */
void generateCube(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices);

/**
 * @brief Generate a plane in the XZ plane.
 * @param vertices Output vertex data.
 * @param indices Output index data.
 * @param width Width in X direction.
 * @param height Height in Z direction.
 * @param subdivisionsX Number of segments in X.
 * @param subdivisionsZ Number of segments in Z.
 */
void generatePlane(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float width = 1.0f, float height = 1.0f,
                   int subdivisionsX = 1, int subdivisionsZ = 1);

/**
 * @brief Generate a UV sphere.
 * @param vertices Output vertex data.
 * @param indices Output index data.
 * @param radius Sphere radius.
 * @param segments Number of horizontal segments.
 * @param rings Number of vertical rings.
 */
void generateSphere(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                    float radius = 0.5f, int segments = 32, int rings = 16);

/**
 * @brief Generate a cylinder aligned to Y axis.
 * @param vertices Output vertex data.
 * @param indices Output index data.
 * @param radius Cylinder radius.
 * @param height Cylinder height.
 * @param segments Number of radial segments.
 */
void generateCylinder(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                      float radius = 0.5f, float height = 1.0f, int segments = 32);

/**
 * @brief Generate a torus.
 * @param vertices Output vertex data.
 * @param indices Output index data.
 * @param majorRadius Distance from center to tube center.
 * @param minorRadius Tube radius.
 * @param majorSegments Segments around the ring.
 * @param minorSegments Segments around the tube.
 */
void generateTorus(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float majorRadius = 0.5f, float minorRadius = 0.2f,
                   int majorSegments = 32, int minorSegments = 16);

} // namespace primitives

} // namespace vivid
