#pragma once

#include <vector>
#include <memory>
#include <glm/glm.hpp>

// Forward declarations for Diligent types
namespace Diligent {
    struct IRenderDevice;
    struct IBuffer;
}

namespace vivid {

/// Vertex structure for 3D rendering
/// Layout matches DiligentFX PBR_Renderer expectations
struct Vertex3D {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    glm::vec3 tangent;

    Vertex3D() = default;
    Vertex3D(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& texcoord, const glm::vec3& tan = glm::vec3(1, 0, 0))
        : position(pos), normal(norm), uv(texcoord), tangent(tan) {}
};

/// Mesh data container
struct MeshData {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    void clear() {
        vertices.clear();
        indices.clear();
    }

    bool empty() const { return vertices.empty(); }
};

/// GPU mesh with vertex and index buffers
class Mesh {
public:
    Mesh() = default;
    ~Mesh();

    // Non-copyable
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Movable
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;

    /// Create GPU buffers from mesh data
    bool create(Diligent::IRenderDevice* device, const MeshData& data);

    /// Release GPU resources
    void release();

    /// Get vertex buffer
    Diligent::IBuffer* vertexBuffer() const { return vertexBuffer_; }

    /// Get index buffer
    Diligent::IBuffer* indexBuffer() const { return indexBuffer_; }

    /// Get number of indices
    uint32_t indexCount() const { return indexCount_; }

    /// Get number of vertices
    uint32_t vertexCount() const { return vertexCount_; }

private:
    Diligent::IBuffer* vertexBuffer_ = nullptr;
    Diligent::IBuffer* indexBuffer_ = nullptr;
    uint32_t vertexCount_ = 0;
    uint32_t indexCount_ = 0;
};

/// Mesh generation utilities
namespace MeshUtils {

/// Create a unit cube centered at origin (side length 1)
MeshData createCube();

/// Create a UV sphere
/// @param segments Number of horizontal segments (longitude)
/// @param rings Number of vertical segments (latitude)
/// @param radius Sphere radius
MeshData createSphere(int segments = 32, int rings = 16, float radius = 0.5f);

/// Create a plane in XZ space
/// @param width Width of the plane
/// @param depth Depth of the plane
/// @param segmentsX Number of segments along X
/// @param segmentsZ Number of segments along Z
MeshData createPlane(float width = 1.0f, float depth = 1.0f, int segmentsX = 1, int segmentsZ = 1);

/// Create a cylinder
/// @param segments Number of radial segments
/// @param radius Cylinder radius
/// @param height Cylinder height
MeshData createCylinder(int segments = 32, float radius = 0.5f, float height = 1.0f);

/// Create a torus (donut)
/// @param segments Number of segments around the tube
/// @param rings Number of segments around the torus
/// @param radius Major radius (center to tube center)
/// @param tubeRadius Minor radius (tube thickness)
MeshData createTorus(int segments = 32, int rings = 16, float radius = 0.5f, float tubeRadius = 0.2f);

/// Create a cone
/// @param segments Number of radial segments
/// @param radius Base radius
/// @param height Cone height
MeshData createCone(int segments = 32, float radius = 0.5f, float height = 1.0f);

/// Calculate tangent vectors for a mesh
void calculateTangents(MeshData& mesh);

} // namespace MeshUtils

} // namespace vivid
