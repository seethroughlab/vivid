#pragma once

#include <vivid/mesh.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>

namespace vivid::csg {

/**
 * @brief Result of CSG operations - compatible with vivid::MeshData
 */
struct CSGMesh {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    bool valid() const { return !vertices.empty() && !indices.empty(); }

    // Combine with another mesh (concatenate geometry)
    CSGMesh& append(const CSGMesh& other);

    // Convert to MeshData for use with Mesh::create()
    MeshData toMeshData() const {
        MeshData data;
        data.vertices = vertices;
        data.indices = indices;
        return data;
    }
};

/**
 * @brief CSG solid for boolean operations.
 *
 * Wraps Manifold library for fast, robust CSG with guaranteed manifold output.
 * All operations return new Solid objects (immutable pattern).
 */
class Solid {
public:
    Solid();
    ~Solid();
    Solid(const Solid& other);
    Solid& operator=(const Solid& other);
    Solid(Solid&& other) noexcept;
    Solid& operator=(Solid&& other) noexcept;

    // === PRIMITIVES ===

    /// Create a box centered at origin
    static Solid box(float width, float height, float depth);
    static Solid box(glm::vec3 size);

    /// Create a sphere centered at origin
    static Solid sphere(float radius, int segments = 24);

    /// Create a cylinder along Y axis, centered at origin
    static Solid cylinder(float radius, float height, int segments = 32);

    /// Create a cone along Y axis, base at origin
    static Solid cone(float radius, float height, int segments = 32);

    /// Create a torus centered at origin, lying in XZ plane
    static Solid torus(float majorRadius, float minorRadius,
                       int majorSegments = 32, int minorSegments = 16);

    /// Create from existing triangle mesh (must be manifold/watertight)
    static Solid fromMesh(const std::vector<Vertex3D>& vertices,
                          const std::vector<uint32_t>& indices);

    /// Create from MeshData
    static Solid fromMeshData(const MeshData& data) {
        return fromMesh(data.vertices, data.indices);
    }

    // === BOOLEAN OPERATIONS ===

    /// Union: combine two solids
    Solid unite(const Solid& other) const;
    Solid operator+(const Solid& other) const { return unite(other); }

    /// Subtraction: remove other from this
    Solid subtract(const Solid& other) const;
    Solid operator-(const Solid& other) const { return subtract(other); }

    /// Intersection: keep only overlapping regions
    Solid intersect(const Solid& other) const;
    Solid operator&(const Solid& other) const { return intersect(other); }

    // === TRANSFORMS ===

    /// Translate the solid
    Solid translate(float x, float y, float z) const;
    Solid translate(glm::vec3 offset) const;

    /// Rotate around axis (angle in radians)
    Solid rotate(float angle, glm::vec3 axis) const;

    /// Rotate by Euler angles (in radians)
    Solid rotateX(float angle) const;
    Solid rotateY(float angle) const;
    Solid rotateZ(float angle) const;

    /// Scale uniformly or non-uniformly
    Solid scale(float factor) const;
    Solid scale(float x, float y, float z) const;
    Solid scale(glm::vec3 factors) const;

    /// Apply arbitrary 4x4 transform matrix
    Solid transform(const glm::mat4& matrix) const;

    /// Mirror across a plane (specified by normal through origin)
    Solid mirror(glm::vec3 normal) const;

    // === OUTPUT ===

    /// Export to CSGMesh format
    CSGMesh toMesh() const;

    /// Export directly to MeshData (convenience)
    MeshData toMeshData() const { return toMesh().toMeshData(); }

    /// Check if the solid is valid (non-empty manifold)
    bool valid() const;

    /// Get approximate triangle count
    size_t triangleCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// === CONVENIENCE FUNCTIONS ===

/// Create a rounded box (box with filleted edges)
Solid roundedBox(float width, float height, float depth, float radius, int segments = 8);

/// Create a hexagonal prism along Y axis
Solid hexPrism(float radius, float height);

/// Create a wedge/ramp shape
Solid wedge(float width, float height, float depth);

/// Create an array of solids in a line
Solid linearArray(const Solid& base, glm::vec3 spacing, int count);

/// Create a radial array of solids around Y axis
Solid radialArray(const Solid& base, int count, float radius = 0.0f);

} // namespace vivid::csg
