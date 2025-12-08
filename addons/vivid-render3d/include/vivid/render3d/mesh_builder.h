#pragma once

#include <vivid/render3d/mesh.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <memory>

namespace manifold {
    class Manifold;
}

namespace vivid::render3d {

/// Axis for mirroring operations
enum class Axis { X, Y, Z };

/// Builder for constructing meshes procedurally
///
/// MeshBuilder can operate in two modes:
/// 1. Direct vertex mode: Add vertices/faces manually (not manifold-safe)
/// 2. Manifold mode: Created via primitive generators, supports CSG operations
class MeshBuilder {
public:
    MeshBuilder();
    ~MeshBuilder();
    MeshBuilder(const MeshBuilder& other);
    MeshBuilder& operator=(const MeshBuilder& other);
    MeshBuilder(MeshBuilder&& other) noexcept;
    MeshBuilder& operator=(MeshBuilder&& other) noexcept;

    // -------------------------------------------------------------------------
    /// @name Vertex Manipulation
    /// @{

    /// Add a vertex with just position
    MeshBuilder& addVertex(glm::vec3 pos);

    /// Add a vertex with position and normal
    MeshBuilder& addVertex(glm::vec3 pos, glm::vec3 normal);

    /// Add a vertex with position, normal, and UV
    MeshBuilder& addVertex(glm::vec3 pos, glm::vec3 normal, glm::vec2 uv);

    /// Add a vertex with all attributes
    MeshBuilder& addVertex(glm::vec3 pos, glm::vec3 normal, glm::vec2 uv, glm::vec4 color);

    /// Add a complete Vertex3D
    MeshBuilder& addVertex(const Vertex3D& v);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Face Construction
    /// @{

    /// Add a triangle from vertex indices
    MeshBuilder& addTriangle(uint32_t a, uint32_t b, uint32_t c);

    /// Add a quad from vertex indices (splits into 2 triangles)
    MeshBuilder& addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Modifiers
    /// @{

    /// Compute smooth normals from face data
    MeshBuilder& computeNormals();

    /// Compute flat normals (faceted look, duplicates vertices)
    MeshBuilder& computeFlatNormals();

    /// Compute tangents for normal mapping (requires valid UVs)
    MeshBuilder& computeTangents();

    /// Apply a transformation matrix
    MeshBuilder& transform(const glm::mat4& m);

    /// Translate all vertices
    MeshBuilder& translate(glm::vec3 offset);

    /// Scale all vertices
    MeshBuilder& scale(glm::vec3 s);

    /// Scale uniformly
    MeshBuilder& scale(float s);

    /// Rotate around an axis (angle in radians)
    MeshBuilder& rotate(float angle, glm::vec3 axis);

    /// Mirror across an axis (creates symmetric copy)
    MeshBuilder& mirror(Axis axis);

    /// Invert normals and winding order
    MeshBuilder& invert();

    /// @}
    // -------------------------------------------------------------------------
    /// @name CSG Boolean Operations
    /// @{

    /// Union: combine with another mesh
    MeshBuilder& add(const MeshBuilder& other);

    /// Difference: subtract another mesh
    MeshBuilder& subtract(const MeshBuilder& other);

    /// Intersection: keep only overlapping volume
    MeshBuilder& intersect(const MeshBuilder& other);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Build
    /// @{

    /// Build the final mesh
    Mesh build();

    /// Clear all data
    void clear();

    /// Get current vertex count
    size_t vertexCount() const { return m_vertices.size(); }

    /// Get current index count
    size_t indexCount() const { return m_indices.size(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Primitive Generators
    /// @{

    /// Create a box
    static MeshBuilder box(float w, float h, float d);
    static MeshBuilder box(glm::vec3 size);

    /// Create a sphere
    static MeshBuilder sphere(float radius, int segments = 16);

    /// Create a cylinder
    static MeshBuilder cylinder(float radius, float height, int segments = 16);

    /// Create a cone
    static MeshBuilder cone(float radius, float height, int segments = 16);

    /// Create a torus
    static MeshBuilder torus(float outerRadius, float innerRadius,
                             int segments = 16, int rings = 16);

    /// Create a plane (XZ plane, Y up)
    static MeshBuilder plane(float width, float height,
                             int subdivisionsX = 1, int subdivisionsY = 1);

    /// @}

    /// Check if this builder has valid manifold data for CSG
    bool isManifold() const { return m_manifold != nullptr; }

private:
    std::vector<Vertex3D> m_vertices;
    std::vector<uint32_t> m_indices;

    // Internal manifold representation for CSG operations
    std::unique_ptr<manifold::Manifold> m_manifold;

    // Sync manifold data to vertices/indices
    void syncFromManifold();

    // Create manifold from current vertices/indices (best effort)
    void syncToManifold();

    // Private constructor from manifold
    explicit MeshBuilder(std::unique_ptr<manifold::Manifold> m);
};

} // namespace vivid::render3d
