#include <vivid/render3d/mesh_builder.h>
#include <glm/gtc/matrix_transform.hpp>
#include <manifold/manifold.h>
#include <cmath>
#include <unordered_map>

namespace vivid::render3d {

// -----------------------------------------------------------------------------
// Constructors / Assignment
// -----------------------------------------------------------------------------

MeshBuilder::MeshBuilder() = default;

MeshBuilder::~MeshBuilder() = default;

MeshBuilder::MeshBuilder(const MeshBuilder& other)
    : m_vertices(other.m_vertices)
    , m_indices(other.m_indices)
{
    if (other.m_manifold) {
        m_manifold = std::make_unique<manifold::Manifold>(*other.m_manifold);
    }
}

MeshBuilder& MeshBuilder::operator=(const MeshBuilder& other) {
    if (this != &other) {
        m_vertices = other.m_vertices;
        m_indices = other.m_indices;
        if (other.m_manifold) {
            m_manifold = std::make_unique<manifold::Manifold>(*other.m_manifold);
        } else {
            m_manifold.reset();
        }
    }
    return *this;
}

MeshBuilder::MeshBuilder(MeshBuilder&& other) noexcept
    : m_vertices(std::move(other.m_vertices))
    , m_indices(std::move(other.m_indices))
    , m_manifold(std::move(other.m_manifold))
{}

MeshBuilder& MeshBuilder::operator=(MeshBuilder&& other) noexcept {
    if (this != &other) {
        m_vertices = std::move(other.m_vertices);
        m_indices = std::move(other.m_indices);
        m_manifold = std::move(other.m_manifold);
    }
    return *this;
}

MeshBuilder::MeshBuilder(std::unique_ptr<manifold::Manifold> m)
    : m_manifold(std::move(m))
{
    syncFromManifold();
}

void MeshBuilder::syncFromManifold() {
    m_vertices.clear();
    m_indices.clear();

    if (!m_manifold || m_manifold->IsEmpty()) {
        return;
    }

    manifold::MeshGL mesh = m_manifold->GetMeshGL();

    // Extract vertices
    size_t numVerts = mesh.vertProperties.size() / mesh.numProp;
    m_vertices.reserve(numVerts);

    for (size_t i = 0; i < numVerts; ++i) {
        Vertex3D v;
        v.position.x = mesh.vertProperties[i * mesh.numProp + 0];
        v.position.y = mesh.vertProperties[i * mesh.numProp + 1];
        v.position.z = mesh.vertProperties[i * mesh.numProp + 2];
        v.normal = glm::vec3(0, 1, 0);  // Will be recomputed
        v.uv = glm::vec2(0);
        v.color = glm::vec4(1);
        m_vertices.push_back(v);
    }

    // Copy indices
    m_indices.reserve(mesh.triVerts.size());
    for (uint32_t idx : mesh.triVerts) {
        m_indices.push_back(idx);
    }
}

void MeshBuilder::syncToManifold() {
    if (m_vertices.empty() || m_indices.empty()) {
        m_manifold.reset();
        return;
    }

    manifold::MeshGL mesh;
    mesh.numProp = 3;  // Just positions for CSG

    // Copy vertex positions
    mesh.vertProperties.reserve(m_vertices.size() * 3);
    for (const auto& v : m_vertices) {
        mesh.vertProperties.push_back(v.position.x);
        mesh.vertProperties.push_back(v.position.y);
        mesh.vertProperties.push_back(v.position.z);
    }

    // Copy indices
    mesh.triVerts.reserve(m_indices.size());
    for (uint32_t idx : m_indices) {
        mesh.triVerts.push_back(idx);
    }

    // Try to merge vertices to create manifold mesh
    mesh.Merge();

    m_manifold = std::make_unique<manifold::Manifold>(mesh);
}

// -----------------------------------------------------------------------------
// Vertex Manipulation
// -----------------------------------------------------------------------------

MeshBuilder& MeshBuilder::addVertex(glm::vec3 pos) {
    m_vertices.emplace_back(pos);
    return *this;
}

MeshBuilder& MeshBuilder::addVertex(glm::vec3 pos, glm::vec3 normal) {
    m_vertices.emplace_back(pos, normal);
    return *this;
}

MeshBuilder& MeshBuilder::addVertex(glm::vec3 pos, glm::vec3 normal, glm::vec2 uv) {
    m_vertices.emplace_back(pos, normal, uv);
    return *this;
}

MeshBuilder& MeshBuilder::addVertex(glm::vec3 pos, glm::vec3 normal, glm::vec2 uv, glm::vec4 color) {
    m_vertices.emplace_back(pos, normal, uv, color);
    return *this;
}

MeshBuilder& MeshBuilder::addVertex(const Vertex3D& v) {
    m_vertices.push_back(v);
    return *this;
}

// -----------------------------------------------------------------------------
// Face Construction
// -----------------------------------------------------------------------------

MeshBuilder& MeshBuilder::addTriangle(uint32_t a, uint32_t b, uint32_t c) {
    m_indices.push_back(a);
    m_indices.push_back(b);
    m_indices.push_back(c);
    return *this;
}

MeshBuilder& MeshBuilder::addQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    // Split quad into two triangles
    addTriangle(a, b, c);
    addTriangle(a, c, d);
    return *this;
}

// -----------------------------------------------------------------------------
// Modifiers
// -----------------------------------------------------------------------------

MeshBuilder& MeshBuilder::computeNormals() {
    // Reset all normals to zero
    for (auto& v : m_vertices) {
        v.normal = glm::vec3(0);
    }

    // Accumulate face normals at each vertex
    for (size_t i = 0; i + 2 < m_indices.size(); i += 3) {
        uint32_t i0 = m_indices[i];
        uint32_t i1 = m_indices[i + 1];
        uint32_t i2 = m_indices[i + 2];

        glm::vec3 v0 = m_vertices[i0].position;
        glm::vec3 v1 = m_vertices[i1].position;
        glm::vec3 v2 = m_vertices[i2].position;

        glm::vec3 normal = glm::cross(v1 - v0, v2 - v0);

        m_vertices[i0].normal += normal;
        m_vertices[i1].normal += normal;
        m_vertices[i2].normal += normal;
    }

    // Normalize
    for (auto& v : m_vertices) {
        float len = glm::length(v.normal);
        if (len > 0.0001f) {
            v.normal /= len;
        }
    }

    return *this;
}

MeshBuilder& MeshBuilder::computeFlatNormals() {
    // Create new vertex/index lists with duplicated vertices per face
    std::vector<Vertex3D> newVertices;
    std::vector<uint32_t> newIndices;

    for (size_t i = 0; i + 2 < m_indices.size(); i += 3) {
        Vertex3D v0 = m_vertices[m_indices[i]];
        Vertex3D v1 = m_vertices[m_indices[i + 1]];
        Vertex3D v2 = m_vertices[m_indices[i + 2]];

        // Compute face normal
        glm::vec3 normal = glm::normalize(glm::cross(
            v1.position - v0.position,
            v2.position - v0.position
        ));

        // Apply normal to all three vertices
        v0.normal = normal;
        v1.normal = normal;
        v2.normal = normal;

        uint32_t baseIdx = static_cast<uint32_t>(newVertices.size());
        newVertices.push_back(v0);
        newVertices.push_back(v1);
        newVertices.push_back(v2);

        newIndices.push_back(baseIdx);
        newIndices.push_back(baseIdx + 1);
        newIndices.push_back(baseIdx + 2);
    }

    m_vertices = std::move(newVertices);
    m_indices = std::move(newIndices);

    return *this;
}

MeshBuilder& MeshBuilder::transform(const glm::mat4& m) {
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(m)));

    for (auto& v : m_vertices) {
        v.position = glm::vec3(m * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(normalMatrix * v.normal);
    }

    return *this;
}

MeshBuilder& MeshBuilder::translate(glm::vec3 offset) {
    for (auto& v : m_vertices) {
        v.position += offset;
    }
    return *this;
}

MeshBuilder& MeshBuilder::scale(glm::vec3 s) {
    for (auto& v : m_vertices) {
        v.position *= s;
    }
    return *this;
}

MeshBuilder& MeshBuilder::scale(float s) {
    return scale(glm::vec3(s));
}

MeshBuilder& MeshBuilder::rotate(float angle, glm::vec3 axis) {
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), angle, axis);
    return transform(m);
}

MeshBuilder& MeshBuilder::mirror(Axis axis) {
    // Create mirrored copy
    size_t originalVertexCount = m_vertices.size();
    size_t originalIndexCount = m_indices.size();

    // Duplicate all vertices with mirrored positions
    for (size_t i = 0; i < originalVertexCount; ++i) {
        Vertex3D v = m_vertices[i];

        switch (axis) {
            case Axis::X:
                v.position.x = -v.position.x;
                v.normal.x = -v.normal.x;
                break;
            case Axis::Y:
                v.position.y = -v.position.y;
                v.normal.y = -v.normal.y;
                break;
            case Axis::Z:
                v.position.z = -v.position.z;
                v.normal.z = -v.normal.z;
                break;
        }

        m_vertices.push_back(v);
    }

    // Duplicate indices with reversed winding for mirrored faces
    for (size_t i = 0; i + 2 < originalIndexCount; i += 3) {
        uint32_t offset = static_cast<uint32_t>(originalVertexCount);
        // Reverse winding order for correct culling
        m_indices.push_back(m_indices[i] + offset);
        m_indices.push_back(m_indices[i + 2] + offset);
        m_indices.push_back(m_indices[i + 1] + offset);
    }

    return *this;
}

MeshBuilder& MeshBuilder::invert() {
    // Flip normals
    for (auto& v : m_vertices) {
        v.normal = -v.normal;
    }

    // Reverse winding order
    for (size_t i = 0; i + 2 < m_indices.size(); i += 3) {
        std::swap(m_indices[i + 1], m_indices[i + 2]);
    }

    return *this;
}

// -----------------------------------------------------------------------------
// Build
// -----------------------------------------------------------------------------

Mesh MeshBuilder::build() {
    Mesh mesh;
    mesh.vertices = m_vertices;
    mesh.indices = m_indices;
    return mesh;
}

void MeshBuilder::clear() {
    m_vertices.clear();
    m_indices.clear();
}

// -----------------------------------------------------------------------------
// Primitive Generators
// -----------------------------------------------------------------------------

MeshBuilder MeshBuilder::box(float w, float h, float d) {
    return box(glm::vec3(w, h, d));
}

MeshBuilder MeshBuilder::box(glm::vec3 size) {
    // Use Manifold's built-in cube for CSG-safe geometry
    auto m = std::make_unique<manifold::Manifold>(
        manifold::Manifold::Cube(manifold::vec3(size.x, size.y, size.z), true));

    MeshBuilder builder(std::move(m));
    return builder;
}

MeshBuilder MeshBuilder::sphere(float radius, int segments) {
    // Use Manifold's built-in sphere for CSG-safe geometry
    auto m = std::make_unique<manifold::Manifold>(
        manifold::Manifold::Sphere(static_cast<double>(radius), segments));

    MeshBuilder builder(std::move(m));
    return builder;
}

MeshBuilder MeshBuilder::cylinder(float radius, float height, int segments) {
    // Use Manifold's built-in cylinder for CSG-safe geometry
    // Manifold::Cylinder takes (height, radiusLow, radiusHigh, circularSegments, center)
    auto m = std::make_unique<manifold::Manifold>(
        manifold::Manifold::Cylinder(static_cast<double>(height),
                                      static_cast<double>(radius),
                                      static_cast<double>(radius),
                                      segments, true));

    MeshBuilder builder(std::move(m));
    return builder;
}

MeshBuilder MeshBuilder::cone(float radius, float height, int segments) {
    // Use Manifold's cylinder with 0 radius at top for a cone
    auto m = std::make_unique<manifold::Manifold>(
        manifold::Manifold::Cylinder(static_cast<double>(height),
                                      static_cast<double>(radius),
                                      0.0,  // radiusHigh = 0 for cone
                                      segments, true));

    MeshBuilder builder(std::move(m));
    return builder;
}

MeshBuilder MeshBuilder::torus(float outerRadius, float innerRadius, int segments, int rings) {
    MeshBuilder builder;

    for (int ring = 0; ring <= rings; ++ring) {
        float theta = 2.0f * glm::pi<float>() * ring / rings;
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);

        for (int seg = 0; seg <= segments; ++seg) {
            float phi = 2.0f * glm::pi<float>() * seg / segments;
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);

            float x = (outerRadius + innerRadius * cosPhi) * cosTheta;
            float y = innerRadius * sinPhi;
            float z = (outerRadius + innerRadius * cosPhi) * sinTheta;

            glm::vec3 center(outerRadius * cosTheta, 0, outerRadius * sinTheta);
            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(pos - center);

            glm::vec2 uv(static_cast<float>(ring) / rings,
                         static_cast<float>(seg) / segments);

            builder.addVertex(pos, normal, uv);
        }
    }

    for (int ring = 0; ring < rings; ++ring) {
        for (int seg = 0; seg < segments; ++seg) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            builder.addTriangle(current, next, current + 1);
            builder.addTriangle(current + 1, next, next + 1);
        }
    }

    return builder;
}

MeshBuilder MeshBuilder::plane(float width, float height, int subdivisionsX, int subdivisionsY) {
    MeshBuilder builder;

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    for (int y = 0; y <= subdivisionsY; ++y) {
        float v = static_cast<float>(y) / subdivisionsY;
        float pz = -halfH + height * v;

        for (int x = 0; x <= subdivisionsX; ++x) {
            float u = static_cast<float>(x) / subdivisionsX;
            float px = -halfW + width * u;

            builder.addVertex(
                glm::vec3(px, 0, pz),
                glm::vec3(0, 1, 0),
                glm::vec2(u, v)
            );
        }
    }

    for (int y = 0; y < subdivisionsY; ++y) {
        for (int x = 0; x < subdivisionsX; ++x) {
            uint32_t current = y * (subdivisionsX + 1) + x;
            uint32_t next = current + subdivisionsX + 1;

            builder.addQuad(current, next, next + 1, current + 1);
        }
    }

    return builder;
}

// -----------------------------------------------------------------------------
// CSG Operations via Manifold
// -----------------------------------------------------------------------------

namespace {

// Convert MeshBuilder to Manifold
manifold::Manifold toManifold(const std::vector<Vertex3D>& vertices,
                               const std::vector<uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        return manifold::Manifold();
    }

    manifold::MeshGL mesh;
    mesh.numProp = 3;  // Just positions for CSG

    // Copy vertex positions
    mesh.vertProperties.reserve(vertices.size() * 3);
    for (const auto& v : vertices) {
        mesh.vertProperties.push_back(v.position.x);
        mesh.vertProperties.push_back(v.position.y);
        mesh.vertProperties.push_back(v.position.z);
    }

    // Copy indices
    mesh.triVerts.reserve(indices.size());
    for (uint32_t idx : indices) {
        mesh.triVerts.push_back(idx);
    }

    return manifold::Manifold(mesh);
}

// Convert Manifold back to MeshBuilder data
void fromManifold(const manifold::Manifold& manifold,
                  std::vector<Vertex3D>& outVertices,
                  std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    if (manifold.IsEmpty()) {
        return;
    }

    manifold::MeshGL mesh = manifold.GetMeshGL();

    // Extract vertices
    size_t numVerts = mesh.vertProperties.size() / mesh.numProp;
    outVertices.reserve(numVerts);

    for (size_t i = 0; i < numVerts; ++i) {
        Vertex3D v;
        v.position.x = mesh.vertProperties[i * mesh.numProp + 0];
        v.position.y = mesh.vertProperties[i * mesh.numProp + 1];
        v.position.z = mesh.vertProperties[i * mesh.numProp + 2];
        v.normal = glm::vec3(0, 1, 0);  // Will be recomputed
        v.uv = glm::vec2(0);
        v.color = glm::vec4(1);
        outVertices.push_back(v);
    }

    // Copy indices
    outIndices.reserve(mesh.triVerts.size());
    for (uint32_t idx : mesh.triVerts) {
        outIndices.push_back(idx);
    }
}

} // anonymous namespace

MeshBuilder& MeshBuilder::add(const MeshBuilder& other) {
    // Prefer using internal manifold representation if available
    manifold::Manifold a, b;

    if (m_manifold && !m_manifold->IsEmpty()) {
        a = *m_manifold;
    } else {
        a = toManifold(m_vertices, m_indices);
    }

    if (other.m_manifold && !other.m_manifold->IsEmpty()) {
        b = *other.m_manifold;
    } else {
        b = toManifold(other.m_vertices, other.m_indices);
    }

    manifold::Manifold result = a + b;
    m_manifold = std::make_unique<manifold::Manifold>(result);
    syncFromManifold();

    return *this;
}

MeshBuilder& MeshBuilder::subtract(const MeshBuilder& other) {
    // Prefer using internal manifold representation if available
    manifold::Manifold a, b;

    if (m_manifold && !m_manifold->IsEmpty()) {
        a = *m_manifold;
    } else {
        a = toManifold(m_vertices, m_indices);
    }

    if (other.m_manifold && !other.m_manifold->IsEmpty()) {
        b = *other.m_manifold;
    } else {
        b = toManifold(other.m_vertices, other.m_indices);
    }

    manifold::Manifold result = a - b;
    m_manifold = std::make_unique<manifold::Manifold>(result);
    syncFromManifold();

    return *this;
}

MeshBuilder& MeshBuilder::intersect(const MeshBuilder& other) {
    // Prefer using internal manifold representation if available
    manifold::Manifold a, b;

    if (m_manifold && !m_manifold->IsEmpty()) {
        a = *m_manifold;
    } else {
        a = toManifold(m_vertices, m_indices);
    }

    if (other.m_manifold && !other.m_manifold->IsEmpty()) {
        b = *other.m_manifold;
    } else {
        b = toManifold(other.m_vertices, other.m_indices);
    }

    manifold::Manifold result = a ^ b;
    m_manifold = std::make_unique<manifold::Manifold>(result);
    syncFromManifold();

    return *this;
}

} // namespace vivid::render3d
