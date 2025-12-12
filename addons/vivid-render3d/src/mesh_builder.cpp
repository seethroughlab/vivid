#include <vivid/render3d/mesh_builder.h>
#include <glm/gtc/matrix_transform.hpp>
#include <manifold/manifold.h>
#include <manifold/cross_section.h>
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

    // Compute proper normals from geometry
    computeNormals();
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
    // Simple smooth normals: average face normals at each vertex index
    // This works correctly if cap/side vertices have separate indices

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

    // Note: Don't invalidate manifold here - normals are not part of manifold data

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

MeshBuilder& MeshBuilder::computeTangents() {
    // Based on Lengyel's method for computing tangent space
    // See: "Mathematics for 3D Game Programming and Computer Graphics"

    size_t vertexCount = m_vertices.size();
    size_t triangleCount = m_indices.size() / 3;

    // Temporary accumulators for tangent and bitangent
    std::vector<glm::vec3> tan1(vertexCount, glm::vec3(0));
    std::vector<glm::vec3> tan2(vertexCount, glm::vec3(0));

    for (size_t i = 0; i < triangleCount; ++i) {
        uint32_t i0 = m_indices[i * 3 + 0];
        uint32_t i1 = m_indices[i * 3 + 1];
        uint32_t i2 = m_indices[i * 3 + 2];

        const glm::vec3& p0 = m_vertices[i0].position;
        const glm::vec3& p1 = m_vertices[i1].position;
        const glm::vec3& p2 = m_vertices[i2].position;

        const glm::vec2& uv0 = m_vertices[i0].uv;
        const glm::vec2& uv1 = m_vertices[i1].uv;
        const glm::vec2& uv2 = m_vertices[i2].uv;

        glm::vec3 edge1 = p1 - p0;
        glm::vec3 edge2 = p2 - p0;

        glm::vec2 deltaUV1 = uv1 - uv0;
        glm::vec2 deltaUV2 = uv2 - uv0;

        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;

        // Avoid division by zero for degenerate UVs
        if (std::abs(det) < 1e-8f) {
            continue;
        }

        float r = 1.0f / det;

        glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r;
        glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * r;

        // Accumulate per-vertex
        tan1[i0] += tangent;
        tan1[i1] += tangent;
        tan1[i2] += tangent;

        tan2[i0] += bitangent;
        tan2[i1] += bitangent;
        tan2[i2] += bitangent;
    }

    // Orthonormalize and compute handedness for each vertex
    for (size_t i = 0; i < vertexCount; ++i) {
        const glm::vec3& n = m_vertices[i].normal;
        const glm::vec3& t = tan1[i];

        // Gram-Schmidt orthonormalize: T' = normalize(T - N * dot(N, T))
        glm::vec3 tangent = t - n * glm::dot(n, t);
        float len = glm::length(tangent);

        if (len > 1e-6f) {
            tangent /= len;
        } else {
            // Fallback: create arbitrary tangent perpendicular to normal
            if (std::abs(n.x) < 0.9f) {
                tangent = glm::normalize(glm::cross(n, glm::vec3(1, 0, 0)));
            } else {
                tangent = glm::normalize(glm::cross(n, glm::vec3(0, 1, 0)));
            }
        }

        // Handedness: sign of dot(cross(N, T), B)
        // If negative, bitangent should be flipped in shader
        float handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;

        m_vertices[i].tangent = glm::vec4(tangent, handedness);
    }

    return *this;
}

MeshBuilder& MeshBuilder::transform(const glm::mat4& m) {
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(m)));

    for (auto& v : m_vertices) {
        v.position = glm::vec3(m * glm::vec4(v.position, 1.0f));
        v.normal = glm::normalize(normalMatrix * v.normal);
        // Transform tangent direction (preserve handedness in w)
        glm::vec3 tan = glm::normalize(glm::mat3(m) * glm::vec3(v.tangent));
        v.tangent = glm::vec4(tan, v.tangent.w);
    }

    // Invalidate cached manifold - it's now stale
    m_manifold.reset();

    return *this;
}

MeshBuilder& MeshBuilder::translate(glm::vec3 offset) {
    for (auto& v : m_vertices) {
        v.position += offset;
    }
    // Invalidate cached manifold - it's now stale
    m_manifold.reset();
    return *this;
}

MeshBuilder& MeshBuilder::scale(glm::vec3 s) {
    for (auto& v : m_vertices) {
        v.position *= s;
    }
    // Invalidate cached manifold - it's now stale
    m_manifold.reset();
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

    // Invalidate cached manifold - it's now stale
    m_manifold.reset();

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

    // Invalidate cached manifold - it's now stale
    m_manifold.reset();

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
    // Build box manually with proper UV coordinates for each face
    // Each face has its own 4 vertices for correct normals and UVs
    MeshBuilder builder;

    float hx = size.x * 0.5f;
    float hy = size.y * 0.5f;
    float hz = size.z * 0.5f;

    // FRONT face (+Z) - looking from +Z
    {
        glm::vec3 normal(0, 0, 1);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3(-hx, -hy, hz), normal, glm::vec2(0, 1));
        builder.addVertex(glm::vec3( hx, -hy, hz), normal, glm::vec2(1, 1));
        builder.addVertex(glm::vec3( hx,  hy, hz), normal, glm::vec2(1, 0));
        builder.addVertex(glm::vec3(-hx,  hy, hz), normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);
        builder.addTriangle(base, base + 2, base + 3);
    }

    // BACK face (-Z) - looking from -Z
    {
        glm::vec3 normal(0, 0, -1);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3( hx, -hy, -hz), normal, glm::vec2(0, 1));
        builder.addVertex(glm::vec3(-hx, -hy, -hz), normal, glm::vec2(1, 1));
        builder.addVertex(glm::vec3(-hx,  hy, -hz), normal, glm::vec2(1, 0));
        builder.addVertex(glm::vec3( hx,  hy, -hz), normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);
        builder.addTriangle(base, base + 2, base + 3);
    }

    // RIGHT face (+X) - looking from +X
    {
        glm::vec3 normal(1, 0, 0);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3(hx, -hy,  hz), normal, glm::vec2(0, 1));
        builder.addVertex(glm::vec3(hx, -hy, -hz), normal, glm::vec2(1, 1));
        builder.addVertex(glm::vec3(hx,  hy, -hz), normal, glm::vec2(1, 0));
        builder.addVertex(glm::vec3(hx,  hy,  hz), normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);
        builder.addTriangle(base, base + 2, base + 3);
    }

    // LEFT face (-X) - looking from -X
    {
        glm::vec3 normal(-1, 0, 0);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3(-hx, -hy, -hz), normal, glm::vec2(0, 1));
        builder.addVertex(glm::vec3(-hx, -hy,  hz), normal, glm::vec2(1, 1));
        builder.addVertex(glm::vec3(-hx,  hy,  hz), normal, glm::vec2(1, 0));
        builder.addVertex(glm::vec3(-hx,  hy, -hz), normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);
        builder.addTriangle(base, base + 2, base + 3);
    }

    // TOP face (+Y) - looking from +Y
    {
        glm::vec3 normal(0, 1, 0);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3(-hx, hy,  hz), normal, glm::vec2(0, 1));
        builder.addVertex(glm::vec3( hx, hy,  hz), normal, glm::vec2(1, 1));
        builder.addVertex(glm::vec3( hx, hy, -hz), normal, glm::vec2(1, 0));
        builder.addVertex(glm::vec3(-hx, hy, -hz), normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);
        builder.addTriangle(base, base + 2, base + 3);
    }

    // BOTTOM face (-Y) - looking from -Y
    {
        glm::vec3 normal(0, -1, 0);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3(-hx, -hy, -hz), normal, glm::vec2(0, 1));
        builder.addVertex(glm::vec3( hx, -hy, -hz), normal, glm::vec2(1, 1));
        builder.addVertex(glm::vec3( hx, -hy,  hz), normal, glm::vec2(1, 0));
        builder.addVertex(glm::vec3(-hx, -hy,  hz), normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);
        builder.addTriangle(base, base + 2, base + 3);
    }

    // Sync to manifold for CSG operations
    builder.syncToManifold();

    return builder;
}

MeshBuilder MeshBuilder::sphere(float radius, int segments) {
    // Use Manifold's built-in sphere for CSG-safe geometry
    auto m = std::make_unique<manifold::Manifold>(
        manifold::Manifold::Sphere(static_cast<double>(radius), segments));

    MeshBuilder builder(std::move(m));

    // Compute spherical UV coordinates for texture mapping
    for (auto& v : builder.m_vertices) {
        glm::vec3 n = glm::normalize(v.position);
        // Spherical UV mapping: u = longitude, v = latitude
        float u = 0.5f + std::atan2(n.z, n.x) / (2.0f * glm::pi<float>());
        float vCoord = 0.5f - std::asin(n.y) / glm::pi<float>();
        v.uv = glm::vec2(u, vCoord);
    }

    // Fix UV seam: duplicate vertices where triangles span the u=0/1 boundary
    std::vector<Vertex3D>& verts = builder.m_vertices;
    std::vector<uint32_t>& indices = builder.m_indices;

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        float u0 = verts[i0].uv.x;
        float u1 = verts[i1].uv.x;
        float u2 = verts[i2].uv.x;

        // Check if triangle spans the seam (large UV gap)
        float maxU = std::max({u0, u1, u2});
        float minU = std::min({u0, u1, u2});

        if (maxU - minU > 0.5f) {
            // This triangle crosses the seam - duplicate vertices with u < 0.5 and add 1
            auto fixVertex = [&](uint32_t& idx) {
                if (verts[idx].uv.x < 0.5f) {
                    Vertex3D newVert = verts[idx];
                    newVert.uv.x += 1.0f;
                    idx = static_cast<uint32_t>(verts.size());
                    verts.push_back(newVert);
                }
            };
            fixVertex(indices[i]);
            fixVertex(indices[i + 1]);
            fixVertex(indices[i + 2]);
        }
    }

    return builder;
}

MeshBuilder MeshBuilder::cylinder(float radius, float height, int segments) {
    MeshBuilder builder;
    float halfH = height * 0.5f;

    // Generate vertices for the SIDE (separate from caps for sharp edges)
    // Two rings of vertices - top and bottom of the side surface
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        float u = static_cast<float>(i) / segments;

        // Bottom ring (side)
        builder.addVertex(glm::vec3(x, -halfH, z), glm::vec3(0), glm::vec2(u, 0));
        // Top ring (side)
        builder.addVertex(glm::vec3(x, halfH, z), glm::vec3(0), glm::vec2(u, 1));
    }

    // Side faces (CCW winding for outward-facing normals)
    for (int i = 0; i < segments; ++i) {
        uint32_t bl = i * 2;      // bottom-left
        uint32_t tl = i * 2 + 1;  // top-left
        uint32_t br = (i + 1) * 2;      // bottom-right
        uint32_t tr = (i + 1) * 2 + 1;  // top-right

        builder.addTriangle(bl, tl, tr);
        builder.addTriangle(bl, tr, br);
    }

    // TOP CAP - separate vertices with upward normals
    uint32_t topCenterIdx = static_cast<uint32_t>(builder.vertexCount());
    builder.addVertex(glm::vec3(0, halfH, 0), glm::vec3(0, 1, 0), glm::vec2(0.5f, 0.5f));

    uint32_t topRingStart = static_cast<uint32_t>(builder.vertexCount());
    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        builder.addVertex(glm::vec3(x, halfH, z), glm::vec3(0, 1, 0),
                         glm::vec2(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)));
    }

    for (int i = 0; i < segments; ++i) {
        uint32_t curr = topRingStart + i;
        uint32_t next = topRingStart + ((i + 1) % segments);
        builder.addTriangle(topCenterIdx, next, curr);  // CCW when viewed from above
    }

    // BOTTOM CAP - separate vertices with downward normals
    uint32_t botCenterIdx = static_cast<uint32_t>(builder.vertexCount());
    builder.addVertex(glm::vec3(0, -halfH, 0), glm::vec3(0, -1, 0), glm::vec2(0.5f, 0.5f));

    uint32_t botRingStart = static_cast<uint32_t>(builder.vertexCount());
    for (int i = 0; i < segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        float x = radius * std::cos(angle);
        float z = radius * std::sin(angle);
        builder.addVertex(glm::vec3(x, -halfH, z), glm::vec3(0, -1, 0),
                         glm::vec2(0.5f + 0.5f * std::cos(angle), 0.5f - 0.5f * std::sin(angle)));
    }

    for (int i = 0; i < segments; ++i) {
        uint32_t curr = botRingStart + i;
        uint32_t next = botRingStart + ((i + 1) % segments);
        builder.addTriangle(botCenterIdx, curr, next);  // CCW when viewed from below
    }

    // Sync to manifold for CSG operations
    builder.syncToManifold();

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

            // CCW winding for outward-facing normals
            builder.addTriangle(current, current + 1, next);
            builder.addTriangle(current + 1, next + 1, next);
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

MeshBuilder MeshBuilder::pyramid(float baseWidth, float height, int sides) {
    MeshBuilder builder;

    float halfW = baseWidth * 0.5f;
    float halfH = height * 0.5f;

    // Apex at top
    glm::vec3 apex(0, halfH, 0);

    // Generate base vertices
    std::vector<glm::vec3> baseVerts;
    for (int i = 0; i < sides; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / sides;
        // Offset by half a segment so square pyramid has flat sides facing axes
        angle += glm::pi<float>() / sides;
        float x = halfW * std::cos(angle);
        float z = halfW * std::sin(angle);
        baseVerts.push_back(glm::vec3(x, -halfH, z));
    }

    // SIDE FACES - each face connects two base vertices to apex
    for (int i = 0; i < sides; ++i) {
        int next = (i + 1) % sides;

        // Swap order so winding is CCW when viewed from outside
        glm::vec3 v0 = baseVerts[next];
        glm::vec3 v1 = baseVerts[i];

        // Face normal (CCW winding looking from outside)
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = apex - v0;
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

        uint32_t baseIdx = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(v0, normal, glm::vec2(0, 1));
        builder.addVertex(v1, normal, glm::vec2(1, 1));
        builder.addVertex(apex, normal, glm::vec2(0.5f, 0));

        builder.addTriangle(baseIdx, baseIdx + 1, baseIdx + 2);
    }

    // BOTTOM FACE - fan from center
    glm::vec3 bottomNormal(0, -1, 0);
    uint32_t centerIdx = static_cast<uint32_t>(builder.vertexCount());
    builder.addVertex(glm::vec3(0, -halfH, 0), bottomNormal, glm::vec2(0.5f, 0.5f));

    uint32_t bottomRingStart = static_cast<uint32_t>(builder.vertexCount());
    for (int i = 0; i < sides; ++i) {
        float u = 0.5f + 0.5f * (baseVerts[i].x / halfW);
        float v = 0.5f + 0.5f * (baseVerts[i].z / halfW);
        builder.addVertex(baseVerts[i], bottomNormal, glm::vec2(u, v));
    }

    for (int i = 0; i < sides; ++i) {
        int next = (i + 1) % sides;
        // CCW when viewed from below (looking up at bottom face)
        builder.addTriangle(centerIdx, bottomRingStart + next, bottomRingStart + i);
    }

    builder.syncToManifold();
    return builder;
}

MeshBuilder MeshBuilder::wedge(float width, float height, float depth) {
    // Build wedge (triangular prism) manually with correct winding
    // Ramp goes from full height at -X to zero height at +X
    //
    //    (-w/2, h/2)-----(+w/2, -h/2)   <- top edge (sloped)
    //         |    \          |
    //         |      \        |
    //         |        \      |
    //    (-w/2, -h/2)---(+w/2, -h/2)    <- bottom edge
    //
    // Depth extends along Z axis

    MeshBuilder builder;

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;
    float halfD = depth * 0.5f;

    // 6 vertices of the wedge
    glm::vec3 v0(-halfW, -halfH, -halfD);  // back-bottom-left
    glm::vec3 v1(+halfW, -halfH, -halfD);  // back-bottom-right
    glm::vec3 v2(-halfW, +halfH, -halfD);  // back-top-left
    glm::vec3 v3(-halfW, -halfH, +halfD);  // front-bottom-left
    glm::vec3 v4(+halfW, -halfH, +halfD);  // front-bottom-right
    glm::vec3 v5(-halfW, +halfH, +halfD);  // front-top-left

    // BACK FACE (triangle) - looking from -Z
    {
        glm::vec3 normal(0, 0, -1);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(v0, normal, glm::vec2(0, 1));
        builder.addVertex(v2, normal, glm::vec2(0, 0));
        builder.addVertex(v1, normal, glm::vec2(1, 1));
        builder.addTriangle(base, base + 1, base + 2);  // CCW from -Z
    }

    // FRONT FACE (triangle) - looking from +Z
    {
        glm::vec3 normal(0, 0, 1);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(v3, normal, glm::vec2(0, 1));
        builder.addVertex(v4, normal, glm::vec2(1, 1));
        builder.addVertex(v5, normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);  // CCW from +Z
    }

    // BOTTOM FACE (quad) - looking from -Y
    {
        glm::vec3 normal(0, -1, 0);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(v0, normal, glm::vec2(0, 0));
        builder.addVertex(v1, normal, glm::vec2(1, 0));
        builder.addVertex(v4, normal, glm::vec2(1, 1));
        builder.addVertex(v3, normal, glm::vec2(0, 1));
        builder.addTriangle(base, base + 1, base + 2);  // CCW from -Y
        builder.addTriangle(base, base + 2, base + 3);
    }

    // LEFT FACE (quad) - looking from -X
    {
        glm::vec3 normal(-1, 0, 0);
        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(v0, normal, glm::vec2(0, 1));
        builder.addVertex(v3, normal, glm::vec2(1, 1));
        builder.addVertex(v5, normal, glm::vec2(1, 0));
        builder.addVertex(v2, normal, glm::vec2(0, 0));
        builder.addTriangle(base, base + 1, base + 2);  // CCW from -X
        builder.addTriangle(base, base + 2, base + 3);
    }

    // SLOPE FACE (quad) - the ramp surface
    {
        // Normal points up and to the right
        glm::vec3 edge1 = v4 - v2;  // along the slope
        glm::vec3 edge2 = v5 - v2;  // along depth
        glm::vec3 normal = glm::normalize(glm::cross(edge2, edge1));

        uint32_t base = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(v2, normal, glm::vec2(0, 0));
        builder.addVertex(v5, normal, glm::vec2(0, 1));
        builder.addVertex(v4, normal, glm::vec2(1, 1));
        builder.addVertex(v1, normal, glm::vec2(1, 0));
        builder.addTriangle(base, base + 1, base + 2);  // CCW from outside
        builder.addTriangle(base, base + 2, base + 3);
    }

    return builder;
}

MeshBuilder MeshBuilder::frustum(float bottomRadius, float topRadius, float height, int segments) {
    // Build frustum manually with proper UV coordinates
    // Similar to cylinder() but with different top and bottom radii
    MeshBuilder builder;
    float halfH = height * 0.5f;

    // Generate vertices for the SIDE (separate from caps for sharp edges)
    // Two rings of vertices - top and bottom of the side surface
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * glm::pi<float>() * i / segments;
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);
        float u = static_cast<float>(i) / segments;

        // Bottom ring (side) - uses bottomRadius
        float bx = bottomRadius * cosA;
        float bz = bottomRadius * sinA;
        builder.addVertex(glm::vec3(bx, -halfH, bz), glm::vec3(0), glm::vec2(u, 0));

        // Top ring (side) - uses topRadius
        float tx = topRadius * cosA;
        float tz = topRadius * sinA;
        builder.addVertex(glm::vec3(tx, halfH, tz), glm::vec3(0), glm::vec2(u, 1));
    }

    // Side faces (CCW winding for outward-facing normals)
    for (int i = 0; i < segments; ++i) {
        uint32_t bl = i * 2;      // bottom-left
        uint32_t tl = i * 2 + 1;  // top-left
        uint32_t br = (i + 1) * 2;      // bottom-right
        uint32_t tr = (i + 1) * 2 + 1;  // top-right

        builder.addTriangle(bl, tl, tr);
        builder.addTriangle(bl, tr, br);
    }

    // TOP CAP - separate vertices with upward normals (if topRadius > 0)
    if (topRadius > 0.001f) {
        uint32_t topCenterIdx = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3(0, halfH, 0), glm::vec3(0, 1, 0), glm::vec2(0.5f, 0.5f));

        uint32_t topRingStart = static_cast<uint32_t>(builder.vertexCount());
        for (int i = 0; i < segments; ++i) {
            float angle = 2.0f * glm::pi<float>() * i / segments;
            float x = topRadius * std::cos(angle);
            float z = topRadius * std::sin(angle);
            builder.addVertex(glm::vec3(x, halfH, z), glm::vec3(0, 1, 0),
                             glm::vec2(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle)));
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t curr = topRingStart + i;
            uint32_t next = topRingStart + ((i + 1) % segments);
            builder.addTriangle(topCenterIdx, next, curr);  // CCW when viewed from above
        }
    }

    // BOTTOM CAP - separate vertices with downward normals (if bottomRadius > 0)
    if (bottomRadius > 0.001f) {
        uint32_t botCenterIdx = static_cast<uint32_t>(builder.vertexCount());
        builder.addVertex(glm::vec3(0, -halfH, 0), glm::vec3(0, -1, 0), glm::vec2(0.5f, 0.5f));

        uint32_t botRingStart = static_cast<uint32_t>(builder.vertexCount());
        for (int i = 0; i < segments; ++i) {
            float angle = 2.0f * glm::pi<float>() * i / segments;
            float x = bottomRadius * std::cos(angle);
            float z = bottomRadius * std::sin(angle);
            builder.addVertex(glm::vec3(x, -halfH, z), glm::vec3(0, -1, 0),
                             glm::vec2(0.5f + 0.5f * std::cos(angle), 0.5f - 0.5f * std::sin(angle)));
        }

        for (int i = 0; i < segments; ++i) {
            uint32_t curr = botRingStart + i;
            uint32_t next = botRingStart + ((i + 1) % segments);
            builder.addTriangle(botCenterIdx, curr, next);  // CCW when viewed from below
        }
    }

    // Compute smooth normals for the side surface
    builder.computeNormals();

    // Sync to manifold for CSG operations
    builder.syncToManifold();

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

    // Merge duplicate vertices - required for valid manifold after transforms
    mesh.Merge();

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

// -----------------------------------------------------------------------------
// Mesh Combination (Simple)
// -----------------------------------------------------------------------------

MeshBuilder& MeshBuilder::append(const MeshBuilder& other) {
    // Simple geometry concatenation - no CSG, just combine vertices and indices
    if (other.m_vertices.empty()) {
        return *this;
    }

    // Offset for indices
    uint32_t vertexOffset = static_cast<uint32_t>(m_vertices.size());

    // Append vertices
    m_vertices.insert(m_vertices.end(), other.m_vertices.begin(), other.m_vertices.end());

    // Append indices with offset
    m_indices.reserve(m_indices.size() + other.m_indices.size());
    for (uint32_t idx : other.m_indices) {
        m_indices.push_back(idx + vertexOffset);
    }

    // Invalidate manifold - geometry changed
    m_manifold.reset();

    return *this;
}

// -----------------------------------------------------------------------------
// CSG Boolean Operations
// -----------------------------------------------------------------------------

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
