// Mesh utilities implementation

#include "vivid/mesh.h"

#include "Buffer.h"
#include "RenderDevice.h"

#include <cmath>

namespace vivid {

using namespace Diligent;

// Mesh class implementation

Mesh::~Mesh() {
    release();
}

Mesh::Mesh(Mesh&& other) noexcept
    : vertexBuffer_(other.vertexBuffer_)
    , indexBuffer_(other.indexBuffer_)
    , vertexCount_(other.vertexCount_)
    , indexCount_(other.indexCount_)
{
    other.vertexBuffer_ = nullptr;
    other.indexBuffer_ = nullptr;
    other.vertexCount_ = 0;
    other.indexCount_ = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        release();
        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        vertexCount_ = other.vertexCount_;
        indexCount_ = other.indexCount_;
        other.vertexBuffer_ = nullptr;
        other.indexBuffer_ = nullptr;
        other.vertexCount_ = 0;
        other.indexCount_ = 0;
    }
    return *this;
}

bool Mesh::create(IRenderDevice* device, const MeshData& data) {
    if (!device || data.empty()) return false;

    release();

    // Create vertex buffer
    BufferDesc vbDesc;
    vbDesc.Name = "Mesh Vertex Buffer";
    vbDesc.Usage = USAGE_IMMUTABLE;
    vbDesc.BindFlags = BIND_VERTEX_BUFFER;
    vbDesc.Size = static_cast<Uint64>(data.vertices.size() * sizeof(Vertex3D));

    BufferData vbData;
    vbData.pData = data.vertices.data();
    vbData.DataSize = vbDesc.Size;

    device->CreateBuffer(vbDesc, &vbData, &vertexBuffer_);
    if (!vertexBuffer_) return false;

    // Create index buffer
    BufferDesc ibDesc;
    ibDesc.Name = "Mesh Index Buffer";
    ibDesc.Usage = USAGE_IMMUTABLE;
    ibDesc.BindFlags = BIND_INDEX_BUFFER;
    ibDesc.Size = static_cast<Uint64>(data.indices.size() * sizeof(uint32_t));

    BufferData ibData;
    ibData.pData = data.indices.data();
    ibData.DataSize = ibDesc.Size;

    device->CreateBuffer(ibDesc, &ibData, &indexBuffer_);
    if (!indexBuffer_) {
        vertexBuffer_->Release();
        vertexBuffer_ = nullptr;
        return false;
    }

    vertexCount_ = static_cast<uint32_t>(data.vertices.size());
    indexCount_ = static_cast<uint32_t>(data.indices.size());

    return true;
}

void Mesh::release() {
    if (indexBuffer_) {
        indexBuffer_->Release();
        indexBuffer_ = nullptr;
    }
    if (vertexBuffer_) {
        vertexBuffer_->Release();
        vertexBuffer_ = nullptr;
    }
    vertexCount_ = 0;
    indexCount_ = 0;
}

// MeshUtils implementation

namespace MeshUtils {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 2.0f * PI;

MeshData createCube() {
    MeshData mesh;

    // Cube faces: each face has 4 vertices with proper normals and UVs

    // Front face (+Z)
    mesh.vertices.push_back({{-0.5f, -0.5f,  0.5f}, {0, 0, 1}, {0, 0}, {1, 0, 0}});
    mesh.vertices.push_back({{ 0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 0}, {1, 0, 0}});
    mesh.vertices.push_back({{ 0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1}, {1, 0, 0}});
    mesh.vertices.push_back({{-0.5f,  0.5f,  0.5f}, {0, 0, 1}, {0, 1}, {1, 0, 0}});

    // Back face (-Z)
    mesh.vertices.push_back({{ 0.5f, -0.5f, -0.5f}, {0, 0, -1}, {0, 0}, {-1, 0, 0}});
    mesh.vertices.push_back({{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 0}, {-1, 0, 0}});
    mesh.vertices.push_back({{-0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1}, {-1, 0, 0}});
    mesh.vertices.push_back({{ 0.5f,  0.5f, -0.5f}, {0, 0, -1}, {0, 1}, {-1, 0, 0}});

    // Top face (+Y)
    mesh.vertices.push_back({{-0.5f,  0.5f,  0.5f}, {0, 1, 0}, {0, 0}, {1, 0, 0}});
    mesh.vertices.push_back({{ 0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 0}, {1, 0, 0}});
    mesh.vertices.push_back({{ 0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1}, {1, 0, 0}});
    mesh.vertices.push_back({{-0.5f,  0.5f, -0.5f}, {0, 1, 0}, {0, 1}, {1, 0, 0}});

    // Bottom face (-Y)
    mesh.vertices.push_back({{-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {0, 0}, {1, 0, 0}});
    mesh.vertices.push_back({{ 0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 0}, {1, 0, 0}});
    mesh.vertices.push_back({{ 0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 1}, {1, 0, 0}});
    mesh.vertices.push_back({{-0.5f, -0.5f,  0.5f}, {0, -1, 0}, {0, 1}, {1, 0, 0}});

    // Right face (+X)
    mesh.vertices.push_back({{ 0.5f, -0.5f,  0.5f}, {1, 0, 0}, {0, 0}, {0, 0, -1}});
    mesh.vertices.push_back({{ 0.5f, -0.5f, -0.5f}, {1, 0, 0}, {1, 0}, {0, 0, -1}});
    mesh.vertices.push_back({{ 0.5f,  0.5f, -0.5f}, {1, 0, 0}, {1, 1}, {0, 0, -1}});
    mesh.vertices.push_back({{ 0.5f,  0.5f,  0.5f}, {1, 0, 0}, {0, 1}, {0, 0, -1}});

    // Left face (-X)
    mesh.vertices.push_back({{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {0, 0}, {0, 0, 1}});
    mesh.vertices.push_back({{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {1, 0}, {0, 0, 1}});
    mesh.vertices.push_back({{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {1, 1}, {0, 0, 1}});
    mesh.vertices.push_back({{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {0, 1}, {0, 0, 1}});

    // Indices (2 triangles per face, 6 faces)
    for (uint32_t face = 0; face < 6; face++) {
        uint32_t base = face * 4;
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    return mesh;
}

MeshData createSphere(int segments, int rings, float radius) {
    MeshData mesh;

    // Generate vertices
    for (int ring = 0; ring <= rings; ring++) {
        float phi = PI * static_cast<float>(ring) / static_cast<float>(rings);
        float sinPhi = std::sin(phi);
        float cosPhi = std::cos(phi);

        for (int seg = 0; seg <= segments; seg++) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            glm::vec3 normal(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
            glm::vec3 position = normal * radius;
            glm::vec2 uv(static_cast<float>(seg) / static_cast<float>(segments),
                         static_cast<float>(ring) / static_cast<float>(rings));
            glm::vec3 tangent(-sinTheta, 0, cosTheta);

            mesh.vertices.push_back({position, normal, uv, tangent});
        }
    }

    // Generate indices
    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            mesh.indices.push_back(current);
            mesh.indices.push_back(next);
            mesh.indices.push_back(current + 1);

            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next);
            mesh.indices.push_back(next + 1);
        }
    }

    return mesh;
}

MeshData createPlane(float width, float depth, int segmentsX, int segmentsZ) {
    MeshData mesh;

    float halfW = width * 0.5f;
    float halfD = depth * 0.5f;

    // Generate vertices
    for (int z = 0; z <= segmentsZ; z++) {
        for (int x = 0; x <= segmentsX; x++) {
            float u = static_cast<float>(x) / static_cast<float>(segmentsX);
            float v = static_cast<float>(z) / static_cast<float>(segmentsZ);

            glm::vec3 position(u * width - halfW, 0, v * depth - halfD);
            glm::vec3 normal(0, 1, 0);
            glm::vec2 uv(u, v);
            glm::vec3 tangent(1, 0, 0);

            mesh.vertices.push_back({position, normal, uv, tangent});
        }
    }

    // Generate indices
    for (int z = 0; z < segmentsZ; z++) {
        for (int x = 0; x < segmentsX; x++) {
            uint32_t current = z * (segmentsX + 1) + x;
            uint32_t next = current + segmentsX + 1;

            mesh.indices.push_back(current);
            mesh.indices.push_back(next);
            mesh.indices.push_back(current + 1);

            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next);
            mesh.indices.push_back(next + 1);
        }
    }

    return mesh;
}

MeshData createCylinder(int segments, float radius, float height) {
    MeshData mesh;

    float halfHeight = height * 0.5f;

    // Side vertices
    for (int i = 0; i <= segments; i++) {
        float theta = TWO_PI * static_cast<float>(i) / static_cast<float>(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);
        float u = static_cast<float>(i) / static_cast<float>(segments);

        glm::vec3 normal(cosT, 0, sinT);
        glm::vec3 tangent(-sinT, 0, cosT);

        // Bottom
        mesh.vertices.push_back({{radius * cosT, -halfHeight, radius * sinT}, normal, {u, 0}, tangent});
        // Top
        mesh.vertices.push_back({{radius * cosT,  halfHeight, radius * sinT}, normal, {u, 1}, tangent});
    }

    // Side indices
    for (int i = 0; i < segments; i++) {
        uint32_t base = i * 2;
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 1);

        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }

    // Top cap
    uint32_t topCenterIdx = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0, halfHeight, 0}, {0, 1, 0}, {0.5f, 0.5f}, {1, 0, 0}});

    for (int i = 0; i <= segments; i++) {
        float theta = TWO_PI * static_cast<float>(i) / static_cast<float>(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        mesh.vertices.push_back({{radius * cosT, halfHeight, radius * sinT},
                                 {0, 1, 0},
                                 {0.5f + 0.5f * cosT, 0.5f + 0.5f * sinT},
                                 {1, 0, 0}});
    }

    for (int i = 0; i < segments; i++) {
        mesh.indices.push_back(topCenterIdx);
        mesh.indices.push_back(topCenterIdx + 1 + i);
        mesh.indices.push_back(topCenterIdx + 2 + i);
    }

    // Bottom cap
    uint32_t botCenterIdx = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0, -halfHeight, 0}, {0, -1, 0}, {0.5f, 0.5f}, {1, 0, 0}});

    for (int i = 0; i <= segments; i++) {
        float theta = TWO_PI * static_cast<float>(i) / static_cast<float>(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        mesh.vertices.push_back({{radius * cosT, -halfHeight, radius * sinT},
                                 {0, -1, 0},
                                 {0.5f + 0.5f * cosT, 0.5f - 0.5f * sinT},
                                 {1, 0, 0}});
    }

    for (int i = 0; i < segments; i++) {
        mesh.indices.push_back(botCenterIdx);
        mesh.indices.push_back(botCenterIdx + 2 + i);
        mesh.indices.push_back(botCenterIdx + 1 + i);
    }

    return mesh;
}

MeshData createTorus(int segments, int rings, float radius, float tubeRadius) {
    MeshData mesh;

    for (int ring = 0; ring <= rings; ring++) {
        float phi = TWO_PI * static_cast<float>(ring) / static_cast<float>(rings);
        float cosPhi = std::cos(phi);
        float sinPhi = std::sin(phi);

        for (int seg = 0; seg <= segments; seg++) {
            float theta = TWO_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            glm::vec3 center(radius * cosPhi, 0, radius * sinPhi);
            glm::vec3 normal(cosTheta * cosPhi, sinTheta, cosTheta * sinPhi);
            glm::vec3 position = center + tubeRadius * normal;
            glm::vec2 uv(static_cast<float>(ring) / static_cast<float>(rings),
                         static_cast<float>(seg) / static_cast<float>(segments));
            glm::vec3 tangent(-sinPhi, 0, cosPhi);

            mesh.vertices.push_back({position, normal, uv, tangent});
        }
    }

    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            uint32_t current = ring * (segments + 1) + seg;
            uint32_t next = current + segments + 1;

            mesh.indices.push_back(current);
            mesh.indices.push_back(next);
            mesh.indices.push_back(current + 1);

            mesh.indices.push_back(current + 1);
            mesh.indices.push_back(next);
            mesh.indices.push_back(next + 1);
        }
    }

    return mesh;
}

MeshData createCone(int segments, float radius, float height) {
    MeshData mesh;

    float halfHeight = height * 0.5f;

    // Apex
    uint32_t apexIdx = 0;
    mesh.vertices.push_back({{0, halfHeight, 0}, {0, 1, 0}, {0.5f, 0}, {1, 0, 0}});

    // Side vertices
    float slopeLen = std::sqrt(radius * radius + height * height);
    float ny = radius / slopeLen;
    float nr = height / slopeLen;

    for (int i = 0; i <= segments; i++) {
        float theta = TWO_PI * static_cast<float>(i) / static_cast<float>(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        glm::vec3 normal(nr * cosT, ny, nr * sinT);
        glm::vec3 tangent(-sinT, 0, cosT);
        float u = static_cast<float>(i) / static_cast<float>(segments);

        mesh.vertices.push_back({{radius * cosT, -halfHeight, radius * sinT}, normal, {u, 1}, tangent});
    }

    // Side indices
    for (int i = 0; i < segments; i++) {
        mesh.indices.push_back(apexIdx);
        mesh.indices.push_back(1 + i);
        mesh.indices.push_back(2 + i);
    }

    // Bottom cap
    uint32_t botCenterIdx = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{0, -halfHeight, 0}, {0, -1, 0}, {0.5f, 0.5f}, {1, 0, 0}});

    for (int i = 0; i <= segments; i++) {
        float theta = TWO_PI * static_cast<float>(i) / static_cast<float>(segments);
        float cosT = std::cos(theta);
        float sinT = std::sin(theta);

        mesh.vertices.push_back({{radius * cosT, -halfHeight, radius * sinT},
                                 {0, -1, 0},
                                 {0.5f + 0.5f * cosT, 0.5f - 0.5f * sinT},
                                 {1, 0, 0}});
    }

    for (int i = 0; i < segments; i++) {
        mesh.indices.push_back(botCenterIdx);
        mesh.indices.push_back(botCenterIdx + 2 + i);
        mesh.indices.push_back(botCenterIdx + 1 + i);
    }

    return mesh;
}

void calculateTangents(MeshData& mesh) {
    // Reset tangents
    for (auto& v : mesh.vertices) {
        v.tangent = glm::vec3(0);
    }

    // Calculate tangents for each triangle
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i];
        uint32_t i1 = mesh.indices[i + 1];
        uint32_t i2 = mesh.indices[i + 2];

        Vertex3D& v0 = mesh.vertices[i0];
        Vertex3D& v1 = mesh.vertices[i1];
        Vertex3D& v2 = mesh.vertices[i2];

        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;

        glm::vec2 deltaUV1 = v1.uv - v0.uv;
        glm::vec2 deltaUV2 = v2.uv - v0.uv;

        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::abs(det) > 1e-6f) {
            float f = 1.0f / det;
            glm::vec3 tangent;
            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);

            v0.tangent += tangent;
            v1.tangent += tangent;
            v2.tangent += tangent;
        }
    }

    // Normalize and orthogonalize tangents
    for (auto& v : mesh.vertices) {
        if (glm::length(v.tangent) > 1e-6f) {
            // Gram-Schmidt orthogonalize
            v.tangent = glm::normalize(v.tangent - v.normal * glm::dot(v.normal, v.tangent));
        } else {
            // Fallback tangent
            v.tangent = glm::vec3(1, 0, 0);
        }
    }
}

} // namespace MeshUtils

} // namespace vivid
