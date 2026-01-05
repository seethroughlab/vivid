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

    /// Project UVs from an axis (box/planar projection)
    /// @param axis The axis to project from (Y = top-down, Z = front, X = side)
    /// @param scale UV scale factor (default 1.0)
    /// @param offset UV offset (default 0,0)
    MeshBuilder& projectUVs(Axis axis, float scale = 1.0f, glm::vec2 offset = glm::vec2(0));

    /// Project UVs from bounding box (normalized 0-1 across mesh extents)
    /// @param axis The axis to project from
    MeshBuilder& projectUVsNormalized(Axis axis);

    /// Apply procedural noise displacement along vertex normals
    /// @param amplitude Maximum displacement distance
    /// @param frequency Noise frequency/scale (higher = more detail)
    /// @param octaves Number of noise layers (more = finer detail)
    /// @param time Time offset for animation
    MeshBuilder& noiseDisplace(float amplitude, float frequency = 1.0f,
                               int octaves = 4, float time = 0.0f);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Mesh Combination
    /// @{

    /// Append another mesh's geometry (simple concatenation, no CSG)
    /// Use this for combining non-overlapping parts into a single mesh
    MeshBuilder& append(const MeshBuilder& other);

    /// @}
    // -------------------------------------------------------------------------
    /// @name CSG Boolean Operations
    /// @{

    /// Union: combine with another mesh (requires valid manifold geometry)
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

    /// Create a pyramid with n-sided base (default 4 = square pyramid)
    /// Base is centered at origin, apex points up (+Y)
    static MeshBuilder pyramid(float baseWidth, float height, int sides = 4);

    /// Create a wedge (triangular prism / ramp)
    /// Ramp goes from full height at -X to zero height at +X
    static MeshBuilder wedge(float width, float height, float depth);

    /// Create a frustum (truncated cone)
    /// Like a cone but with flat top instead of apex
    static MeshBuilder frustum(float bottomRadius, float topRadius, float height, int segments = 16);

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
